"""
Copyright (C) 2025 yuygfgg

This file is part of Vapoursynth-llvmexpr.

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

core = vs.core


@pytest.fixture(scope="module")
def test_clips():
    c0 = core.std.BlankClip(
        width=1920, height=1080, format=vs.YUV420P8, color=[0, 128, 128]
    )
    c1 = core.std.BlankClip(width=1280, height=720, format=vs.RGBH, color=[0, 0, 0])
    c2 = core.std.BlankClip(width=640, height=480, format=vs.GRAY16, color=[0])
    return [c0, c1, c2]


@pytest.mark.parametrize(
    "prop_name, clip_idx, plane_idx, expected",
    [
        # get_width
        ("w0_p0", 0, 0, 1920),
        ("w0_p1", 0, 1, 960),
        ("w1_p0", 1, 0, 1280),
        ("w2_p0", 2, 0, 640),
        ("w_invalid", 10, 0, -1),
        # get_height
        ("h0_p0", 0, 0, 1080),
        ("h0_p1", 0, 1, 540),
        ("h1_p0", 1, 0, 720),
        ("h2_p0", 2, 0, 480),
        ("h_invalid", 10, 0, -1),
    ],
)
def test_singleexpr_width_height(test_clips, prop_name, clip_idx, plane_idx, expected):
    """Test get_width and get_height in SingleExpr mode."""
    func_name = "get_width" if prop_name.startswith("w") else "get_height"
    expr_code = f"""
    @requires std
    n = {clip_idx}
    p = {plane_idx}
    set_propi({prop_name}, {func_name}(n, p));
    """

    r_single = core.llvmexpr.SingleExpr(
        test_clips, expr_code, format=vs.YUV420P8, infix=True
    )

    f_single = r_single.get_frame(0)
    assert f_single.props[prop_name] == expected


@pytest.mark.parametrize(
    "prop_name, clip_idx, expected",
    [
        ("bd0", 0, 8),
        ("bd1", 1, 16),
        ("bd2", 2, 16),
        ("bd_invalid", 10, -1),
    ],
)
def test_singleexpr_bitdepth(test_clips, prop_name, clip_idx, expected):
    """Test get_bitdepth in SingleExpr mode."""
    expr_code = f"""
    @requires std
    n = {clip_idx}
    set_propi({prop_name}, get_bitdepth(n));
    """

    r_single = core.llvmexpr.SingleExpr(
        test_clips, expr_code, format=vs.YUV420P8, infix=True
    )

    f_single = r_single.get_frame(0)
    assert f_single.props[prop_name] == expected


@pytest.mark.parametrize(
    "prop_name, clip_idx, expected",
    [
        ("fmt0", 0, -1),
        ("fmt1", 1, 1),
        ("fmt2", 2, -1),
        ("fmt_invalid", 10, 0),
    ],
)
def test_singleexpr_format(test_clips, prop_name, clip_idx, expected):
    """Test get_fmt in SingleExpr mode."""
    expr_code = f"""
    @requires std
    n = {clip_idx}
    set_propi({prop_name}, get_fmt(n));
    """

    r_single = core.llvmexpr.SingleExpr(
        test_clips, expr_code, format=vs.YUV420P8, infix=True
    )

    f_single = r_single.get_frame(0)
    assert f_single.props[prop_name] == expected


@pytest.mark.parametrize(
    "width, height, format_id, plane_idx, expected_width, expected_height",
    [
        (1920, 1080, vs.YUV420P16, 0, 1920.0, 1080.0),
        (1920, 1080, vs.YUV420P16, 1, 960.0, 540.0),
        (1920, 1080, vs.YUV420P16, 2, 960.0, 540.0),
        (1920, 1080, vs.YUV422P16, 0, 1920.0, 1080.0),
        (1920, 1080, vs.YUV422P16, 1, 960.0, 1080.0),
        (1280, 720, vs.RGBS, 0, 1280.0, 720.0),
        (640, 480, vs.GRAYS, 0, 640.0, 480.0),
    ],
)
def test_expr_width_height(
    width, height, format_id, plane_idx, expected_width, expected_height
):
    """Test get_width and get_height in Expr mode with various formats."""
    test_clip = core.std.BlankClip(width=width, height=height, format=format_id)

    # Test width
    expr_w = f"@requires std\nRESULT = get_width({plane_idx});"
    res_w = core.llvmexpr.Expr(
        test_clip, expr_w if format_id in [vs.GRAYS] else [expr_w, "", ""], infix=True
    )
    f_w = res_w.get_frame(0)
    assert f_w[0][0, 0] == pytest.approx(expected_width)

    # Test height
    expr_h = f"@requires std\nRESULT = get_height({plane_idx});"
    res_h = core.llvmexpr.Expr(
        test_clip, expr_h if format_id in [vs.GRAYS] else [expr_h, "", ""], infix=True
    )
    f_h = res_h.get_frame(0)
    assert f_h[0][0, 0] == pytest.approx(expected_height)
