//===--------------------------------------------------------------------------------*- C++ -*-===//
//                         _       _
//                        | |     | |
//                    __ _| |_ ___| | __ _ _ __   __ _
//                   / _` | __/ __| |/ _` | '_ \ / _` |
//                  | (_| | || (__| | (_| | | | | (_| |
//                   \__, |\__\___|_|\__,_|_| |_|\__, | - GridTools Clang DSL
//                    __/ |                       __/ |
//                   |___/                       |___/
//
//
//  This file is distributed under the MIT License (MIT).
//  See LICENSE.txt for details.
//
//===------------------------------------------------------------------------------------------===//

// RUN: %gtclang% %file% -fno-codegen -freport-pass-stage-split
// EXPECTED: PASS: PassStageSplitter: Test: no split

#include "gtclang_dsl_defs/gtclang_dsl.hpp"

using namespace gtclang::dsl;

stencil Test {
  storage field_a, field_b, field_c, field_d, field_e, field_f;

  Do {
    vertical_region(k_start, k_end) {
      field_d = field_e(i + 1);
      field_c = field_d;
      field_b = field_f(i + 1);
      field_a = field_b + field_c;
    }
  }
};

int main() {}
