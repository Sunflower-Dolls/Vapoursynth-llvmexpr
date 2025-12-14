/**
 * Copyright (C) 2025 yuygfgg
 * 
 * This file is part of Vapoursynth-llvmexpr.
 * 
 * Vapoursynth-llvmexpr is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Vapoursynth-llvmexpr is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Vapoursynth-llvmexpr.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "Tokenizer.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <locale>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <ctre.hpp>

namespace {

// TODO: Use std::from_chars when libc++ supports it.
inline double locale_independent_stod(const std::string& s) {
    std::istringstream iss(s);
    iss.imbue(std::locale::classic());
    double val = std::numeric_limits<double>::quiet_NaN();
    if (!(iss >> val) || !iss.eof()) {
        throw std::runtime_error(std::format("Failed to parse number: {}", s));
    }
    return val;
}

inline int svtoi(std::string_view sv) {
    int val = 0;
    const char* start = sv.data();
    const char* end = std::next(start, static_cast<std::ptrdiff_t>(sv.size()));
    // NOLINTNEXTLINE(readability-implicit-bool-conversion)
    auto [ptr, ec] = std::from_chars(start, end, val);
    if (ec == std::errc::invalid_argument) {
        throw std::invalid_argument(
            std::format("Failed to parse integer from '''{}'''", sv));
    }
    if (ec == std::errc::result_out_of_range) {
        throw std::out_of_range(
            std::format("Integer out of range for '''{}'''", sv));
    }
    if (ptr != end) {
        throw std::invalid_argument(
            std::format("Invalid integer format '''{}'''", sv));
    }
    return val;
}

// Compile-time token definitions registry
// Keywords with exact match
inline std::optional<Token> parse_add(std::string_view input) {
    if (input == "+") {
        return Token{.type = TokenType::Add,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sub(std::string_view input) {
    if (input == "-") {
        return Token{.type = TokenType::Sub,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_mul(std::string_view input) {
    if (input == "*") {
        return Token{.type = TokenType::Mul,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_div(std::string_view input) {
    if (input == "/") {
        return Token{.type = TokenType::Div,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_mod(std::string_view input) {
    if (input == "%") {
        return Token{.type = TokenType::Mod,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_gt(std::string_view input) {
    if (input == ">") {
        return Token{.type = TokenType::Gt,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_lt(std::string_view input) {
    if (input == "<") {
        return Token{.type = TokenType::Lt,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_eq(std::string_view input) {
    if (input == "=") {
        return Token{.type = TokenType::Eq,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_ternary(std::string_view input) {
    if (input == "?") {
        return Token{.type = TokenType::Ternary,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_x(std::string_view input) {
    if (input == "X") {
        return Token{.type = TokenType::ConstantX,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_y(std::string_view input) {
    if (input == "Y") {
        return Token{.type = TokenType::ConstantY,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_n(std::string_view input) {
    if (input == "N") {
        return Token{.type = TokenType::ConstantN,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_ge(std::string_view input) {
    if (input == ">=") {
        return Token{.type = TokenType::Ge,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_le(std::string_view input) {
    if (input == "<=") {
        return Token{.type = TokenType::Le,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_pow_op(std::string_view input) {
    if (input == "**") {
        return Token{.type = TokenType::Pow,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_or(std::string_view input) {
    if (input == "or") {
        return Token{.type = TokenType::Or,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_pi(std::string_view input) {
    if (input == "pi") {
        return Token{.type = TokenType::ConstantPi,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_and(std::string_view input) {
    if (input == "and") {
        return Token{.type = TokenType::And,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_xor(std::string_view input) {
    if (input == "xor") {
        return Token{.type = TokenType::Xor,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_not(std::string_view input) {
    if (input == "not") {
        return Token{.type = TokenType::Not,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_pow(std::string_view input) {
    if (input == "pow") {
        return Token{.type = TokenType::Pow,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_min(std::string_view input) {
    if (input == "min") {
        return Token{.type = TokenType::Min,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_max(std::string_view input) {
    if (input == "max") {
        return Token{.type = TokenType::Max,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_fma(std::string_view input) {
    if (input == "fma") {
        return Token{.type = TokenType::Fma,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_exp(std::string_view input) {
    if (input == "exp") {
        return Token{.type = TokenType::Exp,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_log(std::string_view input) {
    if (input == "log") {
        return Token{.type = TokenType::Log,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_abs(std::string_view input) {
    if (input == "abs") {
        return Token{.type = TokenType::Abs,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sin(std::string_view input) {
    if (input == "sin") {
        return Token{.type = TokenType::Sin,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_cos(std::string_view input) {
    if (input == "cos") {
        return Token{.type = TokenType::Cos,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_tan(std::string_view input) {
    if (input == "tan") {
        return Token{.type = TokenType::Tan,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sgn(std::string_view input) {
    if (input == "sgn") {
        return Token{.type = TokenType::Sgn,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_neg(std::string_view input) {
    if (input == "neg") {
        return Token{.type = TokenType::Neg,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_store_abs(std::string_view input) {
    if (input == "@[]") {
        return Token{.type = TokenType::StoreAbs,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_fn(std::string_view input) {
    if (input == "clip") {
        return Token{.type = TokenType::Clip,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sqrt(std::string_view input) {
    if (input == "sqrt") {
        return Token{.type = TokenType::Sqrt,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_ceil(std::string_view input) {
    if (input == "ceil") {
        return Token{.type = TokenType::Ceil,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_asin(std::string_view input) {
    if (input == "asin") {
        return Token{.type = TokenType::Asin,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_acos(std::string_view input) {
    if (input == "acos") {
        return Token{.type = TokenType::Acos,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_atan(std::string_view input) {
    if (input == "atan") {
        return Token{.type = TokenType::Atan,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_exp2(std::string_view input) {
    if (input == "exp2") {
        return Token{.type = TokenType::Exp2,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_log2(std::string_view input) {
    if (input == "log2") {
        return Token{.type = TokenType::Log2,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sinh(std::string_view input) {
    if (input == "sinh") {
        return Token{.type = TokenType::Sinh,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_cosh(std::string_view input) {
    if (input == "cosh") {
        return Token{.type = TokenType::Cosh,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_tanh(std::string_view input) {
    if (input == "tanh") {
        return Token{.type = TokenType::Tanh,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_bitor(std::string_view input) {
    if (input == "bitor") {
        return Token{.type = TokenType::Bitor,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_atan2(std::string_view input) {
    if (input == "atan2") {
        return Token{.type = TokenType::Atan2,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clamp(std::string_view input) {
    if (input == "clamp") {
        return Token{.type = TokenType::Clamp,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_floor(std::string_view input) {
    if (input == "floor") {
        return Token{.type = TokenType::Floor,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_trunc(std::string_view input) {
    if (input == "trunc") {
        return Token{.type = TokenType::Trunc,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_round(std::string_view input) {
    if (input == "round") {
        return Token{.type = TokenType::Round,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_log10(std::string_view input) {
    if (input == "log10") {
        return Token{.type = TokenType::Log10,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_width(std::string_view input) {
    if (input == "width") {
        return Token{.type = TokenType::ConstantWidth,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_bitand(std::string_view input) {
    if (input == "bitand") {
        return Token{.type = TokenType::Bitand,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_bitxor(std::string_view input) {
    if (input == "bitxor") {
        return Token{.type = TokenType::Bitxor,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_bitnot(std::string_view input) {
    if (input == "bitnot") {
        return Token{.type = TokenType::Bitnot,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_height(std::string_view input) {
    if (input == "height") {
        return Token{.type = TokenType::ConstantHeight,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_exit_no_write(std::string_view input) {
    if (input == "^exit^") {
        return Token{.type = TokenType::ExitNoWrite,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_copysign(std::string_view input) {
    if (input == "copysign") {
        return Token{.type = TokenType::Copysign,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

// CTRE-based regex parsers
inline std::optional<Token> parse_plane_width(std::string_view input) {
    if (auto m = ctre::match<R"(^width\^(\d+)$)">(input)) {
        int plane_idx = svtoi(m.template get<1>().to_view());
        return Token{.type = TokenType::ConstantPlaneWidth,
                     .text = std::string(input),
                     .payload = TokenPayloadPlaneDim{.plane_idx = plane_idx}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_plane_height(std::string_view input) {
    if (auto m = ctre::match<R"(^height\^(\d+)$)">(input)) {
        int plane_idx = svtoi(m.template get<1>().to_view());
        return Token{.type = TokenType::ConstantPlaneHeight,
                     .text = std::string(input),
                     .payload = TokenPayloadPlaneDim{.plane_idx = plane_idx}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_width(std::string_view input) {
    if (auto m = ctre::match<R"(^(?:src(\d+)|([x-za-w])):width$)">(input)) {
        TokenPayloadClipDim data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        return Token{.type = TokenType::ConstantClipWidth,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_height(std::string_view input) {
    if (auto m = ctre::match<R"(^(?:src(\d+)|([x-za-w])):height$)">(input)) {
        TokenPayloadClipDim data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        return Token{.type = TokenType::ConstantClipHeight,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_plane_width(std::string_view input) {
    if (auto m =
            ctre::match<R"(^(?:src(\d+)|([x-za-w])):width\^(\d+)$)">(input)) {
        TokenPayloadClipPlaneDim data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.plane_idx = svtoi(m.template get<3>().to_view());
        return Token{.type = TokenType::ConstantClipPlaneWidth,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_plane_height(std::string_view input) {
    if (auto m =
            ctre::match<R"(^(?:src(\d+)|([x-za-w])):height\^(\d+)$)">(input)) {
        TokenPayloadClipPlaneDim data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.plane_idx = svtoi(m.template get<3>().to_view());
        return Token{.type = TokenType::ConstantClipPlaneHeight,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_dup(std::string_view input) {
    if (auto m = ctre::match<R"(^dup(\d*)$)">(input)) {
        int n = 0;
        if (m.template get<1>()) {
            auto digit_sv = m.template get<1>().to_view();
            if (!digit_sv.empty()) {
                n = svtoi(digit_sv);
            }
        }
        if (n < 0) {
            throw std::runtime_error("Invalid dupN value");
        }
        return Token{.type = TokenType::Dup,
                     .text = std::string(input),
                     .payload = TokenPayloadStackOp{n}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_drop(std::string_view input) {
    if (auto m = ctre::match<R"(^drop(\d*)$)">(input)) {
        int n = 1;
        if (m.template get<1>()) {
            auto digit_sv = m.template get<1>().to_view();
            if (!digit_sv.empty()) {
                n = svtoi(digit_sv);
            }
        }
        if (n < 0) {
            throw std::runtime_error("Invalid dropN value");
        }
        return Token{.type = TokenType::Drop,
                     .text = std::string(input),
                     .payload = TokenPayloadStackOp{n}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_swap(std::string_view input) {
    if (auto m = ctre::match<R"(^swap(\d*)$)">(input)) {
        int n = 1;
        if (m.template get<1>()) {
            auto digit_sv = m.template get<1>().to_view();
            if (!digit_sv.empty()) {
                n = svtoi(digit_sv);
            }
        }
        if (n < 0) {
            throw std::runtime_error("Invalid swapN value");
        }
        return Token{.type = TokenType::Swap,
                     .text = std::string(input),
                     .payload = TokenPayloadStackOp{n}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sortn(std::string_view input) {
    if (auto m = ctre::match<R"(^sort(\d+)$)">(input)) {
        int n = svtoi(m.template get<1>().to_view());
        if (n < 0) {
            throw std::runtime_error("Invalid sortN value");
        }
        return Token{.type = TokenType::SortN,
                     .text = std::string(input),
                     .payload = TokenPayloadStackOp{n}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_label_def(std::string_view input) {
    if (auto m = ctre::match<R"(^#(.+)$)">(input)) {
        return Token{.type = TokenType::LabelDef,
                     .text = std::string(input),
                     .payload = TokenPayloadLabel{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_jump(std::string_view input) {
    if (auto m = ctre::match<R"(^(.+)#$)">(input)) {
        return Token{.type = TokenType::Jump,
                     .text = std::string(input),
                     .payload = TokenPayloadLabel{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_var_store(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)!$)">(input)) {
        return Token{.type = TokenType::VarStore,
                     .text = std::string(input),
                     .payload = TokenPayloadVar{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_var_load(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)@$)">(input)) {
        return Token{.type = TokenType::VarLoad,
                     .text = std::string(input),
                     .payload = TokenPayloadVar{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_array_alloc_static(std::string_view input) {
    if (auto m =
            ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)\{\}\^(\d+)$)">(input)) {
        int static_size = svtoi(m.template get<2>().to_view());
        return Token{.type = TokenType::ArrayAllocStatic,
                     .text = std::string(input),
                     .payload = TokenPayloadArrayOp{
                         .name = std::string(m.template get<1>().to_view()),
                         .static_size = static_size}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_array_alloc_dyn(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)\{\}\^$)">(input)) {
        return Token{.type = TokenType::ArrayAllocDyn,
                     .text = std::string(input),
                     .payload = TokenPayloadArrayOp{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_array_store(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)\{\}!$)">(input)) {
        return Token{.type = TokenType::ArrayStore,
                     .text = std::string(input),
                     .payload = TokenPayloadArrayOp{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_array_load(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)\{\}@$)">(input)) {
        return Token{.type = TokenType::ArrayLoad,
                     .text = std::string(input),
                     .payload = TokenPayloadArrayOp{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_rel(std::string_view input) {
    if (auto m = ctre::match<
            R"(^(?:src(\d+)|([x-za-w]))\[\s*(-?\d+)\s*,\s*(-?\d+)\s*\](?::([cm]))?$)">(
            input)) {
        TokenPayloadClipAccess data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.rel_x = svtoi(m.template get<3>().to_view());
        data.rel_y = svtoi(m.template get<4>().to_view());

        // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
        if (m.template get<5>()) {
            data.has_mode = true;
            data.use_mirror = (m.template get<5>().to_view() == "m");
        }
        // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
        return Token{.type = TokenType::ClipRel,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_abs(std::string_view input) {
    if (auto m = ctre::match<R"(^(?:src(\d+)|([x-za-w]))\[\](?::([mcb]))?$)">(
            input)) {
        TokenPayloadClipAccess data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        if (m.template get<3>()) {
            char mode_char = m.template get<3>().to_view()[0];
            if (mode_char == 'm') {
                data.has_mode = true;
                data.use_mirror = true;
            } else if (mode_char == 'c') {
                data.has_mode = true;
                data.use_mirror = false;
            } else if (mode_char == 'b') {
                data.has_mode = false;
            }
        } else {
            data.has_mode = true;
            data.use_mirror = false;
        }
        return Token{.type = TokenType::ClipAbs,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_cur(std::string_view input) {
    if (auto m = ctre::match<R"(^(?:src(\d+)|([x-za-w]))$)">(input)) {
        TokenPayloadClipAccess data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        return Token{.type = TokenType::ClipCur,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_prop_access(std::string_view input) {
    if (auto m = ctre::match<
            R"(^(?:src(\d+)|([x-za-w]))\.([a-zA-Z_][a-zA-Z0-9_]*)$)">(input)) {
        TokenPayloadPropAccess data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.prop_name = std::string(m.template get<3>().to_view());
        return Token{.type = TokenType::PropAccess,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_prop_exists(std::string_view input) {
    if (auto m = ctre::match<
            R"(^(?:src(\d+)|([x-za-w]))\.([a-zA-Z_][a-zA-Z0-9_]*)\?$)">(
            input)) {
        TokenPayloadPropAccess data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.prop_name = std::string(m.template get<3>().to_view());
        return Token{.type = TokenType::PropExists,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_abs_plane(std::string_view input) {
    if (auto m =
            ctre::match<R"(^(?:src(\d+)|([x-za-w]))\^(\d+)\[\]$)">(input)) {
        TokenPayloadClipAccessPlane data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.plane_idx = svtoi(m.template get<3>().to_view());
        return Token{.type = TokenType::ClipAbsPlane,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_store_abs_plane(std::string_view input) {
    if (auto m = ctre::match<R"(^@\[\]\^(\d+)$)">(input)) {
        int plane_idx = svtoi(m.template get<1>().to_view());
        return Token{.type = TokenType::StoreAbsPlane,
                     .text = std::string(input),
                     .payload =
                         TokenPayloadStoreAbsPlane{.plane_idx = plane_idx}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_prop_store(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)\$(af|ai|f|i|d)?$)">(
            input)) {
        PropWriteType type = PropWriteType::Float; // Default for bare '$'
        if (m.template get<2>()) {
            auto suffix = m.template get<2>().to_view();
            if (suffix == "i") {
                type = PropWriteType::Int;
            } else if (suffix == "f") {
                type = PropWriteType::Float;
            } else if (suffix == "ai") {
                type = PropWriteType::AutoInt;
            } else if (suffix == "af") {
                type = PropWriteType::AutoFloat;
            } else if (suffix == "d") {
                type = PropWriteType::Delete;
            }
        }

        return Token{
            .type = TokenType::PropStore,
            .text = std::string(input),
            .payload = TokenPayloadPropStore{
                .prop_name = std::string(m.template get<1>().to_view()),
                .type = type}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_number(std::string_view input) {
    if (auto m = ctre::match<
            R"(^(?:(0x[0-9a-fA-F]+(?:\.[0-9a-fA-F]+(?:p[+\-]?\d+)?)?)|(0[0-7]+)|([+\-]?\d+(?:\.\d+)?(?:[eE][+\-]?\d+)?))$)">(
            input)) {
        double val = NAN;
        if (m.template get<2>()) { // Octal integer
            long long llval = 0;
            auto sv = m.template get<2>().to_view();
            auto octal_sv = sv.substr(1); // Skip the leading '0'
            const char* octal_begin = octal_sv.data();
            const char* octal_end = std::next(
                octal_begin, static_cast<std::ptrdiff_t>(octal_sv.size()));
            auto res = std::from_chars(
                octal_begin, octal_end,
                llval, // NOLINT(readability-implicit-bool-conversion)
                8);    // NOLINT(cppcoreguidelines-avoid-magic-numbers)
            if (res.ec != std::errc{} || res.ptr != octal_end) {
                throw std::runtime_error(
                    std::format("Failed to parse octal number: {}", sv));
            }
            val = static_cast<double>(llval);
        } else { // Hex or decimal float/integer
            val = locale_independent_stod(std::string(input));
        }
        return Token{.type = TokenType::Number,
                     .text = std::string(input),
                     .payload = TokenPayloadNumber{val}};
    }
    return std::nullopt;
}

// Dynamic behavior resolvers for stack operations
inline TokenBehavior dup_behavior(const Token& t) {
    const auto& payload = std::get<TokenPayloadStackOp>(t.payload);
    return {.arity = payload.n + 1, .stack_effect = 1};
}

inline TokenBehavior drop_behavior(const Token& t) {
    const auto& payload = std::get<TokenPayloadStackOp>(t.payload);
    return {.arity = payload.n, .stack_effect = -payload.n};
}

inline TokenBehavior swap_behavior(const Token& t) {
    const auto& payload = std::get<TokenPayloadStackOp>(t.payload);
    return {.arity = payload.n + 1, .stack_effect = 0};
}

inline TokenBehavior sortn_behavior(const Token& t) {
    const auto& payload = std::get<TokenPayloadStackOp>(t.payload);
    return {.arity = payload.n, .stack_effect = 0};
}

inline TokenBehavior prop_store_behavior(const Token& t) {
    const auto& payload = std::get<TokenPayloadPropStore>(t.payload);
    if (payload.type == PropWriteType::Delete) {
        return {.arity = 0, .stack_effect = 0};
    }
    return {.arity = 1, .stack_effect = -1};
}

// Compile-time token definitions table
constexpr auto get_token_definitions() {
    return std::array{
        TokenDefinition{.type = TokenType::Add,
                        .name = "+",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_add,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Sub,
                        .name = "-",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_sub,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Mul,
                        .name = "*",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_mul,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Div,
                        .name = "/",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_div,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Mod,
                        .name = "%",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_mod,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Gt,
                        .name = ">",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_gt,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Lt,
                        .name = "<",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_lt,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Eq,
                        .name = "=",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_eq,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Ternary,
                        .name = "?",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -2},
                        .parser = parse_ternary,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ConstantX,
                        .name = "X",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_x,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::ConstantY,
                        .name = "Y",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_y,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::ConstantN,
                        .name = "N",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_n,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Ge,
                        .name = ">=",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_ge,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Le,
                        .name = "<=",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_le,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Pow,
                        .name = "**",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_pow_op,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Or,
                        .name = "or",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_or,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ConstantPi,
                        .name = "pi",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_pi,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::And,
                        .name = "and",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_and,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Xor,
                        .name = "xor",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_xor,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Not,
                        .name = "not",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_not,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Pow,
                        .name = "pow",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_pow,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Min,
                        .name = "min",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_min,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Max,
                        .name = "max",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_max,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Fma,
                        .name = "fma",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -2},
                        .parser = parse_fma,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Exp,
                        .name = "exp",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_exp,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Log,
                        .name = "log",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_log,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Abs,
                        .name = "abs",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_abs,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Sin,
                        .name = "sin",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_sin,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Cos,
                        .name = "cos",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_cos,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Tan,
                        .name = "tan",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_tan,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Sgn,
                        .name = "sgn",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_sgn,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Neg,
                        .name = "neg",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_neg,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::StoreAbs,
                        .name = "@[]",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -3},
                        .parser = parse_store_abs,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::Clip,
                        .name = "clip",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -2},
                        .parser = parse_clip_fn,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Sqrt,
                        .name = "sqrt",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_sqrt,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Ceil,
                        .name = "ceil",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_ceil,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Asin,
                        .name = "asin",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_asin,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Acos,
                        .name = "acos",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_acos,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Atan,
                        .name = "atan",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_atan,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Exp2,
                        .name = "exp2",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_exp2,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Log2,
                        .name = "log2",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_log2,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Sinh,
                        .name = "sinh",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_sinh,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Cosh,
                        .name = "cosh",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_cosh,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Tanh,
                        .name = "tanh",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_tanh,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Bitor,
                        .name = "bitor",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_bitor,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Atan2,
                        .name = "atan2",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_atan2,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Clamp,
                        .name = "clamp",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -2},
                        .parser = parse_clamp,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Floor,
                        .name = "floor",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_floor,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Trunc,
                        .name = "trunc",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_trunc,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Round,
                        .name = "round",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_round,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Log10,
                        .name = "log10",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_log10,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ConstantWidth,
                        .name = "width",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_width,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Bitand,
                        .name = "bitand",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_bitand,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Bitxor,
                        .name = "bitxor",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_bitxor,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Bitnot,
                        .name = "bitnot",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_bitnot,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ConstantHeight,
                        .name = "height",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_height,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ConstantPlaneWidth,
                        .name = "plane_width",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_plane_width,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ConstantPlaneHeight,
                        .name = "plane_height",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_plane_height,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ConstantClipWidth,
                        .name = "clip_width",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_width,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ConstantClipHeight,
                        .name = "clip_height",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_height,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ConstantClipPlaneWidth,
                        .name = "clip_plane_width",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_plane_width,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ConstantClipPlaneHeight,
                        .name = "clip_plane_height",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_plane_height,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ExitNoWrite,
                        .name = "^exit^",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_exit_no_write,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::Copysign,
                        .name = "copysign",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_copysign,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Dup,
                        .name = "dupN",
                        .behavior = DynamicBehaviorFn(dup_behavior),
                        .parser = parse_dup,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Drop,
                        .name = "dropN",
                        .behavior = DynamicBehaviorFn(drop_behavior),
                        .parser = parse_drop,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Swap,
                        .name = "swapN",
                        .behavior = DynamicBehaviorFn(swap_behavior),
                        .parser = parse_swap,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::SortN,
                        .name = "sortN",
                        .behavior = DynamicBehaviorFn(sortn_behavior),
                        .parser = parse_sortn,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::LabelDef,
                        .name = "label_def",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 0},
                        .parser = parse_label_def,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Jump,
                        .name = "jump",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = -1},
                        .parser = parse_jump,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::VarStore,
                        .name = "VarStore",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = -1},
                        .parser = parse_var_store,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::VarLoad,
                        .name = "var_load",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_var_load,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ArrayAllocStatic,
                        .name = "array_alloc_static",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 0},
                        .parser = parse_array_alloc_static,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ArrayAllocDyn,
                        .name = "array_alloc_dyn",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = -1},
                        .parser = parse_array_alloc_dyn,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ArrayStore,
                        .name = "array_store",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -2},
                        .parser = parse_array_store,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ArrayLoad,
                        .name = "array_load",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_array_load,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ClipRel,
                        .name = "clip_rel",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_rel,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::ClipAbs,
                        .name = "clip_abs",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_clip_abs,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ClipCur,
                        .name = "clip_cur",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_cur,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::PropAccess,
                        .name = "prop_access",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_prop_access,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::PropExists,
                        .name = "prop_exists",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_prop_exists,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ClipAbsPlane,
                        .name = "clip_abs_plane",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_clip_abs_plane,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::StoreAbsPlane,
                        .name = "store_abs_plane",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -3},
                        .parser = parse_store_abs_plane,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::PropStore,
                        .name = "prop_store",
                        .behavior = DynamicBehaviorFn(prop_store_behavior),
                        .parser = parse_prop_store,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::Number,
                        .name = "number",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_number,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
    };
}

} // anonymous namespace

std::vector<Token> tokenize(const std::string& expr, int num_inputs,
                            ExprMode mode) {
    std::vector<Token> tokens;
    int idx = 0;

    auto is_space = [](char c) { return std::isspace(c); };
    auto to_string_view = [](auto r) {
        return std::string_view(r.begin(), r.end());
    };

    constexpr auto TOKEN_DEFS = get_token_definitions();

    for (const auto str_token_view :
         expr | std::views::chunk_by([=](char a, char b) {
             return is_space(a) == is_space(b);
         }) | std::views::filter([=](auto r) { return !is_space(r.front()); }) |
             std::views::transform(to_string_view)) {
        std::optional<Token> parsed_token;

        for (const auto& definition : TOKEN_DEFS) {
            // Check mode restrictions
            bool skip = false;
            if (mode == ExprMode::Expr && !definition.available_in_expr) {
                skip = true;
            }
            if (mode == ExprMode::SingleExpr &&
                !definition.available_in_single_expr) {
                skip = true;
            }
            if (skip) {
                continue;
            }

            if ((parsed_token = definition.parser(str_token_view))) {
                break;
            }
        }

        if (!parsed_token) {
            throw std::runtime_error(std::format("Invalid token: {} (idx {})",
                                                 std::string(str_token_view),
                                                 idx));
        }

        // Post-parse validation for clip indices
        if (parsed_token->type == TokenType::ClipRel ||
            parsed_token->type == TokenType::ClipAbs ||
            parsed_token->type == TokenType::ClipCur) {
            if (std::get<TokenPayloadClipAccess>(parsed_token->payload)
                        .clip_idx < 0 ||
                std::get<TokenPayloadClipAccess>(parsed_token->payload)
                        .clip_idx >= num_inputs) {
                throw std::runtime_error(
                    std::format("Invalid clip index in token: {} (idx {})",
                                std::string(str_token_view), idx));
            }
        } else if (parsed_token->type == TokenType::PropAccess) {
            if (std::get<TokenPayloadPropAccess>(parsed_token->payload)
                        .clip_idx < 0 ||
                std::get<TokenPayloadPropAccess>(parsed_token->payload)
                        .clip_idx >= num_inputs) {
                throw std::runtime_error(
                    std::format("Invalid clip index in token: {} (idx {})",
                                std::string(str_token_view), idx));
            }
        } else if (parsed_token->type == TokenType::ClipAbsPlane) {
            if (std::get<TokenPayloadClipAccessPlane>(parsed_token->payload)
                        .clip_idx < 0 ||
                std::get<TokenPayloadClipAccessPlane>(parsed_token->payload)
                        .clip_idx >= num_inputs) {
                throw std::runtime_error(
                    std::format("Invalid clip index in token: {} (idx {})",
                                std::string(str_token_view), idx));
            }
        }

        tokens.push_back(*parsed_token);
        idx++;
    }
    return tokens;
}

TokenBehavior get_token_behavior(const Token& token) {
    constexpr auto TOKEN_DEFS = get_token_definitions();

    const auto* it = std::ranges::find_if(
        TOKEN_DEFS, [&](const auto& def) { return def.type == token.type; });

    return std::visit(
        [&token](auto&& arg) -> TokenBehavior {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, TokenBehavior>) {
                return arg;
            } else if constexpr (std::is_same_v<T, DynamicBehaviorFn>) {
                return arg(token);
            }
        },
        it->behavior);
}
