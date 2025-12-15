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

#include "StructurizeCFGPass.hpp"
#include "../framework/AnalysisManager.hpp"
#include "BlockAnalysisPass.hpp"
#include "StackSafetyPass.hpp"

#include "../utils/DynamicBitset.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <stack>
#include <vector>

namespace analysis {

namespace {

using Bitset = analysis::dynamic_bitset::DynamicBitset;

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

struct SCCDecomposition {
    std::vector<int> comp;               // node -> component id, -1 unreachable
    std::vector<std::vector<int>> nodes; // component id -> nodes
};

SCCDecomposition compute_scc_kosaraju(const std::vector<CFGBlock>& cfg,
                                      const std::vector<int>& reachable) {
    int n = (int)cfg.size();
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
    int comp_count = 0;
    for (int k = (int)order.size() - 1; k >= 0; --k) {
        int v0 = order[(size_t)k];
        if (comp[v0] != -1) {
            continue;
        }
        std::deque<int> q;
        q.push_back(v0);
        comp[v0] = comp_count;
        while (!q.empty()) {
            int v = q.front();
            q.pop_front();
            for (int p : gr[v]) {
                if (comp[p] == -1) {
                    comp[p] = comp_count;
                    q.push_back(p);
                }
            }
        }
        comp_count++;
    }

    std::vector<std::vector<int>> nodes((size_t)comp_count);
    for (int i = 0; i < n; ++i) {
        if (reachable[i] == 0 || comp[i] < 0) {
            continue;
        }
        nodes[(size_t)comp[i]].push_back(i);
    }

    return {.comp = std::move(comp), .nodes = std::move(nodes)};
}

bool scc_is_cyclic(const std::vector<CFGBlock>& cfg,
                   const std::vector<int>& reachable,
                   const std::vector<int>& scc_nodes) {
    if (scc_nodes.empty()) {
        return false;
    }
    if (scc_nodes.size() > 1) {
        return true;
    }
    int u = scc_nodes[0];
    if (reachable[u] == 0) {
        return false;
    }
    return std::ranges::any_of(cfg[u].successors,
                               [u](int v) { return v == u; });
}

std::vector<int> scc_entry_nodes(const std::vector<CFGBlock>& cfg,
                                 const std::vector<int>& reachable,
                                 const SCCDecomposition& scc, int cid) {
    std::vector<int> entry;
    for (int v : scc.nodes[(size_t)cid]) {
        if (reachable[v] == 0) {
            continue;
        }
        for (int p : cfg[v].predecessors) {
            if (p < 0 || static_cast<size_t>(p) >= cfg.size()) {
                continue;
            }
            if (reachable[p] == 0) {
                continue;
            }
            if (scc.comp[p] != cid) {
                entry.push_back(v);
                break;
            }
        }
    }
    std::ranges::sort(entry);
    auto ret = std::ranges::unique(entry);
    entry.erase(ret.begin(), ret.end());
    return entry;
}

bool check_reducible(const std::vector<CFGBlock>& cfg,
                     const std::vector<int>& reachable) {
    int n = (int)cfg.size();
    if (n == 0) {
        return true;
    }

    auto scc = compute_scc_kosaraju(cfg, reachable);
    for (size_t cid = 0; cid < scc.nodes.size(); ++cid) {
        const auto& nodes = scc.nodes[cid];
        if (!scc_is_cyclic(cfg, reachable, nodes)) {
            continue;
        }
        // Reducible iff the SCC has at most one entry node.
        auto entry_nodes = scc_entry_nodes(cfg, reachable, scc, (int)cid);
        if (entry_nodes.size() > 1) {
            return false;
        }
    }
    return true;
}

void rebuild_predecessors(std::vector<CFGBlock>& cfg) {
    for (auto& b : cfg) {
        b.predecessors.clear();
    }
    for (size_t u = 0; u < cfg.size(); ++u) {
        for (int v : cfg[u].successors) {
            if (v >= 0 && static_cast<size_t>(v) < cfg.size()) {
                cfg[(size_t)v].predecessors.push_back((int)u);
            }
        }
    }
}

bool node_split_make_reducible(std::vector<CFGBlock>& cfg,
                               std::vector<int>& origin_map,
                               size_t max_blocks) {
    if (cfg.empty()) {
        return false;
    }

    bool changed_any = false;
    // Iterate because splitting one SCC can expose/alter others.
    // Bound iterations to avoid pathological cases.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
    for (int iter = 0; iter < 128; ++iter) {
        rebuild_predecessors(cfg);
        auto reachable = compute_reachable(cfg);
        auto scc = compute_scc_kosaraju(cfg, reachable);

        bool changed_this_iter = false;
        for (size_t cid = 0; cid < scc.nodes.size(); ++cid) {
            const auto& comp_nodes = scc.nodes[cid];
            if (!scc_is_cyclic(cfg, reachable, comp_nodes)) {
                continue;
            }
            auto entry_nodes = scc_entry_nodes(cfg, reachable, scc, (int)cid);
            if (entry_nodes.size() <= 1) {
                continue;
            }

            // Heuristic: keep the first entry node as the "primary" entry of
            // the original SCC, and duplicate the SCC for each other entry.
            int primary_entry = entry_nodes.front();
            (void)primary_entry;

            std::vector<uint8_t> in_component(cfg.size(), 0);
            for (int v : comp_nodes) {
                if (v >= 0 && static_cast<size_t>(v) < in_component.size()) {
                    in_component[(size_t)v] = 1;
                }
            }

            int original_node_count = (int)cfg.size();

            for (size_t ei = 1; ei < entry_nodes.size(); ++ei) {
                int entry = entry_nodes[ei];

                if (cfg.size() + comp_nodes.size() > max_blocks) {
                    return changed_any;
                }

                std::vector<int> clone_of((size_t)original_node_count, -1);

                // Create clones for all nodes in this SCC.
                for (int v : comp_nodes) {
                    CFGBlock nb = cfg[(size_t)v];
                    nb.successors.clear();
                    nb.predecessors.clear();

                    int new_idx = (int)cfg.size();
                    cfg.push_back(std::move(nb));
                    origin_map.push_back(origin_map[(size_t)v]);
                    if (v >= 0 && v < original_node_count) {
                        clone_of[(size_t)v] = new_idx;
                    }
                }

                // Wire clone successors (internal edges are remapped to clones,
                // external edges keep their target).
                for (int v : comp_nodes) {
                    int v_clone = clone_of[(size_t)v];
                    if (v_clone < 0) {
                        continue;
                    }
                    for (int s : cfg[(size_t)v].successors) {
                        if (s >= 0 && s < original_node_count &&
                            in_component[(size_t)s] != 0) {
                            cfg[(size_t)v_clone].successors.push_back(
                                clone_of[(size_t)s]);
                        } else {
                            cfg[(size_t)v_clone].successors.push_back(s);
                        }
                    }
                }

                // Redirect all external incoming edges to `entry` to the clone.
                int entry_clone = clone_of[(size_t)entry];
                for (int p = 0; p < original_node_count; ++p) {
                    if (reachable[(size_t)p] == 0) {
                        continue;
                    }
                    if (in_component[(size_t)p] != 0) {
                        continue; // keep SCC-internal edges pointing to original
                    }
                    for (int& s : cfg[(size_t)p].successors) {
                        if (s == entry) {
                            s = entry_clone;
                        }
                    }
                }
            }

            changed_this_iter = true;
            changed_any = true;
            break; // CFG changed; restart SCC discovery
        }

        if (!changed_this_iter) {
            break;
        }
    }
    return changed_any;
}

bool tail_duplicate_trivial_joins(std::vector<CFGBlock>& cfg,
                                  std::vector<int>& origin_map,
                                  size_t max_blocks) {
    if (cfg.empty()) {
        return false;
    }

    rebuild_predecessors(cfg);
    auto reachable = compute_reachable(cfg);
    auto scc = compute_scc_kosaraju(cfg, reachable);

    std::vector<uint8_t> cyclic(cfg.size(), 0);
    for (const auto& comp_nodes : scc.nodes) {
        if (!scc_is_cyclic(cfg, reachable, comp_nodes)) {
            continue;
        }
        for (int v : comp_nodes) {
            if (v >= 0 && static_cast<size_t>(v) < cyclic.size()) {
                cyclic[(size_t)v] = 1;
            }
        }
    }

    bool changed = false;
    int original_n = (int)cfg.size();

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
    constexpr int K_MAX_TRIVIAL_TOKENS = 6;

    for (int c = 1; c < original_n; ++c) {
        if (reachable[(size_t)c] == 0) {
            continue;
        }
        if (cyclic[(size_t)c] != 0) {
            continue;
        }
        if (cfg[(size_t)c].successors.size() > 1) {
            continue;
        }
        if (cfg[(size_t)c].predecessors.size() < 2) {
            continue;
        }
        int tok_count =
            cfg[(size_t)c].end_token_idx - cfg[(size_t)c].start_token_idx;
        if (tok_count > K_MAX_TRIVIAL_TOKENS) {
            continue;
        }

        auto preds = cfg[(size_t)c].predecessors;
        std::ranges::sort(preds);
        auto ret = std::ranges::unique(preds);
        preds.erase(ret.begin(), ret.end());

        for (int p : preds) {
            if (cfg.size() + 1 > max_blocks) {
                rebuild_predecessors(cfg);
                return changed;
            }

            CFGBlock clone = cfg[(size_t)c];
            clone.predecessors.clear();

            int clone_idx = (int)cfg.size();
            cfg.push_back(std::move(clone));
            origin_map.push_back(origin_map[(size_t)c]);

            for (int& s : cfg[(size_t)p].successors) {
                if (s == c) {
                    s = clone_idx;
                }
            }
        }

        changed = true;
    }

    rebuild_predecessors(cfg);
    return changed;
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
        Bitset new_dom(n);
        new_dom.setAll();
        if (cfg[i].predecessors.empty()) {
            new_dom.resetAll();
        } else {
            for (int p : cfg[i].predecessors) {
                if (!reachable[p]) {
                    continue;
                }
                new_dom.intersectWith(dom[p]);
            }
        }
        new_dom.set(i);
        return new_dom;
    };

