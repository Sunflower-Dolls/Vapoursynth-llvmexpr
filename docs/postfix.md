### VapourSynth Expr Syntax Documentation

`llvmexpr` provides two powerful VapourSynth functions that evaluate mathematical or logical expressions: `Expr` and `SingleExpr`. Their core is the `expr` string, which uses Reverse Polish Notation (RPN). This guide details the syntax of that string, highlighting the differences between the two functions.

---

### **1. Core Concepts & Execution Models**

#### **1.1. `Expr`: Per-Pixel Execution**

`Expr` is the traditional function. It processes a clip pixel-by-pixel. The provided RPN expression is executed once for *every single pixel* of the output frame. This makes it ideal for standard image filtering, such as applying a brightness curve, combining clips, or spatial filtering.

- **Key Characteristics:**
    - Operates on a "current pixel" concept, with `X` and `Y` coordinates available.
    - Can have different expressions for each color plane.
    - The final value on the stack is implicitly written to the current pixel's location.

#### **1.2. `SingleExpr`: Per-Frame Execution**

`SingleExpr` is a specialized variant where the RPN expression is executed only *once for the entire frame*. This model is not suitable for general image processing but excels at tasks that require summarizing or distributing data across a frame.

- **Key Characteristics:**
    - No "current pixel" concept; `X` and `Y` are unavailable.
    - A single expression is used for all planes.
    - All output must be done explicitly by writing to absolute pixel coordinates or by writing to frame properties.
    - The stack must be empty at the end of execution.

> **Note:** Throughout this document, features specific to one function will be explicitly marked. If not marked, the feature is available in both `Expr` and `SingleExpr`.

#### **1.3. Reverse Polish Notation (RPN)**

Instead of the conventional `A + B`, RPN places operators _after_ their operands: `A B +`. The expression is evaluated using a stack. Values are pushed onto the stack, and operators pop values, perform a calculation, and push the result back onto the stack.

- **Example:** To calculate `(5 + 3) * 2`, the RPN expression is `5 3 + 2 *`.
  1.  `5`: Push 5. Stack: `[5]`
  2.  `3`: Push 3. Stack: `[5, 3]`
  3.  `+`: Pop 3 and 5, calculate 8, push 8. Stack: `[8]`
  4.  `2`: Push 2. Stack: `[8, 2]`
  5.  `*`: Pop 2 and 8, calculate 16, push 16. Stack: `[16]`

In `Expr`, the final value on the stack is the result for the pixel. In `SingleExpr`, the stack must be empty after execution.

#### **1.4. Data Ranges**

`Expr` does not normalize input clip values. You must account for the native range of the pixel format.

- **8-bit integer:** 0 to 255
- **10-bit integer:** 0 to 1023
- **16-bit integer:** 0 to 65535
- **32-bit float:** Typically 0.0 to 1.0 for Luma (Y) and Alpha, and -0.5 to 0.5 for Chroma (U/V).

When mixing formats, scale values accordingly. For example, to add an 8-bit value to a 10-bit value, you might use `x y 4 * +` (multiplying the 8-bit value `y` by 4 to scale it to the 10-bit range).

When a non-integer result is writing to an integer pixel, it will be rounded to the nearest even integer.

---

### **2. Operands: Pushing Values onto the Stack**

#### **2.1. Clip Identifiers**

Input clips are the primary source of pixel values. They can be referenced in two ways:

- **By Letter (up to 26 clips):**
  - `x`: The first input clip.
  - `y`: The second input clip.
  - `z`: The third input clip.
  - `a` to `w`: The 4th to 26th clips.

- **By Index (arbitrary number of clips):**
  - `srcN`: Accesses the N-th input clip (0-indexed).
  - `src0` is equivalent to `x`.
  - `src1` is equivalent to `y`.
  - `src26` accesses the 27th clip.

#### **2.2. Numeric Constants**

Literals are pushed directly onto the stack. The decimal separator for floating-point numbers is always a period (`.`), regardless of the system's locale settings.

- **Standard:** `128`, `3.14`, `-0.5`
- **Hexadecimal:** `0x10` (16), `0xFF` (255).
- **Octal:** `010` (8). Note that invalid octal numbers like `09` are parsed as floats (`9.0`).

**Example:** `x 128 -` (In `Expr`, this subtracts 128 from each pixel value of the first clip).

#### **2.3. Special Constants & Coordinates**

These operators push a specific value onto the stack without needing an operand.

