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
import numpy as np

core = vs.core


def test_simple_pixel_write():
    """Test writing a single pixel value."""
    clip = core.std.BlankClip(width=100, height=100, format=vs.GRAY8, color=0)
    res = core.llvmexpr.SingleExpr(clip, "128 50 60 @[]^0")
    frame = res.get_frame(0)
    assert frame[0][60, 50] == 128
    assert frame[0][0, 0] == 0
    assert res.format.name == "Gray8"


def test_pixel_read_write():
    """Test reading a pixel from a source and writing it to a destination."""
    clip = core.std.BlankClip(width=100, height=100, format=vs.GRAY8, color=0)

    def modify_frame(n, f):
        fout = f.copy()
        arr = np.asarray(fout[0])
        for y in range(100):
            for x in range(100):
                arr[y, x] = (x + y) % 256
        return fout

    clip = core.std.ModifyFrame(clip, clip, modify_frame)

    res = core.llvmexpr.SingleExpr(clip, "10 20 src0^0[] 30 40 @[]^0")
    frame = res.get_frame(0)
    assert frame[0][40, 30] == 30


def test_property_write():
    """Test writing a value to a frame property."""
    clip = core.std.BlankClip()
    res = core.llvmexpr.SingleExpr(clip, """123.45 MyTestProp$""")
    frame = res.get_frame(0)
    assert frame.props["MyTestProp"] == pytest.approx(123.45)


def test_property_read_write():
    """Test reading a frame property, modifying it, and writing to a new property."""
    clip = core.std.BlankClip(width=1920, height=1080).std.SetFrameProps(
        _Width=1920, _Height=1080
    )
    res = core.llvmexpr.SingleExpr(
        clip, """src0._Width 2 / HalfWidth$ src0._Height 2 / HalfHeight$"""
    )
    frame = res.get_frame(0)
    assert frame.props["HalfWidth"] == 1920 / 2
    assert frame.props["HalfHeight"] == 1080 / 2


def test_stack_not_empty_error():
    """Test that an expression leaving items on the stack raises an error."""
    clip = core.std.BlankClip()
    with pytest.raises(vs.Error, match="Expression stack not balanced"):
        core.llvmexpr.SingleExpr(clip, "1 2 +")


@pytest.mark.parametrize(
    "expr", ["X", "Y", "x", "y", "z", "a", "x[1,0]", "^exit^", "@[]"]
)
def test_disabled_token_error(expr: str):
    """Test that using tokens disabled in SingleExpr raises an error."""
    clip = core.std.BlankClip()
    with pytest.raises(vs.Error, match="Invalid token"):
        core.llvmexpr.SingleExpr(clip, expr)


def test_loop_avg_brightness():
    """Test a loop that calculates the average brightness of a region."""
    clip = core.std.BlankClip(width=100, height=100, format=vs.GRAY8, color=50)
    expr = """
        0 sum!
        0 i!
        width^0 height^0 * num_pixels!
        #loop_i
            0 j!
            #loop_j
                j@ i@ src0^0[] sum@ + sum!
                j@ 1 + j!  
            j@ width^0 < loop_j#
            i@ 1 + i!
        i@ height^0 < loop_i#
        sum@ num_pixels@ / AvgBrightness$
    """
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert frame.props["AvgBrightness"] == pytest.approx(50.0)


def test_multi_clip_read():
    """Test reading from multiple input clips."""
    clip1 = core.std.BlankClip(color=[10, 10, 10])
    clip2 = core.std.BlankClip(color=[20, 20, 20])
    res = core.llvmexpr.SingleExpr(
        [clip1, clip2], """0 0 src0^0[] 0 0 src1^0[] + Sum$"""
    )
    frame = res.get_frame(0)
    assert frame.props["Sum"] == 30


