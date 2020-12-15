# -*- coding: utf-8 -*-
##===-----------------------------------------------------------------------------*- Python -*-===##
##                          _
##                         | |
##                       __| | __ ___      ___ ___
##                      / _` |/ _` \ \ /\ / / '_  |
##                     | (_| | (_| |\ V  V /| | | |
##                      \__,_|\__,_| \_/\_/ |_| |_| - Compiler Toolchain
##
##
##  This file is distributed under the MIT License (MIT).
##  See LICENSE.txt for details.
##
##===------------------------------------------------------------------------------------------===##

"""Generate input for StagerMerger tests"""

import os

import dawn4py
from dawn4py.serialization import SIR
from dawn4py.serialization import utils as sir_utils
from google.protobuf.json_format import MessageToJson, Parse

backend = "c++-naive-ico"

def sparse_temporary():
    outputfile = "DontDemoteSparse"
    interval = sir_utils.make_interval(SIR.Interval.Start, SIR.Interval.End, 0, 0)

    body_ast = sir_utils.make_ast(
        [
            sir_utils.make_loop_stmt(
                sir_utils.make_assignment_stmt(    
                sir_utils.make_field_access_expr("sparseF"),                
                sir_utils.make_literal_access_expr("1.", SIR.BuiltinType.Double),
                "="),
                [SIR.LocationType.Value("Edge"), SIR.LocationType.Value("Cell"), SIR.LocationType.Value("Edge")]),
            sir_utils.make_assignment_stmt(
                sir_utils.make_field_access_expr("outF"),
                sir_utils.make_reduction_over_neighbor_expr(
                    "+", 
                    sir_utils.make_binary_operator(
                        sir_utils.make_unstructured_field_access_expr("inF", horizontal_offset=sir_utils.make_unstructured_offset(False)),
                        "*",
                        sir_utils.make_unstructured_field_access_expr("sparseF", horizontal_offset=sir_utils.make_unstructured_offset(True))),
                    sir_utils.make_literal_access_expr("0.", SIR.BuiltinType.Double),
                    [SIR.LocationType.Value("Edge"), SIR.LocationType.Value("Cell"), SIR.LocationType.Value("Edge")]),
                "="),           
        ]
    )

    vertical_region_stmt = sir_utils.make_vertical_region_decl_stmt(
        body_ast, interval, SIR.VerticalRegion.Forward
    )

    sir = sir_utils.make_sir(
        outputfile,
        SIR.GridType.Value("Unstructured"),
        [
            sir_utils.make_stencil(
                "generated",
                sir_utils.make_ast([vertical_region_stmt]),
                [
                    sir_utils.make_field(
                        "inF",
                        sir_utils.make_field_dimensions_unstructured(
                            [SIR.LocationType.Value("Edge")], 1
                        ),
                    ),
                    sir_utils.make_field(
                        "outF",
                        sir_utils.make_field_dimensions_unstructured(
                            [SIR.LocationType.Value("Edge")], 1
                        ),
                    ),
                    sir_utils.make_field(
                        "sparseF",
                        sir_utils.make_field_dimensions_unstructured(
                            [SIR.LocationType.Value("Edge"), SIR.LocationType.Value("Cell"), SIR.LocationType.Value("Edge")], 1
                        ),
                        is_temporary=True
                    ),                   
                ],
            ),
        ],
    )
    sim = dawn4py.lower_and_optimize(sir, groups=[])
    with open(outputfile, mode="w") as f:
        f.write(MessageToJson(sim["generated"]))
    os.rename(outputfile, "../input/" + outputfile + ".iir")


if __name__ == "__main__":
    sparse_temporary()    