- `pi`: The mathematical constant π (approximately 3.14159).
- `N`: The current frame number.
- `width`: The width of the frame (of the current plane in `Expr`, or the container/luma plane in `SingleExpr`).
- `height`: The height of the frame (of the current plane in `Expr`, or the container/luma plane in `SingleExpr`).
- `width^plane_no`: (**SingleExpr only**) The width of the specified plane (`plane_no` is an integer, e.g., `width^0`). This is useful for formats with subsampled chroma.
- `height^plane_no`: (**SingleExpr only**) The height of the specified plane (e.g., `height^1`). This is useful for formats with subsampled chroma.
- `C:width`: (**SingleExpr only**) The width of the luma plane of a specific input clip `C` (e.g., `x:width`, `src1:width`).
- `C:height`: (**SingleExpr only**) The height of the luma plane of a specific input clip `C`.
- `C:width^P`: (**SingleExpr only**) The width of plane `P` of a specific input clip `C` (e.g., `y:width^1`).
- `C:height^P`: (**SingleExpr only**) The height of plane `P` of a specific input clip `C`.
- `X`: (**Expr only**) The current pixel's column coordinate.
- `Y`: (**Expr only**) The current pixel's row coordinate.

---

### **3. Operators: Manipulating the Stack**

#### **3.1. Arithmetic Operators**

| Operator | Operands | Description                                              |
| :------- | :------- | :------------------------------------------------------- |
| `+`      | 2        | Addition                                                 |
| `-`      | 2        | Subtraction                                              |
| `*`      | 2        | Multiplication                                           |
| `/`      | 2        | Division                                                 |
| `%`      | 2        | C's `fmodf`. `x 1.0 %` gives the fractional part of `x`. |

#### **3.2. Comparison & Logical Operators**

These operators treat any value greater than 0 as `true`. They return `1.0` for true and `0.0` for false.

| Operator | Operands | Description              |
| :------- | :------- | :----------------------- |
| `>`      | 2        | Greater than             |
| `<`      | 2        | Less than                |
| `=`      | 2        | Equal to                 |
| `>=`     | 2        | Greater than or equal to |
| `<=`     | 2        | Less than or equal to    |
| `and`    | 2        | Logical AND              |
| `or`     | 2        | Logical OR               |
| `xor`    | 2        | Logical XOR              |
| `not`    | 1        | Logical NOT              |

#### **3.3. Mathematical Functions**

| Function                      | Operands | Description                                                                   |
| :---------------------------- | :------- | :---------------------------------------------------------------------------- |
| **Power & Root**              |          |                                                                               |
| `pow` or `**`                 | 2        | `x y pow` raises `x` to the power of `y`. `**` is an alias.                   |
| `sqrt`                        | 1        | `x sqrt` is the square root of `x`.                                           |
| **Exponential & Logarithmic** |          |                                                                               |
| `exp`                         | 1        | `x exp` is e^x.                                                               |
| `exp2`                        | 1        | `x exp2` computes 2^x.                                                        |
| `log`                         | 1        | `x log` is the natural logarithm of `x`.                                      |
| `log2`                        | 1        | `x log2` computes the base-2 logarithm of `x`.                                |
| `log10`                       | 1        | `x log10` computes the base-10 logarithm of `x`.                              |
| **Trigonometric**             |          |                                                                               |
| `sin`, `cos`, `tan`           | 1        | Sine, Cosine, Tangent.                                                        |
| `asin`, `acos`, `atan`        | 1        | Arc Sine, Arc Cosine, Arc Tangent.                                            |
| `atan2`                       | 2        | `y x atan2` computes `atan(y/x)` using signs to find the correct quadrant.    |
| `sinh`, `cosh`, `tanh`        | 1        | Hyperbolic Sine, Cosine, Tangent.                                             |
| **Rounding**                  |          |                                                                               |
| `floor`                       | 1        | Rounds down to the nearest integer.                                           |
| `ceil`                        | 1        | Rounds up to the nearest integer.                                             |
| `round`                       | 1        | Rounds to the nearest integer.                                                |
| `trunc`                       | 1        | Truncates towards zero.                                                       |
| **Other**                     |          |                                                                               |
| `abs`                         | 1        | `x abs` is the absolute value of `x`.                                         |
| `copysign`                    | 2        | `x y copysign` returns a value with the magnitude of `x` and the sign of `y`. |
| `fma`                         | 3        | `a b c fma` computes `(a * b) + c` as a single fused multiply-add.            |
| `neg`                         | 1        | `x neg` negates `x`.                                                          |
| `sgn`                         | 1        | `x sgn` is the sign of `x`: -1 if x<0; 0 when x==0; 1 if x>0.                 |