def test_multi_plane_write():
    """Test writing to multiple planes in a YUV clip."""
    clip = core.std.BlankClip(
        format=vs.YUV420P8, width=16, height=16, color=[16, 128, 128]
    )
    res = core.llvmexpr.SingleExpr(clip, "10 0 0 @[]^0 20 1 1 @[]^1 30 2 2 @[]^2")
    frame = res.get_frame(0)
    assert frame[0][0, 0] == 10
    assert frame[1][1, 1] == 20
    assert frame[2][2, 2] == 30

    assert frame[0][1, 1] == 16
    assert frame[1][0, 0] == 128
    assert frame[2][0, 0] == 128


def test_atomic_prop_write():
    """Test that property writes are visible within the same expression."""
    clip = core.std.BlankClip()
    expr = """
        10 MyVar$
        x.MyVar 5 + MyVar$
        x.MyVar 2 * MyVar2$
    """
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert frame.props["MyVar"] == pytest.approx(15.0)
    assert frame.props["MyVar2"] == pytest.approx(30.0)


def test_atomic_prop_read_modify_write():
    """Test reading an input property and writing back to it."""
    clip = core.std.BlankClip().std.SetFrameProps(MyVar=10)
    expr = "x.MyVar 5 + MyVar$"
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert frame.props["MyVar"] == pytest.approx(15.0)


def test_empty_expr():
    """Test that an empty expression is valid and does nothing."""
    clip = core.std.BlankClip(color=[42, 42, 42])
    clipb = core.std.BlankClip(color=[22, 22, 22])
    res = core.llvmexpr.SingleExpr([clip, clipb], "")
    frame = res.get_frame(0)
    assert frame[0][0, 0] == 42


@pytest.mark.parametrize(
    "format,w,h,w1,h1,w2,h2",
    [
        (vs.YUV420P8, 1280, 720, 640, 360, 640, 360),
        (vs.RGB24, 640, 480, 640, 480, 640, 480),
        (vs.GRAY8, 1920, 1080, 0, 0, 0, 0),  # only one plane
    ],
)
def test_plane_dimensions(format, w, h, w1, h1, w2, h2):
    """Test getting plane dimensions."""
    clip = core.std.BlankClip(format=format, width=w, height=h)
    expr = """
        width^0 Plane0Width$
        height^0 Plane0Height$
    """
    if clip.format.num_planes > 1:
        expr += """
            width^1 Plane1Width$
            height^1 Plane1Height$
            width^2 Plane2Width$
            height^2 Plane2Height$
        """

    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert frame.props["Plane0Width"] == w
    assert frame.props["Plane0Height"] == h
    if clip.format.num_planes > 1:
        assert frame.props["Plane1Width"] == w1
        assert frame.props["Plane1Height"] == h1
        assert frame.props["Plane2Width"] == w2
        assert frame.props["Plane2Height"] == h2


def test_invalid_plane_dimension():
    """Test that getting dimension of an invalid plane raises an error."""
    clip = core.std.BlankClip(format=vs.GRAYS, width=1280, height=720)
    with pytest.raises(vs.Error, match="Invalid plane index"):
        core.llvmexpr.SingleExpr(clip, "width^3 prop$")


def test_array_static_allocation():
    """Test static array allocation in SingleExpr mode."""
    clip = core.std.BlankClip(width=100, height=100, format=vs.GRAY8, color=0)
    # Allocate array, store value, read it back, write to property
    expr = """
        buffer{}^10
        123.45 5 buffer{}!
        5 buffer{}@ result$
    """
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert frame.props["result"] == pytest.approx(123.45)


def test_array_dynamic_allocation():
    """Test dynamic array allocation."""
    clip = core.std.BlankClip(width=100, height=100, format=vs.GRAY8, color=0)
    expr = """
        5 3 * size!
        0 buffer{}^
        size@ buffer{}^
        99.9 10 buffer{}!
        10 buffer{}@ result$
    """
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert frame.props["result"] == pytest.approx(99.9)


