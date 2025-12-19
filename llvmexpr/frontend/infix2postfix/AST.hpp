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

#ifndef LLVMEXPR_FRONTEND_INFIX2POSTFIX_AST_HPP
#define LLVMEXPR_FRONTEND_INFIX2POSTFIX_AST_HPP

#include "Symbol.hpp"
#include "types.hpp"
#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace infix2postfix {

// Forward declarations
struct Expr;
struct Stmt;
struct BlockStmt;
struct GlobalDecl;
struct FunctionDef;
struct BuiltinFunction;

// Expression node types
struct NumberExpr {
    Token value;
    Range range;

    explicit NumberExpr(Token val)
        : value(std::move(val)), range(value.range) {}
};

struct VariableExpr {
    Token name;
    Range range;
    std::shared_ptr<Symbol> symbol;

    explicit VariableExpr(Token n) : name(std::move(n)), range(name.range) {}
};

struct UnaryExpr {
    Token op;
    std::unique_ptr<Expr> right;
    Range range;

    UnaryExpr(Token o, std::unique_ptr<Expr> r);
};

struct BinaryExpr {
    std::unique_ptr<Expr> left;
    Token op;
    std::unique_ptr<Expr> right;
    Range range;

    BinaryExpr(std::unique_ptr<Expr> l, Token o, std::unique_ptr<Expr> r);
};

struct TernaryExpr {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Expr> true_expr;
    std::unique_ptr<Expr> false_expr;
    Range range;

    TernaryExpr(std::unique_ptr<Expr> c, std::unique_ptr<Expr> t,
                std::unique_ptr<Expr> f);
};

struct CallExpr {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
    Range range;
    const FunctionSignature* resolved_signature = nullptr;
    FunctionDef* resolved_def = nullptr;
    const BuiltinFunction* resolved_builtin = nullptr;

    CallExpr(Token callee_token, std::vector<std::unique_ptr<Expr>> a);
};

struct PropAccessExpr {
    Token clip;
    Token prop;
    Range range;

    PropAccessExpr(Token c, Token p)
        : clip(std::move(c)), prop(std::move(p)), range(clip.range) {}
};

struct StaticRelPixelAccessExpr {
    Token clip;
    Token offset_x;
    Token offset_y;
    std::string boundary_suffix;
    Range range;

    StaticRelPixelAccessExpr(Token c, Token x, Token y, std::string suffix)
        : clip(std::move(c)), offset_x(std::move(x)), offset_y(std::move(y)),
          boundary_suffix(std::move(suffix)), range(clip.range) {}
};

struct FrameDimensionExpr {
    std::string dimension_name; // "width" or "height"
    std::unique_ptr<Expr> plane_index_expr;
    Range range;

    FrameDimensionExpr(Token keyword, std::unique_ptr<Expr> plane);
};

struct ArrayAccessExpr {
    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
    Range range;
    std::shared_ptr<Symbol> array_symbol;

    ArrayAccessExpr(std::unique_ptr<Expr> arr, std::unique_ptr<Expr> idx);
};

// Statement node types
struct ExprStmt {
    std::unique_ptr<Expr> expr;
    Range range;

    explicit ExprStmt(std::unique_ptr<Expr> e);
};

struct AssignStmt {
    Token name;
    std::unique_ptr<Expr> value;
    Range range;
    std::shared_ptr<Symbol> symbol;

    AssignStmt(Token n, std::unique_ptr<Expr> v);
};

struct ArrayAssignStmt {
    std::unique_ptr<Expr> target; // Should be an ArrayAccessExpr
    std::unique_ptr<Expr> value;
    Range range;

    ArrayAssignStmt(std::unique_ptr<Expr> t, std::unique_ptr<Expr> v);
};

struct BlockStmt {
    std::vector<std::unique_ptr<Stmt>> statements;
    Range range;

    explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> s);
};

struct IfStmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> then_branch;
    std::unique_ptr<Stmt> else_branch;
    Range range;

    IfStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> t,
           std::unique_ptr<Stmt> e);
};

struct WhileStmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
    Range range;

    WhileStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> b);
};

struct ReturnStmt {
    Token keyword;
    std::unique_ptr<Expr> value;
    Range range;

    ReturnStmt(Token kw, std::unique_ptr<Expr> v);
};

struct LabelStmt {
    Token name;
    Range range;
    std::shared_ptr<Symbol> symbol;

