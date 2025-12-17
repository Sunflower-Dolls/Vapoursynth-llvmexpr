# Vapoursynth-llvmexpr

A [VapourSynth](https://www.vapoursynth.com/) filter for evaluating complex mathematical or logical expressions. It utilizes multiple backends to accelerate computations, including an LLVM-based JIT (Just-In-Time) compiler for native CPU code and a Vulkan-based backend for GPU execution.

The plugin provides two main logical functions:
*   `llvmexpr.Expr`: Evaluates an expression for **every pixel** in a frame, ideal for spatial filtering and general image manipulation.
*   `llvmexpr.SingleExpr`: Evaluates an expression only **once per frame**, designed for tasks like calculating frame-wide statistics, reading specific pixels, and writing to frame properties or arbitrary pixel locations.

For the `Expr` mode, two backends are available:
1.  `llvmexpr.Expr`: The standard CPU-based backend.
2.  `llvmexpr.VkExpr`: A Vulkan-based GPU backend.

`llvmexpr.Expr` is designed to be a powerful and feature-rich alternative to `akarin.Expr`. It is (almost) fully compatible with `akarin`'s syntax and extends it with additional features, most notably **Turing-complete control flow**, **array and dynamic memory allocation**, **advanced math functions** and **C-style infix syntax**. See [Migrating From Akarin](docs/migrating_from_akarin.md) for a detailed comparison.

`llvmexpr.VkExpr` is fully compatible with `llvmexpr.Expr`'s syntax (both postfix and infix) and semantics. It allows running the same expressions on the GPU. The only difference is that in infix mode, an additional `__GPU__` macro is defined.

In terms of performance, `llvmexpr` may excel at complex mathematical computations. However, its performance can be limited by memory access patterns. In scenarios involving heavy random memory access or specific spatial operations (see `rotate clip` in benchmarks), `akarin.Expr` may offer better performance.

> [!NOTE]
> While `VkExpr` offers GPU acceleration, it is not automatically faster than the CPU `Expr`. For simple expressions, the driver submission overhead and memory transfer costs may outweight the computational benefits.


## Core Components

The `llvmexpr` plugin is a VapourSynth filter that accepts expression strings. At runtime, it JIT-compiles these expressions into highly efficient machine code.

The plugin supports two syntax modes:
- **Postfix notation** (RPN - Reverse Polish Notation): The default, direct format
- **Infix notation** (C-style): Enabled via the `infix` parameter, expressions are automatically converted to postfix internally

### `llvmexpr.Expr` (Per-Pixel)

This function applies an expression to each pixel of the video frame.

**Function Signature:**
```
llvmexpr.Expr(clip[] clips, string[] expr[, int format, int boundary=0, string dump_ir="", int opt_level=5, int approx_math=2, int infix=0])
```

**Parameters:**
- `clips`: Input video clips
- `expr`: Expression strings (one per plane). Format depends on `infix` parameter
- `format`: Output format (optional). This parameter controls the `sampleType` (integer or float) and `bitsPerSample` (bit depth) of the output clip. The `colorFamily`, `subSamplingW`, `subSamplingH`, `width`, and `height` of the output clip are always inherited from the first input clip and cannot be changed by this parameter.
- `boundary`: Boundary handling mode (0=clamp, 1=mirror)
- `dump_ir`: Path to dump LLVM IR for debugging (optional)
- `opt_level`: Optimization level (> 0, default: 5)
- `approx_math`: Approximate math mode (default: 2)
  - `0`: Disabled – use precise LLVM intrinsics for all math operations
  - `1`: Enabled – use fast approximate implementations for `exp`, `log`, `sin`, `cos`, `tan`, `acos`, `atan`, `asin`, `atan2`.
  - `2`: Auto (recommended) – first tries with approximate math enabled; if LLVM reports that the inner loop cannot be vectorized, the compiler automatically recompiles the same function with approximate math disabled and JITs that precise version instead.
- `infix`: Expression format (default: 0)
  - `0`: Postfix notation (RPN)
  - `1`: Infix notation (C-style) - automatically converted to postfix

### `llvmexpr.VkExpr` (Per-Pixel, GPU Backend)

This function is a Vulkan-based GPU accelerated backend for `Expr`. It accepts the same parameters and syntax as `Expr` (excluding CPU-specific optimization flags like `dump_ir` or `opt_level`).

**Function Signature:**
```
llvmexpr.VkExpr(clip[] clips, string[] expr[, int format, int boundary=0, int num_streams=8, int device_id=-1, string dump_glsl="", int infix=0])
```

**Parameters:**
- `clips`: Input video clips
- `expr`: Expression strings (one per plane). Format depends on `infix` parameter
- `format`: Output format (optional). Same behavior as `Expr`.
- `boundary`: Boundary handling mode (0=clamp, 1=mirror)
- `dump_glsl`: Path to dump GLSL shader for debugging (optional)
- `infix`: Expression format (default: 0)
  - `0`: Postfix notation (RPN)
  - `1`: Infix notation (C-style) - automatically converted to postfix. In this mode, a specialized `__GPU__` macro is defined.
- `num_streams`: Number of concurrent Vulkan streams (default: 8). Increase this for better parallelism if you have a powerful GPU, or decrease it if you run into insufficient vram.
- `device_id`: Selects which Vulkan physical device to run on (default: -1 = auto).

### `llvmexpr.SingleExpr` (Per-Frame)

This function executes an expression only once per frame. It is not suitable for typical image filtering but is powerful for tasks that involve reading from arbitrary coordinates, calculating frame-wide metrics, and writing results to other pixels or to frame properties.

**Function Signature:**
```
llvmexpr.SingleExpr(clip[] clips, string expr[, int format, int boundary=0, string dump_ir="", int opt_level=5, int approx_math=2, int infix=0])
```

**Parameters:**
- `clips`: Input video clips.
- `expr`: A single expression string. Unlike `Expr`, only one string is accepted for all planes. Format depends on `infix` parameter
- `format`: Output format (optional). This parameter controls the `sampleType` (integer or float) and `bitsPerSample` (bit depth) of the output clip. The `colorFamily`, `subSamplingW`, `subSamplingH`, `width`, and `height` of the output clip are always inherited from the first input clip and cannot be changed by this parameter.
- `boundary`: Boundary handling mode for pixel reads (0=clamp, 1=mirror). This does not affect writes.
- `dump_ir`: Path to dump LLVM IR for debugging (optional).
- `opt_level`: Optimization level (> 0, default: 5).
- `approx_math`: Approximate math mode (default: 2). See description under `Expr` for details.
- `infix`: Expression format (default: 0)
  - `0`: Postfix notation (RPN)
  - `1`: Infix notation (C-style) - automatically converted to postfix

### LLVMExpr Infix Syntax Highlighting VSCode Extension

A VSCode extension for syntax highlighting of LLVMExpr infix expressions is available. It is not yet published to the VSCode Marketplace, but can be installed manually by copying the extension files to the `.vscode/extensions` directory.

```bash
cp -r llvmexpr-vsc ~/.vscode/extensions/llvmexpr-vsc
```

### Examples

See [examples](examples) for examples of infix code.

- [8x8 DCT](examples/8x8dct.expr) - 8x8 Discrete Cosine Transform (Expr)
- [8x8 IDCT](examples/8x8idct.expr) - 8x8 Inverse Discrete Cosine Transform (Expr)
- [NL-Means](examples/nl-means.expr) - Non-Local Means Denoising (Expr)
- [Area Filter](examples/area_filter.expr) - Connected Component Area Filtering (SingleExpr)

## Documentation

*   **[Infix Syntax](docs/infix.md)**: Describes the C-style syntax for use with the `infix=1` parameter or the `infix2postfix` CLI tool.
*   **[Postfix Syntax](docs/postfix.md)**: The core RPN syntax and operator reference for the `llvmexpr` plugin.

## Dependencies

*   [Clang](https://clang.llvm.org/) (>= 20.0.0)
*   [VapourSynth](https://www.vapoursynth.com/) SDK (headers)
*   [LLVM](https://llvm.org/) development libraries (>= 20.0.0)
*   [Meson](https://mesonbuild.com/) build system
*   [Shaderc](https://github.com/google/shaderc)

## Building and installing

1.  **Configure the build directory:**
    ```sh
    meson setup builddir
    ```

2.  **Compile and install the plugin:**
    ```sh
    ninja -C builddir install
    ```

This will build and install the VapourSynth plugin. The `infix2postfix` CLI tool will be built in the `builddir` directory but not installed.

## Testing

To run the tests, you need to have VapourSynth installed.

This project uses pytest for testing.

```sh
pytest .
```

### infix2postfix CLI Tool

A command-line tool for converting infix expressions to postfix format is available after building:

```sh
builddir/infix2postfix input.expr -m expr -o output.expr [--dump-ast]
```

**Parameters:**
- First argument: Input file containing infix expression
- `-m MODE`: Mode (`expr` for per-pixel expressions, `single` for per-frame expressions)
- `-o FILE`: Output file for the converted postfix expression
- `-D MACRO[=value]`: Define a preprocessor macro (can be used multiple times)
- `--dump-ast`: (Optional) Dump the AST of the expression to the console
- `-E`: (Optional) Output preprocessed code and print macro expansion trace to the console

**Example:**
```bash
builddir/infix2postfix input.expr -m expr -o output.expr -D VERSION=3 -D DEBUG --dump-ast
```

Alternatively, you can use the `infix=1` parameter directly in the VapourSynth plugin to convert expressions at runtime.

## Benchmarks

```bash
python benchmark/benchmark.py
```

| Test Case                    | llvmexpr    | Vkexpr      | akarin         |
| ---------------------------- | ----------- | ----------- | -------------- |
| simple arithmetic            | 2709.97 FPS | 1688.37 FPS | 3034.23 FPS    |
| logical condition            | 2924.98 FPS | 1746.74 FPS | 2992.52 FPS    |
| data range clamp             | 2810.59 FPS | 1754.51 FPS | 2954.08 FPS    |
| complex math chain           | 1244.32 FPS | 1745.35 FPS | 1187.82 FPS    |
| trigonometry coords          | 1957.59 FPS | 1757.37 FPS | FAILED (Error) |
| power function               | 2943.97 FPS | 1719.75 FPS | 2961.36 FPS    |
| stack dup                    | 2946.42 FPS | 1711.48 FPS | 2976.05 FPS    |
| named variables              | 2906.29 FPS | 1767.21 FPS | 2983.00 FPS    |
| static relative access       | 2650.49 FPS | 1712.48 FPS | 2835.97 FPS    |
| dynamic absolute access      | 2737.71 FPS | 1761.36 FPS | 2760.62 FPS    |
| bitwise and                  | 2982.76 FPS | 1705.09 FPS | 2913.48 FPS    |
| gain                         | 1337.62 FPS | 1713.87 FPS | 1651.66 FPS    |
| power with loop              | 2971.21 FPS | 1747.09 FPS | FAILED (Error) |
| 3D rendering                 | 359.66 FPS  | 1329.74 FPS | 187.07 FPS     |
| 3D rendering 2 (icosahedron) | 526.46 FPS  | 1549.25 FPS | 315.62 FPS     |
| rotate clip                  | 205.34 FPS  | 1543.45 FPS | 334.82 FPS     |
| 8x8 dct                      | 173.91 FPS  | 1313.57 FPS | 177.54 FPS     |
| 8x8 idct                     | 184.99 FPS  | 1333.53 FPS | 159.09 FPS     |

Geometric mean FPS (common successful tests only):
  llvmexpr: 1223.78 FPS
  Vkexpr: 1636.41 FPS
  akarin: 1196.40 FPS
