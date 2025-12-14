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

#ifndef LLVMEXPR_INFIX2POSTFIX_TOKENIZER_HPP
#define LLVMEXPR_INFIX2POSTFIX_TOKENIZER_HPP

#include "types.hpp"

#include <map>
#include <string>
#include <vector>

namespace infix2postfix {

class Tokenizer {
  public:
    explicit Tokenizer(std::string source);
    std::vector<Token> tokenize();

  private:
    Token nextToken();
    [[nodiscard]] char peek(int offset = 0) const;
    char advance();
    void skipWhitespaceAndComments();
    [[nodiscard]] Token makeToken(TokenType type,
                                  const std::string& value = "") const;
    Token identifier();
    Token number();
    Token globalDeclaration();

    std::string source;
    size_t current = 0;
    size_t start = 0;
    int line = 1;
    int column = 1;
    int start_line = 1;
    int start_column = 1;

    static const std::map<std::string, TokenType> keywords;
};

} // namespace infix2postfix

#endif