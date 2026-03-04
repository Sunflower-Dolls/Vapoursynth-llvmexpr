"""
Copyright (C) 2025 sgt0
Copyright (C) 2025 yuygfgg

This file is part of Vapoursynth-llvmexpr.

This file is derived from and has been modified from code
originally contributed by sgt0 in a pull request to the akarin-vapoursynth-plugin
project (https://github.com/Jaded-Encoding-Thaumaturgy/akarin-vapoursynth-plugin/pull/12),
which was licensed under LGPLv3.

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

import random
import pytest
import vapoursynth as vs
from conftest import get_expr_func

core = vs.core


@pytest.mark.parametrize(
    "input_format, a, b, expr, expected",
    [
        (vs.GRAYS, 2.0, 3.0, "x y +", 5.0),
        (vs.GRAYS, 7.0, 5.0, "x y -", 2.0),
        (vs.GRAYS, 4.0, 2.5, "x y *", 10.0),
        (vs.GRAYS, 7.5, 2.5, "x y /", 3.0),
        (vs.GRAYS, 5.25, 1.0, "x 1.0 %", 0.25),
    ],
)
def test_arithmetic(
    backend: str, input_format: int, a: float, b: float, expr: str, expected: float
) -> None:
    expr_func = get_expr_func(backend)
    c1 = core.std.BlankClip(format=input_format, color=a)
    c2 = core.std.BlankClip(format=input_format, color=b)
    res = expr_func([c1, c2], expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(expected)


@pytest.mark.parametrize(
    "a, b, expr, expected",
    [
        (3.0, 2.0, "x y >", 1.0),
        (2.0, 3.0, "x y <", 1.0),
        (3.0, 3.0, "x y =", 1.0),
        (3.0, 3.0, "x y >=", 1.0),
        (2.0, 3.0, "x y <=", 1.0),
        (1.0, 0.0, "x y and", 0.0),
        (1.0, 0.0, "x y or", 1.0),
        (1.0, 1.0, "x y xor", 0.0),
        (0.0, 0.0, "x not", 1.0),
        (-2, -3, "x y and", 0.0),
        (-2, -1, "x y or", 0.0),
        (-2, -1, "x y xor", 0.0),
        (-2, 3, "x y xor", 1.0),
    ],
)
def test_comparison_and_logical(
    backend: str, a: float, b: float, expr: str, expected: float
) -> None:
    expr_func = get_expr_func(backend)
    c1 = core.std.BlankClip(format=vs.GRAYS, color=a)
    c2 = core.std.BlankClip(format=vs.GRAYS, color=b)
    res = expr_func([c1, c2], expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(expected)


@pytest.mark.parametrize(
    "input_format, input_value, expr, expected",
    [
        (vs.GRAY8, 0, "0 exp", 1.0),
        (vs.GRAY8, 0, "x exp", 1.0),
        (vs.GRAY8, 1, "x exp", 2.71828),
        (vs.GRAYS, 0.5, "x exp", 1.64872),
    ],
)
def test_exp(
    backend: str, input_format: int, input_value: int, expr: str, expected: float
) -> None:
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=input_format, color=input_value)
    result = expr_func(clip, expr, vs.GRAYS)
    assert result.get_frame(0)[0][0, 0] == pytest.approx(expected)


@pytest.mark.parametrize(
    "input_format, input_value, expr, expected",
    [
        (vs.GRAY8, 1, "x log", 0),
        (vs.GRAYS, 7.38905, "x log", 2),
    ],
)
def test_log(
    backend: str, input_format: int, input_value: int, expr: str, expected: float
) -> None:
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=input_format, color=input_value)
    result = expr_func(clip, expr, vs.GRAYS)
    assert result.get_frame(0)[0][0, 0] == pytest.approx(expected)


@pytest.mark.parametrize(
    "val, expr, expected",
    [
        (1.9, "x floor", 1.0),
        (1.1, "x ceil", 2.0),
        (2.49, "x round", 2.0),
        (2.5, "x round", 3.0),
        (-2.9, "x trunc", -2.0),
        (2.9, "x abs", 2.9),
        (3.0, "x neg", -3.0),
        (-5.0, "x sgn", -1.0),
        (0.0, "x sgn", 0.0),
        (7.0, "x sgn", 1.0),
        (2.0, "x -3 copysign", -2.0),
        (2.0, "2 3 4 fma", 10.0),
    ],
)
def test_rounding_and_misc(
    backend: str, val: float, expr: str, expected: float
) -> None:
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=val)
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(expected)


@pytest.mark.parametrize("input_format", [vs.GRAY8, vs.GRAY16, vs.GRAYS])
@pytest.mark.parametrize(
    "expr, expected",
    [
        ("x 1.5 pow", 0.0),
    ],
)
def test_pow(backend: str, input_format: int, expr: str, expected: float) -> None:
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=input_format, color=0)
    result = expr_func(clip, expr, vs.GRAYS)
    assert result.get_frame(0)[0][0, 0] == pytest.approx(expected)


@pytest.mark.parametrize(
    "input_format, input_value, expr, expected",
    [
        (vs.GRAY8, 0, "x sin", 0),
        (vs.GRAY8, 1, "x sin", 0.8414709568023682),
        (vs.GRAY8, 2, "x sin", 0.9092974066734314),
    ],
)
def test_sin(
    backend: str, input_format: int, input_value: int, expr: str, expected: float
) -> None:
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=input_format, color=input_value)
    result = expr_func(clip, expr, vs.GRAYS)
    assert result.get_frame(0)[0][0, 0] == pytest.approx(expected)


def test_gh_11(backend: str) -> None:
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=vs.GRAY8, color=0)
    result = expr_func(clip, "x 128 / 0.86 pow 255 *")
    assert result.get_frame(0)[0][0, 0] == pytest.approx(6.122468756907559e-31)

    clip = core.std.BlankClip(format=vs.GRAY16, color=0)
    result = expr_func(clip, "x 32768 / 0.86 pow 65535 *")
    assert result.get_frame(0)[0][0, 0] == pytest.approx(1.5734745330615421e-28)


@pytest.mark.parametrize(
    "val, min_v, max_v",
    [
        (5.0, 2.0, 8.0),
        (1.0, 2.0, 8.0),
    ],
)
def test_min_max_clip(backend: str, val: float, min_v: float, max_v: float) -> None:
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=val)
    res_max = expr_func(c, "x 3 max", vs.GRAYS)
    res_min = expr_func(c, "x 3 min", vs.GRAYS)
    res_clip = expr_func(c, f"x {min_v} {max_v} clip", vs.GRAYS)
    assert res_max.get_frame(0)[0][0, 0] == pytest.approx(max(val, 3.0))
    assert res_min.get_frame(0)[0][0, 0] == pytest.approx(min(val, 3.0))
    assert res_clip.get_frame(0)[0][0, 0] == pytest.approx(min(max(val, min_v), max_v))


@pytest.mark.parametrize(
    "a, b, expr, expected",
    [
        (5.9, 1.0, "x y bitand", 0.0),
        (5.0, 2.0, "x y bitor", 7.0),
        (5.0, 1.0, "x y bitxor", 4.0),
        (
            5.0,
            0.0,
            "x bitnot",
            (float(2**32 - 1 - 5)),
        ),
    ],
)
def test_bitwise(backend: str, a: float, b: float, expr: str, expected: float) -> None:
    expr_func = get_expr_func(backend)
    c1 = core.std.BlankClip(format=vs.GRAYS, color=a)
    c2 = core.std.BlankClip(format=vs.GRAYS, color=b)
    res = expr_func([c1, c2], expr, vs.GRAYS)
    # bitnot result depends on implementation width; for generality, compare masked
    out = res.get_frame(0)[0][0, 0]
    if "bitnot" in expr:
        assert int(out) & 0xFFFFFFFF == (~int(a)) & 0xFFFFFFFF
    else:
        assert out == pytest.approx(expected)


def test_stack_manipulation(backend: str) -> None:
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=3.0)
    res_dup = expr_func(c, "x dup *", vs.GRAYS)
    assert res_dup.get_frame(0)[0][0, 0] == pytest.approx(9.0)
    c0 = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    res_swap = expr_func(c0, "2 5 swap -", vs.GRAYS)
    assert res_swap.get_frame(0)[0][0, 0] == pytest.approx(3.0)
    res_drop = expr_func(c0, "1 2 3 drop2", vs.GRAYS)
    assert res_drop.get_frame(0)[0][0, 0] == pytest.approx(1.0)
    res_sort = expr_func(c0, "3 1 2 sort3 drop2", vs.GRAYS)
    assert res_sort.get_frame(0)[0][0, 0] == pytest.approx(3.0)


_LARGE_SORT_TEST_DATA = []
_rng = random.Random(42)
for n in list(range(1, 65)) + [137, 279]:
    numbers = [_rng.uniform(-1000, 1000) for _ in range(n)]
    _LARGE_SORT_TEST_DATA.append((n, numbers))


@pytest.mark.parametrize("n, numbers", _LARGE_SORT_TEST_DATA)
def test_large_sort(backend: str, n: int, numbers: list[float]) -> None:
    expr_func = get_expr_func(backend)
    c0 = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    expr = " ".join(map(str, numbers)) + f" sort{n}"
    expr_kwargs = {"tile_x": 0, "tile_y": 0} if backend == "Expr" else {}

    for i in range(n):
        full_expr = expr + f" drop{n - i - 1} a! drop{i} a@"
        res = expr_func(c0, full_expr, vs.GRAYS, **expr_kwargs)
        val = res.get_frame(0)[0][0, 0]
        assert val == pytest.approx(sorted(numbers)[n - 1 - i])


@pytest.mark.parametrize(
    "numbers, expr, expected",
    [
        ([2, 1, 0, 3], "argmin4", 2),
        ([2, 1, 0, 3], "argmax4", 3),
        ([0, 0, 0, 0], "argmin4", 0),
        ([3, 3, 3, 3], "argmax4", 0),
        ([1, 2, 3, 1], "argmin4", 0),
        ([5, 1, 5, 2], "argmax4", 0),
        ([5, -1, 10, 10, 2], "argmax5", 2),
    ],
)
def test_argmin_argmax(
    backend: str, numbers: list[float], expr: str, expected: float
) -> None:
    expr_func = get_expr_func(backend)
    c0 = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    full_expr = " ".join(map(str, numbers)) + f" {expr}"
    res = expr_func(c0, full_expr, vs.GRAYS)
    val = res.get_frame(0)[0][0, 0]
    assert val == pytest.approx(expected)


@pytest.mark.parametrize(
    "numbers, n",
    [
        ([2, 1, 0, 3], 4),
        ([1, 1, 1, 1], 4),
        ([5, 2, 5, 1], 4),
        ([10, 20, 30, 10, 20], 5),
    ],
)
def test_argsort(backend: str, numbers: list[float], n: int) -> None:
    expr_func = get_expr_func(backend)
    c0 = core.std.BlankClip(format=vs.GRAYS, color=0.0)

    indexed_numbers = list(enumerate(numbers))
    indexed_numbers.sort(key=lambda x: x[1])
    expected_indices = [float(idx) for idx, val in indexed_numbers]

    full_expr = " ".join(map(str, numbers)) + f" argsort{n}"

    for i in range(n):
        test_expr = full_expr + f" drop{i} a! drop{n - 1 - i} a@"
        res = expr_func(c0, test_expr, vs.GRAYS)
        val = res.get_frame(0)[0][0, 0]
        assert val == pytest.approx(expected_indices[i])


def test_named_variables_and_loop_power(backend: str) -> None:
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=2.0)
    y = core.std.BlankClip(format=vs.GRAYS, color=4.0)
    expr = "x base! 1 result! y counter! #loop result@ base@ * result! counter@ 1 - counter! counter@ loop# result@"
    res = expr_func([c, y], expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(16.0)


def test_constants_and_coords(backend: str) -> None:
    expr_func = get_expr_func(backend)
    c0 = core.std.BlankClip(format=vs.GRAYS, color=0.0, width=3, height=2)
    res_pi = expr_func(c0, "pi", vs.GRAYS)
    assert res_pi.get_frame(0)[0][0, 0] == pytest.approx(3.14159265, rel=1e-6)
    res_N = expr_func(c0, "N", vs.GRAYS)
    assert res_N.get_frame(3)[0][0, 0] == pytest.approx(3.0)
    res_wh = expr_func(c0, "width height +", vs.GRAYS)
    assert res_wh.get_frame(0)[0][0, 0] == pytest.approx(5.0)
    res_X = expr_func(c0, "X", vs.GRAYS)
    res_Y = expr_func(c0, "Y", vs.GRAYS)
    fX = res_X.get_frame(0)
    fY = res_Y.get_frame(0)
    assert fX[0][1, 2] == pytest.approx(2.0)
    assert fY[0][1, 2] == pytest.approx(1.0)


def test_conditional_ternary(backend: str) -> None:
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=10.0)
    res = expr_func(c, "x 5 > 1 0 ?", vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(1.0)


def test_pixel_access_static_and_dynamic(backend: str) -> None:
    expr_func = get_expr_func(backend)
    base = core.std.BlankClip(format=vs.GRAYS, color=0.0, width=4, height=2)
    ramp = expr_func(base, "X", vs.GRAYS)
    src = core.std.BlankClip(format=vs.GRAYS, color=99.0, width=4, height=2)
    expr_rel = "y[-1,0]"
    res_rel = expr_func([src, ramp], expr_rel, vs.GRAYS)
    f = res_rel.get_frame(0)
    assert f[0][0, 2] == pytest.approx(1.0)
    expr_abs = "1 1 y[]"
    res_abs = expr_func([src, ramp], expr_abs, vs.GRAYS)
    assert res_abs.get_frame(0)[0][0, 0] == pytest.approx(1.0)


def test_frame_property_access(backend: str) -> None:
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    c = core.std.SetFrameProps(c, _TestProp=0.25)
    res = expr_func(c, "x._TestProp", vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(0.25)


def test_frame_property_exists(backend: str) -> None:
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=0.0)

    c_with_prop = core.std.SetFrameProps(c, _TestProp=123)
    res_exists = expr_func(c_with_prop, "x._TestProp?", vs.GRAYS)
    assert res_exists.get_frame(0)[0][0, 0] == pytest.approx(1.0)

    res_not_exists = expr_func(c, "x._MissingProp?", vs.GRAYS)
    assert res_not_exists.get_frame(0)[0][0, 0] == pytest.approx(0.0)

    c2 = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    c2_with_prop = core.std.SetFrameProps(c2, _AnotherProp=456)

    res_multi_exists = expr_func([c, c2_with_prop], "y._AnotherProp?", vs.GRAYS)
    assert res_multi_exists.get_frame(0)[0][0, 0] == pytest.approx(1.0)

    res_multi_not_exists = expr_func([c_with_prop, c2], "y._TestProp?", vs.GRAYS)
    assert res_multi_not_exists.get_frame(0)[0][0, 0] == pytest.approx(0.0)

    res_multi_src1 = expr_func([c, c2_with_prop], "src1._AnotherProp?", vs.GRAYS)
    assert res_multi_src1.get_frame(0)[0][0, 0] == pytest.approx(1.0)


def test_direct_output_write_and_exit(backend: str) -> None:
    expr_func = get_expr_func(backend)
    base = core.std.BlankClip(format=vs.GRAYS, color=0.0, width=4, height=4)
    expr = "X 1 = Y 2 = and 5 1 2 @[] ^exit^ 0 ?"
    res = expr_func(base, expr, vs.GRAYS)
    fr = res.get_frame(0)
    assert fr[0][2, 1] == pytest.approx(5.0)
    assert fr[0][0, 0] == pytest.approx(0.0)


boundary_test_cases = [
    # Default boundary (clamp)
    pytest.param("x[-1,-1]", None, 0, 0, 0.0, id="clamp_default_topleft"),
    pytest.param("x[1,1]", None, 3, 3, 15.0, id="clamp_default_bottomright"),
    pytest.param("x[-2,0]", None, 1, 1, 4.0, id="clamp_default_rel"),
    # Explicit clamp with boundary parameter
    pytest.param("x[-1,-1]", 0, 0, 0, 0.0, id="clamp_param_topleft"),
    pytest.param("x[1,1]", 0, 3, 3, 15.0, id="clamp_param_bottomright"),
    # Explicit clamp with :c suffix
    pytest.param("x[-1,-1]:c", None, 0, 0, 0.0, id="clamp_suffix_topleft"),
    pytest.param("x[1,1]:c", None, 3, 3, 15.0, id="clamp_suffix_bottomright"),
    # Mirror with boundary parameter
    pytest.param("x[-1,-1]", 1, 0, 0, 0.0, id="mirror_param_topleft"),
    pytest.param("x[1,1]", 1, 3, 3, 15.0, id="mirror_param_bottomright"),
    pytest.param("x[-2,0]", 1, 1, 1, 4.0, id="mirror_param_rel"),
    # Mirror with :m suffix
    pytest.param("x[-1,-1]:m", None, 0, 0, 0.0, id="mirror_suffix_topleft"),
    pytest.param("x[1,1]:m", None, 3, 3, 15.0, id="mirror_suffix_bottomright"),
    # Override behavior
    pytest.param("x[-1,-1]:m", 0, 0, 0, 0.0, id="override_clamp_with_mirror"),
    pytest.param("x[-1,-1]:c", 1, 0, 0, 0.0, id="override_mirror_with_clamp"),
    # More mirror tests
    pytest.param("x[4,4]", 1, 0, 0, 15.0, id="mirror_param_far_coord1"),
    pytest.param("x[5,5]", 1, 0, 0, 10.0, id="mirror_param_far_coord2"),
    pytest.param("x[-4,-4]", 1, 0, 0, 15.0, id="mirror_param_far_coord3"),
]


@pytest.mark.parametrize("expr, boundary, x, y, expected", boundary_test_cases)
def test_boundary_conditions(
    backend: str,
    ramp_clip: vs.VideoNode,
    expr: str,
    boundary: int | None,
    x: int,
    y: int,
    expected: float,
) -> None:
    expr_func = get_expr_func(backend)
    if boundary:
        res = expr_func(ramp_clip, expr, boundary=boundary)
    else:
        res = expr_func(ramp_clip, expr)

    frame = res.get_frame(0)
    assert frame[0][y, x] == pytest.approx(expected)


# Tests for absolute pixel access boundary conditions
abs_boundary_test_cases = [
    # Default is clamp
    pytest.param("-1 -1 x[]", None, 0.0, id="abs_default_clamp_topleft"),
    pytest.param("4 4 x[]", None, 15.0, id="abs_default_clamp_bottomright"),
    # Default clamp should ignore boundary=1 (mirror)
    pytest.param("-1 -1 x[]", 1, 0.0, id="abs_default_clamp_overrides_mirror_param"),
    # Explicit clamp :c
    pytest.param("-1 -1 x[]:c", None, 0.0, id="abs_explicit_clamp_topleft"),
    # Explicit clamp should ignore boundary=1 (mirror)
    pytest.param("-1 -1 x[]:c", 1, 0.0, id="abs_explicit_clamp_overrides_mirror_param"),
    # Explicit mirror :m
    pytest.param("-1 -1 x[]:m", None, 0.0, id="abs_explicit_mirror_topleft"),
    pytest.param("4 4 x[]:m", None, 15.0, id="abs_explicit_mirror_bottomright"),
    # Explicit mirror should ignore boundary=0 (clamp)
    pytest.param("-1 -1 x[]:m", 0, 0.0, id="abs_explicit_mirror_overrides_clamp_param"),
    # Use boundary param :b
    pytest.param("-1 -1 x[]:b", 0, 0.0, id="abs_b_uses_clamp_param"),
    pytest.param("-1 -1 x[]:b", 1, 0.0, id="abs_b_uses_mirror_param"),
    pytest.param("4 4 x[]:b", 0, 15.0, id="abs_b_uses_clamp_param_br"),
    pytest.param("4 4 x[]:b", 1, 15.0, id="abs_b_uses_mirror_param_br"),
]


@pytest.mark.parametrize("expr, boundary, expected", abs_boundary_test_cases)
def test_abs_boundary_conditions(
    backend: str,
    ramp_clip: vs.VideoNode,
    expr: str,
    boundary: int | None,
    expected: float,
) -> None:
    expr_func = get_expr_func(backend)
    if boundary is not None:
        res = expr_func(ramp_clip, expr, boundary=boundary)
    else:
        res = expr_func(ramp_clip, expr)

    # We test at a single pixel, since the coordinates are absolute
    frame = res.get_frame(0)
    assert frame[0][0, 0] == pytest.approx(expected)


def test_non_integer_coordinate_rounding(backend: str) -> None:
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=0.0, width=4, height=2)
    c = expr_func(c, "X")
    res = expr_func(c, "X 0.5 + Y 0.5 + x[]", vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(0.0)
    assert res.get_frame(0)[0][0, 1] == pytest.approx(2.0)
    assert res.get_frame(0)[0][0, 2] == pytest.approx(2.0)
    assert res.get_frame(0)[0][0, 3] == pytest.approx(3.0)


@pytest.mark.parametrize(
    "expr, err_msg",
    [
        ("2 3 + atan2 1", "Stack underflow"),
        ("1 +", "Stack underflow"),
        ("sin", "Stack underflow"),
        ("1 2 ?", "Stack underflow"),
        ("1 dup1", "Stack underflow"),
        ("1 2", "Expression stack not balanced"),
        ("my_label#", "Undefined label for jump"),
        ("my_var@", "Variable is uninitialized"),
        ("#L #L", "Duplicate label"),
        ("1 drop2", "Stack underflow"),
        ("2 3 swap2", "Stack underflow"),
        ("invalid_token", "Invalid token"),
        ("a{}^10 a{}^10 0", "Statically allocated array cannot be reallocated"),
    ],
)
def test_validation_errors(backend: str, expr: str, err_msg: str) -> None:
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip()
    with pytest.raises(vs.Error, match=err_msg):
        expr_func(c, expr)


def test_tiling_param_validation_expr_backend() -> None:
    clip = core.std.BlankClip(format=vs.GRAYS, width=4, height=4, color=0.0)
    with pytest.raises(vs.Error, match="tile_x must be -1 or >= 0"):
        core.llvmexpr.Expr(clip, "x", tile_x=-2)
    with pytest.raises(vs.Error, match="tile_y must be -1 or >= 0"):
        core.llvmexpr.Expr(clip, "x", tile_y=-2)


def test_tiling_matches_baseline_expr_backend(ramp_clip: vs.VideoNode) -> None:
    expr = "x[-1,0] x[1,0] + X +"
    baseline = core.llvmexpr.Expr(ramp_clip, expr, vs.GRAYS, tile_x=0, tile_y=0)
    tiled = core.llvmexpr.Expr(ramp_clip, expr, vs.GRAYS, tile_x=8, tile_y=4)

    f_baseline = baseline.get_frame(0)
    f_tiled = tiled.get_frame(0)
    for y in range(baseline.height):
        for x in range(baseline.width):
            assert f_tiled[0][y, x] == pytest.approx(f_baseline[0][y, x])


@pytest.mark.parametrize("tile_x, tile_y", [(-1, 0), (0, -1), (-1, -1)])
def test_auto_tiling_matches_baseline_expr_backend(
    ramp_clip: vs.VideoNode, tile_x: int, tile_y: int
) -> None:
    expr = "x[-1,0] x[1,0] + X +"
    baseline = core.llvmexpr.Expr(ramp_clip, expr, vs.GRAYS, tile_x=0, tile_y=0)
    autotiled = core.llvmexpr.Expr(
        ramp_clip, expr, vs.GRAYS, tile_x=tile_x, tile_y=tile_y
    )

    f_baseline = baseline.get_frame(0)
    f_autotiled = autotiled.get_frame(0)
    for y in range(baseline.height):
        for x in range(baseline.width):
            assert f_autotiled[0][y, x] == pytest.approx(f_baseline[0][y, x])


subsampled_test_cases = [
    # Relative access within bounds
    pytest.param("x[0,-1]", 1, 1, 1.0, id="subsampled_rel_in_bounds"),
    # Absolute access within bounds
    pytest.param("0 1 x[]", 0, 0, 2.0, id="subsampled_abs_in_bounds"),
    # Relative access, clamp boundary
    pytest.param("x[-1,-1]", 0, 0, 0.0, id="subsampled_rel_clamp"),
    # Relative access, mirror boundary
    pytest.param("x[-1,-1]:m", 0, 0, 0.0, id="subsampled_rel_mirror"),
    # Absolute access, clamp boundary
    pytest.param("-1 -1 x[]", 0, 0, 0.0, id="subsampled_abs_clamp"),
    # Absolute access, mirror boundary
    pytest.param("-1 -1 x[]:m", 0, 0, 0.0, id="subsampled_abs_mirror"),
    # Relative access, positive out of bounds, check if height is correct
    pytest.param("x[2,0]", 0, 0, 1.0, id="subsampled_rel_clamp_positive_y"),
]


@pytest.mark.parametrize("expr, x, y, expected", subsampled_test_cases)
def test_subsampled_plane_access(
    backend: str,
    subsampled_ramp_clip: vs.VideoNode,
    expr: str,
    x: int,
    y: int,
    expected: float,
) -> None:
    expr_func = get_expr_func(backend)
    res = expr_func(subsampled_ramp_clip, ["", expr])
    frame = res.get_frame(0)
    assert frame[1][y, x] == pytest.approx(expected)


def test_array_static_allocation_basic(backend: str) -> None:
    """
    Test basic static array allocation and access.
    """
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=vs.GRAYS, width=10, height=10, color=0)
    expr = "buffer{}^10 42.0 5 buffer{}! 5 buffer{}@"
    res = expr_func(clip, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(42.0)


def test_array_write_and_read_multiple(backend: str) -> None:
    """
    Test writing and reading multiple values to/from array.
    """
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=vs.GRAYS, width=10, height=10, color=0)
    expr = """
        arr{}^5
        10.0 0 arr{}!
        20.0 1 arr{}!
        30.0 2 arr{}!
        0 arr{}@ 1 arr{}@ + 2 arr{}@ +
    """
    res = expr_func(clip, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(60.0)


def test_array_lookup_table(backend: str) -> None:
    """
    Test using array as a lookup table.
    """
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=vs.GRAYS, width=10, height=10, color=2.0)
    # Create a lookup table with powers of 2
    expr = """
        lut{}^5
        1.0 0 lut{}!
        2.0 1 lut{}!
        4.0 2 lut{}!
        8.0 3 lut{}!
        16.0 4 lut{}!
        x lut{}@
    """
    res = expr_func(clip, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(4.0)  # lut[2] = 4.0


def test_array_with_variables(backend: str) -> None:
    """
    Test array operations combined with variables.
    """
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=vs.GRAYS, width=10, height=10, color=3.0)
    expr = """
        data{}^3
        100.0 val!
        val@ 0 data{}!
        val@ 2 * 1 data{}!
        val@ 3 * 2 data{}!
        X data{}@
    """
    res = expr_func(clip, expr, vs.GRAYS)
    frame = res.get_frame(0)
    assert frame[0][0, 0] == pytest.approx(100.0)  # data[0]
    assert frame[0][0, 1] == pytest.approx(200.0)  # data[1]
    assert frame[0][0, 2] == pytest.approx(300.0)  # data[2]


def test_array_boundary_access(backend: str) -> None:
    """
    Test accessing first and last elements of array.
    """
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=vs.GRAYS, width=10, height=10, color=0)
    expr = """
        arr{}^10
        111.0 0 arr{}!
        999.0 9 arr{}!
        X 5 < 0 arr{}@ 9 arr{}@ ?
    """
    res = expr_func(clip, expr, vs.GRAYS)
    frame = res.get_frame(0)
    assert frame[0][0, 0] == pytest.approx(111.0)  # x < 5, use arr[0]
    assert frame[0][0, 7] == pytest.approx(999.0)  # x >= 5, use arr[9]


def test_array_float_index_truncation(backend: str) -> None:
    """
    Test that float indices are properly converted to integers.
    """
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=vs.GRAYS, width=10, height=10, color=0)
    # Use float index 2.7, should truncate to 2
    expr = """
        arr{}^5
        10.0 0 arr{}!
        20.0 1 arr{}!
        30.0 2 arr{}!
        40.0 3 arr{}!
        2.7 arr{}@
    """
    res = expr_func(clip, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(30.0)


def test_array_multiple_arrays(backend: str) -> None:
    """
    Test using multiple independent arrays.
    """
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=vs.GRAYS, width=10, height=10, color=0)
    expr = """
        a{}^3
        b{}^3
        10.0 0 a{}!
        20.0 1 a{}!
        100.0 0 b{}!
        200.0 1 b{}!
        0 a{}@ 0 b{}@ +
    """
    res = expr_func(clip, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(110.0)  # a[0] + b[0]


def test_array_dynamic_allocation_error(backend: str) -> None:
    """
    Test that dynamic array allocation fails in Expr mode.
    """
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=vs.GRAYS, width=10, height=10, color=0)
    with pytest.raises(
        vs.Error,
    ):
        expr_func(clip, "10 arr{}^ 0", vs.GRAYS)


def test_array_uninitialized_error(backend: str) -> None:
    """
    Test that using uninitialized array raises an error.
    """
    expr_func = get_expr_func(backend)
    clip = core.std.BlankClip(format=vs.GRAYS, width=10, height=10, color=0)
    with pytest.raises(vs.Error, match="Array is uninitialized"):
        expr_func(clip, "0 arr{}@", vs.GRAYS)


@pytest.mark.parametrize("expr", ["x:width", "y:height", "src0:width^0", "z:height^1"])
def test_clip_dim_tokens_disabled_in_expr(backend: str, expr: str) -> None:
    """
    Test that clip dimension tokens are disabled in Expr mode.
    """
    expr_func = get_expr_func(backend)
    clip1 = core.std.BlankClip()
    clip2 = core.std.BlankClip()
    clip3 = core.std.BlankClip()
    clip4 = core.std.BlankClip()
    with pytest.raises(vs.Error, match="Invalid token"):
        expr_func([clip1, clip2, clip3, clip4], expr)


@pytest.mark.parametrize(
    "input_value, expected",
    [
        (0.3, 2.0),  # x <= 0.5 -> else branch
        (0.6, 1.0),  # x > 0.5 -> if branch
        (0.5, 2.0),  # exactly 0.5 -> else branch (not greater)
    ],
)
def test_control_flow_simple_if_else(
    backend: str, input_value: float, expected: float
) -> None:
    """
    Test simple if-else control flow.

    Infix equivalent:
        val = 0.0
        if ($x > 0.5) { val = 1.0 } else { val = 2.0 }
        RESULT = val
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=input_value)
    # Generated postfix from infix2postfix
    expr = "0.0 val! x 0.5 > 0 = __internal_else_0# 1.0 val! 1 __internal_endif_1# #__internal_else_0 2.0 val! #__internal_endif_1 val@"
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(expected)


