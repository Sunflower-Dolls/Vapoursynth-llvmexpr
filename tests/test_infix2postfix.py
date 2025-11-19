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

import subprocess
import tempfile
import os
from pathlib import Path

BUILDDIR = Path(__file__).parent.parent / "builddir"
INFIX2POSTFIX = BUILDDIR / "infix2postfix"

if not INFIX2POSTFIX.exists():
    BUILDDIR = Path(__file__).parent.parent / "build"
    INFIX2POSTFIX = BUILDDIR / "infix2postfix"


def run_infix2postfix(infix_code: str, mode: str = "expr") -> tuple[bool, str]:
    with tempfile.NamedTemporaryFile(mode="w", suffix=".infix", delete=False) as f_in:
        f_in.write(infix_code)
        input_file = f_in.name

    with tempfile.NamedTemporaryFile(mode="r", suffix=".expr", delete=False) as f_out:
        output_file = f_out.name

    try:
        result = subprocess.run(
            [str(INFIX2POSTFIX), input_file, "-m", mode, "-o", output_file],
            capture_output=True,
            text=True,
            timeout=5,
        )

        if result.returncode == 0:
            with open(output_file, "r") as f:
                return True, f.read().strip()
        else:
            return False, result.stderr
    except subprocess.TimeoutExpired:
        return False, "Timeout"
    finally:
        try:
            os.unlink(input_file)
            os.unlink(output_file)
        except Exception:
            pass


class TestSingleExprMode:
    """Test SingleExpr-specific features."""

    def test_frame_width_height(self):
        """Test frame.width[N] and frame.height[N] syntax."""
        infix = """
w0 = frame.width[0]
h0 = frame.height[0]
w1 = frame.width[1]
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "width^0" in output
        assert "height^0" in output
        assert "width^1" in output
        assert "w0!" in output
        assert "h0!" in output

    def test_dyn_four_args(self):
        """Test dyn() with 4 arguments (clip, x, y, plane)."""
        infix = """
val = dyn($x, 100, 200, 0)
val2 = dyn($src0, 50, 50, 1)
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "100 200 x^0[]" in output
        assert "50 50 src0^1[]" in output

    def test_store_four_args(self):
        """Test store() with 4 arguments (x, y, plane, value)."""
        infix = """
store(10, 20, 0, 255)
store(5, 5, 1, 128)
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "255 10 20 @[]^0" in output
        assert "128 5 5 @[]^1" in output

    def test_set_prop_legacy(self):
        """Test legacy set_prop() for frame property writing."""
        infix = """
set_prop(MyProperty, 123.456)
set_prop(TestProp, 42)
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "123.456 MyProperty$f" in output
        assert "42 TestProp$f" in output

    def test_set_prop_typed(self):
        """Test typed set_prop functions (set_propf, set_propi, etc.)."""
        infix = """
set_propf(PropF, 1.0)
set_propi(PropI, 2)
set_propaf(PropAF, 3.0)
set_propai(PropAI, 4)
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        # Check for the main part
        assert "1.0 PropF$f" in output
        assert "2 PropI$i" in output
        assert "3.0 PropAF$af" in output
        assert "4 PropAI$ai" in output

    def test_set_prop_type_consistency_error(self):
        """Test that inconsistent types for the same property cause an error."""
        infix = """
set_propi(MyProp, 1)
set_propf(MyProp, 2.0)
"""
        success, output = run_infix2postfix(infix, "single")
        assert not success, "Should have failed due to type inconsistency"
        assert "inconsistent types" in output
        assert "MyProp" in output

    def test_set_prop_conditional_initialization(self):
        """Test that conditional set_prop generates an initialization prefix."""
        infix = """
if ($N > 10) {
    set_propi(ConditionalProp, 100)
}
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "100 ConditionalProp$i" in output
        assert "#" in output

    def test_set_prop_with_variable(self):
        """Test set_prop() with a variable as the value."""
        infix = """
my_value = 42
set_prop(MyProperty, my_value)
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "42 my_value!" in output
        assert "my_value@ MyProperty$f" in output

    def test_prop_deletion_infix(self):
        """Test remove_prop() and its type consistency with set_prop."""
        infix_remove = "remove_prop(ToDelete)"
        success, output = run_infix2postfix(infix_remove, "single")
        assert success, f"Failed to convert remove_prop: {output}"
        assert output.strip() == "ToDelete$d"

        infix_set_remove = "set_propi(MyProp, 1); remove_prop(MyProp)"
        success, output = run_infix2postfix(infix_set_remove, "single")
        assert success, f"Should pass with set then remove: {output}"
        assert "1 MyProp$i" in output
        assert "MyProp$d" in output

        infix_remove_set = "remove_prop(MyProp); set_propf(MyProp, 2.0)"
        success, output = run_infix2postfix(infix_remove_set, "single")
        assert success, f"Should pass with remove then set: {output}"
        assert "MyProp$d" in output
        assert "2.0 MyProp$f" in output

        infix_fail = "set_propi(MyProp, 1); remove_prop(MyProp); set_propf(MyProp, 3.0)"
        success, output = run_infix2postfix(infix_fail, "single")
        assert not success, "Should fail with inconsistent types around remove_prop"
        assert "inconsistent types" in output
        assert "MyProp" in output

    def test_complex_single_expr(self):
        """Test a complex SingleExpr script."""
        infix = """
# Get frame dimensions
w = frame.width[0]
h = frame.height[0]

# Read corner pixels
top_left = dyn($x, 0, 0, 0)
bottom_right = dyn($x, w - 1, h - 1, 0)

# Write to center
center_val = 255
store(w / 2, h / 2, 0, center_val)

# Save statistics
set_prop(TopLeft, top_left)
set_prop(BottomRight, bottom_right)
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "width^0" in output
        assert "height^0" in output
        assert "x^0[]" in output
        assert "@[]^0" in output
        assert "TopLeft$f" in output
        assert "BottomRight$f" in output


class TestExprMode:
    """Test Expr-specific features."""

    def test_xy_coordinates(self):
        """Test X and Y coordinate access."""
        infix = """
coord_sum = $X + $Y
RESULT = coord_sum
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "X Y +" in output
        assert "RESULT!" in output
        assert "RESULT@" in output

    def test_static_relative_pixel_access(self):
        """Test static relative pixel access like $x[1, 0]."""
        infix = """
right = $x[1, 0]
left = $x[-1, 0]
top = $y[0, -1]
RESULT = right + left + top
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "x[1,0]" in output
        assert "x[-1,0]" in output
        assert "y[0,-1]" in output

    def test_dyn_three_args(self):
        """Test dyn() with 3 arguments (clip, x, y) in Expr mode."""
        infix = """
val = dyn($x, $X * 2, $Y / 2)
RESULT = val
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "X 2 *" in output
        assert "Y 2 /" in output
        assert "x[]" in output

    def test_store_three_args(self):
        """Test store() with 3 arguments (x, y, value) in Expr mode."""
        infix = """
store($X + 1, $Y + 1, 128)
RESULT = $X
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "128" in output
        assert "X 1 +" in output
        assert "Y 1 +" in output
        assert "@[]" in output

    def test_complex_expr(self):
        """Test a complex Expr script with multiple features."""
        infix = """
# Neighbor average
top = $x[0, -1]
bottom = $x[0, 1]
left = $x[-1, 0]
right = $x[1, 0]
avg = (top + bottom + left + right) / 4

# Apply threshold
threshold = 128
result = avg > threshold

RESULT = -result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "x[0,-1]" in output
        assert "x[0,1]" in output
        assert "x[-1,0]" in output
        assert "x[1,0]" in output
        assert "4 /" in output
        assert ">" in output
        assert "neg" in output

    def test_is_prop_exist(self):
        """Test is_prop_exist() builtin function."""
        infix = """
RESULT = 0
if (is_prop_exist($x, MyProp)) {
    RESULT = 1
}
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "x.MyProp?" in output


class TestModeChecking:
    """Test mode-specific restrictions."""

    def test_xy_in_single_mode_error(self):
        """Test that using X/Y in SingleExpr mode causes an error."""
        infix = """
result = $X + $Y
"""
        success, output = run_infix2postfix(infix, "single")
        assert not success, "Should have failed"
        assert "only available in Expr mode" in output

    def test_frame_width_in_expr_mode_error(self):
        """Test that using frame.width[N] in Expr mode causes an error."""
        infix = """
w = frame.width[0]
RESULT = w
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should have failed"
        assert "only available in SingleExpr mode" in output

    def test_dyn_wrong_args_count_single(self):
        """Test that dyn() with 3 args in SingleExpr mode causes an error."""
        infix = """
val = dyn($x, 10, 20)
"""
        success, output = run_infix2postfix(infix, "single")
        assert not success, "Should have failed"

    def test_store_wrong_args_count_expr(self):
        """Test that store() with 4 args in Expr mode causes an error."""
        infix = """
store(10, 20, 0, 255)
RESULT = $X
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should have failed"

    def test_store_wrong_args_count_single(self):
        """Test that store() with 3 args in SingleExpr mode causes an error."""
        infix = """
store(10, 20, 255)
"""
        success, output = run_infix2postfix(infix, "single")
        assert not success, "Should have failed"

    def test_set_prop_in_expr_mode_error(self):
        """Test that set_prop() in Expr mode causes an error."""
        infix = """
set_prop(MyProp, 123)
RESULT = $X
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should have failed"

    def test_exit_in_single_mode_error(self):
        """Test that exit() in SingleExpr mode causes an error."""
        infix = """