    explicit LabelStmt(Token n) : name(std::move(n)), range(name.range) {}
};

struct GotoStmt {
    Token keyword;
    Token label;
    std::unique_ptr<Expr> condition;
    Range range;
    std::shared_ptr<Symbol> target_label_symbol;

    GotoStmt(Token kw, Token l, std::unique_ptr<Expr> c);
};

struct GlobalDecl {
    Token keyword;
    GlobalMode mode;
    std::vector<Token> globals;
    Range range;

    GlobalDecl(Token t, GlobalMode m, std::vector<Token> g = {})
        : keyword(std::move(t)), mode(m), globals(std::move(g)),
          range(keyword.range) {}
};

struct Parameter {
    Token type_name;
    Token name;
    Type type;
};

struct FunctionDef {
    Token name;
    std::vector<Parameter> params;
    std::unique_ptr<BlockStmt> body;
    std::unique_ptr<GlobalDecl> global_decl;
    Range range;
    std::shared_ptr<Symbol> symbol;

    FunctionDef(Token n, std::vector<Parameter> p, std::unique_ptr<BlockStmt> b,
                std::unique_ptr<GlobalDecl> g);
};

struct Expr {
    using ExprVariant = std::variant<NumberExpr, VariableExpr, UnaryExpr,
                                     BinaryExpr, TernaryExpr, CallExpr,
                                     PropAccessExpr, StaticRelPixelAccessExpr,
                                     FrameDimensionExpr, ArrayAccessExpr>;

    ExprVariant value;

    template <typename T> explicit Expr(T&& v) : value(std::forward<T>(v)) {}

    Expr(const Expr&) = delete;
    Expr& operator=(const Expr&) = delete;
    Expr(Expr&&) = default;
    Expr& operator=(Expr&&) = default;
    ~Expr() = default;

    [[nodiscard]] Range range() const {
        return std::visit([](const auto& e) { return e.range; }, value);
    }
};

struct Stmt {
    using StmtVariant = std::variant<ExprStmt, AssignStmt, BlockStmt, IfStmt,
                                     WhileStmt, ReturnStmt, LabelStmt, GotoStmt,
                                     FunctionDef, GlobalDecl, ArrayAssignStmt>;

    StmtVariant value;

    template <typename T> explicit Stmt(T&& v) : value(std::forward<T>(v)) {}

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    Stmt(Stmt&&) = default;
    Stmt& operator=(Stmt&&) = default;
    ~Stmt() = default;

    [[nodiscard]] Range range() const {
        return std::visit([](const auto& s) { return s.range; }, value);
    }
};

inline FrameDimensionExpr::FrameDimensionExpr(Token keyword,
                                              std::unique_ptr<Expr> plane)
    : dimension_name(std::move(keyword.value)),
      plane_index_expr(std::move(plane)), range(keyword.range) {}

inline UnaryExpr::UnaryExpr(Token o, std::unique_ptr<Expr> r)
    : op(std::move(o)), right(std::move(r)) {
    range.start = op.range.start;
    range.end = right ? right->range().end : op.range.end;
}

inline BinaryExpr::BinaryExpr(std::unique_ptr<Expr> l, Token o,
                              std::unique_ptr<Expr> r)
    : left(std::move(l)), op(std::move(o)), right(std::move(r)) {
    range.start = left ? left->range().start : op.range.start;
    range.end = right ? right->range().end : op.range.end;
}

inline TernaryExpr::TernaryExpr(std::unique_ptr<Expr> c,
                                std::unique_ptr<Expr> t,
                                std::unique_ptr<Expr> f)
    : cond(std::move(c)), true_expr(std::move(t)), false_expr(std::move(f)) {
    if (cond) {
        range.start = cond->range().start;
    }
    if (false_expr) {
        range.end = false_expr->range().end;
    } else if (true_expr) {
        range.end = true_expr->range().end;
    } else if (cond) {
        range.end = cond->range().end;
    }
}

inline CallExpr::CallExpr(Token callee_token,
                          std::vector<std::unique_ptr<Expr>> a)
    : callee(std::move(callee_token.value)), args(std::move(a)),
      range(callee_token.range) {}

inline ExprStmt::ExprStmt(std::unique_ptr<Expr> e) : expr(std::move(e)) {
    if (expr) {
        range = expr->range();
    }
}

