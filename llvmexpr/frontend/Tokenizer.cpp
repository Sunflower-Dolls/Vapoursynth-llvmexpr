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
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <ctre.hpp>
#include <format>
#include <locale>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
        return Token{.type = TokenType::ADD,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sub(std::string_view input) {
    if (input == "-") {
        return Token{.type = TokenType::SUB,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_mul(std::string_view input) {
    if (input == "*") {
        return Token{.type = TokenType::MUL,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_div(std::string_view input) {
    if (input == "/") {
        return Token{.type = TokenType::DIV,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_mod(std::string_view input) {
    if (input == "%") {
        return Token{.type = TokenType::MOD,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_gt(std::string_view input) {
    if (input == ">") {
        return Token{.type = TokenType::GT,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_lt(std::string_view input) {
    if (input == "<") {
        return Token{.type = TokenType::LT,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_eq(std::string_view input) {
    if (input == "=") {
        return Token{.type = TokenType::EQ,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_ternary(std::string_view input) {
    if (input == "?") {
        return Token{.type = TokenType::TERNARY,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_x(std::string_view input) {
    if (input == "X") {
        return Token{.type = TokenType::CONSTANT_X,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_y(std::string_view input) {
    if (input == "Y") {
        return Token{.type = TokenType::CONSTANT_Y,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_n(std::string_view input) {
    if (input == "N") {
        return Token{.type = TokenType::CONSTANT_N,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_ge(std::string_view input) {
    if (input == ">=") {
        return Token{.type = TokenType::GE,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_le(std::string_view input) {
    if (input == "<=") {
        return Token{.type = TokenType::LE,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_pow_op(std::string_view input) {
    if (input == "**") {
        return Token{.type = TokenType::POW,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_or(std::string_view input) {
    if (input == "or") {
        return Token{.type = TokenType::OR,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_pi(std::string_view input) {
    if (input == "pi") {
        return Token{.type = TokenType::CONSTANT_PI,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_and(std::string_view input) {
    if (input == "and") {
        return Token{.type = TokenType::AND,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_xor(std::string_view input) {
    if (input == "xor") {
        return Token{.type = TokenType::XOR,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_not(std::string_view input) {
    if (input == "not") {
        return Token{.type = TokenType::NOT,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_pow(std::string_view input) {
    if (input == "pow") {
        return Token{.type = TokenType::POW,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_min(std::string_view input) {
    if (input == "min") {
        return Token{.type = TokenType::MIN,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_max(std::string_view input) {
    if (input == "max") {
        return Token{.type = TokenType::MAX,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_fma(std::string_view input) {
    if (input == "fma") {
        return Token{.type = TokenType::FMA,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_exp(std::string_view input) {
    if (input == "exp") {
        return Token{.type = TokenType::EXP,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_log(std::string_view input) {
    if (input == "log") {
        return Token{.type = TokenType::LOG,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_abs(std::string_view input) {
    if (input == "abs") {
        return Token{.type = TokenType::ABS,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sin(std::string_view input) {
    if (input == "sin") {
        return Token{.type = TokenType::SIN,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_cos(std::string_view input) {
    if (input == "cos") {
        return Token{.type = TokenType::COS,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_tan(std::string_view input) {
    if (input == "tan") {
        return Token{.type = TokenType::TAN,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sgn(std::string_view input) {
    if (input == "sgn") {
        return Token{.type = TokenType::SGN,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_neg(std::string_view input) {
    if (input == "neg") {
        return Token{.type = TokenType::NEG,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_store_abs(std::string_view input) {
    if (input == "@[]") {
        return Token{.type = TokenType::STORE_ABS,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_fn(std::string_view input) {
    if (input == "clip") {
        return Token{.type = TokenType::CLIP,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sqrt(std::string_view input) {
    if (input == "sqrt") {
        return Token{.type = TokenType::SQRT,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_ceil(std::string_view input) {
    if (input == "ceil") {
        return Token{.type = TokenType::CEIL,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_asin(std::string_view input) {
    if (input == "asin") {
        return Token{.type = TokenType::ASIN,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_acos(std::string_view input) {
    if (input == "acos") {
        return Token{.type = TokenType::ACOS,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_atan(std::string_view input) {
    if (input == "atan") {
        return Token{.type = TokenType::ATAN,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_exp2(std::string_view input) {
    if (input == "exp2") {
        return Token{.type = TokenType::EXP2,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_log2(std::string_view input) {
    if (input == "log2") {
        return Token{.type = TokenType::LOG2,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sinh(std::string_view input) {
    if (input == "sinh") {
        return Token{.type = TokenType::SINH,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_cosh(std::string_view input) {
    if (input == "cosh") {
        return Token{.type = TokenType::COSH,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_tanh(std::string_view input) {
    if (input == "tanh") {
        return Token{.type = TokenType::TANH,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_bitor(std::string_view input) {
    if (input == "bitor") {
        return Token{.type = TokenType::BITOR,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_atan2(std::string_view input) {
    if (input == "atan2") {
        return Token{.type = TokenType::ATAN2,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clamp(std::string_view input) {
    if (input == "clamp") {
        return Token{.type = TokenType::CLAMP,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_floor(std::string_view input) {
    if (input == "floor") {
        return Token{.type = TokenType::FLOOR,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_trunc(std::string_view input) {
    if (input == "trunc") {
        return Token{.type = TokenType::TRUNC,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_round(std::string_view input) {
    if (input == "round") {
        return Token{.type = TokenType::ROUND,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_log10(std::string_view input) {
    if (input == "log10") {
        return Token{.type = TokenType::LOG10,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_width(std::string_view input) {
    if (input == "width") {
        return Token{.type = TokenType::CONSTANT_WIDTH,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_bitand(std::string_view input) {
    if (input == "bitand") {
        return Token{.type = TokenType::BITAND,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_bitxor(std::string_view input) {
    if (input == "bitxor") {
        return Token{.type = TokenType::BITXOR,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_bitnot(std::string_view input) {
    if (input == "bitnot") {
        return Token{.type = TokenType::BITNOT,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_constant_height(std::string_view input) {
    if (input == "height") {
        return Token{.type = TokenType::CONSTANT_HEIGHT,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_exit_no_write(std::string_view input) {
    if (input == "^exit^") {
        return Token{.type = TokenType::EXIT_NO_WRITE,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_copysign(std::string_view input) {
    if (input == "copysign") {
        return Token{.type = TokenType::COPYSIGN,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

// CTRE-based regex parsers
inline std::optional<Token> parse_plane_width(std::string_view input) {
    if (auto m = ctre::match<R"(^width\^(\d+)$)">(input)) {
        int plane_idx = svtoi(m.template get<1>().to_view());
        return Token{.type = TokenType::CONSTANT_PLANE_WIDTH,
                     .text = std::string(input),
                     .payload = TokenPayload_PlaneDim{.plane_idx = plane_idx}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_plane_height(std::string_view input) {
    if (auto m = ctre::match<R"(^height\^(\d+)$)">(input)) {
        int plane_idx = svtoi(m.template get<1>().to_view());
        return Token{.type = TokenType::CONSTANT_PLANE_HEIGHT,
                     .text = std::string(input),
                     .payload = TokenPayload_PlaneDim{.plane_idx = plane_idx}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_width(std::string_view input) {
    if (auto m = ctre::match<R"(^(?:src(\d+)|([x-za-w])):width$)">(input)) {
        TokenPayload_ClipDim data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        return Token{.type = TokenType::CONSTANT_CLIP_WIDTH,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_height(std::string_view input) {
    if (auto m = ctre::match<R"(^(?:src(\d+)|([x-za-w])):height$)">(input)) {
        TokenPayload_ClipDim data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        return Token{.type = TokenType::CONSTANT_CLIP_HEIGHT,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_plane_width(std::string_view input) {
    if (auto m =
            ctre::match<R"(^(?:src(\d+)|([x-za-w])):width\^(\d+)$)">(input)) {
        TokenPayload_ClipPlaneDim data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.plane_idx = svtoi(m.template get<3>().to_view());
        return Token{.type = TokenType::CONSTANT_CLIP_PLANE_WIDTH,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_plane_height(std::string_view input) {
    if (auto m =
            ctre::match<R"(^(?:src(\d+)|([x-za-w])):height\^(\d+)$)">(input)) {
        TokenPayload_ClipPlaneDim data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.plane_idx = svtoi(m.template get<3>().to_view());
        return Token{.type = TokenType::CONSTANT_CLIP_PLANE_HEIGHT,
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
        return Token{.type = TokenType::DUP,
                     .text = std::string(input),
                     .payload = TokenPayload_StackOp{n}};
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
        return Token{.type = TokenType::DROP,
                     .text = std::string(input),
                     .payload = TokenPayload_StackOp{n}};
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
        return Token{.type = TokenType::SWAP,
                     .text = std::string(input),
                     .payload = TokenPayload_StackOp{n}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_sortn(std::string_view input) {
    if (auto m = ctre::match<R"(^sort(\d+)$)">(input)) {
        int n = svtoi(m.template get<1>().to_view());
        if (n < 0) {
            throw std::runtime_error("Invalid sortN value");
        }
        return Token{.type = TokenType::SORTN,
                     .text = std::string(input),
                     .payload = TokenPayload_StackOp{n}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_label_def(std::string_view input) {
    if (auto m = ctre::match<R"(^#(.+)$)">(input)) {
        return Token{.type = TokenType::LABEL_DEF,
                     .text = std::string(input),
                     .payload = TokenPayload_Label{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_jump(std::string_view input) {
    if (auto m = ctre::match<R"(^(.+)#$)">(input)) {
        return Token{.type = TokenType::JUMP,
                     .text = std::string(input),
                     .payload = TokenPayload_Label{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_var_store(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)!$)">(input)) {
        return Token{.type = TokenType::VAR_STORE,
                     .text = std::string(input),
                     .payload = TokenPayload_Var{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_var_load(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)@$)">(input)) {
        return Token{.type = TokenType::VAR_LOAD,
                     .text = std::string(input),
                     .payload = TokenPayload_Var{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_array_alloc_static(std::string_view input) {
    if (auto m =
            ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)\{\}\^(\d+)$)">(input)) {
        int static_size = svtoi(m.template get<2>().to_view());
        return Token{.type = TokenType::ARRAY_ALLOC_STATIC,
                     .text = std::string(input),
                     .payload = TokenPayload_ArrayOp{
                         .name = std::string(m.template get<1>().to_view()),
                         .static_size = static_size}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_array_alloc_dyn(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)\{\}\^$)">(input)) {
        return Token{.type = TokenType::ARRAY_ALLOC_DYN,
                     .text = std::string(input),
                     .payload = TokenPayload_ArrayOp{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_array_store(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)\{\}!$)">(input)) {
        return Token{.type = TokenType::ARRAY_STORE,
                     .text = std::string(input),
                     .payload = TokenPayload_ArrayOp{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_array_load(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)\{\}@$)">(input)) {
        return Token{.type = TokenType::ARRAY_LOAD,
                     .text = std::string(input),
                     .payload = TokenPayload_ArrayOp{
                         .name = std::string(m.template get<1>().to_view())}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_rel(std::string_view input) {
    if (auto m = ctre::match<
            R"(^(?:src(\d+)|([x-za-w]))\[\s*(-?\d+)\s*,\s*(-?\d+)\s*\](?::([cm]))?$)">(
            input)) {
        TokenPayload_ClipAccess data;
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
        return Token{.type = TokenType::CLIP_REL,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_abs(std::string_view input) {
    if (auto m = ctre::match<R"(^(?:src(\d+)|([x-za-w]))\[\](?::([mcb]))?$)">(
            input)) {
        TokenPayload_ClipAccess data;
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
        return Token{.type = TokenType::CLIP_ABS,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_cur(std::string_view input) {
    if (auto m = ctre::match<R"(^(?:src(\d+)|([x-za-w]))$)">(input)) {
        TokenPayload_ClipAccess data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        return Token{.type = TokenType::CLIP_CUR,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_prop_access(std::string_view input) {
    if (auto m = ctre::match<
            R"(^(?:src(\d+)|([x-za-w]))\.([a-zA-Z_][a-zA-Z0-9_]*)$)">(input)) {
        TokenPayload_PropAccess data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.prop_name = std::string(m.template get<3>().to_view());
        return Token{.type = TokenType::PROP_ACCESS,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_prop_exists(std::string_view input) {
    if (auto m = ctre::match<
            R"(^(?:src(\d+)|([x-za-w]))\.([a-zA-Z_][a-zA-Z0-9_]*)\?$)">(
            input)) {
        TokenPayload_PropAccess data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.prop_name = std::string(m.template get<3>().to_view());
        return Token{.type = TokenType::PROP_EXISTS,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_clip_abs_plane(std::string_view input) {
    if (auto m =
            ctre::match<R"(^(?:src(\d+)|([x-za-w]))\^(\d+)\[\]$)">(input)) {
        TokenPayload_ClipAccessPlane data;
        if (m.template get<1>()) {
            data.clip_idx = svtoi(m.template get<1>().to_view());
        } else if (m.template get<2>()) {
            data.clip_idx =
                parse_std_clip_idx(m.template get<2>().to_view()[0]);
        }
        data.plane_idx = svtoi(m.template get<3>().to_view());
        return Token{.type = TokenType::CLIP_ABS_PLANE,
                     .text = std::string(input),
                     .payload = data};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_store_abs_plane(std::string_view input) {
    if (auto m = ctre::match<R"(^@\[\]\^(\d+)$)">(input)) {
        int plane_idx = svtoi(m.template get<1>().to_view());
        return Token{.type = TokenType::STORE_ABS_PLANE,
                     .text = std::string(input),
                     .payload =
                         TokenPayload_StoreAbsPlane{.plane_idx = plane_idx}};
    }
    return std::nullopt;
}

inline std::optional<Token> parse_prop_store(std::string_view input) {
    if (auto m = ctre::match<R"(^([a-zA-Z_][a-zA-Z0-9_]*)\$(af|ai|f|i|d)?$)">(
            input)) {
        PropWriteType type = PropWriteType::FLOAT; // Default for bare '$'
        if (m.template get<2>()) {
            auto suffix = m.template get<2>().to_view();
            if (suffix == "i") {
                type = PropWriteType::INT;
            } else if (suffix == "f") {
                type = PropWriteType::FLOAT;
            } else if (suffix == "ai") {
                type = PropWriteType::AUTO_INT;
            } else if (suffix == "af") {
                type = PropWriteType::AUTO_FLOAT;
            } else if (suffix == "d") {
                type = PropWriteType::DELETE;
            }
        }

        return Token{
            .type = TokenType::PROP_STORE,
            .text = std::string(input),
            .payload = TokenPayload_PropStore{
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
        return Token{.type = TokenType::NUMBER,
                     .text = std::string(input),
                     .payload = TokenPayload_Number{val}};
    }
    return std::nullopt;
}

// Dynamic behavior resolvers for stack operations
inline TokenBehavior dup_behavior(const Token& t) {
    const auto& payload = std::get<TokenPayload_StackOp>(t.payload);
    return {.arity = payload.n + 1, .stack_effect = 1};
}

inline TokenBehavior drop_behavior(const Token& t) {
    const auto& payload = std::get<TokenPayload_StackOp>(t.payload);
    return {.arity = payload.n, .stack_effect = -payload.n};
}

inline TokenBehavior swap_behavior(const Token& t) {
    const auto& payload = std::get<TokenPayload_StackOp>(t.payload);
    return {.arity = payload.n + 1, .stack_effect = 0};
}

inline TokenBehavior sortn_behavior(const Token& t) {
    const auto& payload = std::get<TokenPayload_StackOp>(t.payload);
    return {.arity = payload.n, .stack_effect = 0};
}

inline TokenBehavior prop_store_behavior(const Token& t) {
    const auto& payload = std::get<TokenPayload_PropStore>(t.payload);
    if (payload.type == PropWriteType::DELETE) {
        return {.arity = 0, .stack_effect = 0};
    }
    return {.arity = 1, .stack_effect = -1};
}

// Compile-time token definitions table
constexpr auto get_token_definitions() {
    return std::array{
        TokenDefinition{.type = TokenType::ADD,
                        .name = "+",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_add,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::SUB,
                        .name = "-",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_sub,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::MUL,
                        .name = "*",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_mul,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::DIV,
                        .name = "/",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_div,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::MOD,
                        .name = "%",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_mod,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::GT,
                        .name = ">",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_gt,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::LT,
                        .name = "<",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_lt,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::EQ,
                        .name = "=",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_eq,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::TERNARY,
                        .name = "?",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -2},
                        .parser = parse_ternary,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CONSTANT_X,
                        .name = "X",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_x,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::CONSTANT_Y,
                        .name = "Y",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_y,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::CONSTANT_N,
                        .name = "N",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_n,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::GE,
                        .name = ">=",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_ge,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::LE,
                        .name = "<=",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_le,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::POW,
                        .name = "**",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_pow_op,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::OR,
                        .name = "or",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_or,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CONSTANT_PI,
                        .name = "pi",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_pi,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::AND,
                        .name = "and",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_and,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::XOR,
                        .name = "xor",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_xor,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::NOT,
                        .name = "not",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_not,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::POW,
                        .name = "pow",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_pow,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::MIN,
                        .name = "min",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_min,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::MAX,
                        .name = "max",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_max,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::FMA,
                        .name = "fma",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -2},
                        .parser = parse_fma,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::EXP,
                        .name = "exp",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_exp,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::LOG,
                        .name = "log",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_log,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ABS,
                        .name = "abs",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_abs,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::SIN,
                        .name = "sin",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_sin,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::COS,
                        .name = "cos",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_cos,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::TAN,
                        .name = "tan",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_tan,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::SGN,
                        .name = "sgn",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_sgn,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::NEG,
                        .name = "neg",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_neg,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::STORE_ABS,
                        .name = "@[]",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -3},
                        .parser = parse_store_abs,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::CLIP,
                        .name = "clip",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -2},
                        .parser = parse_clip_fn,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::SQRT,
                        .name = "sqrt",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_sqrt,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CEIL,
                        .name = "ceil",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_ceil,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ASIN,
                        .name = "asin",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_asin,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ACOS,
                        .name = "acos",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_acos,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ATAN,
                        .name = "atan",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_atan,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::EXP2,
                        .name = "exp2",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_exp2,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::LOG2,
                        .name = "log2",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_log2,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::SINH,
                        .name = "sinh",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_sinh,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::COSH,
                        .name = "cosh",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_cosh,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::TANH,
                        .name = "tanh",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_tanh,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::BITOR,
                        .name = "bitor",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_bitor,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ATAN2,
                        .name = "atan2",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_atan2,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CLAMP,
                        .name = "clamp",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -2},
                        .parser = parse_clamp,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::FLOOR,
                        .name = "floor",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_floor,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::TRUNC,
                        .name = "trunc",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_trunc,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ROUND,
                        .name = "round",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_round,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::LOG10,
                        .name = "log10",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_log10,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CONSTANT_WIDTH,
                        .name = "width",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_width,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::BITAND,
                        .name = "bitand",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_bitand,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::BITXOR,
                        .name = "bitxor",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_bitxor,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::BITNOT,
                        .name = "bitnot",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_bitnot,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CONSTANT_HEIGHT,
                        .name = "height",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_constant_height,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CONSTANT_PLANE_WIDTH,
                        .name = "plane_width",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_plane_width,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CONSTANT_PLANE_HEIGHT,
                        .name = "plane_height",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_plane_height,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CONSTANT_CLIP_WIDTH,
                        .name = "clip_width",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_width,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CONSTANT_CLIP_HEIGHT,
                        .name = "clip_height",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_height,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CONSTANT_CLIP_PLANE_WIDTH,
                        .name = "clip_plane_width",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_plane_width,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CONSTANT_CLIP_PLANE_HEIGHT,
                        .name = "clip_plane_height",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_plane_height,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::EXIT_NO_WRITE,
                        .name = "^exit^",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_exit_no_write,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::COPYSIGN,
                        .name = "copysign",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_copysign,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::DUP,
                        .name = "dupN",
                        .behavior = DynamicBehaviorFn(dup_behavior),
                        .parser = parse_dup,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::DROP,
                        .name = "dropN",
                        .behavior = DynamicBehaviorFn(drop_behavior),
                        .parser = parse_drop,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::SWAP,
                        .name = "swapN",
                        .behavior = DynamicBehaviorFn(swap_behavior),
                        .parser = parse_swap,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::SORTN,
                        .name = "sortN",
                        .behavior = DynamicBehaviorFn(sortn_behavior),
                        .parser = parse_sortn,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::LABEL_DEF,
                        .name = "label_def",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 0},
                        .parser = parse_label_def,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::JUMP,
                        .name = "jump",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = -1},
                        .parser = parse_jump,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::VAR_STORE,
                        .name = "var_store",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = -1},
                        .parser = parse_var_store,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::VAR_LOAD,
                        .name = "var_load",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_var_load,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ARRAY_ALLOC_STATIC,
                        .name = "array_alloc_static",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 0},
                        .parser = parse_array_alloc_static,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ARRAY_ALLOC_DYN,
                        .name = "array_alloc_dyn",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = -1},
                        .parser = parse_array_alloc_dyn,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ARRAY_STORE,
                        .name = "array_store",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -2},
                        .parser = parse_array_store,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::ARRAY_LOAD,
                        .name = "array_load",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_array_load,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CLIP_REL,
                        .name = "clip_rel",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_rel,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::CLIP_ABS,
                        .name = "clip_abs",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_clip_abs,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CLIP_CUR,
                        .name = "clip_cur",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_clip_cur,
                        .available_in_expr = true,
                        .available_in_single_expr = false},
        TokenDefinition{.type = TokenType::PROP_ACCESS,
                        .name = "prop_access",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_prop_access,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::PROP_EXISTS,
                        .name = "prop_exists",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 1},
                        .parser = parse_prop_exists,
                        .available_in_expr = true,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::CLIP_ABS_PLANE,
                        .name = "clip_abs_plane",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_clip_abs_plane,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::STORE_ABS_PLANE,
                        .name = "store_abs_plane",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -3},
                        .parser = parse_store_abs_plane,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::PROP_STORE,
                        .name = "prop_store",
                        .behavior = DynamicBehaviorFn(prop_store_behavior),
                        .parser = parse_prop_store,
                        .available_in_expr = false,
                        .available_in_single_expr = true},
        TokenDefinition{.type = TokenType::NUMBER,
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

    constexpr auto token_defs = get_token_definitions();

    for (const auto str_token_view :
         expr | std::views::chunk_by([=](char a, char b) {
             return is_space(a) == is_space(b);
         }) | std::views::filter([=](auto r) { return !is_space(r.front()); }) |
             std::views::transform(to_string_view)) {
        std::optional<Token> parsed_token;

        for (const auto& definition : token_defs) {
            // Check mode restrictions
            bool skip = false;
            if (mode == ExprMode::EXPR && !definition.available_in_expr) {
                skip = true;
            }
            if (mode == ExprMode::SINGLE_EXPR &&
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
        if (parsed_token->type == TokenType::CLIP_REL ||
            parsed_token->type == TokenType::CLIP_ABS ||
            parsed_token->type == TokenType::CLIP_CUR) {
            if (std::get<TokenPayload_ClipAccess>(parsed_token->payload)
                        .clip_idx < 0 ||
                std::get<TokenPayload_ClipAccess>(parsed_token->payload)
                        .clip_idx >= num_inputs) {
                throw std::runtime_error(
                    std::format("Invalid clip index in token: {} (idx {})",
                                std::string(str_token_view), idx));
            }
        } else if (parsed_token->type == TokenType::PROP_ACCESS) {
            if (std::get<TokenPayload_PropAccess>(parsed_token->payload)
                        .clip_idx < 0 ||
                std::get<TokenPayload_PropAccess>(parsed_token->payload)
                        .clip_idx >= num_inputs) {
                throw std::runtime_error(
                    std::format("Invalid clip index in token: {} (idx {})",
                                std::string(str_token_view), idx));
            }
        } else if (parsed_token->type == TokenType::CLIP_ABS_PLANE) {
            if (std::get<TokenPayload_ClipAccessPlane>(parsed_token->payload)
                        .clip_idx < 0 ||
                std::get<TokenPayload_ClipAccessPlane>(parsed_token->payload)
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
    constexpr auto token_defs = get_token_definitions();

    const auto* it = std::ranges::find_if(
        token_defs, [&](const auto& def) { return def.type == token.type; });

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