#### **3.4. Conditional Operator**

- `?`: A ternary operator. `C A B ?` is equivalent to `C > 0 ? A : B`. If `C` is greater than 0, `A` is evaluated and its result is pushed. Otherwise, `B` is evaluated and its result is pushed.
  - **Example:** `x 128 > x 0 ?` (If the pixel value is greater than 128, keep it, otherwise set it to 0). This is an `Expr` example.

#### **3.5. Min/Max & Clamping**

| Operator          | Operands | Description                                                            |
| :---------------- | :------- | :--------------------------------------------------------------------- |
| `max`             | 2        | Returns the larger of the two values.                                  |
| `min`             | 2        | Returns the smaller of the two values.                                 |
| `clip` or `clamp` | 3        | `x min_val max_val clip` clamps `x` to the range `[min_val, max_val]`. |

**Example:** `x 16 235 clip` clamps the pixel value to the broadcast-safe range [16, 235]. This is an `Expr` example.

#### **3.6. Bitwise Operators**

These operators round floating-point values to nearest integers before the operation.

| Operator | Description |
| :------- | :---------- |
| `bitand` | Bitwise AND |
| `bitor`  | Bitwise OR  |
| `bitxor` | Bitwise XOR |
| `bitnot` | Bitwise NOT |

---

### **4. Advanced Operations**

#### **4.1. Stack Manipulation**

| Operator         | Description                                                           | Example                                          |
| :--------------- | :-------------------------------------------------------------------- | :----------------------------------------------- |
| `dup`            | (1 operand) Duplicates the top item.                                  | `x dup *` is equivalent to `x 2 pow`.            |
| `swap`           | (2 operands) Swaps the top two items.                                 | `x y swap -` is equivalent to `y x -`.           |
| `dupN`           | Duplicates the item N positions from the top. `dup0` is `dup`.        |                                                  |
| `swapN`          | Swaps the top item with the item N positions down. `swap1` is `swap`. |                                                  |
| `drop` / `dropN` | Drops the top N items. `drop` is an alias for `drop1`.                | `1 2 3 drop2` results in a stack of `[1]`.       |
| `sortN`          | Sorts the top N items, with the smallest value ending up on top.      | `3 1 2 sort3` results in a stack of `[3, 2, 1]`. |

#### **4.2. Named Variables**

- `var!`: Pops the top value from the stack and stores it in a variable named `var`.
- `var@`: Pushes the value of the variable `var` onto the stack.

**Variable Initialization:** Variables are allocated when the expression is compiled, but they are **not** automatically initialized to any value. It is your responsibility to store a value into a variable (`!`) before you load from it (`@`).

The compiler performs a rigorous static analysis of the control flow. If it detects any path where a variable could be read before it is guaranteed to have been written to, it will raise an error and refuse to compile the expression. This prevents the use of uninitialized variables.

**Example:** `x 2 / my_var! my_var@ my_var@ *` (In `Expr`, this calculates `(x/2)^2`).

#### **4.3. Arrays**

Arrays provide a way to store and manipulate multiple values in a named collection. They are particularly useful for implementing lookup tables, buffering pixel data, or performing complex iterative computations.

##### **4.3.1. Array Allocation**

Arrays must be allocated before use. The allocation method depends on whether the size is known at compile time and which execution mode you're using.

- **Static Allocation:** `arrayname{}^size`
  - Allocates an array with a fixed size known at compile time.
  - `size` must be a positive integer literal (e.g., `buffer{}^256`).
  - Available in both `Expr` and `SingleExpr`.
  - Statically allocated arrays are allocated on the stack and are friendly to LLVM's auto-vectorizer. However, do note that their size must be carefully chosen to avoid excessive stack usage.
  - Stack effect: 0 (no values consumed or produced).

- **Dynamic Allocation:** `size arrayname{}^` (**SingleExpr only**)
  - Allocates an array with a size determined at runtime.
  - Pops the `size` value from the stack. If the value is not an integer, it will be **truncated toward zero**.
    - Examples: `10.9` → `10`, `3.1` → `3`, `-2.9` → `-2`
  - The array can be heap-allocated and can be resized by allocating again with a different size.
  - **Not available in `Expr` mode** - attempting to use this will result in a compilation error.
  - Stack effect: -1 (consumes the size value).
  - **Example:** `width^0 height^0 * pixelbuffer{}^` allocates an array sized to hold every pixel in plane 0.

