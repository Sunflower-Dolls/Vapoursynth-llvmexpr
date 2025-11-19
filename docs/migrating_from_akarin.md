## Migrating from `akarin.Expr` to `llvmexpr`

This document serves as a guide for users familiar with `akarin.Expr` who are transitioning to `llvmexpr.Expr`.

While `llvmexpr` is designed for full syntax compatibility with `akarin`'s RPN, its enhanced feature set introduce new possibilities and some important distinctions.

### Differences

The most significant changes when migrating from `akarin.Expr` to `llvmexpr` stem from fundamental differences in the evaluation engine and math library implementations.

#### 1. Evaluation Mode: Always-Float vs. Type-Aware

The primary difference lies in their evaluation modes. `llvmexpr` operates exclusively in a **32-bit always-float mode**. In contrast, `akarin.Expr` uses a **type-aware, integer-preserving mode** that only promotes values to floating-point when an operation requires it. The `opt` parameter in `akarin` switches how this mode treats pixel data loaded from integer-format clips.

##### Integer Wrap-around Behavior

`akarin`'s engine tracks the type of each value. If an operation involves only integers, it performs the calculation using 32-bit signed two's complement arithmetic, which enables integer wrap-around.

*   **In `akarin`**: Wrap-around occurs whenever an operation's inputs are all integers. This is true for integer constants even with `opt=0`.
    *   `'2147483647 1 +'` → `int` + `int` → wraps to `-2147483648`.

`llvmexpr` does not have this type-aware behavior. It promotes all numeric inputs to 32-bit floats at the earliest opportunity.

*   **In `llvmexpr`**: The expression `'2147483647 1 +'` will always be evaluated using floating-point math, resulting in `2147483648.0`. Wrap-around is impossible.

> [!WARNING]
> **Implication:** Any expression that relies on integer overflow as a mechanism is **not** directly portable and will produce fundamentally different results in `llvmexpr`.

##### Integer Precision & The Critical Role of `akarin`'s `opt` Parameter

A standard 32-bit float can only represent all consecutive integers exactly up to **$2^{24}$**.

The `opt` parameter in `akarin` controls the **initial type** of pixel data loaded from integer-format clips (`x`, `y`, etc.):

*   **`opt=0` (default):** Loads integer pixel data **as floats**. In this mode, if a clip has more than 24 bits of integer precision, that extra precision is **lost immediately upon loading**, before any operations are performed.
*   **`opt=1`:** Loads integer pixel data **as integers**. This mode **preserves the full bit-depth** of the source clip, allowing for precise calculations on values up to 32 bits.

This leads to a stark difference in capabilities:

| Feature                                    | `llvmexpr`                                  | `akarin.Expr (opt=0)`                       | `akarin.Expr (opt=1)`                         |
| :----------------------------------------- | :------------------------------------------ | :------------------------------------------ | :-------------------------------------------- |
| **Model**                                  | Always-Float                                | Type-Aware                                  | Type-Aware, Integer-Preserving                |
| **Loading `x` from a 32-bit integer clip** | Converted to float, **precision lost**      | Converted to float, **precision lost**      | Loaded as 32-bit integer, **full precision**  |
| **Bitwise Ops on >24bit `x`**              | **Incorrect** (operates on imprecise float) | **Incorrect** (operates on imprecise float) | **Correct** (operates on precise integer)     |
| **Arithmetic on >24bit `x`**               | Imprecise float arithmetic                  | Imprecise float arithmetic                  | Precise integer arithmetic (with wrap-around) |

**Conclusion for Migration:**

*   `llvmexpr`: everything is a float, and precision is always limited to 24 bits for integer values.
*   `akarin` with `opt=0` behaves like `llvmexpr` *when dealing with pixel data*, as the initial conversion to float limits precision. However, it still performs integer math on integer constants.
*   `akarin` with `opt=1` unlocks a integer-only, high-precision pipeline that `llvmexpr` cannot replicate.

> [!IMPORTANT]
> **Migration Consideration:** Users migrating from `akarin.Expr` must be mindful of this core difference. Expressions that depend on the precise representation of integers greater than 24 bits (especially for bitwise operations) or on integer wrap-around behavior will not work as expected in `llvmexpr` and may require algorithmic changes.

#### 2. `pow` Function and Approximate Math

Behavior of the `pow` function and approximate math differs significantly:
*   **`akarin.Expr`** uses approximate math for the `pow` function.
*   **`llvmexpr`** does not implement an approximate `pow`, as the precise version always offers better performance. It will always use the precise implementation.

This means that even when forcing approximate math in `llvmexpr` (`approx_math=1`), calculations involving `pow` may produce different results compared to `akarin.Expr`. It is impossible to fully replicate `akarin.Expr`'s `pow` behavior (unless you write the approximate `pow` logic yourself in the expression).

Beyond `pow`, `llvmexpr` provides more explicit control over mathematical precision. With `approx_math=2` (the default auto mode) or `approx_math=0` (forced precise), `llvmexpr` will (or may) use precise mathematical functions. This can lead to different results compared to `akarin.Expr`, which use approximate math only.

#### 3. Decimal Separator

`llvmexpr` always uses a period (`.`) as the decimal separator for floating-point numbers, regardless of system locale. In contrast, `akarin.Expr`'s parser is locale-dependent and may expect a comma (`,`) in certain regions. This can cause expressions to fail when moved between systems with different locale configurations. `llvmexpr` provides consistent parsing by ignoring locale for decimal separators.

#### 4. Incompatible Positional Arguments