    init();

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 1; i < n; ++i) {
            if (reachable[i] == 0) {
                continue;
            }
            Bitset new_dom = compute_for_node(i);
            if (!new_dom.equals(dom[i])) {
                dom[i] = std::move(new_dom);
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
    int node_count = n + 1; // including virtual exit node

    std::vector<std::vector<int>> succ((size_t)node_count);
    std::vector<std::vector<int>> pred((size_t)node_count);

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
        pdom.reserve((size_t)node_count);
        for (int i = 0; i < node_count; ++i) {
            pdom.emplace_back(node_count);
        }
        for (int i = 0; i < node_count; ++i) {
            pdom[i].setAll();
        }
        pdom[n].resetAll();
        pdom[n].set(n);
        return pdom;
    };

    build_reverse_graph();

    std::vector<Bitset> pdom = init();

    auto compute_for_node = [&](int i) -> Bitset {
        Bitset new_pdom(node_count);
        new_pdom.setAll();
        for (int s : succ[i]) {
            new_pdom.intersectWith(pdom[s]);
        }
        new_pdom.set(i);
        return new_pdom;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < n; ++i) {
            if (reachable[i] == 0) {
                continue;
            }
            Bitset new_pdom = compute_for_node(i);
            if (!new_pdom.equals(pdom[i])) {
                pdom[i] = std::move(new_pdom);
                changed = true;
            }
        }
    }

