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

#include "ReloopPass.hpp"
#include "../framework/AnalysisManager.hpp"
#include "BlockAnalysisPass.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <stack>

namespace analysis {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
struct Bitset {
    std::vector<uint64_t> words;
    int nbits = 0;

    explicit Bitset(int n = 0) : words((n + 63) / 64, 0), nbits(n) {}

    void setAll() {
        std::ranges::fill(words, ~uint64_t(0));
        int extra = int(words.size() * 64) - nbits;
        if (extra > 0 && !words.empty()) {
            words.back() &= (~uint64_t(0)) >> extra;
        }
    }

    void resetAll() { std::ranges::fill(words, 0); }

    void set(int i) { words[size_t(i) / 64] |= (uint64_t(1) << (i % 64)); }

    void reset(int i) { words[size_t(i) / 64] &= ~(uint64_t(1) << (i % 64)); }

    [[nodiscard]] bool test(int i) const {
        return (((words[size_t(i) / 64] >> (i % 64)) & 1U) != 0);
    }

    bool intersectWith(const Bitset& other) {
        bool changed = false;
        for (size_t i = 0; i < words.size(); ++i) {
            uint64_t nw = words[i] & other.words[i];
            changed |= (nw != words[i]);
            words[i] = nw;
        }
        return changed;
    }

    [[nodiscard]] bool equals(const Bitset& other) const {
        return words == other.words;
    }
};
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

std::vector<int> compute_reachable(const std::vector<CFGBlock>& cfg) {
    std::vector<int> reachable(cfg.size(), 0);
    if (cfg.empty()) {
        return reachable;
    }
    std::deque<int> q;
    reachable[0] = 1;
    q.push_back(0);
    while (!q.empty()) {
        int u = q.front();
        q.pop_front();
        for (int v : cfg[u].successors) {
            if (v >= 0 && static_cast<size_t>(v) < cfg.size() &&
                reachable[v] == 0) {
                reachable[v] = 1;
                q.push_back(v);
            }
        }
    }
    return reachable;
}

void compute_reducibility(ReloopResult& result,
                          const std::vector<CFGBlock>& cfg,
                          const std::vector<int>& reachable) {
    int n = (int)cfg.size();
    if (n == 0) {
        result.reducible = true;
        return;
    }

    std::vector<std::vector<int>> g(n);
    std::vector<std::vector<int>> gr(n);
    for (int u = 0; u < n; ++u) {
        if (reachable[u] == 0) {
            continue;
        }
        for (int v : cfg[u].successors) {
            if (v >= 0 && v < n && reachable[v] != 0) {
                g[u].push_back(v);
                gr[v].push_back(u);
            }
        }
    }

    // Kosaraju SCC
    std::vector<int> order;
    order.reserve((size_t)n);
    std::vector<int> vis(n, 0);
    for (int i = 0; i < n; ++i) {
        if (reachable[i] == 0 || vis[i] != 0) {
            continue;
        }
        std::stack<std::pair<int, size_t>> st;
        st.emplace(i, 0);
        vis[i] = 1;
        while (!st.empty()) {
            auto& [u, idx] = st.top();
            if (idx < g[u].size()) {
                int v = g[u][idx++];
                if (vis[v] == 0) {
                    vis[v] = 1;
                    st.emplace(v, 0);
                }
            } else {
                order.push_back(u);
                st.pop();
            }
        }
    }

    std::vector<int> comp(n, -1);
    int compCount = 0;
    for (int k = (int)order.size() - 1; k >= 0; --k) {
        int v0 = order[(size_t)k];
        if (comp[v0] != -1) {
            continue;
        }
        std::deque<int> q;
        q.push_back(v0);
        comp[v0] = compCount;
        while (!q.empty()) {
            int v = q.front();
            q.pop_front();
            for (int p : gr[v]) {
                if (comp[p] == -1) {
                    comp[p] = compCount;
                    q.push_back(p);
                }
            }
        }
        compCount++;
    }

    std::vector<std::vector<int>> nodes((size_t)compCount);
    for (int i = 0; i < n; ++i) {
        if (reachable[i] == 0) {
            continue;
        }
        if (comp[i] >= 0) {
            nodes[(size_t)comp[i]].push_back(i);
        }
    }

    for (int cid = 0; cid < compCount; ++cid) {
        const auto& scc = nodes[(size_t)cid];
        if (scc.empty()) {
            continue;
        }

        bool cyclic = false;
        if (scc.size() > 1) {
            cyclic = true;
        } else {
            int u = scc[0];
            for (int v : g[u]) {
                if (v == u) {
                    cyclic = true;
                    break;
                }
            }
        }
        if (!cyclic) {
            continue;
        }

        std::set<int> sccSet(scc.begin(), scc.end());
        std::set<int> entryFrom;
        for (int v : scc) {
            for (int p : gr[v]) {
                if (!sccSet.contains(p)) {
                    entryFrom.insert(p);
                }
            }
        }
        if (entryFrom.size() > 1) {
            result.reducible = false;
            result.success = false;
            return;
        }
    }

    result.reducible = true;
}

std::vector<Bitset> compute_dominators(const std::vector<CFGBlock>& cfg,
                                       const std::vector<int>& reachable) {
    int n = (int)cfg.size();
    std::vector<Bitset> dom;
    dom.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        dom.emplace_back(n);
    }
    if (n == 0) {
        return dom;
    }