> [!CAUTION]
> When migrating from `akarin.Expr`, **do not perform a simple find-and-replace** on the function name, especially if you use positional arguments. The optional parameters for `akarin.Expr` and `llvmexpr.Expr` are in a **different order**, which can lead to silent errors or immediate script failure.

#### 5. Parameter Signature Mismatch

A side-by-side comparison reveals the incompatibility:

| Index | `akarin.Expr`             | `llvmexpr.Expr`           | Compatibility  |
| :---- | :------------------------ | :------------------------ | :------------- |
| 1     | `clips`                   | `clips`                   | ✅ Match        |
| 2     | `expr`                    | `expr`                    | ✅ Match        |
| 3     | `format` (optional)       | `format` (optional)       | ✅ Match        |
| **4** | **`opt`** (optional)      | **`boundary`** (optional) | ❌ **MISMATCH** |
| **5** | **`boundary`** (optional) | **`dump_ir`** (optional)  | ❌ **MISMATCH** |

#### 6. Different Error Messages

When an RPN fails to compile, `llvmexpr` will raise a different error message than `akarin.Expr`.

For example, `akarin.Expr('invalid')` raises `vapoursynth.Error: Expr: failed to convert 'invalid' to float`, while `llvmexpr.Expr('invalid')` raises `vapoursynth.Error: Expr: Invalid token: invalid (idx 0)`. `akarin.Expr('+')` raises `vapoursynth.Error: Expr: insufficient values on stack: +` while `llvmexpr.Expr('+')` raises `vapoursynth.Error: Expr: Stack underflow before executing block 0: depth_in = 0, min_needed = 1. start token '+' (idx 0).`.

If your code relies on the exact error message, you will need to adjust it.

### Enhancements

`llvmexpr` introduces major new features and usability improvements that significantly expand upon the capabilities of `akarin.Expr`.

#### 1. Major New Capabilities

These are fundamental features in `llvmexpr` that enable entirely new types of operations not possible in `akarin.Expr`.

*   **Turing-Complete Control Flow:** `llvmexpr` allows for complex, iterative algorithms using labels and conditional jumps, making the expression engine Turing-complete.
    *   **Labels (`#label_name`):** Defines a destination for a jump.
    *   **Conditional Jumps (`label_name#`):** Pops a value from the stack. If the value is true (> 0), execution jumps to the corresponding label. This enables the creation of loops and complex conditional logic.

*   **Direct Output Control:** Provides operators to write to arbitrary pixel locations and suppress the default output, allowing for advanced spatial operations like rotations or custom warps directly within an expression.
    *   **`@[]`:** Pops a value and `x`, `y` coordinates (`val absX absY @[]`), and writes the value to the output frame at `[absX, absY]`.
    *   **`^exit^`:** A special marker that suppresses the default write to the current pixel `[X, Y]` if it (or a variable it was assigned to) is the only item left on the stack after evaluation.

#### 2. Expanded Syntax and Function Library

`llvmexpr` extends `akarin`'s syntax with a significantly larger set of mathematical functions and more flexible syntax.

*   **Expanded Mathematical Functions:**
    *   **Trigonometry:** `tan`, `asin`, `acos`, `atan`, `atan2`
    *   **Hyperbolic:** `sinh`, `cosh`, `tanh`
    *   **Logarithmic:** `log2`, `log10`
    *   **Exponential:** `exp2`
    *   **Rounding:** `ceil`
    *   **Miscellaneous:** `copysign`, `fma` (fused multiply-add), `neg`, `sgn` (signum).
*   **Enhanced Pixel Access Modifiers:** Extends absolute pixel access with explicit boundary mode control.
    *   `absX absY clip[]:m`: Forces mirrored boundary for absolute access.
    *   `absX absY clip[]:b`: Forces the use of the filter's global `boundary` parameter for absolute access.
    *   `absX absY clip[]:c`: Forces clamped boundary for absolute access (default when not specified, same as akarin.Expr).
*   **Relaxed Offset Range for Static Pixel Access:**: While `akarin.Expr` requires `-width < relX < width && -height < relY < height` in `x[relX,relY]` syntax, `llvmexpr` allows `relX` and `relY` to be any integer value.

#### 3. Infix (C-Style) Syntax Support

`llvmexpr` introduces a major usability enhancement not available in `akarin.Expr`: support for C-style infix notation.

*   **`infix=1` Parameter:** By setting `infix=1`, you can write expressions in a familiar, algebraic style. The plugin automatically converts these expressions to its internal postfix representation at runtime.
*   **Improved Readability:** Infix notation makes complex mathematical and logical expressions significantly easier to write, read, and maintain compared to RPN.
*   **`akarin.Expr` Compatibility:** `akarin.Expr` exclusively uses postfix (RPN) syntax. This feature is unique to `llvmexpr`.

**Example:**

An expression to calculate the Euclidean distance in RPN would be:
```
x X - 2 pow y Y - 2 pow + sqrt
```

With `infix=1`, the same logic can be expressed more intuitively:
```c
RESULT = sqrt(pow($x - $X, 2) + pow($y - $Y, 2))
```

#### 4. Core Engine and Usability Enhancements
*   **Advanced Function Parameters:** Offers more fine-grained control over compilation and execution:
    *   `dump_ir`: Dumps the intermediate LLVM IR for debugging and performance analysis.
    *   `opt_level`: Provides explicit control over the LLVM optimization level.
    *   `approx_math`: An intelligent mode for using fast, approximate math functions, with an automatic fallback to precise versions if the LLVM vectorizer fails.