@pytest.mark.parametrize(
    "input_value, expected",
    [
        (0.3, 1.0),  # x <= 0.5 -> else branch
        (0.6, 2.0),  # 0.5 < x <= 0.8 -> inner else
        (0.9, 3.0),  # x > 0.8 -> inner if
    ],
)
def test_control_flow_nested_if_else(
    backend: str, input_value: float, expected: float
) -> None:
    """
    Test nested if-else control flow.

    Infix equivalent:
        val = 0.0
        if ($x > 0.5) {
            if ($x > 0.8) { val = 3.0 } else { val = 2.0 }
        } else {
            val = 1.0
        }
        RESULT = val
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=input_value)
    # Generated postfix from infix2postfix
    expr = "0.0 val! x 0.5 > 0 = __internal_else_0# x 0.8 > 0 = __internal_else_2# 3.0 val! 1 __internal_endif_3# #__internal_else_2 2.0 val! #__internal_endif_3 1 __internal_endif_1# #__internal_else_0 1.0 val! #__internal_endif_1 val@"
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(expected)


@pytest.mark.parametrize(
    "input_value, expected",
    [
        (0.1, 3.0),  # 3 levels deep
        (0.25, 4.0),  # 2 levels deep (mid-low)
        (0.4, 5.0),  # 2 levels deep (mid-mid)
        (0.6, 6.0),  # 2 levels deep (mid-high)
        (0.8, 7.0),  # 3 levels deep (high)
    ],
)
def test_control_flow_deeply_nested_if_else(
    backend: str, input_value: float, expected: float
) -> None:
    """
    Test deeply nested if-else (3 levels).

    Infix equivalent:
        val = 0.0
        if ($x < 0.2) {
            val = 3.0
        } else {
            if ($x < 0.5) {
                if ($x < 0.3) { val = 4.0 } else { val = 5.0 }
            } else {
                if ($x < 0.7) { val = 6.0 } else { val = 7.0 }
            }
        }
        RESULT = val
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=input_value)
    expr = """
        0.0 val!
        x 0.2 < 0 = __else1#
                3.0 val!
            1 __endif1#
            #__else1
                x 0.5 < 0 = __else2#
                    x 0.3 < 0 = __else3#
                        4.0 val!
                    1 __endif3#
                    #__else3
                        5.0 val!
                    #__endif3
                1 __endif2#
                #__else2
                    x 0.7 < 0 = __else4#
                        6.0 val!
                    1 __endif4#
                    #__else4
                        7.0 val!
                    #__endif4
                #__endif2
            #__endif1
            val@
        """
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(expected)