    auto init = [&]() {
        for (int i = 0; i < n; ++i) {
            if (!reachable[i]) {
                dom[i].resetAll();
                dom[i].set(i);
            } else {
                dom[i].setAll();
            }
        }
        dom[0].resetAll();
        dom[0].set(0);
    };

    auto compute_for_node = [&](int i) -> Bitset {
        Bitset newDom(n);
        newDom.setAll();
        if (cfg[i].predecessors.empty()) {
            newDom.resetAll();
        } else {
            for (int p : cfg[i].predecessors) {
                if (!reachable[p]) {
                    continue;
                }
                newDom.intersectWith(dom[p]);
            }
        }
        newDom.set(i);
        return newDom;
    };

    init();

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 1; i < n; ++i) {
            if (reachable[i] == 0) {
                continue;
            }
            Bitset newDom = compute_for_node(i);
            if (!newDom.equals(dom[i])) {
                dom[i] = std::move(newDom);
                changed = true;
            }
        }
    }
    return dom;
}

// Post-dominators with a virtual exit node at index n.
std::vector<Bitset> compute_postdominators(const std::vector<CFGBlock>& cfg,
                                           const std::vector<int>& reachable) {
    int n = (int)cfg.size();
    int N = n + 1; // including virtual exit node

    std::vector<std::vector<int>> succ(N);
    std::vector<std::vector<int>> pred(N);

    auto build_reverse_graph = [&]() {
        for (int i = 0; i < n; ++i) {
            if (!reachable[i]) {
                continue;
            }
            for (int s : cfg[i].successors) {
                if (s >= 0 && s < n && reachable[s]) {
                    succ[i].push_back(s);
                    pred[s].push_back(i);
                }
            }
            if (succ[i].empty()) {
                succ[i].push_back(n);
                pred[n].push_back(i);
            }
        }
    };

    auto init = [&]() {
        std::vector<Bitset> pdom;
        pdom.reserve((size_t)N);
        for (int i = 0; i < N; ++i) {
            pdom.emplace_back(N);
        }
        for (int i = 0; i < N; ++i) {
            pdom[i].setAll();
        }
        pdom[n].resetAll();
        pdom[n].set(n);
        return pdom;
    };

    build_reverse_graph();

    std::vector<Bitset> pdom = init();

    auto compute_for_node = [&](int i) -> Bitset {
        Bitset newPdom(N);
        newPdom.setAll();
        for (int s : succ[i]) {
            newPdom.intersectWith(pdom[s]);
        }
        newPdom.set(i);
        return newPdom;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < n; ++i) {
            if (reachable[i] == 0) {
                continue;
            }
            Bitset newPdom = compute_for_node(i);
            if (!newPdom.equals(pdom[i])) {
                pdom[i] = std::move(newPdom);
                changed = true;
            }
        }
    }

    return pdom;
}

