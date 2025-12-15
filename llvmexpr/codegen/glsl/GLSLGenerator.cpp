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

#include "GLSLGenerator.hpp"
#include "../Sorting.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <numbers>
#include <ranges>
#include <stdexcept>

GLSLGenerator::GLSLGenerator(
    const std::vector<Token>& tokens, int num_inputs,
    [[maybe_unused]] int width, [[maybe_unused]] int height,
    bool mirror_boundary,
    const std::map<std::pair<int, std::string>, int>& prop_map,
    const analysis::ExpressionAnalysisResults& analysis_results)
    : tokens(tokens), num_inputs(num_inputs), mirror_boundary(mirror_boundary),
      prop_map(prop_map), analysis(analysis_results) {

    const auto& var_result = analysis.getVariableUsageResult();
    for (const auto& var_name : var_result.all_vars) {
        user_variables.insert(var_name);
    }

#ifndef NDEBUG
    if (const char* env = std::getenv("LLVMEXPR_GLSL_STRUCTURIZECFG_DEBUG")) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        debug_structurize_cfg = (env[0] != '\0' && env[0] != '0');
    }
#endif
}

void GLSLGenerator::emit(const std::string& code) { out << code; }

void GLSLGenerator::emitLine(const std::string& code) {
    for (int i = 0; i < indent_level; ++i) {
        out << "    ";
    }
    out << code << "\n";
}

void GLSLGenerator::emitNewline() { out << "\n"; }

void GLSLGenerator::indent() { ++indent_level; }

void GLSLGenerator::dedent() {
    if (indent_level > 0) {
        --indent_level;
    }
}

void GLSLGenerator::debugEmitCfgComment() {
#ifdef NDEBUG
    return;
#else
    if (!debug_structurize_cfg) {
        return;
    }
    const auto& cfg = getCodegenCfgBlocks();
    const auto& structurize = analysis.getStructurizeCFGResult();

    emitLine("// --- llvmexpr GLSL StructurizeCFG debug ---");
    emitLine(std::format("// blocks = {}, structurize.success = {}", cfg.size(),
                         structurize.success ? 1 : 0));
    for (size_t i = 0; i < cfg.size(); ++i) {
        const auto& b = cfg[i];
        std::string succs;
        for (size_t j = 0; j < b.successors.size(); ++j) {
            succs += std::format("{}{}", b.successors[j],
                                 (j + 1 == b.successors.size()) ? "" : ",");
        }
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
        int ip = (i < structurize.ipdom.size()) ? structurize.ipdom[i] : -999;
        emitLine(std::format("// B{}: [{}..{}) succ=[{}] ipdom={}", i,
                             b.start_token_idx, b.end_token_idx, succs, ip));
    }
    for (const auto& [hdr, follow] : structurize.loop_follow) {
        emitLine(std::format("// loop header {} follow {}", hdr, follow));
    }
    emitLine("// --- end structurize debug ---");
#endif
}

const std::vector<analysis::CFGBlock>&
GLSLGenerator::getCodegenCfgBlocks() const {
    const auto& structurize = analysis.getStructurizeCFGResult();
    if (!structurize.structured_cfg_blocks.empty()) {
        return structurize.structured_cfg_blocks;
    }
    return analysis.getCFGBlocks();
}

const std::vector<int>& GLSLGenerator::getCodegenStackDepthIn() const {
    const auto& structurize = analysis.getStructurizeCFGResult();
    if (!structurize.structured_stack_depth_in.empty()) {
        return structurize.structured_stack_depth_in;
    }
    return analysis.getStackDepthIn();
}

int GLSLGenerator::computeBranchJoin(int t, int f, int stop_block) const {
    const auto& structurize = analysis.getStructurizeCFGResult();
    int join = lcaPostdom(t, f, structurize.ipdom);
    if (join == -1 && stop_block != -1) {
        join = stop_block;
    }
    return join;
}

std::string GLSLGenerator::getLoopBreakFlag(int header) {
    auto it = loop_break_flags.find(header);
    if (it != loop_break_flags.end()) {
        return it->second;
    }
    std::string name = std::format("_brk_{}", break_flag_counter++);
    loop_break_flags.emplace(header, name);
    return name;
}

std::optional<int>
GLSLGenerator::findEnclosingLoopForFollow(int target_block,
                                          const LoopContext& loop_ctx) const {
    const auto& structurize = analysis.getStructurizeCFGResult();
    for (int header : loop_ctx.header_stack | std::views::reverse) {
        auto fit = structurize.loop_follow.find(header);
        if (fit != structurize.loop_follow.end() &&
            fit->second == target_block) {
            return header;
        }
    }
    return std::nullopt;
}

void GLSLGenerator::emitUnwindBreakIfNeeded(const LoopContext& loop_ctx) {
    std::string cond;
    if (structured_exit_enabled) {
        cond = "_llvmexpr_exit";
    }
    for (int header : loop_ctx.header_stack) {
        auto it = loop_break_flags.find(header);
        if (it == loop_break_flags.end()) {
            continue;
        }
        if (!cond.empty()) {
            cond += " || ";
        }
        cond += it->second;
    }
    if (cond.empty()) {
        return;
    }

    emitLine(std::format("if ({}) {{", cond));
    indent();
    emitLine("break;");
    dedent();
    emitLine("}");
}