@pytest.mark.parametrize(
    "input_value, expected",
    [
        (0.0, 0.0),  # fib(0) = 0
        (1.0, 1.0),  # fib(1) = 1
        (5.0, 5.0),  # fib(5) = 5
        (10.0, 55.0),  # fib(10) = 55
    ],
)
def test_control_flow_while_loop_fibonacci(
    backend: str, input_value: float, expected: float
) -> None:
    """
    Test while loop that computes Fibonacci number.

    Infix equivalent:
        n = $x
        if (n <= 1) {
            result = n
        } else {
            a = 0.0; b = 1.0; i = 2.0
            while (i <= n) {
                temp = a + b
                a = b
                b = temp
                i = i + 1.0
            }
            result = b
        }
        RESULT = result
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=input_value)
    # Manually construct postfix for Fibonacci
    expr = """
        x n!
        0.0 result!
        n@ 1 <= 0 = else_branch#
            n@ result!
        1 endif#
        #else_branch
            0.0 a!
            1.0 b!
            2.0 i!
            #fib_loop
                i@ n@ <= 0 = fib_end#
                a@ b@ + temp!
                b@ a!
                temp@ b!
                i@ 1.0 + i!
            1 fib_loop#
            #fib_end
            b@ result!
        #endif
        result@
    """
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(expected)


def test_control_flow_goto_simple(backend: str) -> None:
    """
    Test simple goto with forward jump.

    This tests unconditional forward jumps.
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    # Jump over assignment of 1.0, so result should be 2.0
    expr = """
        0.0 result!
        1 skip#
        1.0 result!
        #skip
        result@ 2.0 + result!
        result@
    """
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(2.0)