int compute_ipdom_for_node(int node, const std::vector<Bitset>& pdom) {
    // ipdom(node) is the closest strict post-dominator of node in the
    // post-dominator tree.
    int N = (int)pdom.size();
    // Collect strict post-dominators.
    std::vector<int> strict;
    strict.reserve((size_t)N);
    for (int i = 0; i < N; ++i) {
        if (i != node && pdom[node].test(i)) {
            strict.push_back(i);
        }
    }
    if (strict.empty()) {
        return -1;
    }

    // Choose the closest strict post-dominator.
    // For post-dominators, "A post-dominates B" iff A ∈ pdom[B].
    // The immediate post-dominator of `node` is the strict post-dominator that
    // does NOT post-dominate any other strict post-dominator of `node`.
    int best = -1;
    for (int c : strict) {
        bool postdominatesOther = false;
        for (int other : strict) {
            if (other == c) {
                continue;
            }
            // If c post-dominates `other`, then c cannot be the immediate
            // post-dominator of `node`
            if (pdom[other].test(c)) {
                postdominatesOther = true;
                break;
            }
        }
        if (!postdominatesOther) {
            best = c;
            break;
        }
    }
    return best;
}

std::vector<int> compute_ipdom_vector(const std::vector<Bitset>& pdom,
                                      int realNodeCount,
                                      const std::vector<int>& reachable) {
    std::vector<int> ipdom(realNodeCount, -1);
    int exitNode = realNodeCount;
    for (int i = 0; i < realNodeCount; ++i) {
        if (reachable[i] == 0) {
            continue;
        }
        int ip = compute_ipdom_for_node(i, pdom);
        ipdom[i] = (ip == exitNode) ? -1 : ip;
    }
    return ipdom;
}

void compute_natural_loops(ReloopResult& result,
                           const std::vector<CFGBlock>& cfg,
                           const std::vector<int>& reachable,
                           const std::vector<Bitset>& dom) {
    int n = (int)cfg.size();
    for (int u = 0; u < n; ++u) {
        if (reachable[u] == 0) {
            continue;
        }
        for (int h : cfg[u].successors) {
            if (h < 0 || h >= n || reachable[h] == 0) {
                continue;
            }
            if (!dom[u].test(h)) {
                continue;
            }
            auto& body = result.loop_body[h];
            std::deque<int> work;
            body.insert(h);
            if (!body.contains(u)) {
                body.insert(u);
                work.push_back(u);
            }
            while (!work.empty()) {
                int x = work.front();
                work.pop_front();
                for (int p : cfg[x].predecessors) {
                    if (reachable[p] == 0) {
                        continue;
                    }
                    if (!body.contains(p)) {
                        body.insert(p);
                        work.push_back(p);
                    }
                }
            }
        }
    }
}

void compute_loop_follow(ReloopResult& result) {
    for (auto& [header, body] : result.loop_body) {
        int f =
            (header >= 0 && static_cast<size_t>(header) < result.ipdom.size())
                ? result.ipdom[header]
                : -1;
        while (f != -1 && body.contains(f)) {
            f = result.ipdom[f];
        }
        result.loop_follow[header] = f;
    }
}

void compute_innermost_loop_header(ReloopResult& result,
                                   const std::vector<int>& reachable) {
    int n = (int)result.innermost_loop_header.size();
    for (int b = 0; b < n; ++b) {
        if (reachable[b] == 0) {
            continue;
        }
        int bestHeader = -1;
        size_t bestSize = std::numeric_limits<size_t>::max();
        for (const auto& [header, body] : result.loop_body) {
            if (body.contains(b) && body.size() < bestSize) {
                bestSize = body.size();
                bestHeader = header;
            }
        }
        result.innermost_loop_header[b] = bestHeader;
    }
}

} // namespace

ReloopPass::Result ReloopPass::run(const std::vector<Token>& /*unused*/,
                                   AnalysisManager& am) {
    const auto& block_result = am.getResult<BlockAnalysisPass>();
    const auto& cfg = block_result.cfg_blocks;

    Result result;
    result.ipdom.assign(cfg.size(), -1);
    result.innermost_loop_header.assign(cfg.size(), -1);

    if (cfg.empty()) {
        return result;
    }

    const auto reachable = compute_reachable(cfg);
    compute_reducibility(result, cfg, reachable);
    const auto dom = compute_dominators(cfg, reachable);
    const auto pdom = compute_postdominators(cfg, reachable);

    int n = (int)cfg.size();
    result.ipdom = compute_ipdom_vector(pdom, n, reachable);

    compute_natural_loops(result, cfg, reachable, dom);
    compute_loop_follow(result);
    compute_innermost_loop_header(result, reachable);

    return result;
}

} // namespace analysis