std::string GLSLGenerator::newTemp() {
    return std::format("t_{}", temp_counter++);
}

std::string GLSLGenerator::newSlot() {
    return std::format("s_{}", slot_counter++);
}

std::string GLSLGenerator::pop() {
    if (stack.empty()) {
        throw std::runtime_error("GLSLGenerator: stack underflow");
    }
    std::string val = stack.back();
    stack.pop_back();
    return val;
}

void GLSLGenerator::push(const std::string& val) { stack.push_back(val); }

std::string GLSLGenerator::peek(int offset) const {
    if (static_cast<size_t>(offset) >= stack.size()) {
        throw std::runtime_error("GLSLGenerator: invalid stack peek");
    }
    return stack[stack.size() - 1 - offset];
}

std::string GLSLGenerator::floatLiteral(double val) {
    if (std::isnan(val)) {
        return "(0.0 / 0.0)"; // NaN
    }
    if (std::isinf(val)) {
        return val > 0 ? "(1.0 / 0.0)" : "(-1.0 / 0.0)";
    }
    std::string s = std::format("{:.10g}", val);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) {
        s += ".0";
    }
    return s;
}

std::string GLSLGenerator::binaryOp(const std::string& op) {
    std::string b = pop();
    std::string a = pop();
    std::string temp = newTemp();
    emitLine(std::format("float {} = {} {} {};", temp, a, op, b));
    return temp;
}

std::string GLSLGenerator::binaryCmp(const std::string& op) {
    std::string b = pop();
    std::string a = pop();
    std::string temp = newTemp();
    emitLine(std::format("float {} = ({} {} {}) ? 1.0 : 0.0;", temp, a, op, b));
    return temp;
}

std::string GLSLGenerator::unaryFn(const std::string& fn) {
    std::string a = pop();
    std::string temp = newTemp();
    emitLine(std::format("float {} = {}({});", temp, fn, a));
    return temp;
}

std::string GLSLGenerator::binaryFn(const std::string& fn) {
    std::string b = pop();
    std::string a = pop();
    std::string temp = newTemp();
    emitLine(std::format("float {} = {}({}, {});", temp, fn, a, b));
    return temp;
}

std::string GLSLGenerator::emitClampCoord(const std::string& coord,
                                          const std::string& max_dim) {
    std::string temp = newTemp();
    emitLine(
        std::format("int {} = clamp({}, 0, {} - 1);", temp, coord, max_dim));
    return temp;
}

std::string GLSLGenerator::emitMirrorCoord(const std::string& coord,
                                           const std::string& max_dim) {
    std::string temp = newTemp();
    emitLine(std::format("int {};", temp));
    emitLine("{");
    indent();
    emitLine(std::format("int _period = 2 * ({});", max_dim));
    emitLine(
        std::format("int _mod = int(mod(float({}), float(_period)));", coord));
    emitLine(std::format("if (_mod >= ({})) {{ {} = _period - 1 - _mod; }}",
                         max_dim, temp));
    emitLine(std::format("else {{ {} = _mod; }}", temp));
    dedent();
    emitLine("}");
    return temp;
}

std::string GLSLGenerator::emitFinalCoord(const std::string& coord,
                                          const std::string& max_dim,
                                          bool use_mirror) {
    if (use_mirror) {
        return emitMirrorCoord(coord, max_dim);
    }
    return emitClampCoord(coord, max_dim);
}

std::string GLSLGenerator::emitPixelIndex(const std::string& x,
                                          const std::string& y) {
    std::string temp = newTemp();
    emitLine(
        std::format("uint {} = uint({}) + uint({}) * pc.width;", temp, x, y));
    return temp;
}

std::string GLSLGenerator::emitPixelLoad(int clip_idx, const std::string& x,
                                         const std::string& y,
                                         bool use_mirror) {
    std::string final_x = emitFinalCoord(x, "int(pc.width)", use_mirror);
    std::string final_y = emitFinalCoord(y, "int(pc.height)", use_mirror);
    std::string idx = emitPixelIndex(final_x, final_y);
    std::string temp = newTemp();
    emitLine(std::format("float {} = src{}.data[{}];", temp, clip_idx, idx));
    return temp;
}

std::string GLSLGenerator::generate() {
    out.str("");
    out.clear();
    indent_level = 0;
    temp_counter = 0;
    slot_counter = 0;
    break_flag_counter = 0;
    loop_break_flags.clear();
    stack.clear();

    emitHeader();
    emitBufferDeclarations();
    emitHelperFunctions();
    emitMainFunction();

    return out.str();
}