def test_control_flow_goto_backward_loop(backend: str) -> None:
    """
    Test goto with backward jump (manual loop).

    Count down from 5 to 0, incrementing result each iteration.
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    expr = """
        5.0 counter!
        0.0 result!
        #loop_top
            counter@ 0 <= loop_done#
            result@ 1.0 + result!
            counter@ 1.0 - counter!
        1 loop_top#
        #loop_done
        result@
    """
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(5.0)


@pytest.mark.parametrize(
    "input_value, expected",
    [
        (1.0, 57),
        (0.5, 57),
        (0.1, 87.4000015258789),
    ],
)
def test_control_flow_irreducible_double_entry(
    backend: str, input_value: float, expected: float
) -> None:
    """
    Test irreducible control flow with multiple entry points into a loop.

    This creates a CFG where a loop can be entered from two different paths,
    which is the hallmark of an irreducible CFG.

    Infix equivalent:
        val = $x
        step1:
        val = val * 2.0
        if (val < 10.0) { goto step1 }
        val = val - 1.0
        if (val > 50.0) { goto done }
        goto step1
        done:
        RESULT = val
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=input_value)
    # Generated and adapted postfix
    expr = """
        x val!
        #step1
            val@ 2.0 * val!
            val@ 10.0 < step1#
            val@ 1.0 - val!
            val@ 50.0 > done#
        1 step1#
        #done
        val@
    """
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(expected)