##### **4.3.2. Array Access**

- **Reading:** `index arrayname{}@`
  - Reads a value from the array at the specified index.
  - Pops the `index` from the stack. If the index is not an integer, it will be **truncated toward zero**.
    - Examples: `3.7 buffer{}@` accesses index `3`, `0.1 buffer{}@` accesses index `0`, `-1.5 buffer{}@` accesses index `-1` (may cause undefined behavior).
  - Pushes the value stored at that index onto the stack.
  - Stack effect: 0 (consumes index, produces value).
  - **Example:** `5 buffer{}@` reads the value at index 5 from `buffer`.

- **Writing:** `value index arrayname{}!`
  - Writes a value to the array at the specified index.
  - Pops the `index` and then the `value` from the stack. If the index is not an integer, it will be **truncated toward zero**.
    - Examples: `42.0 3.9 buffer{}!` writes to index `3`, not `4` (truncation, not rounding).
  - Stack effect: -2 (consumes both value and index).
  - **Example:** `42.0 10 buffer{}!` writes 42.0 to index 10 of `buffer`.

##### **4.3.3. Array Initialization and Safety**

Like variables, arrays undergo static initialization analysis:
- The compiler verifies that an array is allocated before any read or write operations.
- Attempting to access an uninitialized array will result in a compilation error.
- An array allocated statically (`arrayname{}^size`) cannot be reallocated by any means (either statically or dynamically). The compiler will raise an error if it detects a re-allocation attempt. This applies to both `Expr` and `SingleExpr`.
- Array indices are **not** bounds-checked at runtime. Accessing an out-of-bounds index results in **undefined behavior**.

**Additional Safety Considerations:**
- **Floating-point indices:** While the language accepts floating-point values for array indices and sizes (in dynamic allocation), they are truncated toward zero, not rounded. Use explicit rounding operations (`floor`, `ceil`, `round`) if you need specific rounding behavior before passing values to array operations.
- **Negative sizes/indices:** Passing negative values to dynamic allocation or using negative array indices (which can result from truncating negative floats like `-1.5` → `-1`) will cause undefined behavior.

##### **4.3.4. Array Scope and Persistence**

- **In `Expr`:** Arrays are allocated per-pixel and only exist during the evaluation of that pixel. They cannot share data between pixels.
- **In `SingleExpr`:** Arrays are allocated once per filter instance and persist across frame evaluations.
  - **Important Note:** Due to VapourSynth's parallel frame processing, frames may be processed out of order or simultaneously. Arrays should **not** be used for inter-frame communication or accumulation, as this will produce non-deterministic results.
  - Array values are preserved in intra-frame resizes and reallocations.
  - **Example:**
    Do note that comments are not allowed in the expression, therefore the example is only for demonstration purposes. To actually run this example, for each line everything after `#` should be removed.
    ```
    N 3 + buffer{}^ # Allocate an array "buffer" of size N + 3
    N 1 - N buffer{}! # Write value (N - 1) to index N
    N 6 + buffer{}^ # Reallocate "buffer" to size N + 6
    5 width - N 5 + buffer{}! # Write value (5 - width) to index (N + 5)
    N 5 + buffer{}@ prop1$ # Read the value at index N + 5, should be (5 - width)
    N buffer{}@ prop2$ # Read the value at index N, should be (N - 1)
    ```

#### **4.4. Data Access & Output**

Both `Expr` and `SingleExpr` can read pixel data and frame properties. However, their methods and capabilities differ significantly due to their execution models.

##### **4.4.1. Pixel Access (`Expr` only)**

In `Expr`, you can access pixels from any clip at absolute or relative coordinates.

- **Relative Access:** `clip[relX, relY]:[mode]`
  - Accesses a pixel relative to the current coordinate (`X`, `Y`). `relX` and `relY` must be integer constants.
  - **Example:** `y[-1, 0]` accesses the pixel to the immediate left in the second clip (`y`).
  - **Boundary Suffixes:** If no suffix is provided, the edge behavior is determined by the filter's global `boundary` parameter.
    - `:c`: Forces clamped boundary (edge pixels are repeated).
    - `:m`: Forces mirrored boundary.