inline AssignStmt::AssignStmt(Token n, std::unique_ptr<Expr> v)
    : name(std::move(n)), value(std::move(v)) {
    range.start = name.range.start;
    range.end = value ? value->range().end : name.range.end;
}

inline BlockStmt::BlockStmt(std::vector<std::unique_ptr<Stmt>> s)
    : statements(std::move(s)) {
    if (!statements.empty()) {
        range.start = statements.front()->range().start;
        range.end = statements.back()->range().end;
    }
}

inline IfStmt::IfStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> t,
                      std::unique_ptr<Stmt> e)
    : condition(std::move(c)), then_branch(std::move(t)),
      else_branch(std::move(e)) {
    if (condition) {
        range.start = condition->range().start;
    }
    if (else_branch) {
        range.end = else_branch->range().end;
    } else if (then_branch) {
        range.end = then_branch->range().end;
    } else if (condition) {
        range.end = condition->range().end;
    }
}

inline WhileStmt::WhileStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> b)
    : condition(std::move(c)), body(std::move(b)) {
    if (condition) {
        range.start = condition->range().start;
    }
    if (body) {
        range.end = body->range().end;
    } else if (condition) {
        range.end = condition->range().end;
    }
}

inline ReturnStmt::ReturnStmt(Token kw, std::unique_ptr<Expr> v)
    : keyword(std::move(kw)), value(std::move(v)) {
    range.start = keyword.range.start;
    range.end = value ? value->range().end : keyword.range.end;
}

inline GotoStmt::GotoStmt(Token kw, Token l, std::unique_ptr<Expr> c)
    : keyword(std::move(kw)), label(std::move(l)), condition(std::move(c)) {
    range.start = keyword.range.start;
    range.end = condition ? condition->range().end : label.range.end;
}

inline FunctionDef::FunctionDef(Token n, std::vector<Parameter> p,
                                std::unique_ptr<BlockStmt> b,
                                std::unique_ptr<GlobalDecl> g)
    : name(std::move(n)), params(std::move(p)), body(std::move(b)),
      global_decl(std::move(g)) {
    range.start = name.range.start;
    range.end = body ? body->range.end : name.range.end;
}

inline ArrayAccessExpr::ArrayAccessExpr(std::unique_ptr<Expr> arr,
                                        std::unique_ptr<Expr> idx)
    : array(std::move(arr)), index(std::move(idx)) {
    if (array) {
        range.start = array->range().start;
    }
    if (index) {
        range.end = index->range().end;
    } else if (array) {
        range.end = array->range().end;
    }
}

inline ArrayAssignStmt::ArrayAssignStmt(std::unique_ptr<Expr> t,
                                        std::unique_ptr<Expr> v)
    : target(std::move(t)), value(std::move(v)) {
    if (target) {
        range.start = target->range().start;
    }
    if (value) {
        range.end = value->range().end;
    } else if (target) {
        range.end = target->range().end;
    }
}

struct Program {
    std::vector<std::unique_ptr<Stmt>> statements;
};

template <class, class> struct is_variant_member;

template <class T, class... Types>
struct is_variant_member<T, std::variant<Types...>>
    : std::disjunction<std::is_same<T, Types>...> {};

template <class T, class V>
inline constexpr bool IS_VARIANT_MEMBER_V = is_variant_member<T, V>::value;

template <typename T> struct NodeWrapper {
    static_assert(IS_VARIANT_MEMBER_V<T, Expr::ExprVariant> ||
                      IS_VARIANT_MEMBER_V<T, Stmt::StmtVariant>,
                  "T must be a member of either ExprVariant or StmtVariant");
    using type = std::conditional_t<IS_VARIANT_MEMBER_V<T, Expr::ExprVariant>,
                                    Expr, Stmt>;
};

template <typename T> using node_wrapper_t = typename NodeWrapper<T>::type;

template <typename T, typename... Args> auto make_node(Args&&... args) {
    return std::make_unique<node_wrapper_t<T>>(T(std::forward<Args>(args)...));
}

template <typename T, typename Wrapper>
auto get_if(Wrapper* wrapper) -> decltype(std::get_if<T>(&wrapper->value)) {
    if (!wrapper) {
        return nullptr;
    }
    return std::get_if<T>(&wrapper->value);
}

} // namespace infix2postfix

#endif // LLVMEXPR_FRONTEND_INFIX2POSTFIX_AST_HPP