def test_control_flow_irreducible_cross_jumping(backend: str) -> None:
    """
    Test irreducible control flow with cross-jumping between blocks.

    This creates an irreducible CFG where control flow jumps between
    two blocks that are not in a simple loop relationship.
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=5.0)
    expr = """
        x val!
        0.0 path!
        #block_a
            val@ 20.0 >= done#
            val@ 3.0 + val!
            path@ 1.0 + path!
        1 block_b#
        #block_b
            val@ 20.0 >= done#
            val@ 2.0 + val!
            path@ 1.0 + path!
        1 block_a#
        #done
        val@
    """
    res = expr_func(c, expr, vs.GRAYS)
    # 5 + 3 = 8, 8 + 2 = 10, 10 + 3 = 13, 13 + 2 = 15, 15 + 3 = 18, 18 + 2 = 20
    assert res.get_frame(0)[0][0, 0] == pytest.approx(20.0)


def test_control_flow_irreducible_three_way(backend: str) -> None:
    """
    Test irreducible control flow with three-way branching and cross-jumps.

    Creates a more complex irreducible CFG with three blocks that can
    transition to each other in a non-hierarchical way.
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    # State machine: transition based on counter mod 3
    expr = """
        0.0 result!
        6.0 iter!
        #state_0
            iter@ 0 <= done#
            result@ 1.0 + result!
            iter@ 1.0 - iter!
        1 state_1#
        #state_1
            iter@ 0 <= done#
            result@ 2.0 + result!
            iter@ 1.0 - iter!
        1 state_2#
        #state_2
            iter@ 0 <= done#
            result@ 3.0 + result!
            iter@ 1.0 - iter!
        1 state_0#
        #done
        result@
    """
    res = expr_func(c, expr, vs.GRAYS)
    # 6 iterations: 1+2+3+1+2+3 = 12
    assert res.get_frame(0)[0][0, 0] == pytest.approx(12.0)