void GLSLGenerator::emitHeader() {
    emitLine("#version 450");
    emitLine("#extension GL_EXT_scalar_block_layout : enable");
    emitNewline();
    emitLine(
        "layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;");
    emitNewline();
    emitLine("layout(push_constant) uniform PushConstants {");
    indent();
    emitLine("uint width;");
    emitLine("uint height;");
    emitLine("uint numInputs;");
    emitLine("int frameNumber;");
    dedent();
    emitLine("} pc;");
    emitNewline();
}

void GLSLGenerator::emitBufferDeclarations() {
    // Input buffers
    for (int i = 0; i < num_inputs; ++i) {
        emitLine(std::format("layout(std430, set = 0, binding = {}) readonly "
                             "buffer InputBuffer{} {{",
                             i, i));
        indent();
        emitLine("float data[];");
        dedent();
        emitLine(std::format("}} src{};", i));
        emitNewline();
    }

    // Output buffer
    emitLine(std::format("layout(std430, set = 0, binding = {}) writeonly "
                         "buffer OutputBuffer {{",
                         num_inputs));
    indent();
    emitLine("float data[];");
    dedent();
    emitLine("} dst;");
    emitNewline();

    // Props buffer
    emitLine(std::format(
        "layout(std430, set = 0, binding = {}) readonly buffer PropsBuffer {{",
        num_inputs + 1));
    indent();
    emitLine("float props[];");
    dedent();
    emitLine("} propsData;");
    emitNewline();
}

void GLSLGenerator::emitHelperFunctions() {
    // Currently no helper functions needed
    // May add in the future for complex operations
}

void GLSLGenerator::emitVariableDeclarations() {
    // User-defined variables
    for (const auto& var_name : user_variables) {
        emitLine(std::format("float u_{};", var_name));
    }

    // TODO: Move to analysis/
    for (const auto& token : tokens) {
        if (token.type == TokenType::ArrayAllocStatic) {
            const auto& payload = std::get<TokenPayloadArrayOp>(token.payload);
            if (!arrays.contains(payload.name)) {
                arrays[payload.name] = payload.static_size;
                emitLine(std::format("float a_{}[{}];", payload.name,
                                     payload.static_size));
            }
        }
    }

    // Stack slot variables for merge points
    const auto& cfg_blocks = getCodegenCfgBlocks();
    const auto& stack_depth_in = getCodegenStackDepthIn();
    const auto& structurize = analysis.getStructurizeCFGResult();

    std::set<int> force_slots;
    for (const auto& [header, follow] : structurize.loop_follow) {
        if (follow != -1) {
            force_slots.insert(follow);
        }
    }

    for (size_t i = 0; i < cfg_blocks.size(); ++i) {
        if (cfg_blocks[i].predecessors.size() > 1 ||
            force_slots.contains((int)i)) {
            int depth = stack_depth_in[i];
            std::vector<std::string> slots;
            for (int j = 0; j < depth; ++j) {
                std::string slot = newSlot();
                emitLine(std::format("float {};", slot));
                slots.push_back(slot);
            }
            block_entry_stack[static_cast<int>(i)] = slots;
        }
    }
}

void GLSLGenerator::emitMainFunction() {
    emitLine("void main() {");
    indent();

    emitLine("uint gid = gl_GlobalInvocationID.x;");
    emitLine("uint totalPixels = pc.width * pc.height;");
    emitNewline();
    emitLine("if (gid >= totalPixels) {");
    indent();
    emitLine("return;");
    dedent();
    emitLine("}");
    emitNewline();

    emitLine("int X = int(gid % pc.width);");
    emitLine("int Y = int(gid / pc.width);");
    emitNewline();

    emitVariableDeclarations();
    emitNewline();

    debugEmitCfgComment();
    emitMainFunctionStructured();

    dedent();
    emitLine("}");
}

void GLSLGenerator::emitMainFunctionStateMachine() {
    const auto& cfg_blocks = getCodegenCfgBlocks();

    emitLine("int _state = 0;");
    emitLine("float _result = 0.0;");
    emitNewline();
    emitLine("while (_state != -1) {");
    indent();
    emitLine("switch (_state) {");

    for (size_t i = 0; i < cfg_blocks.size(); ++i) {
        emitLine(std::format("case {}:", i));
        indent();

        if (block_entry_stack.contains(static_cast<int>(i))) {
            stack = block_entry_stack[static_cast<int>(i)];
        }

        emitBlockCode(static_cast<int>(i));

        const auto& block = cfg_blocks[i];
        if (block.successors.empty()) {
            if (!stack.empty()) {
                std::string result = pop();
                emitLine(std::format("_result = {};", result));
            }
            emitLine("_state = -1;");
        } else if (block.successors.size() == 1) {
            int next = block.successors[0];
            emitStackToEntrySlots(next);
            emitLine(std::format("_state = {};", next));
        } else {
            std::string cond = pop();
            int true_target = block.successors[0];
            int false_target = block.successors[1];
            emitStackToEntrySlots(true_target);
            emitStackToEntrySlots(false_target);
            emitLine(std::format("_state = ({} > 0.0) ? {} : {};", cond,
                                 true_target, false_target));
        }

        emitLine("break;");
        dedent();
    }

    emitLine("}"); // end switch
    dedent();
    emitLine("}"); // end while
    emitNewline();

    emitLine("if (floatBitsToUint(_result) != 0x7FC0E71Fu) {");
    indent();
    emitLine("dst.data[gid] = _result;");
    dedent();
    emitLine("}");
}

