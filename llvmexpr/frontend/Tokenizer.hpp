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

#ifndef LLVMEXPR_FRONTEND_TOKENIZER_HPP
#define LLVMEXPR_FRONTEND_TOKENIZER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class TokenType : std::uint8_t {
    // Literals & Constants
    Number,
    ConstantX,               // X
    ConstantY,               // Y
    ConstantWidth,           // width
    ConstantHeight,          // height
    ConstantPlaneWidth,      // width^plane_no
    ConstantPlaneHeight,     // height^plane_no
    ConstantClipWidth,       // srcN:width
    ConstantClipHeight,      // srcN:height
    ConstantClipPlaneWidth,  // srcN:width^plane_no
    ConstantClipPlaneHeight, // srcN:height^plane_no
    ConstantN,               // N
    ConstantPi,              // pi

    // Variable Ops
    VarStore, // my_var!
    VarLoad,  // my_var@

    // Array Ops
    ArrayAllocStatic, // arr{}^N
    ArrayAllocDyn,    // arr{}^
    ArrayStore,       // arr{}!
    ArrayLoad,        // arr{}@

    // Data Access
    ClipRel,       // src[x,y]
    ClipAbs,       // src[]
    ClipCur,       // src
    PropAccess,    // src.prop
    PropExists,    // src.prop?
    ClipAbsPlane,  // src^plane[]
    StoreAbsPlane, // @[]^plane
    PropStore,     // prop$

    // Binary Operators
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Gt,
    Lt,
    Ge,
    Le,
    Eq,
    And,
    Or,
    Xor,
    Bitand,
    Bitor,
    Bitxor,
    Pow,
    Atan2,
    Copysign,
    Min,
    Max,

    // Unary Operators
    Not,
    Bitnot,
    Sqrt,
    Exp,
    Log,
    Abs,
    Floor,
    Ceil,
    Trunc,
    Round,
    Sin,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Exp2,
    Log10,
    Log2,
    Sinh,
    Cosh,
    Tanh,
    Sgn,
    Neg,

    // Ternary and other multi-arg
    Ternary, // ?
    Clip,
    Clamp, // same op, 3 args
    Fma,   // 3 args

    // Stack manipulation
    Dup,
    Drop,
    Swap,
    SortN,

    // Control Flow
    LabelDef, // #my_label
    Jump,     // my_label#

    // Custom output control
    ExitNoWrite, // ^exit^
    StoreAbs,    // @[]
};

struct TokenPayloadNumber {
    double value;
};

struct TokenPayloadVar {
    std::string name;
};

struct TokenPayloadLabel {
    std::string name;
};

struct TokenPayloadStackOp {
    int n;
};

struct TokenPayloadClipAccess {
    int clip_idx;
    int rel_x = 0;
    int rel_y = 0;
    bool use_mirror = false;
    bool has_mode = false;
};

struct TokenPayloadPropAccess {
    int clip_idx;
    std::string prop_name;
};

struct TokenPayloadClipAccessPlane {
    int clip_idx;
    int plane_idx;
};

struct TokenPayloadStoreAbsPlane {
    int plane_idx;
};

enum class PropWriteType : std::uint8_t {
    Float,     // $f or $
    Int,       // $i
    AutoInt,   // $ai
    AutoFloat, // $af
    Delete,    // $d
};

struct TokenPayloadPropStore {
    std::string prop_name;
    PropWriteType type;
};

struct TokenPayloadPlaneDim {
    int plane_idx;
};

struct TokenPayloadClipDim {
    int clip_idx;
};

struct TokenPayloadClipPlaneDim {
    int clip_idx;
    int plane_idx;
};

struct TokenPayloadArrayOp {
    std::string name;
    int static_size = 0; // ARRAY_ALLOC_STATIC
};

struct Token {
    using PayloadVariant = std::variant<
        std::monostate, TokenPayloadNumber, TokenPayloadVar, TokenPayloadLabel,
        TokenPayloadStackOp, TokenPayloadClipAccess, TokenPayloadPropAccess,
        TokenPayloadClipAccessPlane, TokenPayloadStoreAbsPlane,
        TokenPayloadPropStore, TokenPayloadPlaneDim, TokenPayloadClipDim,
        TokenPayloadClipPlaneDim, TokenPayloadArrayOp>;

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
    Expr,
    SingleExpr,
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
                            ExprMode mode = ExprMode::Expr);
TokenBehavior get_token_behavior(const Token& token);

#endif // LLVMEXPR_FRONTEND_TOKENIZER_HPP
