# Infix Syntax Documentation for llvmexpr

## 1. Introduction

The language provides a familiar C-style infix syntax that supports variables, operators, user-defined functions, and special constructs for video processing. It is designed to be expressive and readable, abstracting away the complexities of writing raw postfix expressions.

The infix transpiler supports two distinct execution models, corresponding to VapourSynth's `Expr` and `SingleExpr` functions.

### 1.1. `Expr` Mode: Per-Pixel Execution

This is the traditional model. The script is executed once for *every single pixel* of the output frame. It is ideal for standard image filtering, such as applying a brightness curve, combining clips, or spatial filtering.

- **Key Characteristics:**
    - Operates on a "current pixel" concept, with `$X` and `$Y` coordinates available.
    - The final value assigned to the special `RESULT` variable is implicitly written to the current pixel's location.

### 1.2. `SingleExpr` Mode: Per-Frame Execution

This is a specialized model where the script is executed only *once for the entire frame*. This model is not suitable for general image processing but excels at tasks that require summarizing or distributing data across a frame, such as calculating average brightness or copying frame properties.

- **Key Characteristics:**
    - No "current pixel" concept; `$X` and `$Y` are unavailable.
    - All output must be performed explicitly by writing to absolute pixel coordinates (`store()`) or by writing to frame properties (`set_prop()`).
    - The special `RESULT` variable has no effect on the output frame.

## 2. Preprocessor

The compiler includes a powerful preprocessor that runs before the source code is parsed. It supports macro definitions, conditional compilation, and compile-time expression evaluation, enabling advanced code organization and configuration.

### 2.1. Overview

- **Activation**: Preprocessor directives are lines that begin with an `@` symbol, optionally preceded by whitespace.
- **Execution**: The preprocessor first evaluates all directives and expands all macros to generate an intermediate source text. This text is then tokenized and parsed by the main compiler. The combination of `@define`, constant expression evaluation, and the conditional ternary operator (`? :`) effectively makes the preprocessor a Turing-complete, pure functional language that executes at compile time.

### 2.2. Macro Definition Directives

Macros allow you to define reusable names for constants and parameterized expressions.

#### 2.2.1. `@define`

The `@define` directive is used to create both simple and function-like macros.

**Object-Like Macros**

These are used to define constants or feature flags.

- **Syntax**: `@define NAME [value]`
- **Behavior**:
  - If a `value` is provided, all subsequent occurrences of `NAME` as a whole word will be replaced by the `value`.
  - If the `value` is a constant expression that can be evaluated at compile time, it will be replaced with the computed result.
  - If no `value` is provided, `NAME` is defined with an empty body, which is useful for `@ifdef` checks.

**Examples:**
```
@define MAX_VALUE 255
@define PI 3.14159
@define ENABLE_FEATURE
@define COMPUTED (2 * 3 + 1)  # Expands to 7
```

**Function-Like Macros**

These provide parameterized text replacement, similar to C preprocessor macros.

- **Syntax**: `@define NAME(param1, param2, ...) body`
- **Important**: There must be **no space** between the macro name and the opening parenthesis `(`. A space will cause it to be parsed as an object-like macro.

- **Invocation**: To invoke a function-like macro, use its name followed by parentheses containing the arguments. If a function-like macro's name appears without `()`, it will not be expanded.

```
@define MAX(a, b) ((a) > (b) ? (a) : (b))
@define SQR(x) ((x) * (x))

result = MAX(10, 20);  # Expands to: ((10) > (20) ? (10) : (20))
value = SQR(5);        # Expands to: ((5) * (5))
addr = MAX;            # NOT expanded, remains 'MAX'
```

#### 2.2.2. `@undef`

You can remove a macro definition using the `@undef` directive.

- **Syntax**: `@undef NAME`

### 2.3. Conditional Compilation Directives

Conditional directives allow you to include or exclude blocks of code based on certain conditions.

#### 2.3.1. `@if`, `@else`, `@endif`

The `@if` directive provides powerful conditional compilation based on the value of a constant expression.

- **Syntax**:
  ```
  @if expression
    // Code to include if expression evaluates to non-zero
  @else
    // Optional: code to include otherwise
  @endif
  ```
