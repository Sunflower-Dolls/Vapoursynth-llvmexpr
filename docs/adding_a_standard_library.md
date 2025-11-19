# How to Create a Standard Library for llvmexpr

## 1. Introduction

This document provides a technical guide for developers on how to create and integrate a new standard library into the `infix2postfix` transpiler. The standard library system allows for the creation of reusable functions and constants, which can be imported into user scripts via the `@requires` directive.

## 2. Core Concepts

All standard libraries are defined statically within the transpiler's source code. This approach ensures that no external files are needed at runtime and allows for compile-time verification of the library ecosystem. Libraries are modeled as C++ types and dependencies are resolved at compile time using templates.

### 2.1. File Organization

Standard libraries are organized in the following structure:

- `llvmexpr/frontend/infix2postfix/stdlib/` - Directory containing all standard library definitions. Each library consists of a `.hpp` header and a `.expr` source file.
  - `LibraryBase.hpp` - Base types and concepts for defining libraries
  - `Algorithms.hpp` - Sorting and array manipulation utilities
- `llvmexpr/frontend/infix2postfix/StandardLibrary.hpp` - Main header that includes all libraries

To add a new library, create a new `.hpp` and `.expr` file pair in the `stdlib/` directory.

### 2.2. The Type-based Library Model

Each library is a C++ `struct` type with required static members and a `dependencies` type alias. A `concept` is used to enforce the shape of a valid library.

```cpp
// in llvmexpr/frontend/infix2postfix/stdlib/LibraryBase.hpp
namespace infix2postfix::stdlib {

template<typename T>
concept IsLibrary = requires {
    { T::name } -> std::convertible_to<std::string_view>;
    { T::code } -> std::convertible_to<std::string_view>;
    typename T::dependencies; // std::tuple of dependent libraries
    { T::exports };           // array of ExportedFunction
};

struct ExportedFunction {
    std::string_view name; // User-visible alias
    int param_count;       // 0 for constants, >0 for functions
    ExportMode mode = ExportMode::All; // ExportMode::All, ::Expr, or ::SingleExpr
    std::string_view internal_name_override = ""; // Optional override for internal function name
};

} // namespace infix2postfix::stdlib
```

- **`name`**: Unique library name.
- **`code`**: A `std::string_view` containing the library source code. This is loaded from an external `.expr` file using the `#embed` directive.
- **`dependencies`**: A `std::tuple` of other library types this library depends on.
- **`exports`**: A `static constexpr std::array<ExportedFunction, N>` of public API symbols.

### 2.3. Namespace and Naming Conventions

To prevent name collisions, all symbols defined within a standard library **must** follow a strict internal naming convention:

- **Functions (or function-like macros)**: `___stdlib_<library_name>_<function_name>`
  - The prefix `___stdlib_` and `<library_name>` are **always lowercase**
  - `<function_name>` can be **any case** (e.g., `IF_THEN_ELSE`, `if_then_else`, or `IfThenElse`)
  - Examples: `___stdlib_meta_IF_THEN_ELSE`, `___stdlib_algorithms_sort`
  
- **Constants**: `___STDLIB_<LIBRARY_NAME>_<CONSTANT_NAME>`
  - The prefix `___STDLIB_` and `<LIBRARY_NAME>` are **always uppercase**
  - `<CONSTANT_NAME>` can be **any case**, but uppercase is recommended for constants
  - Examples: `___STDLIB_MATH_PI`, `___STDLIB_META_VERSION`

The `exports` mechanism creates user-friendly aliases for these internal names. For example, `___stdlib_math_extra_clamp_value` can be exported as `clamp_value`.

## 3. Step-by-Step Guide

Follow these steps to add a new library.

### Step 1: Choose a Name

Select a short, descriptive, and unique name for your library (e.g., `my_lib`).

**Naming Requirements:**

Library names must follow identifier rules and can only contain:
- Lowercase letters (`a-z`)
- Digits (`0-9`)
- Underscores (`_`)

**Important restrictions:**
- Names must NOT contain dots (`.`), hyphens (`-`), or other special characters
- Names should be in `snake_case` format
- Names must be unique across all standard libraries

**Examples:**
- ✅ Valid: `algorithms`, `math_extra`, `image_processing`, `util2`
- ❌ Invalid: `math.extra` (contains dot), `image-processing` (contains hyphen), `MathExtra` (uppercase)

### Step 2: Create Library Files

A library consists of two files: a C++ header (`.hpp`) and a source file (`.expr`).