def test_array_dynamic_allocation_from_dimensions():
    """Test dynamic array allocation using frame dimensions."""
    clip = core.std.BlankClip(width=100, height=50, format=vs.GRAY8, color=0)
    # Allocate array sized width * height
    expr = """
        width^0 height^0 * buffer{}^
        777.0 500 buffer{}!
        500 buffer{}@ result$
    """
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert frame.props["result"] == pytest.approx(777.0)


def test_array_read_write_pixel_buffer():
    """Test using array as a pixel buffer."""
    clip = core.std.BlankClip(width=100, height=100, format=vs.GRAY8, color=50)
    # Read pixels, store in array, write back with offset
    expr = """
        pixbuf{}^100
        10 20 src0^0[]
        0 pixbuf{}!
        30 40 src0^0[]
        1 pixbuf{}!
        0 pixbuf{}@ 55 66 @[]^0
        1 pixbuf{}@ 77 88 @[]^0
    """
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert frame[0][66, 55] == 50  # pixel from (20, 10)
    assert frame[0][88, 77] == 50  # pixel from (40, 30)


def test_array_multiple_arrays_independent():
    """Test that multiple arrays are independent."""
    clip = core.std.BlankClip(width=10, height=10, format=vs.GRAY8, color=0)
    expr = """
        a{}^5
        b{}^5
        111.0 0 a{}!
        222.0 1 a{}!
        333.0 0 b{}!
        444.0 1 b{}!
        0 a{}@ sum_a$
        1 a{}@ sum_b$
        0 b{}@ diff_a$
        1 b{}@ diff_b$
    """
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert frame.props["sum_a"] == pytest.approx(111.0)
    assert frame.props["sum_b"] == pytest.approx(222.0)
    assert frame.props["diff_a"] == pytest.approx(333.0)
    assert frame.props["diff_b"] == pytest.approx(444.0)


def test_array_with_loop_computation():
    """Test array used in a computation loop."""
    clip = core.std.BlankClip(width=10, height=10, format=vs.GRAY8, color=0)
    # Initialize array with fibonacci-like sequence
    expr = """
        fib{}^10
        1.0 0 fib{}!
        1.0 1 fib{}!
        
        2 idx!
        #loop
        idx@ 10 < not loop_end#
        
        idx@ 1 - fib{}@
        idx@ 2 - fib{}@
        +
        idx@ fib{}!
        
        idx@ 1 + dup idx!
        loop#
        
        #loop_end
        9 fib{}@ result$
    """
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    # Fibonacci: 1, 1, 2, 3, 5, 8, 13, 21, 34, 55
    assert frame.props["result"] == pytest.approx(55.0)


def test_array_float_to_int_index():
    """Test that float indices are converted to integers properly."""
    clip = core.std.BlankClip(width=10, height=10, format=vs.GRAY8, color=0)
    expr = """
        arr{}^5
        10.0 0 arr{}!
        20.0 1 arr{}!
        30.0 2 arr{}!
        40.0 3 arr{}!
        50.0 4 arr{}!
        2.9 arr{}@ result$
    """
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    # 2.9 should truncate to 2
    assert frame.props["result"] == pytest.approx(30.0)


def test_array_reallocation():
    """Test that dynamically allocated arrays can be reallocated."""
    clip = core.std.BlankClip(width=10, height=10, format=vs.GRAY8, color=0)
    expr = """
    10 arr{}^
    50.0 1 arr{}!
    200000000 arr{}^
    70.0 140000000 arr{}!
    140000000 arr{}@ 1 arr{}@ + result$
    """
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert frame.props["result"] == pytest.approx(120.0)


def test_array_uninitialized_error():
    """Test that using uninitialized array raises an error."""
    clip = core.std.BlankClip(width=10, height=10, format=vs.GRAY8, color=0)
    with pytest.raises(vs.Error, match="Array is uninitialized"):
        core.llvmexpr.SingleExpr(clip, "0 arr{}@ result$")


