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
#include <format>
#include <limits>
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

template <std::size_t N> struct FixedString {
    std::array<char, N> value;

    // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    constexpr FixedString(const char (&str)[N]) {
        std::ranges::copy(str, value.begin());
    }

    [[nodiscard]] constexpr std::string_view view() const {
        return {value.data(), N - 1};
    }
};

using Availability = TokenDefinition::Availability;

constexpr Availability operator|(Availability lhs, Availability rhs) {
    return static_cast<Availability>(static_cast<std::uint8_t>(lhs) |
                                     static_cast<std::uint8_t>(rhs));
}

constexpr Availability operator&(Availability lhs, Availability rhs) {
    return static_cast<Availability>(static_cast<std::uint8_t>(lhs) &
                                     static_cast<std::uint8_t>(rhs));
}

constexpr Availability AVAILABILITY_ALL =
    Availability::Expr | Availability::SingleExpr;

constexpr bool supports_mode(Availability availability, ExprMode mode) {
    if (mode == ExprMode::Expr) {
        return static_cast<std::uint8_t>(availability & Availability::Expr) !=
               0;
    }
    return static_cast<std::uint8_t>(availability & Availability::SingleExpr) !=
           0;
}

template <FixedString Str, TokenType Type>
std::optional<Token> parse_literal(std::string_view input) {
    if (input == Str.view()) {
        return Token{.type = Type,
                     .text = std::string(input),
                     .payload = std::monostate{}};
    }
    return std::nullopt;
}

template <FixedString Str, TokenType Type>
consteval TokenDefinition
make_literal_definition(TokenBehavior behavior,
                        Availability availability = AVAILABILITY_ALL) {
    return {.type = Type,
            .name = Str.view(),
            .behavior = behavior,
            .parser = parse_literal<Str, Type>,
            .availability = availability};
}

