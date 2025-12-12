"""
Copyright (C) 2012-2025 myrsloik and other Vapoursynth contributors
Copyright (C) 2025 yuygfgg

This file is part of Vapoursynth-llvmexpr.

This file is derived from and has been significantly modified from test/expr_test.py
from the Vapoursynth project (https://github.com/vapoursynth/vapoursynth),
which was licensed under LGPL-2.1.

As Vapoursynth-llvmexpr is licensed under the GNU General Public License v3, this
modified file is also distributed under the same license.

Vapoursynth-llvmexpr is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Vapoursynth-llvmexpr is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Vapoursynth-llvmexpr.  If not, see <https://www.gnu.org/licenses/>.
"""

import pytest
import vapoursynth as vs
import numpy as np


def get_pixel_value(clip: vs.VideoNode) -> int:
    frame = clip.get_frame(0)
    arr = np.asarray(frame[0])
    return arr[0, 0]


@pytest.fixture(scope="session")
def core() -> vs.Core:
    vs_core = vs.core
    vs_core.num_threads = 1
    return vs_core


EXPR_TEST_CASES = [
    pytest.param(58, "x 2 *", 116, id="op1: multiply"),
    pytest.param(57, "x 2 /", 28, id="op2: integer division"),
    pytest.param(58, "x 2 / 0.1 +", 29, id="op3: float division and add"),
    pytest.param(58, "x 10 +", 68, id="op4: add"),
    pytest.param(58, "x 28 -", 30, id="op5: subtract"),
    pytest.param(58, "x -1 * abs", 58, id="op6: abs"),
    pytest.param(58, "x sqrt", 8, id="op7: sqrt"),
    pytest.param(58, "x dup -", 0, id="op8: dup subtract"),
    pytest.param(58, "x dup +", 116, id="op9: dup add"),
    pytest.param(58, "2 x swap /", 29, id="op10: swap divide"),
    pytest.param(58, "x 60 max", 60, id="op11: max"),
    pytest.param(58, "40 x min", 40, id="op12: min"),
    pytest.param(2, "x exp", 7, id="op13: exp(2)"),
    pytest.param(3, "x exp", 20, id="op14: exp(3)"),
    pytest.param(0, "x exp", 1, id="op15: exp(0)"),
    pytest.param(58, "x log", 4, id="op16: log"),
    pytest.param(58, "x log exp", 58, id="op17: log exp identity"),
    pytest.param(58, "x 10 <", 0, id="op18: less than (false)"),
    pytest.param(58, "10 x <", 1, id="op19: less than (true)"),
    pytest.param(58, "58 x <", 0, id="op20: less than (equal)"),
    pytest.param(58, "x 58 <", 0, id="op21: less than (equal)"),
    pytest.param(58, "10 x >", 0, id="op22: greater than (false)"),
    pytest.param(58, "x 10 >", 1, id="op23: greater than (true)"),
    pytest.param(58, "58 x >", 0, id="op24: greater than (equal)"),
    pytest.param(58, "x 58 >", 0, id="op25: greater than (equal)"),
    pytest.param(58, "x 10 <=", 0, id="op26: less than or equal (false)"),
    pytest.param(58, "10 x <=", 1, id="op27: less than or equal (true)"),
    pytest.param(58, "58 x <=", 1, id="op28: less than or equal (equal)"),
    pytest.param(58, "x 58 <=", 1, id="op29: less than or equal (equal)"),
    pytest.param(58, "10 x >=", 0, id="op30: greater than or equal (false)"),
    pytest.param(58, "x 10 >=", 1, id="op31: greater than or equal (true)"),
    pytest.param(58, "58 x >=", 1, id="op32: greater than or equal (equal)"),
    pytest.param(58, "x 58 >=", 1, id="op33: greater than or equal (equal)"),
    pytest.param((58, 58), "x y =", 1, id="op34: equals (true)"),
    pytest.param(4, "x x 1 - =", 0, id="op35: equals (false)"),
    pytest.param(58, "x 58 =", 1, id="op54: equals (true, single clip)"),
    pytest.param((0, 58), "x y and", 0, id="op36: logical and (0, non-0)"),
    pytest.param((0, 58), "x y or", 1, id="op37: logical or (0, non-0)"),
    pytest.param((0, 58), "x y xor", 1, id="op38: logical xor (0, non-0)"),
    pytest.param((1, 0), "x y and", 0, id="op39: logical and (non-0, 0)"),
    pytest.param((1, 0), "x y or", 1, id="op40: logical or (non-0, 0)"),
    pytest.param((1, 0), "x y xor", 1, id="op41: logical xor (non-0, 0)"),
    pytest.param((0, 0), "x y and", 0, id="op42: logical and (0, 0)"),
    pytest.param((0, 0), "x y or", 0, id="op43: logical or (0, 0)"),
    pytest.param((0, 0), "x y xor", 0, id="op44: logical xor (0, 0)"),
    pytest.param((8, 7), "x y and", 1, id="op45: logical and (non-0, non-0)"),
    pytest.param((8, 7), "x y or", 1, id="op46: logical or (non-0, non-0)"),
    pytest.param((8, 7), "x y xor", 0, id="op47: logical xor (non-0, non-0)"),
    pytest.param((100, 200, 0), "z x y ?", 200, id="op48: ternary (false)"),
    pytest.param((100, 200, 1), "z x y ?", 100, id="op49: ternary (true)"),
    pytest.param((100, 200, 0), "z not x y ?", 100, id="op50: ternary with not (true)"),
    pytest.param(
        (100, 200, 100), "z not x y ?", 200, id="op51: ternary with not (false)"
    ),
    pytest.param(58, "x not", 0, id="op52: logical not (non-zero)"),
    pytest.param(58, "x not not", 1, id="op53: logical double not"),
    pytest.param(3, "x 2 pow", 9, id="op55: power"),
    pytest.param(6, "2 x pow", 64, id="op56: power (swapped)"),
    pytest.param((10, 2, 3), "x y z swap2 * +", 23, id="op57: swap2 (vars)"),
    pytest.param((10, 2, 3), "10 2 3 swap2 * +", 23, id="op58: swap2 (literals)"),
    pytest.param((10, 2, 3), "x y z swap1 * +", 16, id="op59: swap1 (vars)"),
    pytest.param((10, 2, 3), "10 2 3 swap1 * +", 16, id="op60: swap1 (literals)"),
    pytest.param(
        (10, 2, 3),
        "x dup0 dup1 dup2 y swap3 z * + + swap / +",
        35,
        id="op61: complex stack (vars)",
    ),
    pytest.param(
        (10, 2, 3),
        "10 dup0 dup1 dup2 2 swap3 3 * + + swap / +",
        35,
        id="op62: complex stack (literals)",
    ),
    pytest.param(
        (10, 2, 3),
        "x dup0 10 dup2 y swap3 3 * + + swap / +",
        35,
        id="op63: complex stack (mixed)",
    ),
]


@pytest.mark.parametrize("initial_colors, expression, expected_value", EXPR_TEST_CASES)
def test_expr_operations(
    core: vs.Core, initial_colors, expression: str, expected_value: int
):
    if isinstance(initial_colors, tuple):
        clips = [core.std.BlankClip(format=vs.GRAY8, color=c) for c in initial_colors]
    else:
        clips = core.std.BlankClip(format=vs.GRAY8, color=initial_colors)

    result_clip = core.llvmexpr.Expr(clips, expression)

    assert get_pixel_value(result_clip) == expected_value

