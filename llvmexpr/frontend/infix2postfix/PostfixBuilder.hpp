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

#ifndef LLVMEXPR_FRONTEND_INFIX2POSTFIX_POSTFIXBUILDER_HPP
#define LLVMEXPR_FRONTEND_INFIX2POSTFIX_POSTFIXBUILDER_HPP

#include "types.hpp"

#include <string>
#include <vector>

namespace infix2postfix {

class PostfixBuilder {
  public:
    PostfixBuilder() = default;

    // General
    void addOp(TokenType type);
    void addUnaryOp(TokenType type);
    void addTernaryOp();
    void addFunctionCall(const std::string& func_name);
    void append(const PostfixBuilder& other);
    [[nodiscard]] std::string getExpression() const;
    void clear();
    [[nodiscard]] bool empty() const;

    // Literals & Variables
    void addNumber(const std::string& num_literal);
    void addConstant(const std::string& const_name);
    void addVariableLoad(const std::string& var_name);
    void addVariableStore(const std::string& var_name);

    // Control Flow
    void addLabel(const std::string& label_name);
    void addConditionalJump(const std::string& label_name);
    void addUnconditionalJump(const std::string& label_name);
    void prefixLabels(const std::string& prefix);

    // Data Access & I/O
    void addPropAccess(const std::string& clip_name,
                       const std::string& prop_name);
    void addPropExist(const std::string& clip_name,
                      const std::string& prop_name);
    void addSetProp(const std::string& prop_name, const std::string& suffix);
    void addDeleteProp(const std::string& prop_name);
    void addStaticPixelAccess(const std::string& clip_name,
                              const std::string& x, const std::string& y,
                              const std::string& suffix);
    void addDynPixelAccessExpr(const std::string& clip_name,
                               const std::string& suffix);
    void addDynPixelAccessSingle(const std::string& clip_name,
                                 const std::string& plane);
    void addStoreExpr();
    void addStoreSingle(const std::string& plane);
    void addFrameDimension(const std::string& dim, const std::string& plane);
    void addExitMarker();

    // Stack manipulation
    void addDropN(int count = 1);
    void addDupN(int count = 0);
    void addSwapN(int count = 1);
    void addSortN(int count);

    // Array operations
    void addArrayAllocStatic(const std::string& array_name,
                             const std::string& size);
    void addArrayAllocDynamic(const std::string& array_name);
    void addArrayLoad(const std::string& array_name);
    void addArrayStore(const std::string& array_name);

    // For raw/unstructured parts
    void addRaw(const std::string& raw_string);

  private:
    void pushToken(const std::string& token);
    std::vector<std::string> tokens;
};

} // namespace infix2postfix

#endif // LLVMEXPR_FRONTEND_INFIX2POSTFIX_POSTFIXBUILDER_HPP