void GLSLGenerator::emitSetResultAndExit(const std::string& result_expr) {
    emitLine(std::format("_result = {};", result_expr));
    emitLine("_llvmexpr_exit = true;");
    emitLine("break;");
}

void GLSLGenerator::emitResultEpilogueStore() {
    emitLine("if (floatBitsToUint(_result) != 0x7FC0E71Fu) {");
    indent();
    emitLine("dst.data[gid] = _result;");
    dedent();
    emitLine("}");
    emitLine("return;");
}

bool GLSLGenerator::isLoopHeaderActive(int header,
                                       const LoopContext& loop_ctx) const {
    return std::ranges::find(loop_ctx.header_stack, header) !=
           loop_ctx.header_stack.end();
}

bool GLSLGenerator::canEdgeToBlock(int target_block, int stop_block,
                                   LoopContext& loop_ctx) const {
    const auto& structurize = analysis.getStructurizeCFGResult();
    int current_header =
        loop_ctx.header_stack.empty() ? -1 : loop_ctx.header_stack.back();

    if (target_block == stop_block) {
        if (current_header == -1) {
            return true;
        }
        int follow = -1;
        auto it = structurize.loop_follow.find(current_header);
        if (it != structurize.loop_follow.end()) {
            follow = it->second;
        }
        if (stop_block == follow) {
            return true;
        }
        if (structurize.inLoop(current_header, stop_block)) {
            return true;
        }
        // Leaving to an outer follow via break-flag lowering.
        return findEnclosingLoopForFollow(stop_block, loop_ctx).has_value();
    }

    if (current_header != -1) {
        int follow = -1;
        auto it = structurize.loop_follow.find(current_header);
        if (it != structurize.loop_follow.end()) {
            follow = it->second;
        }
        if (target_block == current_header) {
            return true; // continue
        }
        if (target_block == follow) {
            return true; // break
        }
        if (!structurize.inLoop(current_header, target_block)) {
            // Multi-level break to an outer loop follow.
            return findEnclosingLoopForFollow(target_block, loop_ctx)
                .has_value();
        }
    }
    return canStructureFrom(target_block, stop_block, loop_ctx);
}

bool GLSLGenerator::canStructureFrom(int start_block, int stop_block,
                                     LoopContext& loop_ctx) const {
    struct Visitor {
        const GLSLGenerator* gen;

        bool handleLoop(int block, int follow, LoopContext& ctx) const {
            ctx.header_stack.push_back(block);
            bool ok = gen->canStructureFrom(block, follow, ctx);
            ctx.header_stack.pop_back();
            return ok;
        }
        [[nodiscard]] bool visitBlock(int /*block*/) const { return true; }
        [[nodiscard]] bool handleNoSuccessors(int /*block*/) const {
            return true;
        }
        [[nodiscard]] bool handleLoopExitOrContinue(int /*block*/, int /*next*/,
                                                    int /*current_header*/,
                                                    int /*follow*/) const {
            return true;
        }
        [[nodiscard]] bool handleSimpleEdge(int /*block*/, int /*next*/) const {
            return true;
        }
        [[nodiscard]] bool handleNonlocalEdge(int /*block*/, int next,
                                              int stop_block,
                                              LoopContext& ctx) const {
            auto saved = ctx;
            return gen->canEdgeToBlock(next, stop_block, saved);
        }
        bool handleBranch(int /*block*/, int t, int f, int join,
                          int /*stop_block*/, LoopContext& ctx) const {
            auto saved_t = ctx;
            auto saved_f = ctx;
            return gen->canEdgeToBlock(t, join, saved_t) &&
                   gen->canEdgeToBlock(f, join, saved_f);
        }
        [[nodiscard]] bool handleLoopBreak(int /*join*/) const { return true; }
    } visitor{this};

    return traverseStructure(start_block, stop_block, loop_ctx, visitor);
}

int GLSLGenerator::lcaPostdom(int a, int b,
                              const std::vector<int>& ipdom) const {
    if (a == -1 || b == -1) {
        return -1;
    }
    std::set<int> ancestors;
    int x = a;
    while (x != -1) {
        ancestors.insert(x);
        if (x < 0 || static_cast<size_t>(x) >= ipdom.size()) {
            break;
        }
        x = ipdom[x];
    }
    x = b;
    while (x != -1) {
        if (ancestors.contains(x)) {
            return x;
        }
        if (x < 0 || static_cast<size_t>(x) >= ipdom.size()) {
            break;
        }
        x = ipdom[x];
    }
    return -1;
}