- **Behavior**: The directive evaluates the `expression` at compile time. The code block is included if the result is non-zero. For details on what constitutes a valid expression, see [Section 2.6. Constant Expression Evaluation](#26-constant-expression-evaluation).

#### 2.3.2. `@ifdef` and `@ifndef`

These directives check whether a macro is defined, regardless of its value.

- **Syntax**:
  ```
  @ifdef MACRO_NAME
    // Code to include if MACRO_NAME is defined
  @else
    // Optional: code to include if not defined
  @endif
  ```
- `@ifndef` is the reverse: it includes code if a macro is *not* defined.

### 2.4. Other Directives

#### 2.4.1. `@error`

You can use `@error` to force a compilation error with a custom message. This is useful for asserting configurations or flagging deprecated macro usage inside a conditional block.

- **Syntax**: `@error [message]`

**Example:**
```
@if defined(OLD_FEATURE)
  @error OLD_FEATURE is deprecated. Please use NEW_FEATURE instead.
@endif
```

#### 2.4.2. `@requires`

You can include built-in standard libraries using the `@requires` directive. This is the primary way to access a rich set of pre-defined functions and constants.

- **Syntax**: `@requires library_name`
- **Library Name**: Must be a valid identifier (e.g., `algorithms`, `meta`).

**Example:**
```
@requires algorithms
@requires meta
```

### 2.5. The Macro Expansion Process

The preprocessor expands macros according to the following rules:

1.  **Multiple Passes**: The preprocessor makes multiple passes over the source text, expanding macros until no more expansions can be performed. A recursion limit (1000) prevents infinite loops.
2.  **Token-Based Matching**: Macro replacement is performed on a whole-word basis. For example, if `V` is a macro, it will not be expanded inside the identifier `VALUE`.
3.  **Argument Expansion**: For function-like macros, the preprocessor fully expands a macro's arguments *before* substituting them into the macro body, with one key exception (see Token Pasting below). An important consequence of this is that if an expanded argument forms a valid constant expression (e.g., (2+3)), it is immediately evaluated and replaced by its result (e.g., 5) before being passed to the outer macro. This mechanism is crucial for advanced techniques like recursive macros that generate new values at each step. The resulting text is then rescanned for further macro expansions.
4.  **Differences from C/C++**: This preprocessor is similar to C/C++ but lacks a stringification (`#`) operator and variadic macros (`...`, `__VA_ARGS__`). Also, macro definitions must be on a single line.

#### 2.5.1. Token Pasting (`@@`)

The preprocessor supports the `@@` operator for token pasting, which concatenates two tokens into one.

- **Syntax**: `token1 @@ token2`
- **Behavior**: During macro expansion, `@@` is removed, and the adjacent tokens are merged into a single new token. This new token is then available for further macro expansion.

**Example:**
```
@define CONCAT(a, b) a@@b
@define xy 100

# Expands to CONCAT(x, y) -> xy -> 100
RESULT = CONCAT(x, y)
```

**The Argument Expansion Problem**

A common and powerful pattern is to generate identifiers dynamically (e.g., `src0`, `src1`). However, the `@@` operator has a special rule: if a macro parameter is adjacent to `@@` in a macro definition, the corresponding argument is **not** expanded before pasting.

**Problem:**
```
@define PASTE(prefix, num) prefix@@num
@define ONE 1

# This expands to PASTE($src, ONE) -> $src@@ONE -> $srcONE, NOT $src1
RESULT = PASTE($src, ONE) 
```

**Solution: The Two-Step Macro**

To force an argument to be evaluated *before* pasting, you must use a two-level macro definition. This is a standard C preprocessor technique.

1.  **Inner Macro:** Performs the direct pasting.
2.  **Outer Macro:** Calls the inner macro. Its arguments *will* be expanded because they are not next to `@@` in its own definition.

```
# Inner macro for direct pasting
@define PASTE_IMPL(prefix, num) prefix@@num

# Outer macro to force argument evaluation
@define PASTE(prefix, num) PASTE_IMPL(prefix, num)

@define ONE 1

# This now works correctly:
# 1. PASTE($src, ONE) is called.
# 2. Its arguments are expanded: $src remains $src, ONE becomes 1.
# 3. PASTE_IMPL($src, 1) is called.
# 4. It pastes '$src' and '1' into '$src1'.
RESULT = PASTE($src, ONE) # Expands to $src1
```

### 2.6. Constant Expression Evaluation

The preprocessor can evaluate integer and floating-point expressions at compile time. This capability is the engine behind the `@if` directive and allows macros to compute values before the main compilation phase.

An expression is considered a "constant expression" if it consists of literals and other macros that expand to constant values.

The expression evaluator supports:
- **Literals**: Integers (`123`, `0xFF`) and floating-point numbers (`3.14`, `1.0e-5`).
- **Macros**: Previously defined macros are expanded before evaluation.
- **`defined(MACRO)` Operator**: Returns `1` if `MACRO` is defined, `0` otherwise. This is primarily used within `@if` expressions.
- **Operators**: The following operators are supported, with standard C-like precedence:

| Precedence | Operator             | Description              | Arity   | Associativity |
| :--------- | :------------------- | :----------------------- | :------ | :------------ |
| 1          | `? :`                | Ternary Conditional      | Ternary | Right         |
| 2          | `\|\|`               | Logical OR               | Binary  | Left          |
| 3          | `&&`                 | Logical AND              | Binary  | Left          |
| 4          | `\|`                 | Bitwise OR               | Binary  | Left          |
| 5          | `^`                  | Bitwise XOR              | Binary  | Left          |
| 6          | `&`                  | Bitwise AND              | Binary  | Left          |
| 7          | `==`, `!=`           | Equality                 | Binary  | Left          |
| 8          | `<`, `<=`, `>`, `>=` | Relational               | Binary  | Left          |
| 9          | `+`, `-`             | Addition, Subtraction    | Binary  | Left          |
| 10         | `*`, `/`             | Multiplication, Division | Binary  | Left          |
|            | `%`                  | Modulus                  | Binary  | Left          |
| 11         | `**`                 | Exponentiation (Power)   | Binary  | Right         |
| 12         | `-`                  | Negation                 | Unary   | Right         |
|            | `!`                  | Logical NOT              | Unary   | Right         |
|            | `~`                  | Bitwise NOT              | Unary   | Right         |

- **Short-Circuiting**: The logical operators `&&`, `||`, and the ternary operator `? :` use short-circuit evaluation. This means only the necessary operands are evaluated. This behavior is essential for implementing terminating recursive macros.

### 2.7. Compile-Time Intrinsics

The preprocessor provides special intrinsic functions to interact with the constant expression evaluator.

- `is_consteval(expr)`
  - Returns `1` if `expr` can be fully evaluated as a compile-time constant; otherwise returns `0`.
  - This intrinsic never raises an error. An expression that cannot be evaluated (e.g., contains variables) simply yields `0`.

- `consteval(expr)`
  - Forces compile-time evaluation of `expr`. If successful, it is replaced by the computed numeric result.
  - If the expression cannot be evaluated at compile-time, a preprocessing error is raised.

- `static_assert(condition, message)`
  - Evaluates `condition` at compile time. If the expression cannot be evaluated to a constant, or if the result is `0`, it raises a compilation error with the specified `message`.
  - If the `condition` evaluates to a non-zero value, the intrinsic is replaced by the value `1`. This allows `static_assert` to be chained inside other compile-time expressions.

**Example:**
```
@define FAST_PATH(n) ((n) * (n))
@define SLOW_PATH(x) (fma((x), (x), 0))

# Dispatch to a faster implementation if 'x' is a compile-time constant
@define DISPATCH(x) (is_consteval(x) ? FAST_PATH(consteval(x)) : SLOW_PATH(x))

@if is_consteval(LEN)
  # Ensure LEN is a constant before using it to define another macro
  @define BUF_LEN consteval(LEN)
@else
  @define BUF_LEN 256
@endif
```

### 2.8. Advanced Techniques and Examples

By combining macros, conditional logic, and compile-time evaluation, you can achieve powerful metaprogramming effects.

#### 2.8.1. Conditional Expansion with the Ternary Operator

A top-level ternary operator (`? :`) within a macro body acts as a powerful **code generation tool**. If the condition is a constant expression, the preprocessor performs *branch elimination*: the entire construct—including the condition, the `? :` symbols, and the surrounding parentheses—is replaced *only* by the tokens from the chosen branch. The condition itself and the unelected branch are completely discarded from the source text passed to the compiler. This means the unelected branch does not need to be syntactically valid or evaluable.

**Example: Conditional Code Selection**
```
@define CHECK(cond, val) (cond ? val : static_assert(0, Error))

# Because the condition '1' is constant, this expands ONLY to '10'. 
# The static_assert(0, Error) branch is never included in the final source code.
RESULT = CHECK(1, 10) 
```

#### 2.8.2. Recursive Macros

Macros can be defined recursively. When combined with the short-circuiting ternary operator, this enables powerful compile-time computation.

**Example: Compile-Time Factorial**
```
# The recursion terminates because when n == 0, the second branch is not evaluated.
# Note: Parentheses around parameters are essential to avoid operator precedence issues.
@define FACTORIAL(n) ((n) == 0 ? 1 : ((n) * FACTORIAL((n) - 1)))

# The preprocessor evaluates this entire expression to the constant value 120.
RESULT = FACTORIAL(5) 
```

Note that in this example, the expression `(n) - 1` is passed unevaluated at each step, accumulating into a large final expression `((5) * ((4) * ...))`. This is acceptable for simple arithmetic, but will fail if a step inside the recursion requires a single evaluated number (e.g., for token pasting). See [Next Section](#283-forcing-step-by-step-evaluation-in-recursive-macros) for how to force evaluation at each step.

#### 2.8.3. Forcing Step-by-Step Evaluation in Recursive Macros

A critical subtlety of recursive macros arises when an argument needs to be evaluated at *each* step of the recursion, rather than accumulating into a large expression. This is often necessary when the argument is passed to another macro that requires a simple value, such as a token-pasting macro.

**The Problem: Unevaluated Expressions as Arguments**

In the `FACTORIAL` example, the argument `n - 1` is not evaluated to a number at each step. Instead, the preprocessor builds a nested expression like `(5 * (4 * (3 * ...)))`, which is evaluated only at the very end. This works for arithmetic, but fails if an intermediate step requires a single token.

Let's attempt to write a macro that finds the first input clip with a bit depth greater than 16. This requires checking `__INPUT_BITDEPTH_0__`, `__INPUT_BITDEPTH_1__`, etc., by dynamically building the macro name.

**Incorrect Implementation:**
```
@requires meta
@define BITDEPTH_OF(i) PASTE(PASTE(__INPUT_BITDEPTH_, i), __)

# Tries to recurse using `i + 1` directly
@define _FIND_FIRST_GT16(i, n) ((i) >= (n) ? -1 : (BITDEPTH_OF(i) > 16 ? (i) : _FIND_FIRST_GT16((i) + 1, (n))))
```
This produces incorrect results. On the first recursion, the argument `(i) + 1` is passed as a sequence of tokens `(`, `i`, `)`, `+`, `1`, not as a computed numeric value. When `BITDEPTH_OF((i) + 1)` is called, `PASTE` concatenates these tokens literally, generating the invalid macro name `__INPUT_BITDEPTH_(0)+1__` instead of the intended `__INPUT_BITDEPTH_1__`. Since this malformed name is not defined as a macro, it remains unexpanded in the output, causing the expression to fail.

**The Solution: Helper Macros to Force Evaluation**

To force evaluation at each step, the expression must be wrapped in another macro so that arguments are expanded beforehand.

**Correct Implementation:**
```
@requires meta

@define INC(n) ((n) + 1)
@define BITDEPTH_OF(i) PASTE(PASTE(__INPUT_BITDEPTH_, i), __)

@define _FIND_FIRST_GT16(i, n) ((i) >= (n) ? -1 : (BITDEPTH_OF(i) > 16 ? (i) : _FIND_FIRST_GT16(INC(i), (n))))

@define FIND_FIRST_GT16() _FIND_FIRST_GT16(0, __INPUT_NUM__)

# Usage:
# This will expand and evaluate at compile time to the index, or -1.
index = consteval(FIND_FIRST_GT16())
```

**Why It Works:**
The preprocessor's argument expansion rule is the key. Before calling `_FIND_FIRST_GT16`, it first fully expands its arguments.
1.  The argument is the macro call `INC(i)`.
2.  The preprocessor expands `INC(i)`, which results in the token sequence for the expression `((i) + 1)`.
3.  A special rule applies after a macro is fully expanded: if the resulting sequence of tokens forms a valid constant expression, **it is immediately evaluated and replaced by a single numeric token.**
4.  Therefore, if `i` was `0`, `INC(0)` becomes `((0)+1)`, which is evaluated to the single token `1`.
5.  The recursive call is then `_FIND_FIRST_GT16(1, (n))`, with `1` being a single token. `BITDEPTH_OF(1)` now works as expected.

This technique is essential for writing complex recursive macros that perform operations like token pasting or anything else that depends on single-token arguments within the recursion.

### 2.9. Predefined Macros

The preprocessor provides several built-in macros that expose information about the execution context.

#### Mode-Specific Macros
| Macro            | Description                                              |
| :--------------- | :------------------------------------------------------- |
| `__EXPR__`       | Defined when compiling in `Expr` mode (per-pixel).       |
| `__SINGLEEXPR__` | Defined when compiling in `SingleExpr` mode (per-frame). |

#### Context Macros (when `infix=1` is used)
These are defined by the VapourSynth filter when it invokes the transpiler.

| Macro                     | Description                                                                                        |
| :------------------------ | :------------------------------------------------------------------------------------------------- |
| `__WIDTH__`               | Output frame width (integer, sub-sampling not counted).                                            |
| `__HEIGHT__`              | Output frame height (integer, sub-sampling not counted).                                           |
| `__INPUT_NUM__`           | Number of input clips (integer).                                                                   |
| `__OUTPUT_BITDEPTH__`     | Output bit depth.                                                                                  |
| `__OUTPUT_FMT__`          | Output format type (`1` for float, `-1` for integer).                                              |
| `__SUBSAMPLE_W__`         | Horizontal chroma subsampling of the output (`1` for 4:2:x, `0` otherwise).                        |
| `__SUBSAMPLE_H__`         | Vertical chroma subsampling of the output (`1` for 4:2:0, `0` otherwise).                          |
| `__INPUT_BITDEPTH_N__`    | Bit depth of the (N+1)-th input clip (e.g., `__INPUT_BITDEPTH_0__`).                               |
| `__INPUT_FMT_N__`         | Format type of the (N+1)-th input clip (e.g., `__INPUT_FMT_0__`). `1` for float, `-1` for integer. |
| `__INPUT_WIDTH_N__`       | Width of the (N+1)-th input clip's luma plane. (**`SingleExpr` mode only**)                        |
| `__INPUT_HEIGHT_N__`      | Height of the (N+1)-th input clip's luma plane. (**`SingleExpr` mode only**)                       |
| `__INPUT_SUBSAMPLE_W_N__` | Horizontal chroma subsampling of the (N+1)-th input clip. (**`SingleExpr` mode only**)             |
| `__INPUT_SUBSAMPLE_H_N__` | Vertical chroma subsampling of the (N+1)-th input clip. (**`SingleExpr` mode only**)               |
| `__PLANE_NO__`            | Current plane being processed (`0`, `1`, or `2`). (**`Expr` mode only**)                           |

### 2.10. Debugging Macros

The cli tool `infix2postfix` includes a `-E` option that outputs the preprocessed code and a trace of macro expansions.
```
infix2postfix input.expr -m [expr/single] -E
```

This can be very helpful for debugging macro definitions and expansion.

## 3. Lexical Structure

### 3.1. Comments

Comments begin with a `#` character and extend to the end of the line. They are ignored by the parser.

```
# This is a comment.
a = 1 # This is an inline comment.
```

### 3.2. Whitespace

Whitespace characters (spaces, tabs, newlines) are used to separate tokens. Multiple whitespace characters are treated as a single separator. Newlines are significant as they terminate statements.

### 3.3. Identifiers

Identifiers are used for naming variables and functions.

- **Rules:** Must begin with a letter (`a-z`, `A-Z`) or an underscore (`_`) and can be followed by any number of letters, digits, or underscores. The pattern is `[a-zA-Z_]\w*`.
- **Case-Sensitivity:** Identifiers are case-sensitive. `myVar` and `myvar` are different.
- **Reserved Prefix:** Identifiers starting with `__internal_` are reserved for internal use by the transpiler and must not be used in user code.

### 3.4. Literals

#### Numeric Literals

The language supports several formats for numeric constants:

- **Decimal:** Standard integers (`100`, `-42`) and floating-point numbers (`3.14`, `-0.5`, `1.2e-5`). The decimal separator is always a period (`.`), regardless of the system's locale settings.
- **Hexadecimal:** Prefixed with `0x`. Can include a fractional part and a binary exponent (`p-exponent`). Examples: `0xFF`, `0x1.9p-2`.
- **Octal:** Prefixed with a leading `0`. Example: `0755`.

## 4. Program Structure

A program is a script composed of one or more statements.

### 4.1. Statements

- Statements can be terminated by either a newline or a semicolon (`;`).
- Semicolons are optional if a statement is on its own line.
- To place multiple statements on a single line, you must separate them with semicolons.

#### Assignment Statements

An assignment statement computes the value of an expression and stores it in a variable.

- **Syntax:** `variable = expression`
- The stack must be balanced after an assignment (i.e., the expression must result in exactly one value).

#### Expression Statements

A statement can consist of a single expression, such as a function call that does not return a value. The expression is evaluated, and its result is discarded. The net stack effect of such a statement must be zero.

```
# Assignment statement (newline terminated)
my_var = 10 * 2

# Expression statement (semicolon terminated)
my_func(my_var);

# Multiple statements on one line
a = 1; b = 2; c = a + b
```

### 4.2. Final Result (`Expr` mode)

In `Expr` mode, the final output for each pixel is determined by the value assigned to the special `RESULT` variable. This assignment is typically the last statement in the global scope.

```
# ... calculations ...
RESULT = final_value
```

In `SingleExpr` mode, assigning to `RESULT` has no effect. Output in this mode is handled explicitly via the `store()` and `set_prop()` functions.

## 5. Variables and Constants

### 5.1. Variables

- **Declaration:** Variables are declared implicitly upon their first assignment.
- **Usage:** A variable must be guaranteed to be assigned a value before it is used in an expression. The transpiler performs a static analysis to ensure a variable is defined on all possible execution paths before any use. Referencing a variable that is not guaranteed to be initialized will result in a syntax error.

The identifier `_` is treated as a special "write-only" variable for discarding values. You can assign any value to it, but you cannot read from it. This is useful for explicitly ignoring a value you don't need.

- **No Unused Warnings:** Assigning a value to `_` will not trigger an "unused variable" warning.
- **Usage Error:** Attempting to use `_` in an expression (i.e., read its value) will result in a compile-time error.

**Example:**
```
# Correct: assigning a value to _ is allowed
_ = 10 * 2

# Error: attempting to read the value of _
# result = _ + 10 
```

### 5.1.1. Formal Verification of Definite Assignment

The robustness of the static analysis mentioned above is backed by a formal proof using the Coq Proof Assistant. The validity of this proof relies on the consistency between the language's control flow constraints and the proof's axioms.

**Consistency of Hypotheses**

The proof relies on the `step_safety_rule`, which posits that control flow cannot arbitrarily jump into a variable's scope from the outside without passing through its definition. This hypothesis is satisfied by language constraint described in [Section 10.2](#102-goto-and-labels).

**Safety Conclusion**

Given this constraint, the **Definite Assignment Theorem** (`definition_dominates_use`) is proven to hold. Formally, for any valid execution path $\text{trace}$ starting from the beginning of the script ($\text{start}$) to a variable usage point ($\text{target}$):

$$
(\text{start} < \text{def\_pos}) \land (\text{InScope}(\text{target})) \implies \text{def\_pos} \in \text{trace}
$$

This theorem mathematically guarantees that the variable definition *dominates* its use; i.e., it is impossible to reach a usage point without executing the definition. For the rigorous formal proof, refer to [proof.v](./proof.v).


### 5.2. Constants

Constants represent fixed values and are **always** identified by a `$` prefix. They are treated as literal values and do not require prior assignment.

### 5.3. Built-in Constants

The language provides several built-in constants.

**Note on Deprecation:** The `$width` and `$height` constants are deprecated and will be removed in a future version. Please use the corresponding macros provided by the `meta` standard library instead.

| Constant  | Description                                                                                                                                                | Availability |
| :-------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------- | :----------- |
| `$pi`     | The value of π.                                                                                                                                            | Both         |
| `$N`      | The current frame number (0-based).                                                                                                                        | Both         |
| `$X`      | The current column coordinate (chroma-subsampling counted).                                                                                                | `Expr` only  |
| `$Y`      | The current row coordinate (chroma-subsampling counted).                                                                                                   | `Expr` only  |
| `$width`  | (**Deprecated**) The width of the video plane. In `Expr`, this is the width of the current plane. In `SingleExpr`, this is the width of the luma plane.    | Both         |
| `$height` | (**Deprecated**) The height of the video plane. In `Expr`, this is the height of the current plane. In `SingleExpr`, this is the height of the luma plane. | Both         |

### 5.4. Source Clips

Source clips are special constants used to reference input video clips. They must be prefixed with a `$`.

Source clips can be referenced in two equivalent ways:

1.  **Single Letters:** The lowercase letters `xyzabcdefghijklmnopqrstuvw` are aliases for `src0` through `src25` respectively:

- `$x` is equivalent to `$src0`
- `$y` is equivalent to `$src1`
- `$z` is equivalent to `$src2`
- `$a` is equivalent to `$src3`
- And so on through `$w` which is equivalent to `$src25`

2.  **`src` Prefixed:** The identifier `src` followed by one or more digits, prefixed with `$` (e.g., `$src0`, `$src1`).

## 6. Operators

Operators are left-associative, except for the unary and ternary operators. The following table lists operators in order of precedence, from lowest to highest.

| Precedence | Operator             | Description              | Arity   | Associativity |
| :--------- | :------------------- | :----------------------- | :------ | :------------ |
| 1          | `? :`                | Ternary Conditional      | Ternary | Right         |
| 2          | `\|\|`               | Logical OR               | Binary  | Left          |
| 3          | `&&`                 | Logical AND              | Binary  | Left          |
| 4          | `\|`                 | Bitwise OR               | Binary  | Left          |
| 5          | `^`                  | Bitwise XOR              | Binary  | Left          |
| 6          | `&`                  | Bitwise AND              | Binary  | Left          |
| 7          | `==`, `!=`           | Equality                 | Binary  | Left          |
| 8          | `<`, `<=`, `>`, `>=` | Relational               | Binary  | Left          |
| 9          | `+`, `-`             | Addition, Subtraction    | Binary  | Left          |
| 10         | `*`, `/`             | Multiplication, Division | Binary  | Left          |
|            | `%`                  | Modulus                  | Binary  | Left          |
| 11         | `**`                 | Exponentiation (Power)   | Binary  | Right         |
| 12         | `-`                  | Negation                 | Unary   | Right         |
|            | `!`                  | Logical NOT              | Unary   | Right         |
|            | `~`                  | Bitwise NOT              | Unary   | Right         |

Note: Bitwise operators operate on integer values. When operating on floating-point values, operands are first rounded to the nearest integer.

Note: All conditional contexts in the infix syntax use consistent `!= 0` semantics, which is different from that of the underlying RPN language.

## 7. Data Access

Data access methods for pixels and frame properties differ significantly between `Expr` and `SingleExpr` modes.

### 7.1. Frame Property Access

#### Reading (Both Modes)

Read a property from a clip's frame properties using `clip.propname` syntax.

- **Syntax:** `clip.propname`
- `clip` must be a valid source clip identifier (e.g., `$a`, `$src1`). The `$` prefix is required. For example: `$a.propname` or `$src1._Matrix`.

#### Checking Existence (Both Modes)

Check if a frame property exists using the `is_prop_exist` built-in function.

- **Signature:** `is_prop_exist(clip, property_name)`
- **Parameters:**
  - `clip`: A source clip constant (e.g., `$a`, `$src1`).
  - `property_name`: Property name as an identifier.
- **Returns:** `1.0` if the property exists, `0.0` otherwise.

#### Writing (`SingleExpr` only)

Write a frame property using the `set_prop` family of built-in functions. These functions allow you to specify the data type of the property being written.

- **Signatures:**
  - `set_prop(property_name, value)` (alias for `set_propf`)
  - `set_propf(property_name, value)`
  - `set_propi(property_name, value)`
  - `set_propaf(property_name, value)`
  - `set_propai(property_name, value)`

- **Parameters:**
  - `property_name`: Property name as an identifier (not a string). The transpiler treats `property_name` as a literal key for the property map; it is not evaluated as a variable.
  - `value`: The value to write, which can be the result of an expression.

- **Type Suffixes:**
  Each function corresponds to a type suffix in the underlying postfix language, controlling how the property is stored:

| Function     | Postfix Suffix | Resulting Type                                                                                                     |
| :----------- | :------------- | :----------------------------------------------------------------------------------------------------------------- |
| `set_propf`  | `$f`           | Float                                                                                                              |
| `set_propi`  | `$i`           | Integer (value is rounded to the nearest integer)                                                                  |
| `set_propaf` | `$af`          | **Auto Float**: Keeps the type of an existing property on the first source frame, otherwise defaults to float.     |
| `set_propai` | `$ai`          | **Auto Integer**: Keeps the type of an existing property on the first source frame, otherwise defaults to integer. |

**Conditional Writes and Fallback Behavior**

If a `set_prop` call is inside a conditional block (e.g., an `if` statement) and that block is not executed, the property is not written by the expression. In this case, VapourSynth's default behavior takes effect:

- If a property with the same name exists on the first source frame (`$x` or `$src0`), it will be copied to the output frame.
- If the property does not exist on the first source frame, it will not be created on the output frame.

**Type Consistency**

The compiler enforces type consistency at compile time. You cannot write to the same property using functions that imply different types (e.g., mixing `set_propi` and `set_propai` for `MyProp` will result in an error).

**Note on Type Conversion**

The type conversion specified by functions like `set_propi` and `set_propai` applies to how the property is stored on the final output frame's properties. Within the same expression execution, reading a property after it has been written will always yield the original floating-point value that was passed to the `set_prop` function, before any rounding or conversion. The integer conversion happens only when the filter returns the new clip with its frame properties.

> [!NOTE]
> **Read-After-Write Behavior for Properties**
>
> In `SingleExpr` mode, writing a property via `set_prop` **immediately updates** the value returned by subsequent reads of that property from the **first source clip** (`$src0` or `$x`).
>
> *   `set_prop(MyProp, 100); val = $src0.MyProp;` -> `val` will be `100`.
> *   This "shadowing" allows you to use properties as mutable variables during the execution.
> *   This only applies to `$src0`. Properties from other clips (`$src1`, etc.) are read-only and independent.

**Example:**
```
# Write a simple integer value
set_propi(MyInteger, 123);

# Conditionally write a property
if ($N > 10) {
    set_propaf(MyAutoFloat, 99.9);
}
# Note: If N <= 10, MyAutoFloat will be initialized from the property
# on the source frame of clip $x. If it doesn't exist there, the result is undefined.
```

#### Deleting (`SingleExpr` only)

Delete a frame property using the `remove_prop` built-in function.

- **Signature:**
  - `remove_prop(property_name)`

- **Parameters:**
  - `property_name`: The name of the property to delete, as an identifier (not a string).

**Example:**
```
# Remove a property that might exist on the source frame
remove_prop(MyOldProp);
```


### 7.2. Pixel Access (`Expr` mode)

In `Expr` mode, you can access pixels from source clips relative to the current pixel or at absolute coordinates.

#### Static Relative Pixel Access

Access a pixel at a fixed, constant offset from the current coordinate (`$X`, `$Y`).

- **Syntax:** `$clip[offsetX, offsetY]` or `$clip[offsetX, offsetY]:m` or `$clip[offsetX, offsetY]:c`
- `$clip` must be a source clip constant.
- `offsetX` and `offsetY` must be integer literals.
- **Boundary Suffixes:**
  - `:c`: Forces clamped boundary (edge pixels are repeated).
  - `:m`: Forces mirrored boundary.
  - If omitted, the filter's global boundary parameter is used.

#### Dynamic Absolute Pixel Access

Access a pixel at a dynamically calculated coordinate using the 3-argument `dyn()` function. See [section 8.3](#83-mode-specific-functions) for details.

### 7.3. Pixel and Data I/O (`SingleExpr` mode)

In `SingleExpr` mode, all data I/O is explicit and uses absolute coordinates.

#### Plane-Specific Dimensions

**Deprecated:** The `frame.width[N]` and `frame.height[N]` constructs are deprecated and will be removed in a future version. Please use the corresponding macros provided by the `meta` standard library instead.

Access the width and height of specific planes using `frame.width[N]` and `frame.height[N]`.

```
w0 = frame.width[0];   # Width of plane 0 (luma)
h1 = frame.height[1];  # Height of plane 1 (chroma U)
```
**Note:** The plane index `N` must be a literal constant. `frame.width` and `frame.height` are special built-in syntax forms, not member access on an object.

#### Absolute Pixel Reading

Read pixels from specific coordinates and planes using the 4-argument version of `dyn()`. See [section 8.3](#83-mode-specific-functions) for details.

#### Absolute Pixel Writing

Write values to specific output frame locations using the 4-argument version of `store()`. See [section 8.3](#83-mode-specific-functions) for details.

> [!IMPORTANT]
> **Read-After-Write Behavior for Pixels**
>
> Pixel access in `llvmexpr` is **not atomic** in the sense of read-after-write visibility.
> *   **Reading** (e.g., `dyn()`, `$src0[...]`) always reads from the **input frames**.
> *   **Writing** (e.g., `store()`, `RESULT`) always writes to the **output frame**.
>
> Therefore, if you write a value to a pixel and then immediately read from the same coordinate, you will get the **original input value**, not the value you just wrote. To pass data between pixels or steps, use Arrays.

## 8. Functions

### 8.1. Function Calls

Functions are called using standard syntax: `functionName(argument1, argument2, ...)`

### 8.2. Built-in Functions

| Function                          | Arity               | Description                                                                                             |
| :-------------------------------- | :------------------ | :------------------------------------------------------------------------------------------------------ |
| `sin`, `cos`, `tan`               | 1                   | Trigonometric sine, cosine, tangent.                                                                    |
| `asin`, `acos`, `atan`            | 1                   | Inverse trigonometric functions.                                                                        |
| `atan2`                           | 2                   | Two-argument arctangent; `atan2(y, x)`.                                                                 |
| `exp`, `exp2`                     | 1                   | Exponential functions `e^x`, `2^x`.                                                                     |
| `log`, `log2`, `log10`            | 1                   | Natural/base-2/base-10 logarithms.                                                                      |
| `sqrt`                            | 1                   | Square root.                                                                                            |
| `abs`                             | 1                   | Absolute value.                                                                                         |
| `sgn`                             | 1                   | Signum function: -1 if x < 0, 0 if x == 0, 1 if x > 0.                                                  |
| `floor`, `ceil`, `round`, `trunc` | 1                   | Rounding family.                                                                                        |
| `min`, `max`                      | 2                   | Minimum/maximum.                                                                                        |
| `copysign`                        | 2                   | Magnitude of first operand, sign of second.                                                             |
| `clamp`                           | 3                   | `clamp(x, lo, hi)`; clamps to `[lo, hi]`.                                                               |
| `fma`                             | 3                   | Fused multiply-add: `(a * b) + c`.                                                                      |
| `nth_N`                           | `M` (where `M ≥ N`) | Returns the N-th smallest value (1-indexed). E.g., `nth_3(1,2,3,4,5,6)` returns `3`.                    |
| `new`                             | 1                   | Allocates an array. In `Expr` mode, size must be a literal. In `SingleExpr`, size can be an expression. |
| `resize`                          | 1                   | Resizes an array in `SingleExpr` mode only. The array must be previously allocated with `new()`.        |

Notes:

- All built-ins are recognized by name and arity; wrong arity will raise a syntax error.
- `nth_N(...)` supports any `N ≥ 1`. It sorts its `M` arguments internally and returns the `N`-th smallest. This compiles to stack ops using `sortM`/`dropK` under the hood.

### 8.3. Mode-Specific Functions

#### `dyn()` - Dynamic Pixel Access

The `dyn()` function has different signatures for `Expr` and `SingleExpr` modes.

- **`Expr` mode:** `dyn($clip, x_expr, y_expr, [boundary_mode])`
  - Accesses a pixel at a dynamically calculated coordinate.
  - `$clip` must be a source clip constant.
  - `x_expr` and `y_expr` can be any valid expressions. If the coordinates are not integers, they will be rounded half to even.
  - `boundary_mode` (optional): Boundary handling mode.
    - If omitted (3 arguments): Clamped boundary (default, same as `2`).
    - `0`: Use the filter's global boundary parameter (same as the boundary mode in [Section 7.2](#72-pixel-access-expr-mode)).
    - `1`: Mirrored boundary.
    - `2`: Clamped boundary.
  - `dyn($src0, $X + 2, $Y + 3, 0)` is roughly equivalent to `$src0[2, 3]`.

- **`SingleExpr` mode:** `dyn($clip, x, y, plane)`
  - Reads a pixel from an absolute coordinate on a specific plane.
  - **Signature:** `dyn($clip, x, y, plane)`
    - `$clip`: Clip constant (e.g., `$x`, `$y`, `$z`, or `$srcN`).
    - `x`, `y`: Absolute coordinates (can be expressions).
    - `plane`: Plane index (must be a literal constant).
  - **Example:** `val = dyn($x, 100, 200, 0);`

#### `store()` - Pixel Writing

The `store()` function has different signatures for `Expr` and `SingleExpr` modes.

- **`Expr` mode:** `store(x, y, val)`
  - Writes `val` to the output pixel at `[trunc(x), trunc(y)]`.
  - This allows an expression for one pixel to write to another location.

- **`SingleExpr` mode:** `store(x, y, plane, value)`
  - Writes a value to an absolute coordinate on a specific output plane.
  - **Signature:** `store(x, y, plane, value)`
    - `x`, `y`: Absolute coordinates (can be expressions).
    - `plane`: Plane index (must be a literal constant).
    - `value`: Value to write (can be an expression).
  - **Example:** `store(0, 0, 0, 255);`
  - **Warning:** No boundary checking is performed. Writing outside valid frame dimensions causes undefined behavior.

#### `exit()` (`Expr` only)

- **Signature:** `exit()`
- Suppresses the default pixel write to the current coordinate (`$X`, `$Y`). This is useful when an expression only writes to other pixels using `store()`.

### 8.4. User-Defined Functions

- **Definition:**

Functions are defined using the `function` keyword. The function name cannot conflict with any built-in function names.

```
function functionName(Type1 param1, Type2 param2) {
    # Body of the function
    local_var = param1 * param2
    return local_var + 10
}
```

- **Parameter Types:**
  - `Value`: A standard floating-point value, the most general type. This is the default if no type is specified.
  - `Clip`: A source clip constant (e.g., `$a`, `$src1`).
  - `Literal`: A literal constant value (numeric literal).
  - `Array`: A reference to an array created with `new()`.

- **Return Type:** All functions that return a value return type `Value`. There is no syntax for specifying a return type.

- **Return Statement:** The `return` statement exits a function. It can appear multiple times within a function body and can be used with or without a value.

  - `return expression;`: Exits the function and provides a return value.
  - `return;`: Exits a function that does not return a value (a "void" function).

  The language enforces the following rules for `return` statements:

  1.  **Consistency:** Within a single function, all `return` statements must be consistent. You cannot mix `return <value>;` and `return;`.
  2.  **Completeness for Value-Returning Functions:** If a function returns a value (i.e., contains at least one `return <value>;`), the compiler will verify that **all possible control flow paths** end in a `return` statement. If any path can exit without returning a value, a compile-time error is raised.
  3.  **Flexibility for Void Functions:** If a function does not return a value (it only contains empty `return;` statements or no `return` statements at all), it is not required for all paths to have a `return`. The function can simply "fall off" the end when its last statement is executed.

- **Inlining:** Function calls are effectively inlined at compile time. Recursion is not supported.
- **Nesting:** Function definitions cannot be nested.

### 8.5. Function Overloading

The language supports function overloading, allowing multiple functions to share the same name as long as their parameter lists are different in number or type.

When an overloaded function is called, the compiler selects the best-matching overload based on the provided arguments:

1.  **Exact Match:** An overload with parameter types that exactly match the argument types is always chosen.
2.  **Best Fit (Fewest Conversions):** If no exact match is found, the compiler chooses the overload that requires the minimum number of implicit type conversions (e.g., from `Clip` to `Value`).
3.  **Tie-Breaking:** If multiple overloads have the same number of conversions, the one where the first conversion occurs on a later argument (further to the right in the parameter list) is selected.
4.  **Ambiguity:** If a single best overload cannot be determined from these rules, a compile-time error is raised.

**Example: (Expr mode)**
```
# Overloaded function 'process'
function process(Clip c) {
    # processes a clip
    return c * 2 - 1
}

function process(Value v) {
    # processes a numeric value
    return v * 2
}

# Calling the overloads
a = process($x)  # Calls process(Clip c)
b = process(10.0)  # Calls process(Value v)
```

## 9. Scope and Globals

### 9.1. Scopes

- **Global Scope:** Variables defined at the top level of the script.
- **Function Scope:** Each function has its own local scope. This includes its parameters and any variables assigned within its body.
- **Block Scope:** Variables defined inside `if`, `else`, and `while` blocks are local to that specific block. They cannot be accessed outside of the block they are defined in.

**Example of Block Scope:**
```
if ($x > 10) {
    a = 5  # 'a' is defined only inside this block
}

# Attempting to use 'a' here would cause a syntax error
# because 'a' is not defined in the global scope.
# RESULT = a
```
### 9.2. Global Variable Access

By default, functions operate in an isolated scope and cannot access global variables. This behavior can be modified with a global declaration placed immediately before a function definition.

- **`<global.none>`:** (Default) The function cannot access any global variables.
- **`<global.all>`:** The function can access any global variable defined in the script at the time of the function's call.
- **`<global<var1><var2>...>`:** The function can access only the specified global variables.

**Example:**

```
<global<my_global>>
function useGlobal(x) {
    return x + my_global # Accesses global 'my_global'
}

my_global = 100
RESULT = useGlobal(5) # Evaluates to 105
```

- Any global variable a function depends on must be defined before that function is called.

## 10. Control Flow (if/else/while and Labels)

The infix syntax supports structured conditionals, loops, and low-level jumps at both global and function scope. These compile to RPN labels and jumps.

### 10.1. If / Else Blocks

- **Syntax:**

```
if (condition) {
    # statements
} else {
    # statements
}
```

- `else` is optional. Nested blocks are supported.
- The condition is any valid expression; any non-zero value (including negative values) is treated as true, and zero is treated as false.
- Each block is a sequence of normal statements (assignments or expressions).

### 10.2. Goto and Labels

- **Define a label:** `label_name:` (at the start of a line)
- **Unconditional jump:** `goto label_name`

Constraints and rules:

- Jumps may cross `if/else` braces (C-like).
- The `goto` statement cannot jump over a variable's initialization statement and enter that variable's lexical scope.
- The target label must exist somewhere in the script; otherwise a syntax error is raised.
- Goto cannot jump across global scope and functions, or between functions.
- Labels only mark positions; they do not execute anything by themselves.

### 10.3. While Loops

A `while` loop provides a C-style syntax for repeated execution as long as a condition is true.

- **Syntax:**

```
while (condition) {
    # statements
}
```

- The `condition` is any valid expression; any non-zero value (including negative values) is treated as true, and zero is treated as false.
- The loop continues to execute as long as the condition evaluates to true.
- This is syntactic sugar for a structure using labels and conditional `goto`.

**Example:**

A countdown loop using `while`:

```
counter = 4
while (counter > 0) {
    counter = counter - 1
}
RESULT = counter # will be 0
```

## 11. Arrays

Arrays are collections of values that can be created and accessed by an index. They are especially useful in `SingleExpr` mode for tasks like building lookup tables, histograms, or buffering data for complex calculations.

### 11.1. Declaration and Initialization

Arrays are created by calling the built-in `new()` function and assigning the result to a variable.

- **`Expr` Mode (Fixed-Size Only):**
  - The size must be a literal constant.
  - `my_lut = new(256);`

- **`SingleExpr` mode (Fixed or Dynamic Size):**
  - Fixed size: `my_array = new(100);`
  - Dynamic size: The size can be any expression that results in a value.
    ```
    # Create an array to hold a value for each pixel
    frame_size = $width * $height;
    pixel_buffer = new(frame_size);
    ```
  - **Float to Integer Conversion:** If the size expression evaluates to a floating-point value, it will be **truncated toward zero**.
    - Examples: `new(10.9)` allocates size `10`, `new(3.1)` allocates size `3`, `new(-2.9)` allocates size `-2` (which would cause undefined behavior).
    - **Note:** This is truncation, not rounding. `new(10.9)` will allocate 10 elements, not 11.
  - The `resize()` function can be used to change an array's size.
    ```
    pixel_buffer = resize(new_size);
    ```
    Note: An array must be allocated using `new()` before it can be resized. The same float-to-integer truncation applies to `resize()`.

### 11.2. Element Access

C-style square brackets `[]` are used to read from and write to array elements.

- **Writing:** `my_array[index_expression] = value_expression;`
- **Reading:** `value = my_array[index_expression];`

**Float to Integer Conversion:** If the index expression evaluates to a floating-point value, it will be **truncated toward zero**.
- Examples: `my_array[3.7]` accesses index `3`, `my_array[0.1]` accesses index `0`, `my_array[-1.5]` accesses index `-1` (may cause undefined behavior).
- This is truncation, not rounding: `my_array[3.9]` accesses index `3`, not `4`.

**Syntax Disambiguation:** Array access is distinguished from relative pixel access by the number of arguments in the brackets.
- `my_array[i]` (1 argument) is an array access.
- `$x[0, 1]` (2 arguments) is a pixel access.

**Example (`SingleExpr` mode):**
```
# Create and populate a lookup table
lut = new(256);
i = 0;
while (i < 256) {
    lut[i] = i * i; # Store the square of the index
    i = i + 1;
}

# In a later part of the script, use the LUT
some_value = dyn($x, 10, 10, 0);
result_from_lut = lut[some_value]; # Read from the LUT
```

### 11.3. Arrays as Function Parameters

Arrays can be passed to user-defined functions by specifying the `Array` type in the function signature. The array is passed by reference, meaning the function can modify the original array.

**Example:**
```
# Function to fill an array with a value
function fill_array(Array a, Value fill_val, Value len) {
    i = 0;
    while (i < len) {
        a[i] = fill_val;
        i = i + 1;
    }
}

# Usage
my_data = new(10);
fill_array(my_data, 3.14, 10); # my_data is now filled with 3.14
```

### 11.4. Scope and Lifetime

The lifetime of an array depends on the execution mode:

- **`Expr` mode:** Arrays are temporary and exist only for the evaluation of a **single pixel**. They cannot be used to share data between pixels.
- **`SingleExpr` mode:** An array's lifetime is confined to the processing of a **single frame**. Its contents are **not** guaranteed to be preserved between frames due to VapourSynth's parallel processing. You should assume arrays are uninitialized at the start of each frame's evaluation.

### 11.5. Safety

**Warning:** The language does not perform runtime bounds checking on array access. Accessing an index outside the allocated size (e.g., `my_array[-1]` or `my_array[size]`) will result in **undefined behavior**, which may include crashes or memory corruption. It is the script author's responsibility to ensure all access is within bounds.

**Floating-point indices:** While the language accepts floating-point values for array indices and sizes, they are truncated toward zero, not rounded. This may lead to unexpected behavior if you assume rounding. Use explicit rounding functions (`floor()`, `ceil()`, `round()`) if you need specific rounding behavior.
**Negative sizes/indices:** Passing negative values to `new()` or `resize()`, or using negative array indices (which can result from truncating small negative floats like `-0.5` → `0` or larger ones like `-1.5` → `-1`), will cause undefined behavior.

## 12. Standard Library Reference

The `@requires` directive can be used to import built-in standard libraries to extend the language's functionality.

### 12.1. The `meta` Library

The `meta` library provides a collection of powerful macros for compile-time metaprogramming. These macros help you generate repetitive code, perform compile-time assertions, and carry out more advanced preprocessing tasks.

To use this library, add the following to your code:
```
@requires meta
```

**Macro Functions**

-   `ERROR(message)`
    -   **Function:** Generates a compile-time error with the specified `message`, useful in ternary operators.

-   `ASSERT_CONST(expression, context, message)`
    -   **Function:** Asserts that an `expression` must evaluate to true at compile time. If the expression is not a constant or evaluates to false, compilation will fail.
    -   **Parameters:**
        -   `expression`: The compile-time expression to check.
        -   `context`: A token representing the context where the error occurred.
        -   `message`: A token describing the reason for the assertion failure.
    -   **Example:** `ASSERT_CONST(__WIDTH__ > 1024, __WIDTH__, must_be_greater_than_1024)`

-   `PASTE(token1, token2)`
    -   **Function:** Pastes two tokens into one. It uses a two-step macro expansion to ensure that `token1` and `token2` are fully expanded before pasting.
    -   **Example:**
        ```
        @requires meta
        @define VAR_NAME my_var_
        @define INDEX 5
        # Expands to my_var_5
        var = PASTE(VAR_NAME, INDEX)
        ```

-   `JOIN(count, macro, separator)`
    -   **Function:** A recursive macro that generates a sequence of expressions joined by a `separator`. It calls `macro(i)` for each index `i` from 0 to `count - 1`. `count` must be a compile-time constant.
    -   **Parameters:**
        -   `count`: The number of repetitions.
        -   `macro`: A single-argument macro that generates each element in the sequence.
        -   `separator`: The token used to join the elements (e.g., `+`, `,`).
    -   **Example:** Generate `$src0 + $src1 + $src2`
        ```
        @define GET_SRC(i) PASTE($src, i)
        # Expands to (($src0 + $src1) + $src2)
        RESULT = JOIN(3, GET_SRC, +)
        ```

-   `UNROLL(count, macro)`
    -   **Function:** Similar to `JOIN`, but generates a sequence of statements separated by semicolons. It calls `macro(i)` for each index from 0 to `count - 1`. `count` must be a compile-time constant.
    -   **Example:** Generate an unrolled loop
        ```
        @define PROCESS(i) data[i] = data[i] * 2
        # Expands to: data[0] = data[0] * 2; data[1] = data[1] * 2; ...
        UNROLL(5, PROCESS)
        ```

### 12.2. The `algorithms` Library

The `algorithms` library provides a set of common algorithms for operating on arrays.

To use this library, add the following to your code:
```
@requires algorithms
```

**Functions**

-   `swap(Array a, Value i, Value j)`
    -   **Function:** Swaps the two elements at indices `i` and `j` in array `a`.

-   `reverse(Array a, Value begin, Value end)`
    -   **Function:** Reverses the order of elements in array `a` within the range `[begin, end)`.

-   `sort(Array a, Value begin, Value end)`
    -   **Function:** Sorts the elements in array `a` within the range `[begin, end)` in ascending order.
    -   **Implementation:** This function uses Introsort to achieve an average and worst-case time complexity of `O(N log N)`.

-   `find_kth_smallest(Array a, Value begin, Value end, Value k)`
    -   **Function:** Finds the k-th smallest element (0-indexed) in array `a` within the range `[begin, end)`. This operation is more efficient than a full sort, with an average time complexity of `O(N)`.
    -   **Parameters:** `k` is a 0-based index where `0 <= k < (end - begin)`.
    -   **Implementation:** This function uses a quickselect algorithm. In `Expr` mode, if the range size is not a compile-time constant, it falls back to a slower but correct implementation.