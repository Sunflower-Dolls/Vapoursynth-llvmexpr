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

#ifndef LLVMEXPR_ANALYSIS_UTILS_DYNAMIC_BITSET_HPP
#define LLVMEXPR_ANALYSIS_UTILS_DYNAMIC_BITSET_HPP

#include <algorithm>
#include <cstdint>
#include <vector>

namespace analysis::dynamic_bitset {

class DynamicBitset {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
    static constexpr int BITS_PER_WORD = 64;

    std::vector<std::uint64_t> words;
    int nbits = 0;

    void maskUnusedBits() noexcept {
        if (nbits <= 0 || words.empty()) {
            return;
        }
        const int used_in_last = nbits % BITS_PER_WORD;
        if (used_in_last == 0) {
            return;
        }
        const std::uint64_t mask =
            (std::uint64_t(1) << static_cast<unsigned>(used_in_last)) - 1U;
        words.back() &= mask;
    }

  public:
    explicit DynamicBitset(int n = 0)
        : words((static_cast<size_t>(n) + (BITS_PER_WORD - 1)) / BITS_PER_WORD,
                0),
          nbits(n) {}

    void setAll() noexcept {
        std::ranges::fill(words, ~std::uint64_t(0));
        maskUnusedBits();
    }

    void resetAll() noexcept { std::ranges::fill(words, 0); }

    void set(int i) noexcept {
        words[static_cast<size_t>(i) / BITS_PER_WORD] |=
            (std::uint64_t(1) << static_cast<unsigned>(i % BITS_PER_WORD));
    }

    void reset(int i) noexcept {
        words[static_cast<size_t>(i) / BITS_PER_WORD] &=
            ~(std::uint64_t(1) << static_cast<unsigned>(i % BITS_PER_WORD));
    }

    [[nodiscard]] bool test(int i) const noexcept {
        return (((words[static_cast<size_t>(i) / BITS_PER_WORD] >>
                  static_cast<unsigned>(i % BITS_PER_WORD)) &
                 1U) != 0);
    }

    bool intersectWith(const DynamicBitset& other) noexcept {
        bool changed = false;

        if (other.words.size() == words.size()) {
            for (size_t i = 0; i < words.size(); ++i) {
                const std::uint64_t nw = words[i] & other.words[i];
                changed |= (nw != words[i]);
                words[i] = nw;
            }
            maskUnusedBits();
            return changed;
        }

        const size_t n = std::min(words.size(), other.words.size());
        for (size_t i = 0; i < n; ++i) {
            const std::uint64_t nw = words[i] & other.words[i];
            changed |= (nw != words[i]);
            words[i] = nw;
        }
        for (size_t i = n; i < words.size(); ++i) {
            changed |= (words[i] != 0);
            words[i] = 0;
        }
        maskUnusedBits();
        return changed;
    }

    [[nodiscard]] bool equals(const DynamicBitset& other) const noexcept {
        return nbits == other.nbits && words == other.words;
    }
};

} // namespace analysis::dynamic_bitset

#endif // LLVMEXPR_ANALYSIS_UTILS_DYNAMIC_BITSET_HPP