void GLSLGenerator::emitStackToEntrySlots(int target_block) {
    if (!block_entry_stack.contains(target_block)) {
        return;
    }
    const auto& slots = block_entry_stack[target_block];
    for (size_t j = 0; j < slots.size() && j < stack.size(); ++j) {
        emitLine(std::format("{} = {};", slots[j], stack[j]));
    }
}

void GLSLGenerator::emitEdgeToBlock(int target_block, int stop_block,
                                    LoopContext& loop_ctx, bool& ok) {
    if (!ok) {
        return;
    }

    const auto& structurize = analysis.getStructurizeCFGResult();
    int current_header =
        loop_ctx.header_stack.empty() ? -1 : loop_ctx.header_stack.back();

    if (current_header != -1) {
        int follow = -1;
        auto it = structurize.loop_follow.find(current_header);
        if (it != structurize.loop_follow.end()) {
            follow = it->second;
        }

        // Jumping to the stop block inside a loop exits the loop.
        if (target_block == stop_block && stop_block == follow) {
            emitStackToEntrySlots(stop_block);
            emitLine("break;");
            return;
        }

        if (target_block == current_header) {
            emitStackToEntrySlots(target_block);
            emitLine("continue;");
            return;
        }
        if (target_block == follow) {
            emitStackToEntrySlots(follow);
            emitLine("break;");
            return;
        }
        if (!structurize.inLoop(current_header, target_block)) {
            // Multi-level break to an outer loop follow.
            auto outer = findEnclosingLoopForFollow(target_block, loop_ctx);
            if (!outer.has_value()) {
                ok = false;
                return;
            }
            emitStackToEntrySlots(target_block);
            emitLine(std::format("{} = true;", getLoopBreakFlag(*outer)));
            emitLine("break;");
            return;
        }
    }

    if (target_block == stop_block) {
        emitStackToEntrySlots(target_block);
        return;
    }

    emitStackToEntrySlots(target_block);
    emitStructuredFrom(target_block, stop_block, loop_ctx, ok);
}

void GLSLGenerator::emitStructuredFrom(int start_block, int stop_block,
                                       LoopContext& loop_ctx, bool& ok) {
    if (!ok) {
        return;
    }

    struct Visitor {
        GLSLGenerator* gen;
        bool& ok;

        bool handleLoop(int block, int follow, LoopContext& ctx) const {
            std::string flag = gen->getLoopBreakFlag(block);
            gen->emitLine(std::format("bool {} = false;", flag));
            gen->emitLine("while (true) {");
            gen->indent();
            ctx.header_stack.push_back(block);
            gen->emitStructuredFrom(block, follow, ctx, ok);
            ctx.header_stack.pop_back();
            gen->dedent();
            gen->emitLine("}");
            gen->emitUnwindBreakIfNeeded(ctx);
            return ok;
        }
        [[nodiscard]] bool visitBlock(int block) const {
            if (gen->block_entry_stack.contains(block)) {
                gen->stack = gen->block_entry_stack[block];
            }
            gen->emitBlockCode(block);
            return true;
        }
        [[nodiscard]] bool handleNoSuccessors(int /*block*/) const {
            std::string result_expr = gen->stack.empty() ? "0.0" : gen->pop();
            gen->emitSetResultAndExit(result_expr);
            return true;
        }
        [[nodiscard]] bool handleLoopExitOrContinue(int /*block*/, int next,
                                                    int current_header,
                                                    int follow) const {
            if (next == current_header) {
                gen->emitStackToEntrySlots(next);
                gen->emitLine("continue;");
                return true;
            }
            if (next == follow) {
                gen->emitStackToEntrySlots(next);
                gen->emitLine("break;");
                return true;
            }
            ok = false;
            return false;
        }
        [[nodiscard]] bool handleSimpleEdge(int /*block*/, int next) const {
            gen->emitStackToEntrySlots(next);
            return true;
        }
        [[nodiscard]] bool handleNonlocalEdge(int /*block*/, int next,
                                              int stop_block,
                                              LoopContext& ctx) const {
            gen->emitEdgeToBlock(next, stop_block, ctx, ok);
            return ok;
        }
        bool handleBranch(int /*block*/, int t, int f, int join,
                          int /*stop_block*/, LoopContext& ctx) const {
            std::string cond = gen->pop();
            auto base_stack = gen->stack; // stack after popping condition

            if (join == f) {
                gen->emitLine(std::format("if ({} > 0.0) {{", cond));
                gen->indent();
                gen->emitEdgeToBlock(t, join, ctx, ok);
                gen->dedent();
                gen->emitLine("}");
                gen->stack = base_stack;
                gen->emitUnwindBreakIfNeeded(ctx);
                return ok;
            }

            if (join == t) {
                gen->emitLine(std::format("if (!({} > 0.0)) {{", cond));
                gen->indent();
                gen->emitEdgeToBlock(f, join, ctx, ok);
                gen->dedent();
                gen->emitLine("}");
                gen->stack = base_stack;
                gen->emitUnwindBreakIfNeeded(ctx);
                return ok;
            }

            gen->emitLine(std::format("if ({} > 0.0) {{", cond));
            gen->indent();
            gen->emitEdgeToBlock(t, join, ctx, ok);
            gen->dedent();
            gen->emitLine("} else {");
            gen->indent();
            gen->stack = base_stack;
            gen->emitEdgeToBlock(f, join, ctx, ok);
            gen->dedent();
            gen->emitLine("}");
            gen->stack = base_stack;
            gen->emitUnwindBreakIfNeeded(ctx);
            return ok;
        }
        [[nodiscard]] bool handleLoopBreak(int join) const {
            gen->emitStackToEntrySlots(join);
            gen->emitLine("break;");
            return true;
        }
    } visitor{.gen = this, .ok = ok};

    if (!traverseStructure(start_block, stop_block, loop_ctx, visitor)) {
        ok = false;
    }
}

