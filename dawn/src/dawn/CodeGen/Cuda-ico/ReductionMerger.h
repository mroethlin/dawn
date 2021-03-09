//===--------------------------------------------------------------------------------*- C++ -*-===//
//                          _
//                         | |
//                       __| | __ ___      ___ ___
//                      / _` |/ _` \ \ /\ / / '_  |
//                     | (_| | (_| |\ V  V /| | | |
//                      \__,_|\__,_| \_/\_/ |_| |_| - Compiler Toolchain
//
//
//  This file is distributed under the MIT License (MIT).
//  See LICENSE.txt for details.
//
//===------------------------------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <vector>

#include "dawn/AST/ASTExpr.h"
#include "dawn/AST/ASTStmt.h"
#include "dawn/AST/ASTStringifier.h"
#include "dawn/AST/ASTVisitor.h"
#include "dawn/AST/LocationType.h"
#include "dawn/IIR/AST.h"
#include "dawn/IIR/ASTExpr.h"

#include "ASTStencilBody.h"
#include "dawn/Support/Assert.h"

namespace dawn {
namespace codegen {
namespace cudaico {

using MergeGroupMap =
    std::map<int, std::vector<std::vector<std::shared_ptr<ast::ReductionOverNeighborExpr>>>>;

// https://stackoverflow.com/questions/1964150/c-test-if-2-sets-are-disjoint
template <class Set1, class Set2>
bool static is_disjoint(const Set1& set1, const Set2& set2) {
  if(set1.empty() || set2.empty())
    return true;

  typename Set1::const_iterator it1 = set1.begin(), it1End = set1.end();
  typename Set2::const_iterator it2 = set2.begin(), it2End = set2.end();

  if(*it1 > *set2.rbegin() || *it2 > *set1.rbegin())
    return true;

  while(it1 != it1End && it2 != it2End) {
    if(*it1 == *it2)
      return false;
    if(*it1 < *it2) {
      it1++;
    } else {
      it2++;
    }
  }

  return true;
}

class ReductionMergeGroupsComputer {

  class FindReadSet : public ast::ASTVisitorForwardingNonConst {
    std::set<int> readSet_;

    void visit(const std::shared_ptr<ast::FieldAccessExpr>& expr) override {
      readSet_.insert(*expr->getData<iir::IIRAccessExprData>().AccessID);
      for(auto& s : expr->getChildren()) {
        s->accept(*this);
      }
    }
    void visit(const std::shared_ptr<ast::VarAccessExpr>& expr) override {
      readSet_.insert(*expr->getData<iir::IIRAccessExprData>().AccessID);
    }

  public:
    std::set<int> getReadSet() const { return readSet_; }
  };

  static std::set<int> GetReadSet(const std::shared_ptr<ast::Stmt>& stmt) {
    FindReadSet readSetFinder;
    stmt->accept(readSetFinder);
    return readSetFinder.getReadSet();
  }

  static int GetWriteID(const std::shared_ptr<ast::Stmt>& stmt) {
    DAWN_ASSERT(stmt->getKind() == ast::Stmt::Kind::ExprStmt ||
                stmt->getKind() == ast::Stmt::Kind::VarDeclStmt);
    if(stmt->getKind() == ast::Stmt::Kind::ExprStmt) {
      auto exprStmt = std::static_pointer_cast<ast::ExprStmt>(stmt);
      auto assignmentExpr = std::static_pointer_cast<ast::AssignmentExpr>(exprStmt->getExpr());

      if(assignmentExpr->getLeft()->getKind() == ast::Expr::Kind::VarAccessExpr) {
        auto varAccess = std::static_pointer_cast<ast::VarAccessExpr>(assignmentExpr->getLeft());
        return *varAccess->getData<iir::IIRAccessExprData>().AccessID;
      } else if(assignmentExpr->getLeft()->getKind() == ast::Expr::Kind::FieldAccessExpr) {
        auto fieldAccess =
            std::static_pointer_cast<ast::FieldAccessExpr>(assignmentExpr->getLeft());
        return *fieldAccess->getData<iir::IIRAccessExprData>().AccessID;
      } else {
        DAWN_ASSERT(false);
      }
    }
    if(stmt->getKind() == ast::Stmt::Kind::VarDeclStmt) {
      return *stmt->getData<iir::VarDeclStmtData>().AccessID;
    }
    DAWN_ASSERT(false);
    return -1;
  }