- **Absolute Access:** `absX absY clip[]:[mode]`
  - Accesses a pixel at an absolute coordinate. It pops `absY` then `absX` from the stack. These coordinates can be computed by expressions.
  - If the coordinates are not integers, they will be rounded half to even.
  - **Example:** `X 2 / Y x[]` reads the pixel at half the current X coordinate from the first clip, using the default clamp mode.
  - **Boundary Suffixes:**
    - `:c`: Forces clamped boundary. **This is the default if no suffix is specified.**
    - `:m`: Forces mirrored boundary.
    - `:b`: Uses the behavior from the filter's global `boundary` parameter.
  - **Warning:** Absolute access may not be vectorized by the JIT compiler if coordinates are computed at runtime, which can cause severe performance degradation. Use relative access with constant offsets where possible.

##### **4.4.2. Pixel & Data I/O (`SingleExpr` only)**

Since `SingleExpr` has no concept of a "current pixel," all data I/O must be explicit and use absolute coordinates.

- **Absolute Pixel Reading:** `absX absY clip^plane []`
  - A new token `clip^plane` is used to specify both the clip and the plane index (`0` for Y, `1` for U, `2` for V, etc.).
  - The `[]` operator then pops `absY` and `absX` from the stack and pushes the pixel value from the specified location. If the coordinates are floating-point values, they are rounded to the nearest integer (with ties to even).
  - **Boundary Handling:** The boundary behavior is controlled by the filter's global `boundary` parameter, which can be set to clamp (0, default) or mirror (1).
  - **Example:** `100 200 src0^0 []` reads the pixel at coordinate (100, 200) from the first clip's (`src0`) luma plane (`^0`) and pushes it onto the stack.

- **Absolute Pixel Writing:** `value absX absY @[]^plane`
  - This is the primary way to modify the output frame in `SingleExpr`.
  - The operator is suffixed with the target plane index (`^plane`). It pops a `value`, `absX` coordinate, and `absY` coordinate from the stack and writes the value to that location in the output frame's specified plane. If the coordinates are floating-point values, they are rounded to the nearest integer (with ties to even).
  - **Boundary Handling Warning:** Pixel writes perform **no boundary checking**. Writing to coordinates outside the valid frame dimensions (e.g., `[-1, -1]` or `[width, height]`) is a memory error. This leads to **undefined behavior** and can crash the process, corrupt the output frame, or cause other unpredictable issues. It is the expression author's responsibility to ensure all write coordinates are within the valid `[0, width-1]` and `[0, height-1]` range for each plane.
  - **Example:** `255 0 0 @[]^0` writes the value `255` to the top-left pixel (0, 0) of the first plane.
  - **Important:** If a pixel is not explicitly written to, its value is copied from the first input clip (`src0`).

##### **4.4.3. Frame Property Access**

- **Reading (Both `Expr` and `SingleExpr`):** `clip.PropertyName`
  - Loads a scalar numerical frame property. `clip` can be any clip identifier (`x`, `y`, `srcN`, etc.).
  - **Example:** `x.PlaneStatsAverage` pushes the value of the `PlaneStatsAverage` property from the first clip's frame properties.
  - If the property is not a scalar numerical property, its value will be its first byte.
  - If the property does not exist, its value will be `NaN` (Not a Number).

- **Writing (`SingleExpr` only):** `value prop_name$[suffix]`
  - The `$` operator, suffixed with a property name and an optional type specifier, writes a value to a frame property on the output frame.
  - It pops one value from the stack and assigns it to the property `prop_name`.
  - If the property already exists, it is overwritten. This is useful for calculating and passing metadata.
  - **Atomicity:** Property writes are atomic. A subsequent read within the same expression will see the newly written value.
  - **Type Suffixes:** You can control the output property's type using a suffix.

| Suffix      | Type         | Description                                                                                                                                        |
| :---------- | :----------- | :------------------------------------------------------------------------------------------------------------------------------------------------- |
| `$` or `$f` | Float        | Writes the value as a **float**.                                                                                                                   |
| `$i`        | Integer      | Writes the value as an **integer**. The value from the stack is rounded to the nearest integer before being stored.                                |
| `$af`       | Auto Float   | If a property with the same name already exists on the first source frame, its type (int or float) is kept. Otherwise, it defaults to **float**.   |
| `$ai`       | Auto Integer | If a property with the same name already exists on the first source frame, its type (int or float) is kept. Otherwise, it defaults to **integer**. |

  - **Examples:**
    - `x.PlaneStatsMax 2 / MyNewProp$f`: Writes the result as a float.
    - `123.7 MyIntProp$i`: Rounds `123.7` to `124` and writes it as an integer.
    - `N MyFrameNum$ai`: Writes the frame number. If `MyFrameNum` already exists on the source frame as a float, it will be overwritten as a float. If it doesn't exist or has a non-numerical type, it will be created as an integer.