void GLSLGenerator::emitMainFunctionStructured() {
    LoopContext loop_ctx;
    bool ok = analysis.getStructurizeCFGResult().success &&
              canStructureFrom(0, -1, loop_ctx);

    if (!ok) {
#ifndef NDEBUG
        if (debug_structurize_cfg) {
            emitLine("// structurize: preflight can_structure_from() failed. "
                     "falling back to state machine");
        }
#endif
        emitMainFunctionStateMachine();
        return;
    }

    loop_ctx = {};
    bool emit_ok = true;
    emitLine("{");
    indent();

    emitLine("float _result = 0.0;");
    emitLine("bool _llvmexpr_exit = false;");
    emitLine("do {");
    indent();

    structured_exit_enabled = true;
    emitStructuredFrom(0, -1, loop_ctx, emit_ok);
    structured_exit_enabled = false;

    dedent();
    emitLine("} while (false);");
    emitNewline();
    emitResultEpilogueStore();

    dedent();
    emitLine("}");

    // Should be unreachable
    // Fallback: structuring unexpectedly failed at emit time"
    // TODO: Raise a exception instead
    if (!emit_ok) {
        emitNewline();
#ifndef NDEBUG
        if (debug_structurize_cfg) {
            emitLine("// structurize: unexpected emit-time failure. falling "
                     "back "
                     "to state machine");
        }
#endif
        emitMainFunctionStateMachine();
    }
}

void GLSLGenerator::emitBlockCode(int block_idx) {
    const auto& cfg_blocks = getCodegenCfgBlocks();
    const auto& block = cfg_blocks[block_idx];

    for (int i = block.start_token_idx; i < block.end_token_idx; ++i) {
        processToken(tokens[i]);
    }
}