constexpr TokenBehavior BEHAVIOR_BINARY{.arity = 2, .stack_effect = -1};
constexpr TokenBehavior BEHAVIOR_UNARY{.arity = 1, .stack_effect = 0};
constexpr TokenBehavior BEHAVIOR_ZERO_PUSH{.arity = 0, .stack_effect = 1};
constexpr TokenBehavior BEHAVIOR_TERNARY{.arity = 3, .stack_effect = -2};
constexpr TokenBehavior BEHAVIOR_NO_EFFECT{.arity = 0, .stack_effect = 0};

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
        make_literal_definition<FixedString{"+"}, TokenType::Add>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"-"}, TokenType::Sub>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"*"}, TokenType::Mul>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"/"}, TokenType::Div>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"%"}, TokenType::Mod>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{">"}, TokenType::Gt>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"<"}, TokenType::Lt>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"="}, TokenType::Eq>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"?"}, TokenType::Ternary>(
            BEHAVIOR_TERNARY),
        make_literal_definition<FixedString{"X"}, TokenType::ConstantX>(
            BEHAVIOR_ZERO_PUSH, Availability::Expr),
        make_literal_definition<FixedString{"Y"}, TokenType::ConstantY>(
            BEHAVIOR_ZERO_PUSH, Availability::Expr),
        make_literal_definition<FixedString{"N"}, TokenType::ConstantN>(
            BEHAVIOR_ZERO_PUSH),
        make_literal_definition<FixedString{">="}, TokenType::Ge>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"<="}, TokenType::Le>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"**"}, TokenType::Pow>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"or"}, TokenType::Or>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"pi"}, TokenType::ConstantPi>(
            BEHAVIOR_ZERO_PUSH),
        make_literal_definition<FixedString{"and"}, TokenType::And>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"xor"}, TokenType::Xor>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"not"}, TokenType::Not>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"pow"}, TokenType::Pow>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"min"}, TokenType::Min>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"max"}, TokenType::Max>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"fma"}, TokenType::Fma>(
            BEHAVIOR_TERNARY),
        make_literal_definition<FixedString{"exp"}, TokenType::Exp>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"log"}, TokenType::Log>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"abs"}, TokenType::Abs>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"sin"}, TokenType::Sin>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"cos"}, TokenType::Cos>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"tan"}, TokenType::Tan>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"sgn"}, TokenType::Sgn>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"neg"}, TokenType::Neg>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"@[]"}, TokenType::StoreAbs>(
            TokenBehavior{.arity = 3, .stack_effect = -3}, Availability::Expr),
        make_literal_definition<FixedString{"clip"}, TokenType::Clip>(
            TokenBehavior{.arity = 3, .stack_effect = -2}),
        make_literal_definition<FixedString{"sqrt"}, TokenType::Sqrt>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"ceil"}, TokenType::Ceil>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"asin"}, TokenType::Asin>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"acos"}, TokenType::Acos>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"atan"}, TokenType::Atan>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"exp2"}, TokenType::Exp2>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"log2"}, TokenType::Log2>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"sinh"}, TokenType::Sinh>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"cosh"}, TokenType::Cosh>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"tanh"}, TokenType::Tanh>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"bitor"}, TokenType::Bitor>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"atan2"}, TokenType::Atan2>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"clamp"}, TokenType::Clamp>(
            BEHAVIOR_TERNARY),
        make_literal_definition<FixedString{"floor"}, TokenType::Floor>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"trunc"}, TokenType::Trunc>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"round"}, TokenType::Round>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"log10"}, TokenType::Log10>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"width"}, TokenType::ConstantWidth>(
            BEHAVIOR_ZERO_PUSH),
        make_literal_definition<FixedString{"bitand"}, TokenType::Bitand>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"bitxor"}, TokenType::Bitxor>(
            BEHAVIOR_BINARY),
        make_literal_definition<FixedString{"bitnot"}, TokenType::Bitnot>(
            BEHAVIOR_UNARY),
        make_literal_definition<FixedString{"height"},
                                TokenType::ConstantHeight>(BEHAVIOR_ZERO_PUSH),
        make_literal_definition<FixedString{"^exit^"}, TokenType::ExitNoWrite>(
            BEHAVIOR_ZERO_PUSH, Availability::Expr),
        make_literal_definition<FixedString{"copysign"}, TokenType::Copysign>(
            BEHAVIOR_BINARY),
        TokenDefinition{.type = TokenType::ConstantPlaneWidth,
                        .name = "plane_width",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_plane_width,
                        .availability = Availability::SingleExpr},
        TokenDefinition{.type = TokenType::ConstantPlaneHeight,
                        .name = "plane_height",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_plane_height,
                        .availability = Availability::SingleExpr},
        TokenDefinition{.type = TokenType::ConstantClipWidth,
                        .name = "clip_width",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_clip_width,
                        .availability = Availability::SingleExpr},
        TokenDefinition{.type = TokenType::ConstantClipHeight,
                        .name = "clip_height",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_clip_height,
                        .availability = Availability::SingleExpr},
        TokenDefinition{.type = TokenType::ConstantClipPlaneWidth,
                        .name = "clip_plane_width",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_clip_plane_width,
                        .availability = Availability::SingleExpr},
        TokenDefinition{.type = TokenType::ConstantClipPlaneHeight,
                        .name = "clip_plane_height",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_clip_plane_height,
                        .availability = Availability::SingleExpr},
        TokenDefinition{.type = TokenType::Dup,
                        .name = "dupN",
                        .behavior = DynamicBehaviorFn(dup_behavior),
                        .parser = parse_dup,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::Drop,
                        .name = "dropN",
                        .behavior = DynamicBehaviorFn(drop_behavior),
                        .parser = parse_drop,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::Swap,
                        .name = "swapN",
                        .behavior = DynamicBehaviorFn(swap_behavior),
                        .parser = parse_swap,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::SortN,
                        .name = "sortN",
                        .behavior = DynamicBehaviorFn(sortn_behavior),
                        .parser = parse_sortn,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::LabelDef,
                        .name = "label_def",
                        .behavior = BEHAVIOR_NO_EFFECT,
                        .parser = parse_label_def,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::Jump,
                        .name = "jump",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = -1},
                        .parser = parse_jump,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::VarStore,
                        .name = "VarStore",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = -1},
                        .parser = parse_var_store,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::VarLoad,
                        .name = "var_load",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_var_load,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::ArrayAllocStatic,
                        .name = "array_alloc_static",
                        .behavior =
                            TokenBehavior{.arity = 0, .stack_effect = 0},
                        .parser = parse_array_alloc_static,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::ArrayAllocDyn,
                        .name = "array_alloc_dyn",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = -1},
                        .parser = parse_array_alloc_dyn,
                        .availability = Availability::SingleExpr},
        TokenDefinition{.type = TokenType::ArrayStore,
                        .name = "array_store",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -2},
                        .parser = parse_array_store,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::ArrayLoad,
                        .name = "array_load",
                        .behavior =
                            TokenBehavior{.arity = 1, .stack_effect = 0},
                        .parser = parse_array_load,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::ClipRel,
                        .name = "clip_rel",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_clip_rel,
                        .availability = Availability::Expr},
        TokenDefinition{.type = TokenType::ClipAbs,
                        .name = "clip_abs",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_clip_abs,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::ClipCur,
                        .name = "clip_cur",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_clip_cur,
                        .availability = Availability::Expr},
        TokenDefinition{.type = TokenType::PropAccess,
                        .name = "prop_access",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_prop_access,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::PropExists,
                        .name = "prop_exists",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_prop_exists,
                        .availability = AVAILABILITY_ALL},
        TokenDefinition{.type = TokenType::ClipAbsPlane,
                        .name = "clip_abs_plane",
                        .behavior =
                            TokenBehavior{.arity = 2, .stack_effect = -1},
                        .parser = parse_clip_abs_plane,
                        .availability = Availability::SingleExpr},
        TokenDefinition{.type = TokenType::StoreAbsPlane,
                        .name = "store_abs_plane",
                        .behavior =
                            TokenBehavior{.arity = 3, .stack_effect = -3},
                        .parser = parse_store_abs_plane,
                        .availability = Availability::SingleExpr},
        TokenDefinition{.type = TokenType::PropStore,
                        .name = "prop_store",
                        .behavior = DynamicBehaviorFn(prop_store_behavior),
                        .parser = parse_prop_store,
                        .availability = Availability::SingleExpr},
        TokenDefinition{.type = TokenType::Number,
                        .name = "number",
                        .behavior = BEHAVIOR_ZERO_PUSH,
                        .parser = parse_number,
                        .availability = AVAILABILITY_ALL},
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
            if (!supports_mode(definition.availability, mode)) {
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