#### **4.5. `Expr`-Specific Output Control**

These operators provide fine-grained control over the default per-pixel output behavior in `Expr`. They are not available in `SingleExpr`.

| Operator | Operands | Description                                                                                                                                                                                                                                                                                                      |
| :------- | :------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `@[]`    | 3        | `val absX absY @[]` pops a value `val` and two coordinates `absX`, `absY`, and writes `val` to the output pixel at `[absX, absY]` *in the current plane*. This allows an expression for one pixel to write to another. If the coordinates are not integers, they will be truncated to integers.                  |
| `^exit^` | 0        | Pushes a special marker value onto the stack. If, after the entire `Expr` expression is evaluated for a pixel, this marker is the *only* item remaining on the stack, the default write to the current pixel `[X, Y]` is suppressed. This is useful in expressions that only use `@[]` to write to other pixels. |

**Stack Requirements at Exit:**
- **`Expr`:** The stack must contain exactly one value, which becomes the output for the current pixel. Alternatively, it can contain only the `^exit^` marker to suppress output.
- **`SingleExpr`:** The stack must be **empty** at the end of execution. Any leftover values will result in an error.

**Undefined Behavior Warning (`Expr`):**

The behavior of memory writes in `Expr` is undefined under the following conditions:
- If a pixel receives more than one write from any source (default write or `@[]`) during the processing of a single frame.
- If a pixel is not written to at all (and `^exit^` was not used).

For example, an expression like `val x y @[] ^exit^` is valid: it writes `val` to `[x, y]` and then suppresses the default write. An expression like `val x y @[]` is **invalid** because it leaves the stack empty; it should be `val x y @[] ^exit^` to be valid if no default write is desired. If a default write is also desired, one could do `val x y @[] some_other_val`.

---

### **5. Control Flow (Turing-Complete Operations)**

The RPN engine is Turing-complete, allowing for arbitrary loops and conditional branching using labels and jumps. This enables complex iterative algorithms directly within an expression.

**Warning:** With great power comes great responsibility. An infinite loop in your expression will cause the filter to hang indefinitely.

#### **5.1. Labels**

-   `#label_name`: Defines a jump destination. The `#` must be the first character of the token. `label_name` can be any string of characters without whitespace. The label definition itself is a no-op during execution; it only marks a position for jumps to target.

#### **5.2. Conditional Jumps**

-   `label_name#`: Performs a conditional jump. The `#` must be the last character of the token.
    -   It pops the top value from the stack.
    -   If the value is greater than `0` (true), execution jumps to the corresponding `#label_name` and continues from the instruction immediately following the label.
    -   If the value is `0` or less (false), execution ignores the jump and continues with the next instruction in the expression.

#### **5.3. Example: Power Calculation via Loop**

The following expression calculates `x` to the power of 4, equivalent to `x 4 pow`, but demonstrates a loop using variables.

**Expression:** `x base! 1 result! 4 counter! #loop result@ base@ * result! counter@ 1 - counter! counter@ loop# result@`

**Execution Trace:**

1.  **Initialization:**
    -   `x base!`: Stores the pixel value of clip `x` into the variable `base`.
    -   `1 result!`: Initializes `result` to 1.
    -   `4 counter!`: Initializes a loop `counter` to 4.
    -   Stack is now empty.

2.  **Loop Start (`#loop`):**
    -   `result@ base@ * result!`: `result` becomes `result * base`.
    -   `counter@ 1 - counter!`: Decrements the `counter`.
    -   `counter@`: Pushes the current value of `counter` onto the stack.
    -   `loop#`: Pops `counter`.
        -   If `counter` was > 0, execution jumps back to `#loop`.
        -   If `counter` was 0, the jump is not taken.

3.  **Termination:**
    -   After the loop finishes (when `counter` reaches 0), the expression continues.
    -   `result@`: The final calculated value is pushed onto the stack, becoming the output for the pixel.