    return pdom;
}

int compute_ipdom_for_node(int node, const std::vector<Bitset>& pdom) {
    // ipdom(node) is the closest strict post-dominator of node in the
    // post-dominator tree.
    int node_count = static_cast<int>(pdom.size());
    // Collect strict post-dominators.
    std::vector<int> strict;
    strict.reserve((size_t)node_count);
    for (int i = 0; i < node_count; ++i) {
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
        bool postdominates_other = false;
        for (int other : strict) {
            if (other == c) {
                continue;
            }
            // If c post-dominates `other`, then c cannot be the immediate
            // post-dominator of `node`
            if (pdom[other].test(c)) {
                postdominates_other = true;
                break;
            }
        }
        if (!postdominates_other) {
            best = c;
            break;
        }
    }
    return best;
}

std::vector<int> compute_ipdom_vector(const std::vector<Bitset>& pdom,
                                      int real_node_count,
                                      const std::vector<int>& reachable) {
    std::vector<int> ipdom(real_node_count, -1);
    int exit_node = real_node_count;
    for (int i = 0; i < real_node_count; ++i) {
        if (reachable[i] == 0) {
            continue;
        }
        int ip = compute_ipdom_for_node(i, pdom);
        ipdom[i] = (ip == exit_node) ? -1 : ip;
    }
    return ipdom;
}