def test_control_flow_mixed_if_goto(backend: str) -> None:
    """
    Test mixing if-else blocks with goto statements.

    This tests the interaction between structured control flow (if-else)
    and unstructured control flow (goto).
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=7.0)
    expr = """
        x val!
        0.0 result!
        #restart
            val@ 5.0 > 0 = small_val#
                result@ val@ + result!
                val@ 2.0 - val!
                1 restart#
            #small_val
                val@ 0.0 > 0 = done#
                    result@ val@ + result!
                    val@ 1.0 - val!
                    1 restart#
        #done
        result@
    """
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(22.0)


def test_control_flow_complex_state_machine(backend: str) -> None:
    """
    Test complex state machine with 4 states and conditional transitions.

    States: IDLE(0) -> RUNNING(1) -> PAUSED(2) -> STOPPED(3)
    Each state modifies result and transitions based on counter.
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    expr = """
        0.0 state!
        0.0 result!
        8.0 fuel!

        #state_machine
            fuel@ 0.0 <= done#

            state@ 0.0 = 0 = not_idle#
                result@ 1.0 + result!
                fuel@ 1.0 - fuel!
                fuel@ 6.0 < 0 = stay_idle#
                    1.0 state!
                #stay_idle
            1 state_machine#
            #not_idle

            state@ 1.0 = 0 = not_running#
                result@ 10.0 + result!
                fuel@ 1.0 - fuel!
                fuel@ 4.0 < 0 = stay_running#
                    2.0 state!
                #stay_running
            1 state_machine#
            #not_running

            state@ 2.0 = 0 = not_paused#
                result@ 100.0 + result!
                fuel@ 1.0 - fuel!
                fuel@ 2.0 < 0 = stay_paused#
                    3.0 state!
                #stay_paused
            1 state_machine#
            #not_paused

            result@ 1000.0 + result!
            fuel@ 1.0 - fuel!
        1 state_machine#

        #done
        result@
        """
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(1223.0)


