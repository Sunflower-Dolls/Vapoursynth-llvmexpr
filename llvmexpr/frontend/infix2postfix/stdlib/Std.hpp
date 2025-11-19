#ifndef LLVMEXPR_INFIX2POSTFIX_STDLIB_STD_HPP
#define LLVMEXPR_INFIX2POSTFIX_STDLIB_STD_HPP

#include "LibraryBase.hpp"
#include "Meta.hpp"
#include <array>

namespace infix2postfix::stdlib {

struct std {
    static constexpr ::std::string_view name = "std";

    //NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    static constexpr char code_data[] = {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#embed "std.expr"
#pragma clang diagnostic pop
        , 0 // null terminator
    };
    static constexpr ::std::string_view code = ::std::string_view(
        static_cast<const char*>(code_data), sizeof(code_data) - 1);

    using dependencies = ::std::tuple<meta>;

    static constexpr ::std::array<ExportedFunction, 4> exports = {{
        ExportedFunction{.name = "get_width", .param_count = 2},
        ExportedFunction{.name = "get_height", .param_count = 2},
        ExportedFunction{.name = "get_bitdepth", .param_count = 1},
        ExportedFunction{.name = "get_fmt", .param_count = 1},
    }};
};

} // namespace infix2postfix::stdlib

#endif // LLVMEXPR_INFIX2POSTFIX_STDLIB_STD_HPP