void compute_natural_loops(StructurizeCFGResult& result,
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

void compute_loop_follow(StructurizeCFGResult& result) {
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

void compute_innermost_loop_header(StructurizeCFGResult& result,
                                   const std::vector<int>& reachable) {
    int n = (int)result.innermost_loop_header.size();
    for (int b = 0; b < n; ++b) {
        if (reachable[b] == 0) {
            continue;
        }
        int best_header = -1;
        size_t best_size = std::numeric_limits<size_t>::max();
        for (const auto& [header, body] : result.loop_body) {
            if (body.contains(b) && body.size() < best_size) {
                best_size = body.size();
                best_header = header;
            }
        }
        result.innermost_loop_header[b] = best_header;
    }
}

} // namespace

StructurizeCFGPass::Result
StructurizeCFGPass::run(const std::vector<Token>& /*unused*/,
                        AnalysisManager& am) {
    const auto& block_result = am.getResult<BlockAnalysisPass>();
    const auto& cfg = block_result.cfg_blocks;
    const auto& stack_safety = am.getResult<StackSafetyPass>();

    Result result;

    if (cfg.empty()) {
        return result;
    }

    const auto reachable0 = compute_reachable(cfg);
    const bool reducible0 = check_reducible(cfg, reachable0);

    const std::vector<CFGBlock>* analysis_cfg = &cfg;

    std::vector<CFGBlock> work_cfg = cfg;
    std::vector<int> origin_map(cfg.size());
    for (size_t i = 0; i < cfg.size(); ++i) {
        origin_map[i] = static_cast<int>(i);
    }

    constexpr size_t MAX_BLOCKS = 256;
    constexpr size_t MAX_BLOCKS_RATIO = 8;
    size_t max_blocks =
        std::max<size_t>(cfg.size() * MAX_BLOCKS_RATIO, MAX_BLOCKS);

    bool changed =
        tail_duplicate_trivial_joins(work_cfg, origin_map, max_blocks);
    if (!reducible0) {
        changed |= node_split_make_reducible(work_cfg, origin_map, max_blocks);
    }

    if (changed) {
        result.structured_cfg_blocks = std::move(work_cfg);
        result.structured_block_origin = std::move(origin_map);
        analysis_cfg = &result.structured_cfg_blocks;

        result.structured_stack_depth_in.resize(
            result.structured_cfg_blocks.size(), -1);
        for (size_t i = 0; i < result.structured_cfg_blocks.size(); ++i) {
            int orig = result.structured_block_origin[i];
            if (orig >= 0 && static_cast<size_t>(orig) <
                                 stack_safety.stack_depth_in.size()) {
                result.structured_stack_depth_in[i] =
                    stack_safety.stack_depth_in[(size_t)orig];
            }
        }
    }

    const auto reachable = compute_reachable(*analysis_cfg);
    result.reducible = check_reducible(*analysis_cfg, reachable);
    result.success = result.reducible;

    result.ipdom.assign(analysis_cfg->size(), -1);
    result.innermost_loop_header.assign(analysis_cfg->size(), -1);

    const auto dom = compute_dominators(*analysis_cfg, reachable);
    const auto pdom = compute_postdominators(*analysis_cfg, reachable);

    int n = (int)analysis_cfg->size();
    result.ipdom = compute_ipdom_vector(pdom, n, reachable);

    compute_natural_loops(result, *analysis_cfg, reachable, dom);
    compute_loop_follow(result);
    compute_innermost_loop_header(result, reachable);

    return result;
}

} // namespace analysis