exit()
"""
        success, output = run_infix2postfix(infix, "single")
        assert not success, "Should have failed"

    def test_static_pixel_access_in_single_mode_error(self):
        """Test that static pixel access $x[1,0] in SingleExpr mode causes an error."""
        infix = """
val = $x[1, 0]
"""
        success, output = run_infix2postfix(infix, "single")
        assert not success, "Should have failed"


class TestBasicSyntax:
    """Test basic infix syntax conversion."""

    def test_arithmetic_operators(self):
        """Test basic arithmetic operators."""
        infix = """
a = 10 + 20
b = 30 - 15
c = 5 * 6
d = 100 / 4
e = a + b + c + d
RESULT = atan(e)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10 20 +" in output
        assert "30 15 -" in output
        assert "5 6 *" in output
        assert "100 4 /" in output
        assert "atan" in output

    def test_comparison_operators(self):
        """Test comparison operators."""
        infix = """
a = 10 > 5
b = 20 < 30
c = 15 >= 15
d = 10 <= 11
e = 5 == 5
RESULT = a
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10 5 >" in output
        assert "20 30 <" in output
        assert "15 15 >=" in output
        assert "10 11 <=" in output
        assert "5 5 =" in output

    def test_function_calls(self):
        """Test function calls."""
        infix = """
a = sqrt(16)
b = abs(-10)
c = max(5, 10)
d = min(20, 15)
RESULT = a + b
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "16 sqrt" in output
        assert "10 neg abs" in output or "-10 abs" in output
        assert "5 10 max" in output
        assert "20 15 min" in output

    def test_ternary_operator(self):
        """Test ternary operator."""
        infix = """
condition = $X > 100
result = condition ? 255 : 0
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "?" in output

    def test_control_flow(self):
        """Test if-else control flow."""
        infix = """
x_coord = $X
val = -1
if (x_coord > 50) {
    val = 255
} else {
    val = 0
}
RESULT = val
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "#" in output

    def test_if_else_true_value(self):
        """Test if-else with true value."""
        infix = """
a = -1
if (-1) {
    a = 255
} else {
    a = 0
}
RESULT = a
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert (
            output.strip()
            == "-1 a! -1 0 = __internal_else_0# 255 a! 1 __internal_endif_1# #__internal_else_0 0 a! #__internal_endif_1 a@ RESULT! RESULT@"
        )

    def test_while_loop(self):
        """Test while loop."""
        infix = """
counter = 5
sum = 0
while (counter > 0) {
    sum = sum + counter
    counter = counter - 1
}
RESULT = sum
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "#" in output

    def test_function_variable_scope(self):
        """Test function variable scope."""
        infix = """
function test_function(x) {
    return x + var
}
var = 1
RESULT = test_function(10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, f"Should fail, got {output}"

    def test_if_else_scope(self):
        """Test that a variable defined in an if-block is not accessible outside."""
        # var is not defined for outer scope
        infix = """
if (1) {
    var = 1
}
RESULT = var
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, f"Should fail, got {output}"
        assert "Variable 'var' is used before being defined" in output


class TestEdgeCases:
    """Test edge cases and error handling."""

    def test_empty_input(self):
        """Test empty input."""
        infix = ""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Empty input should fail"

    def test_missing_result_expr_mode(self):
        """Test that Expr mode requires RESULT assignment."""
        infix = """
a = 10 + 20
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail without RESULT"
        assert "RESULT" in output

    def test_comments(self):
        """Test that comments are handled correctly."""
        infix = """
# This is a comment
a = 10 # inline comment
# Another comment
RESULT = a
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10 a!" in output

    def test_clip_identifiers(self):
        """Test various clip identifiers."""
        infix = """
val1 = $x
val2 = $y
val3 = $z
val4 = $src0
val5 = $src10
RESULT = val1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert " x " in output or output.startswith("x ")
        assert " y " in output
        assert " z " in output
        assert "src0" in output
        assert "src10" in output

    def test_builtin_constants(self):
        """Test built-in constants."""
        infix = """
pi_val = $pi
frame_num = $N
w = $width
h = $height
RESULT = pi_val
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "pi" in output
        assert "N" in output
        assert "width" in output
        assert "height" in output

    def test_undefined_function(self):
        """Test undefined function."""
        infix = """
RESULT = atan2($x)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail"

    def test_conflict_with_builtin_function(self):
        """Test user defined function conflicts with built-in function."""
        infix = """
function sin(a, b, c, d) {
    return a + b + c + d
}
RESULT = sin(4, 3, 2, 1)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail"
        assert "conflicts with a built-in function" in output


class TestFunctions:
    def test_untyped_function(self):
        """Test that untyped function parameters are allowed and default to Value."""
        infix = """
function add(x, y) {
    return x + y
}
RESULT = add(10, 20)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert (
            output.strip()
            == "10 __internal_func_add_0_x! 20 __internal_func_add_0_y! __internal_func_add_0_x@ __internal_func_add_0_y@ + __internal_ret_add_0! 1 __internal_ret_label_add_0# #__internal_ret_label_add_0 __internal_ret_add_0@ RESULT! RESULT@"
        )

    def test_typed_function_and_global(self):
        """Test typed function and global declaration."""
        infix = """