def test_control_flow_fail_to_reduce(backend: str) -> None:
    """
    Test GLSL codegen the CFG is unable to transform.

    Infix equivalent:
        function test() {
            iter = 0
            max_iter = 100
            cond_val = 0.0
            goto L0_A
            L0_A:
                if (iter >= max_iter) { return 100.0 }
                iter = iter + 1
                cond_val = sin(iter)
                if (cond_val > 0) { goto L1_A } else { goto L1_B }
            L0_B:
                if (iter >= max_iter) { return 200.0 }
                iter = iter + 1
                cond_val = cos(iter)
                if (cond_val > 0) { goto L1_A } else { goto L1_B }
            L1_A:
                cond_val = tan(iter)
                if (cond_val > 0) { goto L2_A } else { goto L2_B }
            L1_B:
                cond_val = sin(iter * 0.5)
                if (cond_val > 0) { goto L2_A } else { goto L2_B }
            L2_A:
                cond_val = cos(iter * 0.5)
                if (cond_val > 0) { goto L0_A } else { goto L0_B }
            L2_B:
                cond_val = sin(iter * 0.2)
                if (cond_val > 0) { goto L0_A } else { goto L0_B }
            return -1.0
        }
        RESULT = test()
    """
    expr_func = get_expr_func(backend)
    c = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    expr = "0 __internal_func_test_0_iter! 100 __internal_func_test_0_max_iter! 0.0 __internal_func_test_0_cond_val! 1 __internal_test_0_L0_A# #__internal_test_0_L0_A __internal_func_test_0_iter@ __internal_func_test_0_max_iter@ >= 0 = __internal_else_0# 100.0 __internal_ret_test_0! 1 __internal_ret_label_test_0# #__internal_else_0 __internal_func_test_0_iter@ 1 + __internal_func_test_0_iter! __internal_func_test_0_iter@ sin __internal_func_test_0_cond_val! __internal_func_test_0_cond_val@ 0 > 0 = __internal_else_2# 1 __internal_test_0_L1_A# 1 __internal_endif_3# #__internal_else_2 1 __internal_test_0_L1_B# #__internal_endif_3 #__internal_test_0_L0_B __internal_func_test_0_iter@ __internal_func_test_0_max_iter@ >= 0 = __internal_else_4# 200.0 __internal_ret_test_0! 1 __internal_ret_label_test_0# #__internal_else_4 __internal_func_test_0_iter@ 1 + __internal_func_test_0_iter! __internal_func_test_0_iter@ cos __internal_func_test_0_cond_val! __internal_func_test_0_cond_val@ 0 > 0 = __internal_else_6# 1 __internal_test_0_L1_A# 1 __internal_endif_7# #__internal_else_6 1 __internal_test_0_L1_B# #__internal_endif_7 #__internal_test_0_L1_A __internal_func_test_0_iter@ tan __internal_func_test_0_cond_val! __internal_func_test_0_cond_val@ 0 > 0 = __internal_else_8# 1 __internal_test_0_L2_A# 1 __internal_endif_9# #__internal_else_8 1 __internal_test_0_L2_B# #__internal_endif_9 #__internal_test_0_L1_B __internal_func_test_0_iter@ 0.5 * sin __internal_func_test_0_cond_val! __internal_func_test_0_cond_val@ 0 > 0 = __internal_else_10# 1 __internal_test_0_L2_A# 1 __internal_endif_11# #__internal_else_10 1 __internal_test_0_L2_B# #__internal_endif_11 #__internal_test_0_L2_A __internal_func_test_0_iter@ 0.5 * cos __internal_func_test_0_cond_val! __internal_func_test_0_cond_val@ 0 > 0 = __internal_else_12# 1 __internal_test_0_L0_A# 1 __internal_endif_13# #__internal_else_12 1 __internal_test_0_L0_B# #__internal_endif_13 #__internal_test_0_L2_B __internal_func_test_0_iter@ 0.2 * sin __internal_func_test_0_cond_val! __internal_func_test_0_cond_val@ 0 > 0 = __internal_else_14# 1 __internal_test_0_L0_A# 1 __internal_endif_15# #__internal_else_14 1 __internal_test_0_L0_B# #__internal_endif_15 -1.0 __internal_ret_test_0! 1 __internal_ret_label_test_0# #__internal_ret_label_test_0 __internal_ret_test_0@ RESULT! RESULT@"
    res = expr_func(c, expr, vs.GRAYS)
    assert res.get_frame(0)[0][0, 0] == pytest.approx(100)
