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

#ifndef LLVMEXPR_FRONTEND_INFIX2POSTFIX_SYMBOLTABLE_HPP
#define LLVMEXPR_FRONTEND_INFIX2POSTFIX_SYMBOLTABLE_HPP

#include "Symbol.hpp"

#include <map>
#include <memory>
#include <string>

namespace infix2postfix {

class SymbolTable {
  public:
    explicit SymbolTable(SymbolTable* parent = nullptr);

    bool define(const std::shared_ptr<Symbol>& symbol);

    [[nodiscard]] std::shared_ptr<Symbol>
    resolve(const std::string& name) const;

    [[nodiscard]] SymbolTable* getParent() const { return parent; }

    [[nodiscard]] const std::map<std::string, std::shared_ptr<Symbol>>&
    getSymbols() const {
        return symbols;
    }

  private:
    SymbolTable* parent;
    std::map<std::string, std::shared_ptr<Symbol>> symbols;
};

} // namespace infix2postfix

#endif // LLVMEXPR_FRONTEND_INFIX2POSTFIX_SYMBOLTABLE_HPP