def test_error_on_array_reallocation():
    """Test that reallocating an array raises an error."""
    clip = core.std.BlankClip(width=10, height=10, format=vs.GRAY8, color=0)
    with pytest.raises(
        vs.Error, match="Statically allocated array cannot be reallocated"
    ):
        core.llvmexpr.SingleExpr(clip, "a{}^10 10 a{}^")


def test_single_expr_frame_property_access_multiple_clips() -> None:
    """Test that SingleExpr can access frame properties from multiple clips without collision."""
    c0 = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    c0 = core.std.SetFrameProps(c0, _TestProp=0.25)

    c1 = core.std.BlankClip(format=vs.GRAYS, color=0.0)
    c1 = core.std.SetFrameProps(c1, _TestProp=0.75)

    expr = "src0._TestProp src1._TestProp + out_sum$"
    res = core.llvmexpr.SingleExpr([c0, c1], expr, vs.GRAYS)
    frame_props = res.get_frame(0).props
    assert frame_props["out_sum"] == pytest.approx(0.25 + 0.75)

    expr_store = "src0._TestProp out0$ src1._TestProp out1$"
    res_store = core.llvmexpr.SingleExpr([c0, c1], expr_store, vs.GRAYS)
    frame_props = res_store.get_frame(0).props
    assert frame_props["out0"] == pytest.approx(0.25)
    assert frame_props["out1"] == pytest.approx(0.75)


def test_multi_clip_different_dimensions():
    """Test that SingleExpr can handle clips with different dimensions."""
    clip1 = core.std.BlankClip(width=100, height=100, format=vs.GRAY8, color=10)
    clip2 = core.std.BlankClip(width=200, height=200, format=vs.GRAY8, color=20)
    expr = "10 10 src0^0[] 150 150 src1^0[] + Sum$"
    res = core.llvmexpr.SingleExpr([clip1, clip2], expr)
    frame = res.get_frame(0)
    assert frame.props["Sum"] == 30
    assert res.width == 100
    assert res.height == 100


def test_clip_dimensions_tokens():
    """Test getting dimensions of specific clips."""
    clip1 = core.std.BlankClip(width=1280, height=720, format=vs.YUV420P8)
    clip2 = core.std.BlankClip(width=640, height=480, format=vs.YUV444P10)
    clip3 = core.std.BlankClip(width=1920, height=1080, format=vs.GRAY16)

    expr = """
        x:width c1w$
        x:height c1h$
        x:width^1 c1wp1$
        x:height^1 c1hp1$

        y:width c2w$
        y:height c2h$
        y:width^2 c2wp2$
        y:height^2 c2hp2$

        src2:width c3w$
        src2:height c3h$
    """
    res = core.llvmexpr.SingleExpr([clip1, clip2, clip3], expr)
    frame = res.get_frame(0)

    assert frame.props["c1w"] == 1280
    assert frame.props["c1h"] == 720
    assert frame.props["c1wp1"] == 1280 / 2
    assert frame.props["c1hp1"] == 720 / 2

    assert frame.props["c2w"] == 640
    assert frame.props["c2h"] == 480
    assert frame.props["c2wp2"] == 640
    assert frame.props["c2hp2"] == 480

    assert frame.props["c3w"] == 1920
    assert frame.props["c3h"] == 1080


@pytest.mark.parametrize(
    "expr, prop_name, expected_value, expected_type",
    [
        ("123 MyInt$i", "MyInt", 123, int),
        ("123.7 MyIntRound$i", "MyIntRound", 124, int),
        ("45.6 MyFloat$f", "MyFloat", pytest.approx(45.6), float),
        ("45 MyFloatInt$f", "MyFloatInt", pytest.approx(45.0), float),
        ("78.9 MyOldFloat$", "MyOldFloat", pytest.approx(78.9), float),
    ],
)
def test_property_write_typed(expr, prop_name, expected_value, expected_type):
    """Test writing properties with explicit type suffixes."""
    clip = core.std.BlankClip()
    res = core.llvmexpr.SingleExpr(clip, expr)
    frame = res.get_frame(0)
    assert prop_name in frame.props
    value = frame.props[prop_name]
    assert value == expected_value
    assert isinstance(value, expected_type)