<global<global_var>>
function test_function(Value x) {
    d = x + global_var
    return d
}
global_var = 1
RESULT = test_function(10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "1 global_var!" in output
        assert "10 __internal_func_test_function_0_x!" in output
        assert "__internal_func_test_function_0_x@ global_var@ +" in output
        assert "RESULT!" in output

    def test_global_var_undefined_before_call(self):
        """Test that calling a function before its required global is defined fails."""
        infix = """
<global<undef>>
function test(a) {
    return a + undef
}
RESULT = test($x)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail when global is undefined before call"
        assert "not defined in global scope before this call" in output
        assert "undef" in output

    def test_global_var_defined_after_call(self):
        """Test that calling a function before its required global is defined fails."""
        infix = """
<global<late_global>>
function test(a) {
    return a + late_global
}
RESULT = test($x)
late_global = 10
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail when global is defined after call"
        assert "not defined in global scope before this call" in output
        assert "late_global" in output

    def test_global_var_defined_before_call(self):
        """Test that calling a function after its required global is defined succeeds."""
        infix = """
<global<early_global>>
function test(a) {
    return a + early_global
}
early_global = 10
RESULT = test($x)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Should succeed when global is defined before call: {output}"
        assert "10 early_global!" in output
        assert "early_global@" in output

    def test_global_all_undefined_variable(self):
        """Test that <global.all> functions cannot use undefined variables."""
        infix = """
<global.all>
function test(a) {
    return a + undefined_var + 1
}
RESULT = test(4)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert (
            not success
        ), "Should fail when using undefined variable with <global.all>"
        assert "undefined_var" in output
        assert "not defined in global scope before this call" in output

    def test_global_all_with_defined_variables(self):
        """Test that <global.all> functions work when all used globals are defined."""
        infix = """
<global.all>
function test(a) {
    return a + c + d
}
c = 5
d = 10
RESULT = test(4)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Should succeed when all used globals are defined: {output}"
        assert "5 c!" in output
        assert "10 d!" in output
        assert "c@" in output
        assert "d@" in output

    def test_typed_function_param_substitution(self):
        """Test typed function parameter substitution."""
        infix = """
function test_function(Value x, Clip clip, Clip a) {
    return x + x + clip.prop + dyn(a, 1, 1)
}
RESULT = test_function(10, $src0, $x)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "src0.prop" in output
        assert "1 1 x[]:c" in output
        assert "$" not in output
        assert "a[]" not in output

    def test_nth_N_function(self):
        """Test nth_N function."""
        infix = """
RESULT = nth_3(4, 3, 2, 1)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "4 3 2 1 sort4 drop2 swap1 drop1 RESULT! RESULT@" in output

    def test_nth_N_function_with_invalid_name(self):
        """Test nth_N function with invalid name."""
        infix = """
RESULT = nth_0(4, 3, 2, 1)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail"
        assert "Invalid nth_N function name 'nth_0'" in output


class TestFunctionOverloading:
    def test_overload_by_type(self):
        """Test function overloading based on parameter types."""
        infix = """
function process(Clip c) {
    return c[1,1] * 2
}
function process(Value v) {
    return v * 2
}
RESULT = process($x) + process(10.0)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert (
            "x[1,1] 2 * __internal_ret_process_0! 1 __internal_ret_label_process_0# #__internal_ret_label_process_0 __internal_ret_process_0@ 10.0 __internal_func_process_1_v! __internal_func_process_1_v@ 2 * __internal_ret_process_1! 1 __internal_ret_label_process_1# #__internal_ret_label_process_1 __internal_ret_process_1@ + RESULT! RESULT@"
            in output
        )

    def test_overload_by_arity(self):
        """Test function overloading based on number of parameters."""
        infix = """
function f(Value x) { return x }
function f(Value x, Value y) { return x + y }
RESULT = f(1) + f(2, 3)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert (
            "1 __internal_func_f_0_x! __internal_func_f_0_x@ __internal_ret_f_0! 1 __internal_ret_label_f_0# #__internal_ret_label_f_0 __internal_ret_f_0@ 2 __internal_func_f_1_x! 3 __internal_func_f_1_y! __internal_func_f_1_x@ __internal_func_f_1_y@ + __internal_ret_f_1! 1 __internal_ret_label_f_1# #__internal_ret_label_f_1 __internal_ret_f_1@ + RESULT! RESULT@"
            in output
        )

    def test_overload_best_fit(self):
        """Test that the best-fit overload (fewest conversions) is chosen."""
        infix = """
function best(Value v, Clip c) { return 1 }
function best(Value v, Value v2) { return 2 }
RESULT = best(1.0, 2.0)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert (
            output.strip()
            == "1.0 __internal_func_best_0_v! 2.0 __internal_func_best_0_v2! 2 __internal_ret_best_0! 1 __internal_ret_label_best_0# #__internal_ret_label_best_0 __internal_ret_best_0@ RESULT! RESULT@"
        )

    def test_overload_tie_break(self):
        """Test overload resolution tie-breaking rule."""
        infix = """
function tie(Clip c, Value v) { return 1 }
function tie(Value v, Clip c) { return 2 }
RESULT = tie($x, $y)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert (
            output.strip()
            == "y __internal_func_tie_0_v! 1 __internal_ret_tie_0! 1 __internal_ret_label_tie_0# #__internal_ret_label_tie_0 __internal_ret_tie_0@ RESULT! RESULT@"
        )

    def test_duplicate_function_error(self):
        """Test that defining a function with the same signature twice errors."""
        infix = """
function dup(Value a) {
    return a + 1
}
function dup(Value b) {
    return b / 2
}
RESULT = dup(4)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail due to duplicate function definition"
        assert "Duplicate function signature" in output

    def test_untyped_duplicate_function_error(self):
        """Test that defining an untyped function with the same signature twice errors."""
        infix = """
function dup(a) {
    return a + 1
}
function dup(b) {
    return b / 2
}
RESULT = dup(4)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail due to duplicate function definition"
        assert "Duplicate function signature" in output


class TestRecursionDetection:
    """Test detection of recursive function calls."""

    def test_direct_recursion(self):
        """Test that direct recursion is detected and reported."""
        infix = """
function factorial(Value n) {
    if (n <= 1) {
        return 1
    }
    return n * factorial(n - 1)
}
RESULT = factorial(5)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail due to direct recursion"
        assert "Recursion is not allowed" in output
        assert "factorial" in output

    def test_indirect_recursion_two_functions(self):
        """Test that indirect recursion between two functions is detected."""
        infix = """
function test1(Value n) {
    if (n <= 0) {
        return 0
    }
    return test2(n - 1)
}

function test2(Value n) {
    if (n <= 0) {
        return 0
    }
    return test1(n - 1)
}

RESULT = test1(5)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail due to indirect recursion"
        assert "Recursion is not allowed" in output
        assert "test1" in output
        assert "test2" in output

    def test_indirect_recursion_three_functions(self):
        """Test that indirect recursion among three functions is detected."""
        infix = """
function a(Value n) {
    return b(n)
}

function b(Value n) {
    return c(n)
}

function c(Value n) {
    return a(n)
}

RESULT = a(1)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail due to indirect recursion"
        assert "Recursion is not allowed" in output

    def test_non_recursive_functions(self):
        """Test that non-recursive functions work correctly."""
        infix = """
function add(Value a, Value b) {
    return a + b
}

function multiply(Value a, Value b) {
    return a * b
}

RESULT = multiply(add(2, 3), 4)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Non-recursive functions should work: {output}"

    def test_function_calling_itself_conditionally(self):
        """Test that conditional recursion is detected."""
        infix = """
function fib(Value n) {
    if (n <= 1) {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

RESULT = fib(10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail due to recursion in fib"
        assert "Recursion is not allowed" in output
        assert "fib" in output

    def test_multiple_independent_functions(self):
        """Test that multiple independent functions don't trigger false positives."""
        infix = """
function helper1(Value x) {
    return x * 2
}

function helper2(Value x) {
    return x + 10
}

function main_func(Value x) {
    return helper1(x) + helper2(x)
}

RESULT = main_func(5)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Independent functions should work: {output}"


class TestStatementTermination:
    """Tests for statement termination rules (newlines and semicolons)."""

    def test_semicolon_termination(self):
        """Test that statements can be terminated by semicolons."""
        infix = """
a = 10;
b = 20;
RESULT = a + b;
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10 a!" in output
        assert "20 b!" in output
        assert "a@ b@ + RESULT!" in output

    def test_multiple_statements_on_one_line(self):
        """Test that multiple statements can be on one line separated by semicolons."""
        infix = "a = 1; b = 2; c = a + b; RESULT = c"
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "1 a! 2 b! a@ b@ + c! c@ RESULT!" in output

    def test_mixed_termination(self):
        """Test a mix of newline and semicolon terminations."""
        infix = """
a = 1;
b = 2
c = a + b; RESULT = c
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "1 a! 2 b! a@ b@ + c! c@ RESULT!" in output

    def test_empty_statements(self):
        """Test that empty statements (extra semicolons) are allowed."""
        infix = """
a = 1;
;
b = 2;;
RESULT = a + b
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "1 a! 2 b! a@ b@ + RESULT!" in output

    def test_missing_terminator_error(self):
        """Test that two statements on one line without a separator is an error."""
        infix = "a = 1 b = 2"
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should have failed"
        assert "Expected newline or semicolon after statement" in output

    def test_junk_after_statement_error(self):
        """Test that junk after a statement on the same line is an error."""
        infix = "a = 1 2"
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should have failed"
        assert "Expected newline or semicolon after statement" in output

    def test_invalid_case(self):
        """Test invalid case."""
        infix = "RESULT = 1 v = 5 + 8"
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should have failed"
        assert "Expected newline or semicolon after statement" in output


class TestSyntaxAndScopeValidation:
    """Tests for new brace, scope and RESULT definition rules."""

    def test_if_without_braces_fails(self):
        """Test that an if statement without braces is an error."""
        infix = "if (1) RESULT = 1"
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "must be a block statement" in output

    def test_while_without_braces_fails(self):
        """Test that a while statement without braces is an error."""
        infix = """
RESULT = 1
while (0) RESULT = 2
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "must be a block statement" in output

    def test_else_without_braces_fails(self):
        """Test that an else statement without braces is an error."""
        infix = """
RESULT = 0
if (0) {
    RESULT = 1
} else RESULT = 2
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "must be a block statement" in output

    def test_else_if_is_allowed(self):
        """Test that 'else if' chain is still allowed."""
        infix = """
RESULT = 0
if (0) {
    RESULT = 1
} else if (1) {
    RESULT = 2
} else {
    RESULT = 3
}
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, output

    def test_variable_leaks_from_while_fails(self):
        """Test that a variable defined in a while loop is not accessible outside."""
        infix = """
while (0) {
    a = 10
}
RESULT = a
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "Variable 'a' is used before being defined" in output

    def test_outer_scope_var_modification_is_ok(self):
        """Test that modifying an outer scope variable inside a block is allowed."""
        infix = """
a = 1
if (1) {
    a = 2
}
RESULT = a
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, output

    def test_result_only_in_block_fails(self):
        """Test that RESULT must be defined in the global scope in expr mode."""
        infix = """
if (1) {
    RESULT = 1
}
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "'RESULT' must be defined in the global scope" in output

    def test_result_in_global_and_block_is_ok(self):
        """Test that RESULT can be defined globally and modified in a block."""
        infix = """
RESULT = 0
if (1) {
    RESULT = 1
}
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, output

    def test_standalone_block_fails(self):
        """Test that a standalone block is not allowed."""
        infix = """
RESULT = 0
{
    a = 1
}
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "Standalone blocks are not allowed" in output


class TestArrays:
    """Test array functionality."""

    def test_array_creation_expr_mode(self):
        """Test array creation with literal size in Expr mode."""
        infix = """
arr = new(10)
arr[0] = 255
RESULT = arr[0]
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "arr{}^10" in output
        assert "255 0 arr{}!" in output
        assert "0 arr{}@" in output

    def test_array_creation_single_mode_literal(self):
        """Test array creation with literal size in SingleExpr mode."""
        infix = """
buffer = new(256)
buffer[0] = 128
val = buffer[0]
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "256 buffer{}^" in output
        assert "128 0 buffer{}!" in output
        assert "0 buffer{}@" in output

    def test_array_creation_single_mode_dynamic(self):
        """Test array creation with dynamic size in SingleExpr mode."""
        infix = """
size = $width * $height
buffer = new(size)
buffer[0] = 255
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "width height * size!" in output
        assert "size@ buffer{}^" in output
        assert "255 0 buffer{}!" in output

    def test_array_resize_single_mode(self):
        """Test array resize in SingleExpr mode."""
        infix = """
arr = new(10)
arr[0] = 100
arr = resize(20)
arr[10] = 200
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "10 arr{}^" in output
        assert "20 arr{}^" in output
        assert "100 0 arr{}!" in output
        assert "200 10 arr{}!" in output

    def test_array_element_access_in_expression(self):
        """Test array element access in expressions."""
        infix = """
arr = new(5)
arr[0] = 10
arr[1] = 20
arr[2] = 30
result = arr[0] + arr[1] * arr[2]
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "0 arr{}@" in output
        assert "1 arr{}@" in output
        assert "2 arr{}@" in output
        assert "+" in output
        assert "*" in output

    def test_array_with_loop(self):
        """Test array usage in a loop."""
        infix = """
arr = new(5)
i = 0
while (i < 5) {
    arr[i] = i * 10
    i = i + 1
}
RESULT = arr[3]
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "arr{}^5" in output
        assert "i@ arr{}!" in output
        assert "3 arr{}@" in output

    def test_array_as_function_parameter(self):
        """Test passing array to function."""
        infix = """
function fill_array(Array a, Value size, Value fill_val) {
    i = 0
    while (i < size) {
        a[i] = fill_val
        i = i + 1
    }
}

my_array = new(3)
fill_array(my_array, 3, 100)
RESULT = my_array[0] + my_array[1] + my_array[2]
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "my_array{}^3" in output
        assert "my_array{}!" in output
        assert "my_array{}@" in output

    def test_array_function_modifies_original(self):
        """Test that array modifications in function affect original."""
        infix = """
function set_first(Array a, Value val) {
    a[0] = val
}

arr = new(1)
arr[0] = 10
set_first(arr, 99)
RESULT = arr[0]
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "arr{}^1" in output
        # The function stores val parameter then writes to array
        assert "__internal_func_set_first_0_val@" in output
        assert "0 arr{}!" in output

    def test_array_with_expression_index(self):
        """Test array access with computed index."""
        infix = """
arr = new(10)
idx = 3 + 2
arr[idx] = 42
val = arr[5]
RESULT = val
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "3 2 + idx!" in output
        assert "42 idx@ arr{}!" in output
        assert "5 arr{}@" in output

    def test_multiple_arrays(self):
        """Test using multiple arrays."""
        infix = """
arr1 = new(2)
arr2 = new(2)
arr1[0] = 10
arr1[1] = 20
arr2[0] = arr1[0] * 2
arr2[1] = arr1[1] * 2
RESULT = arr2[0] + arr2[1]
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "arr1{}^2" in output
        assert "arr2{}^2" in output
        assert "0 arr1{}@" in output
        assert "0 arr2{}!" in output

    def test_array_in_single_expr_with_store(self):
        """Test array in SingleExpr mode with store operations."""
        infix = """
lut = new(256)
i = 0
while (i < 256) {
    lut[i] = i * i
    i = i + 1
}
val = lut[128]
store(0, 0, 0, val)
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "256 lut{}^" in output
        assert "i@ lut{}!" in output
        assert "128 lut{}@" in output

    def test_array_distinguish_from_pixel_access(self):
        """Test that array access is distinguished from pixel access."""
        infix = """
arr = new(5)
arr[3] = $x[1, 0]
RESULT = arr[3]
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "arr{}^5" in output
        assert "x[1,0]" in output  # Pixel access
        assert "3 arr{}!" in output  # Array write
        assert "3 arr{}@" in output  # Array read

    # Error cases

    def test_array_expr_mode_requires_literal_size(self):
        """Test that Expr mode requires literal size for new()."""
        infix = """
size = 10 + $x
arr = new(size)
RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail with non-literal size in Expr mode"
        assert "Array size must be a literal constant" in output

    def test_array_expr_mode_literal_param_size(self):
        """Test that Expr mode accepts Literal-typed parameter for new() size."""
        infix = """
function create_array(Literal size) {
    arr = new(size)
    arr[0] = 100
    return arr[0]
}
RESULT = create_array(10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Should succeed with Literal-typed parameter: {output}"
        assert "arr{}^10" in output
        assert "100 0" in output

    def test_array_resize_not_in_expr_mode(self):
        """Test that resize() is not available in Expr mode."""
        infix = """
arr = new(10)
arr = resize(20)
RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail using resize() in Expr mode"
        assert "resize() is only available in SingleExpr mode" in output

    def test_array_cannot_be_reallocated_with_new(self):
        """Test that an array cannot be allocated twice with new()."""
        infix = """
arr = new(10)
arr = new(20)
RESULT = arr[5]
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail reallocating array with new()"
        assert "Cannot reallocate array" in output

    def test_array_can_be_resized_in_single_mode(self):
        """Test that an array can be resized with resize() in SingleExpr mode."""
        infix = """
arr = new(10)
arr[0] = 100
arr = resize(20)
arr[15] = 200
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "10 arr{}^" in output
        assert "20 arr{}^" in output

    def test_resize_requires_prior_allocation(self):
        """Test that resize() requires the array to be allocated first."""
        infix = """
arr = resize(10)
"""
        success, output = run_infix2postfix(infix, "single")
        assert not success, "Should fail using resize() on unallocated variable"
        assert "is undefined or not an array." in output

    def test_resize_on_non_array_variable(self):
        """Test that resize() cannot be used on non-array variables."""
        infix = """
val = 42
val = resize(10)
"""
        success, output = run_infix2postfix(infix, "single")
        assert not success, "Should fail using resize() on non-array"
        assert "not an array" in output

    def test_array_cannot_be_reassigned_to_value(self):
        """Test that an array variable cannot be reassigned to a non-array value."""
        infix = """
arr = new(10)
arr = 42
"""
        success, output = run_infix2postfix(infix, "single")
        assert not success, "Should fail reassigning array to value"
        assert "is an array and cannot be reassigned" in output

    def test_array_cannot_be_reassigned_to_value_expr_mode(self):
        """Test that an array variable cannot be reassigned to a value in Expr mode."""
        infix = """
arr = new(10)
arr = 42
RESULT = arr
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail reassigning array to value"
        assert "is an array and cannot be reassigned" in output

    def test_array_cannot_be_assigned_to_variable(self):
        """Test that arrays cannot be assigned to other variables."""
        infix = """
arr1 = new(10)
arr2 = arr1
RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail assigning array to variable"
        assert (
            "Variable assignment value must be convertible to a value, got Array."
            in output
        )

    def test_array_cannot_be_assigned_in_function(self):
        """Test that arrays cannot be assigned to variables in functions."""
        infix = """
function process(Array a) {
    tmp = a
    return tmp[0]
}
arr = new(5)
RESULT = process(arr)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail assigning array parameter to variable"
        assert (
            "Variable assignment value must be convertible to a value, got Array."
            in output
        )

    def test_function_cannot_return_array(self):
        """Test that functions cannot return arrays."""
        infix = """
function get_array() {
    arr = new(5)
    return arr
}
RESULT = get_array()
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail returning array from function"
        assert "Functions cannot return arrays" in output

    def test_function_cannot_return_array_parameter(self):
        """Test that functions cannot return array parameters."""
        infix = """
function return_param(Array a) {
    return a
}
arr = new(5)
RESULT = return_param(arr)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail returning array parameter"
        assert "Functions cannot return arrays" in output

    def test_array_access_undefined_variable(self):
        """Test accessing undefined array."""
        infix = """
val = undefined_array[0]
RESULT = val
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail accessing undefined array"
        assert "is used before being defined" in output

    def test_array_access_on_non_array(self):
        """Test array access on non-array variable."""
        infix = """
not_array = 42
val = not_array[0]
RESULT = val
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail accessing non-array as array"
        assert "is not an array" in output

    def test_array_assign_to_non_array(self):
        """Test array assignment to non-array variable."""
        infix = """
not_array = 42
not_array[0] = 10
RESULT = not_array
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail assigning to non-array as array"
        assert "is not an array" in output

    def test_array_function_param_type_mismatch(self):
        """Test passing non-array to Array parameter."""
        infix = """
function process(Array a) {
    return a[0]
}
val = 42
RESULT = process(val)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail passing non-array to Array parameter"

    def test_array_function_param_requires_variable(self):
        """Test that Array parameter requires a variable, not expression."""
        infix = """
function process(Array a) {
    return a[0]
}
arr = new(5)
arr[0] = 10
RESULT = process(arr[0])
"""
        success, output = run_infix2postfix(infix, "expr")
        assert (
            not success
        ), "Should fail passing non-variable expression to Array parameter"

    def test_array_overloading_by_type(self):
        """Test function overloading with Array type."""
        infix = """
function process(Array a) {
    return a[0]
}
function process(Value v) {
    return v * 2
}
arr = new(2)
arr[0] = 10
result1 = process(arr)
result2 = process(20)
RESULT = result1 + result2
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "arr{}^2" in output
        assert "0 arr{}@" in output
        assert "2 *" in output

    def test_array_creation_no_args_fails(self):
        """Test that new() with no arguments fails gracefully."""
        infix = "arr = new()"
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail with no arguments for new()"
        assert "new() requires exactly 1 argument (size)" in output

    def test_array_resize_no_args_fails(self):
        """Test that resize() with no arguments fails gracefully."""
        infix = """
arr = new(1)
arr = resize()
"""
        success, output = run_infix2postfix(infix, "single")
        assert not success, "Should fail with no arguments for resize()"
        assert "resize() requires exactly 1 argument (size)" in output

    def test_array_complex_scenario_expr(self):
        """Test complex array scenario in Expr mode."""
        infix = """
lut = new(256)
i = 0
while (i < 256) {
    lut[i] = sqrt(i)
    i = i + 1
}

pixel = $x
idx = pixel > 255 ? 255 : pixel
RESULT = lut[idx]
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "lut{}^256" in output
        assert "sqrt" in output
        assert "?" in output

    def test_array_complex_scenario_single(self):
        """Test complex array scenario in SingleExpr mode."""
        infix = """
w = frame.width[0]
h = frame.height[0]
histogram = new(256)

i = 0
while (i < 256) {
    histogram[i] = 0
    i = i + 1
}

val1 = dyn($x, 0, 0, 0)
val2 = dyn($x, w - 1, 0, 0)
val3 = dyn($x, 0, h - 1, 0)
val4 = dyn($x, w - 1, h - 1, 0)

set_prop(Histogram0, histogram[0])
set_prop(Histogram255, histogram[255])
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "256 histogram{}^" in output
        # Array write is: value index array{}!
        assert "i@ histogram{}!" in output
        assert "0 histogram{}@" in output
        assert "255 histogram{}@" in output

    def test_array_nested_function_calls(self):
        """Test arrays with nested function calls."""
        infix = """
function get_value(Array a, Value idx) {
    return a[idx]
}

function set_value(Array a, Value idx, Value val) {
    a[idx] = val
}

arr = new(3)
set_value(arr, 0, 100)
set_value(arr, 1, 200)
result = get_value(arr, 0) + get_value(arr, 1)
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "arr{}^3" in output
        assert "arr{}!" in output
        assert "arr{}@" in output

    def test_array_size_Literal(self):
        infix = """
function test(Literal size) {
    arr = new(size)
    return arr[0]
}
RESULT = test(10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "__internal_func_test_0_arr{}^10" in output
        assert "0 __internal_func_test_0_arr{}@" in output


class TestControlFlowValidation:
    # GOTO related tests
    def test_goto_forbidden_label_name(self):
        infix = """
__internal_label:
    RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "cannot start with '__internal_'" in output

    def test_goto_forbidden_target_name(self):
        infix = """
    goto __internal_label
__internal_label:
    RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "cannot start with '__internal_'" in output

    def test_goto_to_nonexistent_label_global(self):
        infix = """
    goto missing_label
    RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "goto target 'missing_label' not found in global scope" in output

    def test_goto_to_nonexistent_label_function(self):
        infix = """
function f() {
    goto missing_label
}
RESULT = f()
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "goto target 'missing_label' not found in function 'f'" in output

    def test_duplicate_label_global(self):
        infix = """
my_label:
    a = 1
my_label:
    a = 2
RESULT = a
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "Duplicate label 'my_label' in global scope" in output

    def test_duplicate_label_function(self):
        infix = """
function f() {
my_label:
    a = 1
my_label:
    a = 2
    return a
}
RESULT = f()
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "Duplicate label 'my_label' in function 'f'" in output

    def test_goto_from_function_to_global_is_disallowed(self):
        infix = """
function f() {
    goto global_label
}
global_label:
RESULT = f()
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert (
            "goto from function 'f' to global label 'global_label' is not allowed"
            in output
        )

    def test_valid_goto_in_function(self):
        infix = """
function f(a) {
    if (a > 0) {
        goto end
    }
    a = a + 1
end:
    return a
}
RESULT = f(10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed: {output}"

    def test_goto_skips_variable_initialization(self):
        """Test that goto cannot skip the first assignment of a variable."""
        infix = """
    goto skip_init
    a = 10
skip_init:
    RESULT = a
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail because goto skips variable initialization"
        assert "goto jumps over initialization of variable 'a'" in output

    # RETURN related tests
    def test_return_in_global_scope(self):
        infix = "return 10"
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "'return' statements are not allowed in the global scope" in output

    def test_code_after_return_is_valid(self):
        """A return statement doesn't have to be the last one; code after it is just unreachable."""
        infix = """
function f() {
    return 1
    a = 2
}
RESULT = f()
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Code after return should be valid: {output}"

    def test_multiple_consecutive_returns_are_valid(self):
        """Multiple returns are allowed; subsequent ones are just unreachable."""
        infix = """
function f() {
    return 1
    return 2
}
RESULT = f()
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Should be valid, second return is just unreachable: {output}"

    def test_return_in_if_block_is_valid(self):
        """A return inside a block is now valid."""
        infix = """
function f(a) {
    if (a > 0) {
        return 1
    }
    return 0
}
RESULT = f(1)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Return in if-block should now be valid: {output}"

    def test_return_in_while_block_is_valid(self):
        """A return inside a while is valid if all paths eventually return."""
        infix = """
function f(a) {
    while(a > 0) {
        return 1
    }
    return 0
}
RESULT = f(1)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert (
            success
        ), f"Return in while-block should be valid if another path returns: {output}"

    def test_valid_single_return(self):
        """A simple single return at the end of a function is valid."""
        infix = """
function f(a) {
    b = a + 1
    return b
}
RESULT = f(10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed: {output}"

    def test_token_pasting_simple(self):
        """Test @@ operator for simple token pasting."""
        infix = """
@define CONCAT(a, b) a@@b
xy = 42
RESULT = CONCAT(x, y)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "42 xy!" in output
        assert "xy@" in output

    def test_token_pasting_with_numbers(self):
        """Test @@ operator with numbers."""
        infix = """
@define CONCAT(a, b) a@@b
RESULT = CONCAT(12, 34)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "1234 RESULT!" in output

    def test_token_pasting_with_empty_arg(self):
        """Test @@ operator with an empty argument."""
        infix = """
@define PASTE(a, b) a@@b
hello = 99
RESULT = PASTE(hello, )
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "99 hello!" in output
        assert "hello@" in output

    def test_token_pasting_with_empty_arg_left(self):
        """Test @@ operator with an empty argument on the left."""
        infix = """
@define PASTE(a, b) a@@b
world = 101
RESULT = PASTE(, world)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "101 world!" in output
        assert "world@" in output

    def test_token_pasting_multiple(self):
        """Test multiple @@ operators."""
        infix = """
@define CONCAT3(a, b, c) a@@b@@c
var = 55
RESULT = CONCAT3(v, a, r)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "55 var!" in output
        assert "var@" in output

    def test_token_pasting_forms_new_macro(self):
        """Test that @@ can form a new macro to be expanded."""
        infix = """
@define MY_MACRO 123
@define CONCAT(a, b) a@@b
RESULT = CONCAT(MY_, MACRO)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "123 RESULT!" in output
    
    def test_nested_pasting_and_expansion(self):
        """Test that a token generated by pasting is correctly identified and expanded if it matches another macro."""
        infix = """
        @requires meta
        
        @define NUM1__ 888
        
        @define TEST_(i) PASTE(NUM, i)
        @define TEST(i) PASTE(TEST_(i), __)

        val = TEST(1)
        RESULT = val
        """
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed recursive pasting expansion: {output}"
        assert "888 val!" in output

class TestVoidFunctions:
    """Tests to ensure void functions are handled correctly."""

    def test_assign_void_user_function_fails(self):
        """Test that assigning the result of a user-defined void function fails."""
        infix = """
function void_func() {
    # No return value
}
a = void_func()
RESULT = 0
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert (
            "Variable assignment value must be convertible to a value, got Void."
            in output
        )

    def test_assign_void_builtin_function_fails(self):
        """Test that assigning the result of a built-in void function (store) fails."""
        infix = """
a = store(0, 0, 0)
RESULT = 0
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert (
            "Variable assignment value must be convertible to a value, got Void."
            in output
        )

    def test_pass_void_user_function_as_arg_fails(self):
        """Test that passing a user-defined void function as an argument fails."""
        infix = """
function void_func() { }
function takes_value(Value v) { return v }
RESULT = takes_value(void_func())
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert (
            "No matching user-defined function for call to 'takes_value(Void)'"
            in output
        )

    def test_pass_void_builtin_function_as_arg_fails(self):
        """Test that passing a built-in void function as an argument fails."""
        infix = """
RESULT = sqrt(store(0, 0, 0))
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success
        assert "No matching overload for function 'sqrt(Void)'" in output


class TestPreprocessor:
    """Test preprocessor directives and macro functionality."""

    def test_simple_define(self):
        """Test basic @define directive."""
        infix = """
@define MAX 100
RESULT = MAX
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "100" in output
        assert "MAX" not in output

    def test_define_with_expression(self):
        """Test @define with arithmetic expression that gets evaluated."""
        infix = """
@define SIZE (2 * 3)
RESULT = SIZE
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "6" in output

    def test_define_in_expression(self):
        """Test using defined macro in expressions."""
        infix = """
@define MULTIPLIER 5
a = MULTIPLIER * 10
RESULT = a + MULTIPLIER
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "5 10 *" in output
        assert "a@ 5 +" in output

    def test_undef(self):
        """Test @undef directive."""
        infix = """
@define TEMP 42
a = TEMP
@undef TEMP
RESULT = a
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "42 a!" in output

    def test_ifdef_true(self):
        """Test @ifdef when macro is defined."""
        infix = """
@define DEBUG
@ifdef DEBUG
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output
        assert "0 RESULT!" not in output

    def test_ifdef_false(self):
        """Test @ifdef when macro is not defined."""
        infix = """
@ifdef UNDEFINED
RESULT = 0
@else
RESULT = 255
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output
        assert not output.startswith("0 RESULT!")

    def test_ifndef_true(self):
        """Test @ifndef when macro is not defined."""
        infix = """
@ifndef UNDEFINED
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_ifndef_false(self):
        """Test @ifndef when macro is defined."""
        infix = """
@define FEATURE
@ifndef FEATURE
RESULT = 0
@else
RESULT = 255
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_nested_ifdef(self):
        """Test nested @ifdef directives."""
        infix = """
@define OUTER
@define INNER
@ifdef OUTER
    @ifdef INNER
        RESULT = 3
    @else
        RESULT = 2
    @endif
@else
    RESULT = 1
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "3 RESULT!" in output

    def test_if_with_literal(self):
        """Test @if with numeric literal."""
        infix = """
@if 1
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_if_with_zero(self):
        """Test @if with zero (false condition)."""
        infix = """
@if 0
RESULT = 0
@else
RESULT = 255
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_if_with_expression(self):
        """Test @if with arithmetic expression."""
        infix = """
@define VERSION 3
@if VERSION > 2
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_if_with_defined_operator(self):
        """Test @if with defined() operator."""
        infix = """
@define FEATURE
@if defined(FEATURE)
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_if_with_defined_operator_false(self):
        """Test @if with defined() operator on undefined macro."""
        infix = """
@if defined(UNDEFINED)
RESULT = 0
@else
RESULT = 255
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_if_with_logical_operators(self):
        """Test @if with logical operators."""
        infix = """
@define A 1
@define B 1
@if A && B
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_if_with_complex_expression(self):
        """Test @if with complex expression."""
        infix = """
@define VERSION 5
@if (VERSION >= 3) && (VERSION <= 10)
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_error_directive_in_active_block(self):
        """Test @error directive in active code block."""
        infix = """
@error This should fail
RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail with @error directive"
        assert "This should fail" in output

    def test_recursive_macro_with_pasting(self):
        """Test recursive macro expansion with token pasting."""
        infix = """
@define MINUS(a, b) ((a) - (b))
@define PASTE(a, b) a@@b
@define EVAL_AND_PASTE(a, b) PASTE(a, b)
@define EXPAND(prefix, n) (n == 1 ? EVAL_AND_PASTE(prefix, 0) : (EXPAND(prefix, MINUS(n, 1)) + EVAL_AND_PASTE(prefix, MINUS(n, 1))))

RESULT = EXPAND($src, 5)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert output.strip() == "src0 src1 + src2 + src3 + src4 + RESULT! RESULT@"

    def test_error_directive_in_inactive_block(self):
        """Test @error directive in inactive code block (should not trigger)."""
        infix = """
@ifdef UNDEFINED
@error This should not fail
@endif
RESULT = 255
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Should succeed: {output}"
        assert "255 RESULT!" in output

    def test_predefined_expr_macro(self):
        """Test __EXPR__ predefined macro in Expr mode."""
        infix = """
@ifdef __EXPR__
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_predefined_singleexpr_macro(self):
        """Test __SINGLEEXPR__ predefined macro in SingleExpr mode."""
        infix = """
@ifdef __SINGLEEXPR__
set_prop(Mode, 1)
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "single")
        assert success, f"Failed to convert: {output}"
        assert "Mode$" in output

    def test_mode_specific_code(self):
        """Test writing mode-specific code with preprocessor."""
        infix = """
@ifdef __EXPR__
val = $X + $Y
RESULT = val
@endif
@ifdef __SINGLEEXPR__
set_prop(Test, 42)
@endif
"""
        success_expr, output_expr = run_infix2postfix(infix, "expr")
        assert success_expr, f"Expr mode failed: {output_expr}"
        assert "X Y +" in output_expr

        success_single, output_single = run_infix2postfix(infix, "single")
        assert success_single, f"Single mode failed: {output_single}"
        assert "Test$" in output_single

    def test_macro_not_expanded_in_comments(self):
        """Test that macros are not expanded in comments."""
        infix = """
@define VALUE 100
# This is VALUE in a comment
RESULT = VALUE
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "100 RESULT!" in output

    def test_multiple_macros(self):
        """Test multiple macro definitions and usage."""
        infix = """
@define WIDTH 1920
@define HEIGHT 1080
@define ASPECT_RATIO (WIDTH / HEIGHT)
result = ASPECT_RATIO
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"

    def test_redefine_macro(self):
        """Test redefining a macro (should work after undef)."""
        infix = """
@define VALUE 10
a = VALUE
@undef VALUE
@define VALUE 20
b = VALUE
RESULT = a + b
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10 a!" in output
        assert "20 b!" in output

    def test_ifdef_without_endif_error(self):
        """Test @ifdef without matching @endif."""
        infix = """
@ifdef TEST
RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail without @endif"

    def test_else_without_if_error(self):
        """Test @else without matching @if/@ifdef/@ifndef."""
        infix = """
@else
RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail with orphaned @else"

    def test_endif_without_if_error(self):
        """Test @endif without matching @if/@ifdef/@ifndef."""
        infix = """
@endif
RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail with orphaned @endif"

    def test_macro_in_function(self):
        """Test using macros in function definitions."""
        infix = """
@define MULTIPLIER 2
function double_it(x) {
    return x * MULTIPLIER
}
RESULT = double_it(21)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "2 *" in output

    def test_conditional_function_definition(self):
        """Test conditionally defining functions."""
        infix = """
@define USE_OPTIMIZED
@ifdef USE_OPTIMIZED
function process(x) {
    return x * 2
}
@else
function process(x) {
    return x + x
}
@endif
RESULT = process(10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "2 *" in output

    def test_define_with_floating_point(self):
        """Test @define with floating-point values."""
        infix = """
@define PI 3.14159
@define HALF_PI (PI / 2)
RESULT = HALF_PI
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"

    def test_macro_case_sensitivity(self):
        """Test that macro names are case-sensitive."""
        infix = """
@define value 10
@define VALUE 20
a = value
b = VALUE
RESULT = a + b
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10 a!" in output
        assert "20 b!" in output

    def test_if_comparison_operators(self):
        """Test @if with various comparison operators."""
        infix = """
@define VER 5
@if VER == 5
RESULT = 1
@else
@if VER != 5
RESULT = 2
@else
@if VER < 5
RESULT = 3
@else
@if VER > 5
RESULT = 4
@else
RESULT = 5
@endif
@endif
@endif
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "1 RESULT!" in output

    def test_complex_preprocessor_scenario(self):
        """Test complex preprocessor usage combining multiple features."""
        infix = """
@define DEBUG
@define THRESHOLD 128
@define SCALE 2

@ifdef DEBUG
# Debug mode enabled
@endif

@if THRESHOLD > 100
function process(x) {
    @ifdef DEBUG
    result = x * SCALE
    @else
    result = x
    @endif
    return result
}
@else
function process(x) {
    return x / SCALE
}
@endif

RESULT = process($x)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "2 *" in output

    def test_error_with_mode_check(self):
        """Test @error combined with mode checking."""
        infix = """
@ifdef __SINGLEEXPR__
@error This script is not compatible with SingleExpr mode
@endif
RESULT = $X
"""
        success_expr, output_expr = run_infix2postfix(infix, "expr")
        assert success_expr, f"Should succeed in Expr mode: {output_expr}"

        success_single, output_single = run_infix2postfix(infix, "single")
        assert not success_single, "Should fail in SingleExpr mode"
        assert "not compatible with SingleExpr mode" in output_single

    def test_macro_with_underscore(self):
        """Test macro names with underscores and constant expressions."""
        infix = """
@define MY_CONSTANT 42
@define _PRIVATE 10
@define __SPECIAL__ 5
@define RES MY_CONSTANT + _PRIVATE + __SPECIAL__
RESULT = RES
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "57" in output

    def test_define_without_value(self):
        """Test @define without value (feature flag)."""
        infix = """
@define FEATURE_ENABLED
@ifdef FEATURE_ENABLED
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_if_with_not_operator(self):
        """Test @if with logical NOT operator."""
        infix = """
@define DISABLED 0
@if !DISABLED
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_if_with_or_operator(self):
        """Test @if with logical OR operator."""
        infix = """
@define A 0
@define B 1
@if A || B
RESULT = 255
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "255 RESULT!" in output

    def test_multiline_conditional(self):
        """Test multiline code in conditional blocks."""
        infix = """
@define ENABLE_PROCESSING
@ifdef ENABLE_PROCESSING
a = 10
b = 20
c = 30
result = a + b + c
RESULT = result
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10 a!" in output
        assert "20 b!" in output
        assert "30 c!" in output

    def test_function_like_macro_basic(self):
        """Test basic function-like macro."""
        infix = """
@define ADD(a, b) ((a) + (b))
RESULT = ADD(10, 20)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        # With constant folding, ADD(10, 20) evaluates to 30
        assert "30" in output
        assert "ADD" not in output

    def test_function_like_macro_single_param(self):
        """Test function-like macro with single parameter."""
        infix = """
@define SQR(x) ((x) * (x))
RESULT = SQR(5)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        # With constant folding, SQR(5) evaluates to 25
        assert "25" in output

    def test_function_like_macro_nested_calls(self):
        """Test nested function-like macro calls."""
        infix = """
@define ADD(a, b) ((a) + (b))
@define MUL(a, b) ((a) * (b))
@define SQR(x) MUL(x, x)
RESULT = SQR(3)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        # With constant folding, SQR(3) -> MUL(3, 3) evaluates to 9
        assert "9" in output

    def test_function_like_macro_complex_args(self):
        """Test function-like macro with complex arguments containing parentheses."""
        infix = """
@define ADD(a, b) ((a) + (b))
@define MUL(a, b) ((a) * (b))
result = ADD(MUL(2, 3), MUL(4, 5))
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "26" in output

    def test_function_like_macro_wrong_arg_count(self):
        """Test function-like macro with wrong number of arguments."""
        infix = """
@define ADD(a, b) ((a) + (b))
RESULT = ADD(1, 2, 3)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail with wrong argument count"
        assert "expects 2 arguments, but 3 were provided" in output

    def test_function_like_macro_empty_params(self):
        """Test function-like macro with empty parameter list."""
        infix = """
@define ZERO() 0
@define ONE() 1
RESULT = ZERO() + ONE()
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "0" in output
        assert "1" in output
        assert "+" in output

    def test_function_like_macro_multiple_params(self):
        """Test function-like macro with multiple parameters."""
        infix = """
@define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
RESULT = CLAMP(50, 0, 100)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "50 RESULT! RESULT@" == output.strip()

    def test_function_like_macro_with_object_macro(self):
        """Test mixing function-like and object-like macros."""
        infix = """
@define PI 3.14159
@define CIRCLE_AREA(r) (PI * (r) * (r))
RESULT = CIRCLE_AREA(10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        # With constant folding, CIRCLE_AREA(10) -> (3.14159 * 10 * 10) evaluates to 314.159
        assert "314.159" in output

    def test_function_like_macro_in_expression(self):
        """Test using function-like macro in complex expressions."""
        infix = """
@define MAX(a, b) ((a) > (b) ? (a) : (b))
@define MIN(a, b) ((a) < (b) ? (a) : (b))
result = MAX(5, 10) + MIN(3, 7)
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10 3 +" in output

    def test_function_like_macro_recursive_expansion(self):
        """Test recursive expansion of function-like macros."""
        infix = """
@define ADD(a, b) ((a) + (b))
@define DOUBLE(x) ADD(x, x)
@define QUAD(x) DOUBLE(DOUBLE(x))
RESULT = QUAD(5)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "20" in output

    def test_function_like_macro_whitespace_handling(self):
        """Test function-like macro with various whitespace in arguments."""
        infix = """
@define ADD(a, b) ((a) + (b))
RESULT = ADD( 10 , 20 )
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        # With constant folding, ADD(10, 20) evaluates to 30
        assert "30" in output

    def test_function_like_macro_duplicate_params_error(self):
        """Test that duplicate parameter names cause an error."""
        infix = """
@define BAD(x, x) ((x) + (x))
RESULT = BAD(1, 2)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail with duplicate parameter names"
        assert "Duplicate parameter name" in output

    def test_function_like_macro_unterminated_params_error(self):
        """Test that unterminated parameter list causes an error."""
        infix = """
@define BAD(a, b
RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail with unterminated parameter list"
        assert "Unterminated parameter list" in output

    def test_function_like_macro_no_space_before_paren(self):
        """Test that space before paren makes it object-like, not function-like."""
        infix = """
@define NOTFUNC (10 + 20)
result = NOTFUNC
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        # Should expand to the evaluated value (30)
        assert "30" in output

    def test_function_like_macro_in_function_definition(self):
        """Test using function-like macro in user function."""
        infix = """
@define ADD(a, b) ((a) + (b))
function process(x) {
    return ADD(x, 10)
}
RESULT = process(5)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10" in output
        assert "+" in output

    def test_function_like_macro_with_ternary(self):
        """Test function-like macro containing ternary operator."""
        infix = """
@define ABS(x) ((x) < 0 ? -(x) : (x))
RESULT = ABS(-5)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "5" in output or "-5" in output
        assert "?" not in output

    def test_function_like_macro_parameter_replacement(self):
        """Test that parameter replacement is word-boundary aware."""
        infix = """
@define TEST(x) (x + x * x)
# Parameter 'x' should be replaced three times
result = TEST(5)
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        # With constant folding, TEST(5) -> (5 + 5 * 5) evaluates to 30
        assert "30" in output

    def test_function_like_macro_word_boundary(self):
        """Test that parameter names don't match partial identifiers."""
        infix = """
@define EXTRACT(a) a
value_a = 10
value_ab = 20
value_abc = 30
result = EXTRACT(value_a) + EXTRACT(value_ab) + EXTRACT(value_abc)
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10" in output
        assert "20" in output
        assert "30" in output
        assert "value_a" in output
        assert "value_ab" in output
        assert "value_abc" in output

    def test_function_like_macro_constant_folding(self):
        """Test that constant expressions in macro expansions are evaluated."""
        infix = """
@define CALC_LEN(b, e) ((e) - (b))
function test(Literal len) {
    arr = new(len)
    return arr[0]
}
RESULT = test(CALC_LEN(0, 10))
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        # The expression ((10) - (0)) should be evaluated to 10
        assert "arr{}^10" in output

    def test_consteval_basic(self):
        """Test basic consteval() operator."""
        infix = """
@define SIZE 10
RESULT = consteval(SIZE * 2)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "20" in output

    def test_consteval_with_expression(self):
        """Test consteval() with complex expression."""
        infix = """
@define A 5
@define B 3
RESULT = consteval(A * A + B * B)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "34" in output

    def test_consteval_fails_on_non_constant(self):
        """Test that consteval() fails on non-constant expression."""
        infix = """
RESULT = consteval($X + 10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail with non-constant expression"
        assert "consteval() requires a constant expression" in output

    def test_is_consteval_with_constant(self):
        """Test is_consteval() with constant expression."""
        infix = """
@define VALUE 42
@if is_consteval(VALUE * 2)
RESULT = 1
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "1 RESULT!" in output

    def test_is_consteval_with_non_constant(self):
        """Test is_consteval() with non-constant expression."""
        infix = """
@if is_consteval($X + 10)
RESULT = 0
@else
RESULT = 1
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "1 RESULT!" in output

    def test_consteval_in_macro_definition(self):
        """Test consteval() in macro definition."""
        infix = """
@define SIZE 8
@define AREA consteval(SIZE * SIZE)
RESULT = AREA
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "64" in output

    def test_is_consteval_in_conditional_macro(self):
        """Test is_consteval() in conditional compilation."""
        infix = """
@define LEN 10
@define DISPATCH(x) (is_consteval(x) ? consteval(x) : (x))
RESULT = DISPATCH(LEN)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10" in output

    def test_consteval_with_nested_macros(self):
        """Test consteval() with nested macro expansion."""
        infix = """
@define INNER 5
@define OUTER(x) ((x) * 2)
RESULT = consteval(OUTER(INNER) + 3)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "13" in output

    def test_consteval_with_defined_operator(self):
        """Test consteval() with defined() operator in macro."""
        infix = """
@define FEATURE
@define CHECK_FEATURE consteval(defined(FEATURE) + defined(UNDEFINED))
RESULT = CHECK_FEATURE
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "1" in output

    def test_is_consteval_in_function_like_macro(self):
        """Test is_consteval() in function-like macro."""
        infix = """
@define SAFE_EVAL(x) (is_consteval(x) ? consteval(x) : 0)
@define CONST 42
RESULT = SAFE_EVAL(CONST)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "42" in output

    def test_consteval_in_if_directive(self):
        """Test consteval() in @if directive."""
        infix = """
@define SIZE 100
@if consteval(SIZE > 50)
RESULT = 1
@else
RESULT = 0
@endif
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "1 RESULT!" in output

    def test_consteval_in_array_size(self):
        """Test consteval() for array size calculation (simulates stdlib use case)."""
        infix = """
@define WIDTH 10
@define HEIGHT 10
@define BUFFER_SIZE consteval(WIDTH * HEIGHT)
function test(Literal size) {
    arr = new(size)
    return arr[0]
}
RESULT = test(BUFFER_SIZE)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "arr{}^100" in output

    def test_stdlib_like_conditional_dispatch(self):
        """Test stdlib-like pattern: use fast path if size is constant."""
        infix = """
@define SIZE 100
@define FAST_PATH(n) consteval((n) * (n) + 1)
@define SLOW_PATH(x) ((x) * (x))
@define DISPATCH(x) (is_consteval(x) ? FAST_PATH(x) : SLOW_PATH(x))
RESULT = DISPATCH(SIZE)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "10001" in output

    def test_consteval_with_ternary_in_macro(self):
        """Test consteval with ternary operator (common in stdlib)."""
        infix = """
@define LEN 5
@define SAFE_SIZE(n) consteval((n) <= 0 ? 1 : (n))
RESULT = SAFE_SIZE(LEN)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "5" in output

    def test_nested_macro_call_with_token_pasting(self):
        """Test passing macro name as argument and using it with token pasting."""
        infix = """
@define _PASTE(a, b) a@@b
@define EVAL_AND_PASTE(a, b) _PASTE(a, b)
@define GET_SRC(i) EVAL_AND_PASTE($src, i)
@define CALL(macro, arg) macro(arg)

RESULT = CALL(GET_SRC, 0)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        # Should expand to $src0, not (0) or empty
        assert "src0" in output
        assert "RESULT!" in output

    def test_recursive_macro_with_token_pasting_simplified(self):
        """Test simplified version of recursive macro with token pasting (JOIN pattern)."""
        infix = """
@define DEC(n) ((n) - 1)
@define _PASTE(a, b) a@@b
@define EVAL_AND_PASTE(a, b) _PASTE(a, b)
@define GET_X(i) EVAL_AND_PASTE($src, i)
@define CALL(macro, arg) macro(arg)
@define _JOIN(count, macro, sep) ((count) == 1 ? CALL(macro, 0) : (_JOIN(DEC(count), macro, sep) sep CALL(macro, DEC(count))))
@define JOIN(count, macro, sep) _JOIN(count, macro, sep)

RESULT = JOIN(3, GET_X, +)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert output.strip() == "src0 src1 + src2 + RESULT! RESULT@"


class TestConditionSemantics:
    """Test that infix conditions are converted to use != 0 semantics."""

    def test_if_statement_inserts_zero_check(self):
        """Test that if statement condition gets 0 = check inserted."""
        infix = """
val = 1
a = 0
if (val) {
    a = 1
}
RESULT = a
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "val@ 0 =" in output

    def test_while_statement_inserts_zero_check(self):
        """Test that while statement condition gets 0 = check inserted."""
        infix = """
counter = 5
while (counter) {
    counter = counter - 1
}
RESULT = 0
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "counter@ 0 =" in output

    def test_ternary_inserts_zero_check(self):
        """Test that ternary operator condition gets 0 = not inserted."""
        infix = """
cond = 1
result = cond ? 1 : 0
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "cond@ 0 = not" in output

    def test_logical_and_inserts_zero_checks(self):
        """Test that && inserts 0 = not for both operands."""
        infix = """
a = 1
b = 1
result = a && b
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "a@ 0 = not" in output
        assert "b@ 0 = not" in output
        assert "and" in output

    def test_logical_or_inserts_zero_checks(self):
        """Test that || inserts 0 = not for both operands."""
        infix = """
x = 0
y = 1
result = x || y
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "x@ 0 = not" in output
        assert "y@ 0 = not" in output
        assert "or" in output

    def test_logical_not_becomes_zero_equals(self):
        """Test that ! becomes 0 = (not 'not' token)."""
        infix = """
val = 1
result = !val
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "val@ 0 =" in output

    def test_conditional_goto_inserts_zero_check(self):
        """Test that conditional goto gets 0 = not inserted."""
        infix = """
flag = 1
if (flag) {
    goto skip
}
skip:
RESULT = 0
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        assert "flag@ 0 =" in output

    def test_complex_logical_expression(self):
        """Test complex logical expression with multiple conversions."""
        infix = """
a = 1
b = 1
c = 0
result = (a && b) || c
RESULT = result
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to convert: {output}"
        # Each operand in && and || should get 0 = not
        assert "a@ 0 = not" in output
        assert "b@ 0 = not" in output
        assert "and" in output
        assert "or" in output


class TestMultipleReturns:
    def test_valid_multiple_value_returns(self):
        """Test a function with multiple value returns where all paths return."""
        infix = """
function sign(x) {
    if (x > 0) {
        return 1
    } else if (x < 0) {
        return -1
    } else {
        return 0
    }
}
RESULT = sign($X)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Should be valid: {output}"
        # Check for the generated structure
        assert "__internal_ret_sign" in output
        assert "__internal_ret_label_sign" in output
        assert "1 __internal_ret_sign_0!" in output
        assert "-1 __internal_ret_sign_0!" in output
        assert "0 __internal_ret_sign_0!" in output
        assert "#__internal_ret_label_sign" in output
        assert "__internal_ret_sign_0@" in output

    def test_valid_void_early_exit(self):
        """Test a void function with an early empty return."""
        infix = """
function do_stuff(val) {
    if (val < 0) {
        return;
    }
    a = val * 2
}
do_stuff(10)
RESULT = 0
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Should be valid: {output}"
        assert "__internal_ret_label_do_stuff" in output
        assert "1 __internal_ret_label_do_stuff_0#" in output  # unconditional jump
        assert "a!" in output

    def test_invalid_missing_return_path(self):
        """Test a value-returning function with a missing return path."""
        infix = """
function invalid_sign(x) {
    if (x > 0) {
        return 1
    }
    # Missing return for x <= 0
}
RESULT = invalid_sign($X)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail due to missing return path"
        assert (
            "Not all control paths in function 'invalid_sign' return a value" in output
        )

    def test_invalid_mixed_returns(self):
        """Test a function with mixed value and void returns."""
        infix = """
function mixed(x) {
    if (x > 0) {
        return 1
    } else {
        return
    }
}
RESULT = mixed($X)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail due to mixed returns"
        assert "has inconsistent return statements" in output

    def test_return_in_while_loop_ok_for_void(self):
        """Test a void function with a return inside a while loop."""
        infix = """
function find_first_negative(Array a, Value size) {
    i = 0
    while (i < size) {
        if (a[i] < 0) {
            return; # Early exit
        }
        i = i + 1
    }
}
arr = new(10)
find_first_negative(arr, 10)
RESULT = 0
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Should be valid: {output}"
        assert "__internal_ret_label_find_first_negative" in output

    def test_return_in_while_loop_fail_for_value_return(self):
        """Test a value-returning function with a return only inside a while loop."""
        infix = """
function find_first_negative_val(Array a, Value size) {
    i = 0
    while (i < size) {
        if (a[i] < 0) {
            return a[i] # Not guaranteed to execute
        }
        i = i + 1
    }
    # Missing return if loop finishes
}
arr = new(10)
RESULT = find_first_negative_val(arr, 10)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "Should fail because return is not guaranteed"
        assert (
            "Not all control paths in function 'find_first_negative_val' return a value"
            in output
        )


class TestStdlibMeta:
    """Test the 'meta' standard library."""

    def test_meta_requires(self):
        """Test that the 'meta' library can be required."""
        infix = """
@requires meta
RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"Failed to require meta: {output}"

    def test_meta_error_macro(self):
        """Test the ERROR() macro from the meta library."""
        infix = """
@requires meta
ERROR(This is a test error from meta lib)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "ERROR() macro should have failed compilation"
        assert "This is a test error from meta lib" in output

    def test_meta_assert_const_macro_true(self):
        """Test ASSERT_CONST() macro when condition is true."""
        infix = """
@requires meta
_ = ASSERT_CONST(1, TestContext, _message)
RESULT = 1
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"ASSERT_CONST() should succeed: {output}"
        assert "1 RESULT! RESULT@" in output

    def test_meta_assert_const_macro_false(self):
        """Test ASSERT_CONST() macro when condition is false."""
        infix = """
@requires meta
ASSERT_CONST(0, TestContext, _this_should_fail)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "ASSERT_CONST() should have failed compilation"
        assert "TestContext_this_should_fail" in output

    def test_meta_assert_const_macro_not_const(self):
        """Test ASSERT_CONST() macro when expression is not a compile-time constant."""
        infix = """
@requires meta
ASSERT_CONST($X, TestContext, _message)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert not success, "ASSERT_CONST() should fail on non-constant expression"
        assert "TestContext_must_be_a_constant_expression" in output

    def test_meta_paste_macro(self):
        """Test the PASTE() macro for argument expansion and token pasting."""
        infix = """
@requires meta
@define A x
@define B y
xy = 100
RESULT = PASTE(A, B)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"PASTE() macro failed: {output}"
        assert "100 xy!" in output
        assert "xy@ RESULT!" in output

    def test_meta_join_macro(self):
        """Test the JOIN() macro for generating expression sequences."""
        infix = """
@requires meta
@define GET_SRC(i) PASTE($src, i)
RESULT = JOIN(3, GET_SRC, +)
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"JOIN() macro failed: {output}"
        assert output.strip() == "src0 src1 + src2 + RESULT! RESULT@"

    def test_meta_unroll_macro(self):
        """Test the UNROLL() macro for generating expression sequences."""
        infix = """
@requires meta
lut = new(256);
@define POW(x) x * x
@define FILL_LUT_ENTRY(i) lut[i] = POW(i)
UNROLL(4, FILL_LUT_ENTRY)
RESULT = lut[2]
"""
        success, output = run_infix2postfix(infix, "expr")
        assert success, f"UNROLL() macro failed: {output}"
        assert (
            output.strip()
            == "lut{}^256 0 0 lut{}! 1 1 lut{}! 4 2 lut{}! 9 3 lut{}! 2 lut{}@ RESULT! RESULT@"
        )

    def test_macros_without_requires_fail(self):
        """Test that meta macros are not available without @requires meta."""
        infix = """
@define GET_SRC(i) i
RESULT = JOIN(3, GET_SRC, +)
"""
        success, _ = run_infix2postfix(infix, "expr")
        assert not success, "Should fail without @requires meta"
