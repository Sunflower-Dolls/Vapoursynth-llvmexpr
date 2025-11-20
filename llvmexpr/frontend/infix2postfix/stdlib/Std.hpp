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

    static constexpr ::std::array<ExportedFunction, 15> exports = {{
        ExportedFunction{.name = "get_width",
                         .param_count = 1,
                         .mode = ExportMode::Expr,
                         .internal_name_override =
                             "___stdlib_std_get_width_expr"},
        ExportedFunction{.name = "get_width",
                         .param_count = 2,
                         .mode = ExportMode::SingleExpr,
                         .internal_name_override =
                             "___stdlib_std_get_width_single"},
        ExportedFunction{.name = "get_height",
                         .param_count = 1,
                         .mode = ExportMode::Expr,
                         .internal_name_override =
                             "___stdlib_std_get_height_expr"},
        ExportedFunction{.name = "get_height",
                         .param_count = 2,
                         .mode = ExportMode::SingleExpr,
                         .internal_name_override =
                             "___stdlib_std_get_height_single"},
        ExportedFunction{.name = "get_bitdepth", .param_count = 1},
        ExportedFunction{.name = "get_sampletype", .param_count = 1},
        ExportedFunction{.name = "get_colorfamily", .param_count = 1},
        ExportedFunction{.name = "cfUndefined", .param_count = 0},
        ExportedFunction{.name = "cfGray", .param_count = 0},
        ExportedFunction{.name = "cfRGB", .param_count = 0},
        ExportedFunction{.name = "cfYUV", .param_count = 0},
        ExportedFunction{.name = "stInteger", .param_count = 0},
        ExportedFunction{.name = "stFloat", .param_count = 0},
    }};
};

} // namespace infix2postfix::stdlib

#endif // LLVMEXPR_INFIX2POSTFIX_STDLIB_STD_HPP