def test_property_write_auto_int():
    """Test auto-int ($ai) property writing."""
    clip = core.std.BlankClip()
    res = core.llvmexpr.SingleExpr(clip, "123.7 NoExist$ai")
    frame = res.get_frame(0)
    assert frame.props["NoExist"] == 124
    assert isinstance(frame.props["NoExist"], int)

    clip_with_int = clip.std.SetFrameProps(Existing=500)
    res_with_int = core.llvmexpr.SingleExpr(clip_with_int, "999 Existing$ai")
    frame_with_int = res_with_int.get_frame(0)
    assert frame_with_int.props["Existing"] == 999
    assert isinstance(frame_with_int.props["Existing"], int)

    clip_with_float = clip.std.SetFrameProps(Existing=500.5)
    res_with_float = core.llvmexpr.SingleExpr(clip_with_float, "999.9 Existing$ai")
    frame_with_float = res_with_float.get_frame(0)
    assert frame_with_float.props["Existing"] == pytest.approx(999.9)
    assert isinstance(frame_with_float.props["Existing"], float)


def test_property_write_auto_float():
    """Test auto-float ($af) property writing."""
    clip = core.std.BlankClip()
    res = core.llvmexpr.SingleExpr(clip, "123.7 NoExist$af")
    frame = res.get_frame(0)
    assert frame.props["NoExist"] == pytest.approx(123.7)
    assert isinstance(frame.props["NoExist"], float)

    clip_with_int = clip.std.SetFrameProps(Existing=500)
    res_with_int = core.llvmexpr.SingleExpr(clip_with_int, "999 Existing$af")
    frame_with_int = res_with_int.get_frame(0)
    assert frame_with_int.props["Existing"] == 999
    assert isinstance(frame_with_int.props["Existing"], int)

    clip_with_float = clip.std.SetFrameProps(Existing=500.5)
    res_with_float = core.llvmexpr.SingleExpr(clip_with_float, "999.9 Existing$af")
    frame_with_float = res_with_float.get_frame(0)
    assert frame_with_float.props["Existing"] == pytest.approx(999.9)
    assert isinstance(frame_with_float.props["Existing"], float)


def test_conditional_prop_write():
    """Test that a prop write can be conditional and is not required on all paths."""
    clip = core.std.BlankClip()
    expr_jump = """
        N 10 > loop_end#
        123 MyProp$
        #loop_end
    """
    res = core.llvmexpr.SingleExpr(clip, expr_jump)

    # Frame 5: N <= 10, so MyProp should be written
    frame1 = res.get_frame(5)
    assert "MyProp" in frame1.props
    assert frame1.props["MyProp"] == 123.0

    # Frame 15: N > 10, so MyProp should NOT be written and should not exist
    frame2 = res.get_frame(15)
    assert "MyProp" not in frame2.props


def test_prop_write_inconsistent_type_error():
    """Test that writing to a property with inconsistent types raises an error."""
    clip = core.std.BlankClip()

    expr_fi = "1.0 MyProp$f 2 MyProp$i"
    with pytest.raises(
        vs.Error,
        match=r"Inconsistent types used for property 'MyProp'\. Previous type: FLOAT \(idx: 1\), current type: INT \(idx: 3\)\.",
    ):
        core.llvmexpr.SingleExpr(clip, expr_fi)

    expr_di = "1.0 MyProp$ 2 MyProp$i"
    with pytest.raises(
        vs.Error,
        match=r"Inconsistent types used for property 'MyProp'\. Previous type: FLOAT \(idx: 1\), current type: INT \(idx: 3\)\.",
    ):
        core.llvmexpr.SingleExpr(clip, expr_di)

    expr_afi = "1.0 MyProp$af 2 MyProp$i"
    with pytest.raises(
        vs.Error,
        match=r"Inconsistent types used for property 'MyProp'\. Previous type: AUTO_FLOAT \(idx: 1\), current type: INT \(idx: 3\)\.",
    ):
        core.llvmexpr.SingleExpr(clip, expr_afi)