void GLSLGenerator::processToken(const Token& token) {
    switch (token.type) {

    // Literals & Constants
    case TokenType::Number: {
        const auto& payload = std::get<TokenPayloadNumber>(token.payload);
        push(floatLiteral(payload.value));
        break;
    }
    case TokenType::ConstantX:
        push("float(X)");
        break;
    case TokenType::ConstantY:
        push("float(Y)");
        break;
    case TokenType::ConstantWidth:
        push("float(pc.width)");
        break;
    case TokenType::ConstantHeight:
        push("float(pc.height)");
        break;
    case TokenType::ConstantN:
        push("float(pc.frameNumber)");
        break;
    case TokenType::ConstantPi:
        push(floatLiteral(std::numbers::pi));
        break;

    // Binary Operators
    case TokenType::Add:
        push(binaryOp("+"));
        break;
    case TokenType::Sub:
        push(binaryOp("-"));
        break;
    case TokenType::Mul:
        push(binaryOp("*"));
        break;
    case TokenType::Div:
        push(binaryOp("/"));
        break;
    case TokenType::Mod: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = mod({}, {});", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::Pow:
        push(binaryFn("pow"));
        break;
    case TokenType::Min:
        push(binaryFn("min"));
        break;
    case TokenType::Max:
        push(binaryFn("max"));
        break;
    case TokenType::Atan2: {
        std::string x_val = pop();
        std::string y_val = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = atan({}, {});", temp, y_val, x_val));
        push(temp);
        break;
    }
    case TokenType::Copysign: {
        std::string sign_val = pop();
        std::string mag_val = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = sign({}) * abs({});", temp, sign_val,
                             mag_val));
        push(temp);
        break;
    }

    // Comparisons
    case TokenType::Gt:
        push(binaryCmp(">"));
        break;
    case TokenType::Lt:
        push(binaryCmp("<"));
        break;
    case TokenType::Ge:
        push(binaryCmp(">="));
        break;
    case TokenType::Le:
        push(binaryCmp("<="));
        break;
    case TokenType::Eq:
        push(binaryCmp("=="));
        break;

    // Logical operators
    case TokenType::And: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format(
            "float {} = (({} > 0.0) && ({} > 0.0)) ? 1.0 : 0.0;", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::Or: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format(
            "float {} = (({} > 0.0) || ({} > 0.0)) ? 1.0 : 0.0;", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::Xor: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format(
            "float {} = (({} > 0.0) != ({} > 0.0)) ? 1.0 : 0.0;", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::Not: {
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = ({} <= 0.0) ? 1.0 : 0.0;", temp, a));
        push(temp);
        break;
    }

    // Bitwise operators
    case TokenType::Bitand: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format(
            "float {} = float(int(round({})) & int(round({})));", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::Bitor: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format(
            "float {} = float(int(round({})) | int(round({})));", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::Bitxor: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format(
            "float {} = float(int(round({})) ^ int(round({})));", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::Bitnot: {
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = float(~int(round({})));", temp, a));
        push(temp);
        break;
    }

    // Unary math functions
    case TokenType::Sqrt: {
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = sqrt(max({}, 0.0));", temp, a));
        push(temp);
        break;
    }
    case TokenType::Exp:
        push(unaryFn("exp"));
        break;
    case TokenType::Log:
        push(unaryFn("log"));
        break;
    case TokenType::Abs:
        push(unaryFn("abs"));
        break;
    case TokenType::Floor:
        push(unaryFn("floor"));
        break;
    case TokenType::Ceil:
        push(unaryFn("ceil"));
        break;
    case TokenType::Trunc:
        push(unaryFn("trunc"));
        break;
    case TokenType::Round:
        push(unaryFn("round"));
        break;
    case TokenType::Sin:
        push(unaryFn("sin"));
        break;
    case TokenType::Cos:
        push(unaryFn("cos"));
        break;
    case TokenType::Tan:
        push(unaryFn("tan"));
        break;
    case TokenType::Asin:
        push(unaryFn("asin"));
        break;
    case TokenType::Acos:
        push(unaryFn("acos"));
        break;
    case TokenType::Atan:
        push(unaryFn("atan"));
        break;
    case TokenType::Exp2:
        push(unaryFn("exp2"));
        break;
    case TokenType::Log10: {
        // log10(x) = log(x) / log(10)
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = log({}) / log(10.0);", temp, a));
        push(temp);
        break;
    }
    case TokenType::Log2:
        push(unaryFn("log2"));
        break;
    case TokenType::Sinh:
        push(unaryFn("sinh"));
        break;
    case TokenType::Cosh:
        push(unaryFn("cosh"));
        break;
    case TokenType::Tanh:
        push(unaryFn("tanh"));
        break;
    case TokenType::Sgn: {
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = ({} != 0.0) ? sign({}) : 0.0;", temp,
                             a, a));
        push(temp);
        break;
    }
    case TokenType::Neg: {
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = -{};", temp, a));
        push(temp);
        break;
    }

    // Ternary and multi-arg
    case TokenType::Ternary: {
        std::string c = pop(); // false branch
        std::string b = pop(); // true branch
        std::string a = pop(); // condition
        std::string temp = newTemp();
        emitLine(
            std::format("float {} = ({} > 0.0) ? {} : {};", temp, a, b, c));
        push(temp);
        break;
    }
    case TokenType::Clip:
    case TokenType::Clamp: {
        std::string max_val = pop();
        std::string min_val = pop();
        std::string val = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = clamp({}, {}, {});", temp, val,
                             min_val, max_val));
        push(temp);
        break;
    }
    case TokenType::Fma: {
        std::string c = pop();
        std::string b = pop();
        std::string a = pop();
        std::string temp = newTemp();
        emitLine(std::format("float {} = fma({}, {}, {});", temp, a, b, c));
        push(temp);
        break;
    }

    // Stack manipulation
    case TokenType::Dup: {
        const auto& payload = std::get<TokenPayloadStackOp>(token.payload);
        push(peek(payload.n));
        break;
    }
    case TokenType::Drop: {
        const auto& payload = std::get<TokenPayloadStackOp>(token.payload);
        for (int i = 0; i < payload.n; ++i) {
            (void)pop();
        }
        break;
    }
    case TokenType::Swap: {
        const auto& payload = std::get<TokenPayloadStackOp>(token.payload);
        size_t top_idx = stack.size() - 1;
        size_t other_idx = stack.size() - 1 - payload.n;
        std::swap(stack[top_idx], stack[other_idx]);
        break;
    }
    case TokenType::SortN: {
        const auto& payload = std::get<TokenPayloadStackOp>(token.payload);
        int n = payload.n;
        if (n < 2) {
            break;
        }

        // Pop n values
        std::vector<std::string> values(n);
        for (int i = 0; i < n; ++i) {
            values[i] = pop();
        }

        auto network = get_sorting_network(n);
        for (const auto& pair : network) {
            std::string temp_min = newTemp();
            std::string temp_max = newTemp();
            int idx1 = pair.first;
            int idx2 = pair.second;
            emitLine(std::format("float {} = min({}, {});", temp_min,
                                 values[idx1], values[idx2]));
            emitLine(std::format("float {} = max({}, {});", temp_max,
                                 values[idx1], values[idx2]));
            values[idx1] = temp_min;
            values[idx2] = temp_max;
        }

        // Push back in reverse order (smallest on top)
        for (int i = n - 1; i >= 0; --i) {
            push(values[i]);
        }
        break;
    }

    // Control flow
    case TokenType::LabelDef:
    case TokenType::Jump:
        // These are no-ops during token processing
        break;

    // Variables
    case TokenType::VarStore: {
        const auto& payload = std::get<TokenPayloadVar>(token.payload);
        std::string val = pop();
        emitLine(std::format("u_{} = {};", payload.name, val));
        break;
    }
    case TokenType::VarLoad: {
        const auto& payload = std::get<TokenPayloadVar>(token.payload);
        push(std::format("u_{}", payload.name));
        break;
    }

    // Pixel access
    case TokenType::ClipCur: {
        const auto& payload = std::get<TokenPayloadClipAccess>(token.payload);
        std::string idx_temp = newTemp();
        emitLine(std::format("uint {} = gid;", idx_temp));
        std::string val_temp = newTemp();
        emitLine(std::format("float {} = src{}.data[{}];", val_temp,
                             payload.clip_idx, idx_temp));
        push(val_temp);
        break;
    }
    case TokenType::ClipRel: {
        const auto& payload = std::get<TokenPayloadClipAccess>(token.payload);
        bool use_mirror =
            payload.has_mode ? payload.use_mirror : mirror_boundary;

        std::string x_expr = std::format("X + {}", payload.rel_x);
        std::string y_expr = std::format("Y + {}", payload.rel_y);

        std::string final_x =
            emitFinalCoord(x_expr, "int(pc.width)", use_mirror);
        std::string final_y =
            emitFinalCoord(y_expr, "int(pc.height)", use_mirror);
        std::string idx = emitPixelIndex(final_x, final_y);

        std::string val_temp = newTemp();
        emitLine(std::format("float {} = src{}.data[{}];", val_temp,
                             payload.clip_idx, idx));
        push(val_temp);
        break;
    }
    case TokenType::ClipAbs: {
        const auto& payload = std::get<TokenPayloadClipAccess>(token.payload);
        std::string coord_y = pop();
        std::string coord_x = pop();
        bool use_mirror =
            payload.has_mode ? payload.use_mirror : mirror_boundary;

        std::string x_int = newTemp();
        std::string y_int = newTemp();
        emitLine(std::format("int {} = int(roundEven({}));", x_int, coord_x));
        emitLine(std::format("int {} = int(roundEven({}));", y_int, coord_y));

        push(emitPixelLoad(payload.clip_idx, x_int, y_int, use_mirror));
        break;
    }

    // Property access
    case TokenType::PropAccess: {
        const auto& payload = std::get<TokenPayloadPropAccess>(token.payload);
        auto key = std::make_pair(payload.clip_idx, payload.prop_name);
        int prop_idx = prop_map.at(key);
        std::string temp = newTemp();
        emitLine(
            std::format("float {} = propsData.props[{}];", temp, prop_idx));
        push(temp);
        break;
    }
    case TokenType::PropExists: {
        const auto& payload = std::get<TokenPayloadPropAccess>(token.payload);
        auto key = std::make_pair(payload.clip_idx, payload.prop_name);
        if (prop_map.contains(key)) {
            int prop_idx = prop_map.at(key);
            std::string temp = newTemp();
            emitLine(std::format(
                "float {} = (floatBitsToUint(propsData.props[{}]) == "
                "0x7FC0BEEFu) ? 0.0 : 1.0;",
                temp, prop_idx));
            push(temp);
        } else {
            push("0.0");
        }
        break;
    }

    // Array operations
    case TokenType::ArrayAllocStatic:
        // This is a no-op at token processing time
        break;
    case TokenType::ArrayLoad: {
        const auto& payload = std::get<TokenPayloadArrayOp>(token.payload);
        std::string idx = pop();
        std::string temp = newTemp();
        emitLine(
            std::format("float {} = a_{}[int({})];", temp, payload.name, idx));
        push(temp);
        break;
    }
    case TokenType::ArrayStore: {
        const auto& payload = std::get<TokenPayloadArrayOp>(token.payload);
        std::string idx = pop();
        std::string val = pop();
        emitLine(std::format("a_{}[int({})] = {};", payload.name, idx, val));
        break;
    }

    case TokenType::StoreAbs: {
        std::string coord_y = pop();
        std::string coord_x = pop();
        std::string val = pop();
        std::string x_int = newTemp();
        std::string y_int = newTemp();
        emitLine(std::format("int {} = int({});", x_int, coord_x));
        emitLine(std::format("int {} = int({});", y_int, coord_y));
        std::string idx = emitPixelIndex(x_int, y_int);
        emitLine(std::format("dst.data[{}] = {};", idx, val));
        break;
    }

    case TokenType::ExitNoWrite: {
        push("uintBitsToFloat(0x7FC0E71Fu)");
        break;
    }

    default:
        throw std::runtime_error(
            std::format("GLSLGenerator: unhandled token type {}",
                        static_cast<int>(token.type)));
    }
}
