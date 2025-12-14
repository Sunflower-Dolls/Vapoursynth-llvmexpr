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

#include "ASTPrinter.hpp"
#include "Builtins.hpp"
#include "llvmexpr/utils/EnumName.hpp"

#include <cctype>
#include <cstring>

namespace infix2postfix {

std::string ASTPrinter::print(const Program* program) {
    ss.str("");
    ss.clear();
    indent_level = 0;
    if (program != nullptr) {
        for (const auto& stmt : program->statements) {
            print(stmt.get());
        }
    }
    return ss.str();
}

void ASTPrinter::print(const Stmt* stmt) {
    if (stmt == nullptr) {
        return;
    }
    std::visit([this](const auto& s) { this->visit(s); }, stmt->value);
}

void ASTPrinter::print(const Expr* expr) {
    if (expr == nullptr) {
        line("<null expr>");
        return;
    };
    std::visit([this](const auto& e) { this->visit(e); }, expr->value);
}

void ASTPrinter::indent() { indent_level++; }

void ASTPrinter::unindent() { indent_level--; }

// Stmt visitors
void ASTPrinter::visit(const ExprStmt& stmt) {
    line("ExprStmt:");
    indent();
    print(stmt.expr.get());
    unindent();
}

void ASTPrinter::visit(const AssignStmt& stmt) {
    line("AssignStmt: {}", stmt.name.value);
    if (stmt.symbol) {
        indent();
        std::string type_name = std::string(enum_name(stmt.symbol->type));
        line("Symbol: {} (type: {})", stmt.symbol->name, type_name);
        unindent();
    }
    indent();
    line("Value:");
    indent();
    print(stmt.value.get());
    unindent();
    unindent();
}

void ASTPrinter::visit(const ArrayAssignStmt& stmt) {
    line("ArrayAssignStmt:");
    indent();
    line("Target:");
    indent();
    print(stmt.target.get());
    unindent();
    line("Value:");
    indent();
    print(stmt.value.get());
    unindent();
    unindent();
}

void ASTPrinter::visit(const BlockStmt& stmt) {
    line("BlockStmt:");
    indent();
    for (const auto& s : stmt.statements) {
        print(s.get());
    }
    unindent();
}

void ASTPrinter::visit(const IfStmt& stmt) {
    line("IfStmt:");
    indent();
    line("Condition:");
    indent();
    print(stmt.condition.get());
    unindent();
    line("Then:");
    indent();
    print(stmt.then_branch.get());
    unindent();
    if (stmt.else_branch) {
        line("Else:");
        indent();
        print(stmt.else_branch.get());
        unindent();
    }
    unindent();
}

void ASTPrinter::visit(const WhileStmt& stmt) {
    line("WhileStmt:");
    indent();
    line("Condition:");
    indent();
    print(stmt.condition.get());
    unindent();
    line("Body:");
    indent();
    print(stmt.body.get());
    unindent();
    unindent();
}

void ASTPrinter::visit(const ReturnStmt& stmt) {
    line("ReturnStmt:");
    if (stmt.value) {
        indent();
        print(stmt.value.get());
        unindent();
    }
}

void ASTPrinter::visit(const LabelStmt& stmt) {
    line("LabelStmt: {}", stmt.name.value);
    if (stmt.symbol) {
        indent();
        line("Symbol: {}", stmt.symbol->name);
        unindent();
    }
}

void ASTPrinter::visit(const GotoStmt& stmt) {
    line("GotoStmt: {}", stmt.label.value);
    if (stmt.target_label_symbol) {
        indent();
        line("Target Symbol: {}", stmt.target_label_symbol->name);
        unindent();
    }
    if (stmt.condition) {
        indent();
        line("Condition:");
        indent();
        print(stmt.condition.get());
        unindent();
        unindent();
    }
}

void ASTPrinter::visit(const GlobalDecl& stmt) {
    std::string mode_name = std::string(enum_name(stmt.mode));
    line("GlobalDecl: mode={}", mode_name);
    if (stmt.mode == GlobalMode::Specific) {
        indent();
        for (const auto& g : stmt.globals) {
            line("Global: {}", g.value);
        }
        unindent();
    }
}

void ASTPrinter::visit(const FunctionDef& stmt) {
    line("FunctionDef: {}", stmt.name.value);
    if (stmt.symbol) {
        indent();
        line("Symbol: {}", stmt.symbol->name);
        unindent();
    }
    indent();
    if (!stmt.params.empty()) {
        line("Params:");
        indent();
        for (const auto& p : stmt.params) {
            std::string type_name = std::string(enum_name(p.type));
            line("Param: {} (type: {})", p.name.value, type_name);
        }
        unindent();
    }
    if (stmt.global_decl) {
        visit(*stmt.global_decl);
    }
    line("Body:");
    indent();
    if (stmt.body) {
        visit(*stmt.body);
    }
    unindent();
    unindent();
}

// Expr visitors
void ASTPrinter::visit(const NumberExpr& expr) {
    line("NumberExpr: {}", expr.value.value);
}

void ASTPrinter::visit(const VariableExpr& expr) {
    line("VariableExpr: {}", expr.name.value);
    if (expr.symbol) {
        indent();
        std::string type_name = std::string(enum_name(expr.symbol->type));
        line("Symbol: {} (type: {})", expr.symbol->name, type_name);
        unindent();
    }
}

void ASTPrinter::visit(const UnaryExpr& expr) {
    line("UnaryExpr: {}", expr.op.value);
    indent();
    print(expr.right.get());
    unindent();
}

void ASTPrinter::visit(const BinaryExpr& expr) {
    line("BinaryExpr: {}", expr.op.value);
    indent();
    line("Left:");
    indent();
    print(expr.left.get());
    unindent();
    line("Right:");
    indent();
    print(expr.right.get());
    unindent();
    unindent();
}

void ASTPrinter::visit(const TernaryExpr& expr) {
    line("TernaryExpr:");
    indent();
    line("Condition:");
    indent();
    print(expr.cond.get());
    unindent();
    line("True:");
    indent();
    print(expr.true_expr.get());
    unindent();
    line("False:");
    indent();
    print(expr.false_expr.get());
    unindent();
    unindent();
}

void ASTPrinter::visit(const CallExpr& expr) {
    line("CallExpr: {}", expr.callee);
    if (expr.resolved_signature != nullptr) {
        indent();
        line("Resolved to user function: {}", expr.resolved_signature->name);
        unindent();
    }
    if (expr.resolved_builtin != nullptr) {
        indent();
        line("Resolved to builtin: {}", expr.resolved_builtin->name);
        unindent();
    }
    if (expr.resolved_def != nullptr) {
        indent();
        line("Resolved def: {}", expr.resolved_def->name.value);
        unindent();
    }
    if (!expr.args.empty()) {
        indent();
        line("Args:");
        indent();
        for (const auto& arg : expr.args) {
            print(arg.get());
        }
        unindent();
        unindent();
    }
}

void ASTPrinter::visit(const PropAccessExpr& expr) {
    line("PropAccessExpr: {}.{}", expr.clip.value, expr.prop.value);
}

void ASTPrinter::visit(const StaticRelPixelAccessExpr& expr) {
    line("StaticRelPixelAccessExpr: {}[{},{}]{}", expr.clip.value,
         expr.offset_x.value, expr.offset_y.value, expr.boundary_suffix);
}

void ASTPrinter::visit(const FrameDimensionExpr& expr) {
    line("FrameDimensionExpr: {}.{}", "frame", expr.dimension_name);
    indent();
    line("Plane:");
    indent();
    print(expr.plane_index_expr.get());
    unindent();
    unindent();
}

void ASTPrinter::visit(const ArrayAccessExpr& expr) {
    line("ArrayAccessExpr:");
    if (expr.array_symbol) {
        indent();
        line("Array Symbol: {}", expr.array_symbol->name);
        unindent();
    }
    indent();
    line("Array:");
    indent();
    print(expr.array.get());
    unindent();
    line("Index:");
    indent();
    print(expr.index.get());
    unindent();
    unindent();
}

} // namespace infix2postfix