def test_prop_write_safety_pass_unconditional():
    """Test valid prop writes that are guaranteed to execute unconditionally."""
    clip = core.std.BlankClip()

    expr_after_loop = """
        0 i!
        #loop
        i@ 1 + i!
        i@ 10 < loop#
        i@ MyProp$
    """
    res = core.llvmexpr.SingleExpr(clip, expr_after_loop)
    frame = res.get_frame(0)
    assert frame.props["MyProp"] == 10.0


def test_prop_write_safety_pass_if_else():
    """Test that prop writes in both branches of an if/else are valid."""
    clip = core.std.BlankClip()
    expr_if_else = """
        N 10 <= if_true#
        200 MyProp$
        1 endif#
    #if_true
        100 MyProp$
    #endif
    """
    res = core.llvmexpr.SingleExpr(clip, expr_if_else)
    frame1 = res.get_frame(5)
    assert frame1.props["MyProp"] == 100.0
    frame2 = res.get_frame(15)
    assert frame2.props["MyProp"] == 200.0

    def test_conditional_prop_write_copy_fallback():
        """Test that a conditionally-written prop falls back to copying from src0."""
        clip = core.std.BlankClip().std.SetFrameProps(MyProp=100)
        expr = """
            N 5 = if_true#
            999 MyProp$
            1 endif#
        #if_true
        #endif
        """
        res = core.llvmexpr.SingleExpr(clip, expr)

        # Frame 5: N == 5, prop is NOT written by expr. Should be copied from source.
        frame1 = res.get_frame(5)
        assert "MyProp" in frame1.props
        assert frame1.props["MyProp"] == 100

        # Frame 10: N != 5, prop IS written by expr. Should be the new value.
        frame2 = res.get_frame(10)
        assert "MyProp" in frame2.props
        assert frame2.props["MyProp"] == 999


def test_property_deletion():
    """Test deleting frame properties using the $d suffix."""
    clip_with_prop = core.std.BlankClip().std.SetFrameProps(ToDelete=100)
    res_delete = core.llvmexpr.SingleExpr(clip_with_prop, "ToDelete$d")
    frame_deleted = res_delete.get_frame(0)
    assert "ToDelete" not in frame_deleted.props

    clip_no_prop = core.std.BlankClip()
    res_delete_nonexistent = core.llvmexpr.SingleExpr(clip_no_prop, "NonExistent$d")
    frame_delete_nonexistent = res_delete_nonexistent.get_frame(0)
    assert "NonExistent" not in frame_delete_nonexistent.props
    clip_with_prop_and_other = core.std.BlankClip().std.SetFrameProps(
        ToKeep=50, ToDelete=100
    )
    res_delete_one = core.llvmexpr.SingleExpr(clip_with_prop_and_other, "ToDelete$d")
    frame_delete_one = res_delete_one.get_frame(0)
    assert "ToDelete" not in frame_delete_one.props
    assert "ToKeep" in frame_delete_one.props
    assert frame_delete_one.props["ToKeep"] == 50

    clip_del_write = core.std.BlankClip().std.SetFrameProps(MyProp=100)
    res_del_write = core.llvmexpr.SingleExpr(clip_del_write, "MyProp$d 42 MyProp$")
    frame_del_write = res_del_write.get_frame(0)
    assert "MyProp" in frame_del_write.props
    assert frame_del_write.props["MyProp"] == 42.0

    clip_write_del = core.std.BlankClip().std.SetFrameProps(MyProp=100)
    res_write_del = core.llvmexpr.SingleExpr(clip_write_del, "42 MyProp$ MyProp$d")
    frame_write_del = res_write_del.get_frame(0)
    assert "MyProp" not in frame_write_del.props