  class FindMergeGroupsVisitor : public ast::ASTVisitorForwarding {

    MergeGroupMap blockMergeGroups;

    void visit(const std::shared_ptr<const ast::BlockStmt>& stmt) override {
      std::vector<std::vector<std::shared_ptr<ast::ReductionOverNeighborExpr>>> mergeGroups;

      int outerIterator = 0;
      int innerIterator = 0;

      auto mayContainReduction = [](const ast::Stmt& stmt) -> bool {
        return stmt.getKind() == ast::Stmt::Kind::ExprStmt ||
               stmt.getKind() == ast::Stmt::Kind::VarDeclStmt;
      };

      auto getReduction = [&](ast::Stmt& stmt) -> std::shared_ptr<ast::ReductionOverNeighborExpr> {
        if(!mayContainReduction(stmt)) {
          return 0;
        }
        FindReduceOverNeighborExpr redFinder;
        stmt.accept(redFinder);
        if(!redFinder.hasReduceOverNeighborExpr()) {
          return 0;
        }
        DAWN_ASSERT(redFinder.reduceOverNeighborExprs().size() == 1);
        return redFinder.reduceOverNeighborExprs()[0];
      };

      std::set<int> writeSet;
      std::set<int> readSet;

      while(outerIterator < stmt->getStatements().size()) {
        const auto& curRedcution = getReduction(*stmt->getStatements()[outerIterator]);
        if(curRedcution) {
          writeSet.insert(GetWriteID(stmt->getStatements()[outerIterator]));
          std::vector<std::shared_ptr<ast::ReductionOverNeighborExpr>> mergeGroup;
          mergeGroup.push_back(curRedcution);
          innerIterator = outerIterator + 1;
          bool compatible = true;
          while(compatible && innerIterator < stmt->getStatements().size()) {
            const auto& nextRedcution = getReduction(*stmt->getStatements()[innerIterator]);
            if(!nextRedcution) {
              compatible = false;
              break;
            } else {
              auto nextReadSet = GetReadSet(stmt->getStatements()[innerIterator]);
              readSet.insert(nextReadSet.begin(), nextReadSet.end());
              compatible &= curRedcution->getIterSpace() == nextRedcution->getIterSpace();
              compatible &= is_disjoint(writeSet, readSet);
              if(compatible) {
                mergeGroup.push_back(nextRedcution);
                innerIterator++;
              } else {
                break;
              }
            }
          }
          mergeGroups.push_back(mergeGroup);
          outerIterator = innerIterator;
        } else {
          outerIterator++;
        }
      }
      blockMergeGroups[stmt->getID()] = mergeGroups;

      for(auto& s : stmt->getChildren()) {
        s->accept(*this);
      }
    }

  public:
    MergeGroupMap getMergGroupsByBlock() { return blockMergeGroups; }

    void dumpMergeGroups() {
      for(const auto& blockIt : blockMergeGroups) {
        std::cout << "BLOCK: " << blockIt.first << "\n";
        for(int groupIt = 0; groupIt < blockIt.second.size(); groupIt++) {
          std::cout << "  GROUP: " << groupIt << "\n";
          for(auto& redIt : blockIt.second[groupIt]) {
            std::cout << "    " << ast::ASTStringifier::toString(redIt) << "\n";
          }
        }
      }
    }
  };

public:
  static MergeGroupMap ComputeReductionMergeGroups(
      const std::shared_ptr<iir::StencilInstantiation>& stencilInstantiation) {
    FindMergeGroupsVisitor mergeGroupVtor;
    for(const auto& doMethod : iterateIIROver<iir::DoMethod>(*(stencilInstantiation->getIIR()))) {
      doMethod->getAST().accept(mergeGroupVtor);
    }
    return mergeGroupVtor.getMergGroupsByBlock();
  }
};

} // namespace cudaico
} // namespace codegen
} // namespace dawn