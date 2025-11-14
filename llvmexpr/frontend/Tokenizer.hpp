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

#ifndef LLVMEXPR_TOKENIZER_HPP
#define LLVMEXPR_TOKENIZER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class TokenType : std::uint8_t {
    // Literals & Constants
    NUMBER,
    CONSTANT_X,                 // X
    CONSTANT_Y,                 // Y
    CONSTANT_WIDTH,             // width
    CONSTANT_HEIGHT,            // height
    CONSTANT_PLANE_WIDTH,       // width^plane_no
    CONSTANT_PLANE_HEIGHT,      // height^plane_no
    CONSTANT_CLIP_WIDTH,        // srcN:width
    CONSTANT_CLIP_HEIGHT,       // srcN:height
    CONSTANT_CLIP_PLANE_WIDTH,  // srcN:width^plane_no
    CONSTANT_CLIP_PLANE_HEIGHT, // srcN:height^plane_no
    CONSTANT_N,                 // N
    CONSTANT_PI,                // pi

    // Variable Ops
    VAR_STORE, // my_var!
    VAR_LOAD,  // my_var@

    // Array Ops
    ARRAY_ALLOC_STATIC, // arr{}^N
    ARRAY_ALLOC_DYN,    // arr{}^
    ARRAY_STORE,        // arr{}!
    ARRAY_LOAD,         // arr{}@

    // Data Access
    CLIP_REL,        // src[x,y]
    CLIP_ABS,        // src[]
    CLIP_CUR,        // src
    PROP_ACCESS,     // src.prop
    CLIP_ABS_PLANE,  // src^plane[]
    STORE_ABS_PLANE, // @[]^plane
    PROP_STORE,      // prop$

    // Binary Operators
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    GT,
    LT,
    GE,
    LE,
    EQ,
    AND,
    OR,
    XOR,
    BITAND,
    BITOR,
    BITXOR,
    POW,
    ATAN2,
    COPYSIGN,
    MIN,
    MAX,

    // Unary Operators
    NOT,
    BITNOT,
    SQRT,
    EXP,
    LOG,
    ABS,
    FLOOR,
    CEIL,
    TRUNC,
    ROUND,
    SIN,
    COS,
    TAN,
    ASIN,
    ACOS,
    ATAN,
    EXP2,
    LOG10,
    LOG2,
    SINH,
    COSH,
    TANH,
    SGN,
    NEG,

    // Ternary and other multi-arg
    TERNARY, // ?
    CLIP,
    CLAMP, // same op, 3 args
    FMA,   // 3 args

    // Stack manipulation
    DUP,
    DROP,
    SWAP,
    SORTN,

    // Control Flow
    LABEL_DEF, // #my_label
    JUMP,      // my_label#

    // Custom output control
    EXIT_NO_WRITE, // ^exit^
    STORE_ABS,     // @[]
};

struct TokenPayload_Number {
    double value;
};

struct TokenPayload_Var {
    std::string name;
};

struct TokenPayload_Label {
    std::string name;
};

struct TokenPayload_StackOp {
    int n;
};

struct TokenPayload_ClipAccess {
    int clip_idx;
    int rel_x = 0;
    int rel_y = 0;
    bool use_mirror = false;
    bool has_mode = false;
};

struct TokenPayload_PropAccess {
    int clip_idx;
    std::string prop_name;
};

struct TokenPayload_ClipAccessPlane {
    int clip_idx;
    int plane_idx;
};

struct TokenPayload_StoreAbsPlane {
    int plane_idx;
};

struct TokenPayload_PropStore {
    std::string prop_name;
};

struct TokenPayload_PlaneDim {
    int plane_idx;
};

struct TokenPayload_ClipDim {
    int clip_idx;
};

struct TokenPayload_ClipPlaneDim {
    int clip_idx;
    int plane_idx;
};

struct TokenPayload_ArrayOp {
    std::string name;
    int static_size = 0; // ARRAY_ALLOC_STATIC
};

struct Token {
    using PayloadVariant =
        std::variant<std::monostate, TokenPayload_Number, TokenPayload_Var,
                     TokenPayload_Label, TokenPayload_StackOp,
                     TokenPayload_ClipAccess, TokenPayload_PropAccess,
                     TokenPayload_ClipAccessPlane, TokenPayload_StoreAbsPlane,
                     TokenPayload_PropStore, TokenPayload_PlaneDim,
                     TokenPayload_ClipDim, TokenPayload_ClipPlaneDim,
                     TokenPayload_ArrayOp>;

    TokenType type;
    std::string text;
    PayloadVariant payload;
};

struct TokenBehavior {
    int arity;
    int stack_effect;
};

using DynamicBehaviorFn = TokenBehavior (*)(const Token&);
using BehaviorResolver = std::variant<TokenBehavior, DynamicBehaviorFn>;

enum class ExprMode : std::uint8_t {
    EXPR,
    SINGLE_EXPR,
};

// Utility functions
constexpr int parse_std_clip_idx(char c) {
    if (c >= 'x' && c <= 'z') {
        return c - 'x';
    }
    return c - 'a' + 3;
}

using TokenParser = std::optional<Token> (*)(std::string_view);

struct TokenDefinition {
    TokenType type;
    std::string_view name;
    BehaviorResolver behavior;
    TokenParser parser;

    bool available_in_expr = true;
    bool available_in_single_expr = true;
};

std::vector<Token> tokenize(const std::string& expr, int num_inputs,
                            ExprMode mode = ExprMode::EXPR);
TokenBehavior get_token_behavior(const Token& token);

#endif // LLVMEXPR_TOKENIZER_HPP