1.  Create the source file `llvmexpr/frontend/infix2postfix/stdlib/my_lib.expr`. The filename should be the library name in snake_case.

    ```
    # My new library
    function ___stdlib_my_lib_do_something(Value a, Value b) {
        return a + b * 2;
    }

    @define ___STDLIB_MY_LIB_MY_CONSTANT 42
    ```

2.  Create the header file `llvmexpr/frontend/infix2postfix/stdlib/MyLib.hpp`. The filename should be the library name in PascalCase.

**File structure template:**

```cpp
#ifndef LLVMEXPR_INFIX2POSTFIX_STDLIB_MYLIB_HPP
#define LLVMEXPR_INFIX2POSTFIX_STDLIB_MYLIB_HPP

#include "LibraryBase.hpp"
// Include any dependencies here

namespace infix2postfix::stdlib {

struct my_lib {
    static constexpr std::string_view name = "my_lib";
    
    //NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    static constexpr char code_data[] = {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#embed "my_lib.expr"
#pragma clang diagnostic pop
        , 0 // null terminator
    };
    static constexpr std::string_view code = std::string_view(
        static_cast<const char*>(code_data), sizeof(code_data) - 1);
    
    using dependencies = std::tuple<>; // fill in Step 3
    
    static constexpr std::array<ExportedFunction, 2> exports = {{
        ExportedFunction{.name = "do_something", .param_count = 2},
        ExportedFunction{.name = "MY_CONSTANT", .param_count = 0},
    }};
};

} // namespace infix2postfix::stdlib

#endif // LLVMEXPR_INFIX2POSTFIX_STDLIB_MYLIB_HPP
```

### Step 3: Define Dependencies

List any other standard libraries that your code depends on by listing their types inside a `std::tuple`.

**Example (no dependencies):**
```cpp
using dependencies = std::tuple<>;
```

**Example (with dependencies):**
```cpp
using dependencies = std::tuple<stdlib::algorithms>;
```

> [!IMPORTANT]
> Dependencies should **only** be declared in the `.hpp` file using the `dependencies` tuple. Do **not** use the `@requires` directive in the `.expr` file. However, it is recommended to document the dependencies in a comment at the top of the `.expr` file for clarity.


### Step 4: Define Exports

Specify which internal symbols should be exposed to the user. This is crucial for creating the public API of your library.

- For each function, provide its public alias and parameter count.
- For each constant, provide its public alias and a parameter count of `0`.

**Example:**
```cpp
static constexpr std::array<ExportedFunction, 2> exports = {{
    ExportedFunction{"do_something", 2},
    ExportedFunction{"MY_CONSTANT", 0},
}};
```

### Mode-Specific Exports

You can restrict a function to a specific compilation mode (`Expr` or `SingleExpr`) using the `mode` field. You can also map the same user-facing name to different internal functions using `internal_name_override`.

**Example:**
```cpp
static constexpr std::array<ExportedFunction, 2> exports = {{
    // Available only in Expr mode, maps to ___stdlib_mylib_get_width_expr
    ExportedFunction{.name = "get_width", .param_count = 1, .mode = ExportMode::Expr, .internal_name_override = "___stdlib_mylib_get_width_expr"},
    
    // Available only in SingleExpr mode, maps to ___stdlib_mylib_get_width_single
    ExportedFunction{.name = "get_width", .param_count = 2, .mode = ExportMode::SingleExpr, .internal_name_override = "___stdlib_mylib_get_width_single"},
}};
```

### Step 5: Register in `StandardLibrary.hpp`

Add your library to the main `StandardLibrary.hpp` file in two places:

1. Include the header file:
```cpp
// Import all standard library definitions
#include "stdlib/LibraryBase.hpp"
#include "stdlib/Algorithms.hpp"
#include "stdlib/MyLib.hpp"  // <- add here
```

2. Add to `AllStandardLibraries` tuple:
```cpp
using AllStandardLibraries = std::tuple<
    stdlib::algorithms,
    stdlib::my_lib  // <- add here
>;
```

## 4. File Structure Summary

After following these steps, your file structure should look like:

```
llvmexpr/frontend/infix2postfix/
├── StandardLibrary.hpp         # Main entry point (includes all libraries)
├── StandardLibrary.cpp         # Runtime manager implementation
└── stdlib/
    ├── LibraryBase.hpp        # Base types and concepts
    ├── Algorithms.hpp         # algorithms library
    ├── algorithms.expr
    └── MyLib.hpp              # Your new library
    └── my_lib.expr
```