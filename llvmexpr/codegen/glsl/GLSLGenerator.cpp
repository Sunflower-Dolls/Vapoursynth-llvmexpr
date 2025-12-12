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

#include <cmath>
#include <format>
#include <numbers>
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
}

void GLSLGenerator::emit(const std::string& code) { out << code; }

void GLSLGenerator::emit_line(const std::string& code) {
    for (int i = 0; i < indent_level; ++i) {
        out << "    ";
    }
    out << code << "\n";
}

void GLSLGenerator::emit_newline() { out << "\n"; }

void GLSLGenerator::indent() { ++indent_level; }

void GLSLGenerator::dedent() {
    if (indent_level > 0) {
        --indent_level;
    }
}

std::string GLSLGenerator::new_temp() {
    return std::format("t_{}", temp_counter++);
}

std::string GLSLGenerator::new_slot() {
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

std::string GLSLGenerator::float_literal(double val) {
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

std::string GLSLGenerator::binary_op(const std::string& op) {
    std::string b = pop();
    std::string a = pop();
    std::string temp = new_temp();
    emit_line(std::format("float {} = {} {} {};", temp, a, op, b));
    return temp;
}

std::string GLSLGenerator::binary_cmp(const std::string& op) {
    std::string b = pop();
    std::string a = pop();
    std::string temp = new_temp();
    emit_line(
        std::format("float {} = ({} {} {}) ? 1.0 : 0.0;", temp, a, op, b));
    return temp;
}

std::string GLSLGenerator::unary_fn(const std::string& fn) {
    std::string a = pop();
    std::string temp = new_temp();
    emit_line(std::format("float {} = {}({});", temp, fn, a));
    return temp;
}

std::string GLSLGenerator::binary_fn(const std::string& fn) {
    std::string b = pop();
    std::string a = pop();
    std::string temp = new_temp();
    emit_line(std::format("float {} = {}({}, {});", temp, fn, a, b));
    return temp;
}

std::string GLSLGenerator::emit_clamp_coord(const std::string& coord,
                                            const std::string& max_dim) {
    std::string temp = new_temp();
    emit_line(
        std::format("int {} = clamp({}, 0, {} - 1);", temp, coord, max_dim));
    return temp;
}

std::string GLSLGenerator::emit_mirror_coord(const std::string& coord,
                                             const std::string& max_dim) {
    std::string temp = new_temp();
    emit_line(std::format("int {};", temp));
    emit_line("{");
    indent();
    emit_line(std::format("int _period = 2 * ({});", max_dim));
    emit_line(
        std::format("int _mod = int(mod(float({}), float(_period)));", coord));
    emit_line(std::format("if (_mod >= ({})) {{ {} = _period - 1 - _mod; }}",
                          max_dim, temp));
    emit_line(std::format("else {{ {} = _mod; }}", temp));
    dedent();
    emit_line("}");
    return temp;
}

std::string GLSLGenerator::emit_final_coord(const std::string& coord,
                                            const std::string& max_dim,
                                            bool use_mirror) {
    if (use_mirror) {
        return emit_mirror_coord(coord, max_dim);
    }
    return emit_clamp_coord(coord, max_dim);
}

std::string GLSLGenerator::emit_pixel_index(const std::string& x,
                                            const std::string& y) {
    std::string temp = new_temp();
    emit_line(
        std::format("uint {} = uint({}) + uint({}) * pc.width;", temp, x, y));
    return temp;
}

std::string GLSLGenerator::emit_pixel_load(int clip_idx, const std::string& x,
                                           const std::string& y,
                                           bool use_mirror) {
    std::string final_x = emit_final_coord(x, "int(pc.width)", use_mirror);
    std::string final_y = emit_final_coord(y, "int(pc.height)", use_mirror);
    std::string idx = emit_pixel_index(final_x, final_y);
    std::string temp = new_temp();
    emit_line(std::format("float {} = src{}.data[{}];", temp, clip_idx, idx));
    return temp;
}

std::string GLSLGenerator::generate() {
    out.str("");
    out.clear();
    indent_level = 0;
    temp_counter = 0;
    slot_counter = 0;
    stack.clear();

    emit_header();
    emit_buffer_declarations();
    emit_helper_functions();
    emit_main_function();

    return out.str();
}

void GLSLGenerator::emit_header() {
    emit_line("#version 450");
    emit_line("#extension GL_EXT_scalar_block_layout : enable");
    emit_newline();
    emit_line(
        "layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;");
    emit_newline();
    emit_line("layout(push_constant) uniform PushConstants {");
    indent();
    emit_line("uint width;");
    emit_line("uint height;");
    emit_line("uint numInputs;");
    emit_line("int frameNumber;");
    dedent();
    emit_line("} pc;");
    emit_newline();
}

void GLSLGenerator::emit_buffer_declarations() {
    // Input buffers
    for (int i = 0; i < num_inputs; ++i) {
        emit_line(std::format("layout(std430, set = 0, binding = {}) readonly "
                              "buffer InputBuffer{} {{",
                              i, i));
        indent();
        emit_line("float data[];");
        dedent();
        emit_line(std::format("}} src{};", i));
        emit_newline();
    }

    // Output buffer
    emit_line(std::format("layout(std430, set = 0, binding = {}) writeonly "
                          "buffer OutputBuffer {{",
                          num_inputs));
    indent();
    emit_line("float data[];");
    dedent();
    emit_line("} dst;");
    emit_newline();

    // Props buffer
    emit_line(std::format(
        "layout(std430, set = 0, binding = {}) readonly buffer PropsBuffer {{",
        num_inputs + 1));
    indent();
    emit_line("float props[];");
    dedent();
    emit_line("} propsData;");
    emit_newline();
}

void GLSLGenerator::emit_helper_functions() {
    // Currently no helper functions needed
    // May add in the future for complex operations
}

void GLSLGenerator::emit_variable_declarations() {
    // User-defined variables
    for (const auto& var_name : user_variables) {
        emit_line(std::format("float u_{};", var_name));
    }

    // TODO: Move to analysis/
    for (const auto& token : tokens) {
        if (token.type == TokenType::ARRAY_ALLOC_STATIC) {
            const auto& payload = std::get<TokenPayload_ArrayOp>(token.payload);
            if (!arrays.contains(payload.name)) {
                arrays[payload.name] = payload.static_size;
                emit_line(std::format("float a_{}[{}];", payload.name,
                                      payload.static_size));
            }
        }
    }

    // Stack slot variables for merge points
    const auto& cfg_blocks = analysis.getCFGBlocks();
    const auto& stack_depth_in = analysis.getStackDepthIn();
    for (size_t i = 0; i < cfg_blocks.size(); ++i) {
        if (cfg_blocks[i].predecessors.size() > 1) {
            int depth = stack_depth_in[i];
            std::vector<std::string> slots;
            for (int j = 0; j < depth; ++j) {
                std::string slot = new_slot();
                emit_line(std::format("float {};", slot));
                slots.push_back(slot);
            }
            block_entry_stack[static_cast<int>(i)] = slots;
        }
    }
}

void GLSLGenerator::emit_main_function() {
    emit_line("void main() {");
    indent();

    emit_line("uint gid = gl_GlobalInvocationID.x;");
    emit_line("uint totalPixels = pc.width * pc.height;");
    emit_newline();
    emit_line("if (gid >= totalPixels) {");
    indent();
    emit_line("return;");
    dedent();
    emit_line("}");
    emit_newline();

    emit_line("int X = int(gid % pc.width);");
    emit_line("int Y = int(gid / pc.width);");
    emit_newline();

    emit_variable_declarations();
    emit_newline();

    const auto& cfg_blocks = analysis.getCFGBlocks();

    emit_line("int _state = 0;");
    emit_line("float _result = 0.0;");
    emit_newline();
    emit_line("while (_state != -1) {");
    indent();
    emit_line("switch (_state) {");

    for (size_t i = 0; i < cfg_blocks.size(); ++i) {
        emit_line(std::format("case {}:", i));
        indent();

        if (block_entry_stack.contains(static_cast<int>(i))) {
            stack = block_entry_stack[static_cast<int>(i)];
        }

        emit_block_code(static_cast<int>(i));

        const auto& block = cfg_blocks[i];
        if (block.successors.empty()) {
            if (!stack.empty()) {
                std::string result = pop();
                emit_line(std::format("_result = {};", result));
            }
            emit_line("_state = -1;");
        } else if (block.successors.size() == 1) {
            int next = block.successors[0];
            if (block_entry_stack.contains(next)) {
                const auto& slots = block_entry_stack[next];
                for (size_t j = 0; j < slots.size() && j < stack.size(); ++j) {
                    emit_line(std::format("{} = {};", slots[j], stack[j]));
                }
            }
            emit_line(std::format("_state = {};", next));
        } else {
            std::string cond = pop();
            int true_target = block.successors[0];
            int false_target = block.successors[1];

            for (int target : {true_target, false_target}) {
                if (block_entry_stack.contains(target)) {
                    const auto& slots = block_entry_stack[target];
                    for (size_t j = 0; j < slots.size() && j < stack.size();
                         ++j) {
                        emit_line(std::format("{} = {};", slots[j], stack[j]));
                    }
                }
            }

            emit_line(std::format("_state = ({} > 0.0) ? {} : {};", cond,
                                  true_target, false_target));
        }

        emit_line("break;");
        dedent();
    }

    emit_line("}"); // end switch
    dedent();
    emit_line("}"); // end while
    emit_newline();

    // Check for EXIT_NO_WRITE: skip store if result equals EXIT_NAN_PAYLOAD
    // (0x7FC0E71F)
    emit_line("if (floatBitsToUint(_result) != 0x7FC0E71Fu) {");
    indent();
    emit_line("dst.data[gid] = _result;");
    dedent();
    emit_line("}");

    dedent();
    emit_line("}");
}

void GLSLGenerator::emit_block_code(int block_idx) {
    const auto& cfg_blocks = analysis.getCFGBlocks();
    const auto& block = cfg_blocks[block_idx];

    for (int i = block.start_token_idx; i < block.end_token_idx; ++i) {
        process_token(tokens[i]);
    }
}

void GLSLGenerator::process_token(const Token& token) {
    switch (token.type) {

    // Literals & Constants
    case TokenType::NUMBER: {
        const auto& payload = std::get<TokenPayload_Number>(token.payload);
        push(float_literal(payload.value));
        break;
    }
    case TokenType::CONSTANT_X:
        push("float(X)");
        break;
    case TokenType::CONSTANT_Y:
        push("float(Y)");
        break;
    case TokenType::CONSTANT_WIDTH:
        push("float(pc.width)");
        break;
    case TokenType::CONSTANT_HEIGHT:
        push("float(pc.height)");
        break;
    case TokenType::CONSTANT_N:
        push("float(pc.frameNumber)");
        break;
    case TokenType::CONSTANT_PI:
        push(float_literal(std::numbers::pi));
        break;

    // Binary Operators
    case TokenType::ADD:
        push(binary_op("+"));
        break;
    case TokenType::SUB:
        push(binary_op("-"));
        break;
    case TokenType::MUL:
        push(binary_op("*"));
        break;
    case TokenType::DIV:
        push(binary_op("/"));
        break;
    case TokenType::MOD: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = mod({}, {});", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::POW:
        push(binary_fn("pow"));
        break;
    case TokenType::MIN:
        push(binary_fn("min"));
        break;
    case TokenType::MAX:
        push(binary_fn("max"));
        break;
    case TokenType::ATAN2: {
        std::string x_val = pop();
        std::string y_val = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = atan({}, {});", temp, y_val, x_val));
        push(temp);
        break;
    }
    case TokenType::COPYSIGN: {
        std::string sign_val = pop();
        std::string mag_val = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = sign({}) * abs({});", temp, sign_val,
                              mag_val));
        push(temp);
        break;
    }

    // Comparisons
    case TokenType::GT:
        push(binary_cmp(">"));
        break;
    case TokenType::LT:
        push(binary_cmp("<"));
        break;
    case TokenType::GE:
        push(binary_cmp(">="));
        break;
    case TokenType::LE:
        push(binary_cmp("<="));
        break;
    case TokenType::EQ:
        push(binary_cmp("=="));
        break;

    // Logical operators
    case TokenType::AND: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format(
            "float {} = (({} > 0.0) && ({} > 0.0)) ? 1.0 : 0.0;", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::OR: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format(
            "float {} = (({} > 0.0) || ({} > 0.0)) ? 1.0 : 0.0;", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::XOR: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format(
            "float {} = (({} > 0.0) != ({} > 0.0)) ? 1.0 : 0.0;", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::NOT: {
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = ({} <= 0.0) ? 1.0 : 0.0;", temp, a));
        push(temp);
        break;
    }

    // Bitwise operators
    case TokenType::BITAND: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format(
            "float {} = float(int(round({})) & int(round({})));", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::BITOR: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format(
            "float {} = float(int(round({})) | int(round({})));", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::BITXOR: {
        std::string b = pop();
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format(
            "float {} = float(int(round({})) ^ int(round({})));", temp, a, b));
        push(temp);
        break;
    }
    case TokenType::BITNOT: {
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = float(~int(round({})));", temp, a));
        push(temp);
        break;
    }

    // Unary math functions
    case TokenType::SQRT: {
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = sqrt(max({}, 0.0));", temp, a));
        push(temp);
        break;
    }
    case TokenType::EXP:
        push(unary_fn("exp"));
        break;
    case TokenType::LOG:
        push(unary_fn("log"));
        break;
    case TokenType::ABS:
        push(unary_fn("abs"));
        break;
    case TokenType::FLOOR:
        push(unary_fn("floor"));
        break;
    case TokenType::CEIL:
        push(unary_fn("ceil"));
        break;
    case TokenType::TRUNC:
        push(unary_fn("trunc"));
        break;
    case TokenType::ROUND:
        push(unary_fn("round"));
        break;
    case TokenType::SIN:
        push(unary_fn("sin"));
        break;
    case TokenType::COS:
        push(unary_fn("cos"));
        break;
    case TokenType::TAN:
        push(unary_fn("tan"));
        break;
    case TokenType::ASIN:
        push(unary_fn("asin"));
        break;
    case TokenType::ACOS:
        push(unary_fn("acos"));
        break;
    case TokenType::ATAN:
        push(unary_fn("atan"));
        break;
    case TokenType::EXP2:
        push(unary_fn("exp2"));
        break;
    case TokenType::LOG10: {
        // log10(x) = log(x) / log(10)
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = log({}) / log(10.0);", temp, a));
        push(temp);
        break;
    }
    case TokenType::LOG2:
        push(unary_fn("log2"));
        break;
    case TokenType::SINH:
        push(unary_fn("sinh"));
        break;
    case TokenType::COSH:
        push(unary_fn("cosh"));
        break;
    case TokenType::TANH:
        push(unary_fn("tanh"));
        break;
    case TokenType::SGN: {
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = ({} != 0.0) ? sign({}) : 0.0;", temp,
                              a, a));
        push(temp);
        break;
    }
    case TokenType::NEG: {
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = -{};", temp, a));
        push(temp);
        break;
    }

    // Ternary and multi-arg
    case TokenType::TERNARY: {
        std::string c = pop(); // false branch
        std::string b = pop(); // true branch
        std::string a = pop(); // condition
        std::string temp = new_temp();
        emit_line(
            std::format("float {} = ({} > 0.0) ? {} : {};", temp, a, b, c));
        push(temp);
        break;
    }
    case TokenType::CLIP:
    case TokenType::CLAMP: {
        std::string max_val = pop();
        std::string min_val = pop();
        std::string val = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = clamp({}, {}, {});", temp, val,
                              min_val, max_val));
        push(temp);
        break;
    }
    case TokenType::FMA: {
        std::string c = pop();
        std::string b = pop();
        std::string a = pop();
        std::string temp = new_temp();
        emit_line(std::format("float {} = fma({}, {}, {});", temp, a, b, c));
        push(temp);
        break;
    }

    // Stack manipulation
    case TokenType::DUP: {
        const auto& payload = std::get<TokenPayload_StackOp>(token.payload);
        push(peek(payload.n));
        break;
    }
    case TokenType::DROP: {
        const auto& payload = std::get<TokenPayload_StackOp>(token.payload);
        for (int i = 0; i < payload.n; ++i) {
            (void)pop();
        }
        break;
    }
    case TokenType::SWAP: {
        const auto& payload = std::get<TokenPayload_StackOp>(token.payload);
        size_t top_idx = stack.size() - 1;
        size_t other_idx = stack.size() - 1 - payload.n;
        std::swap(stack[top_idx], stack[other_idx]);
        break;
    }
    case TokenType::SORTN: {
        const auto& payload = std::get<TokenPayload_StackOp>(token.payload);
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
            std::string temp_min = new_temp();
            std::string temp_max = new_temp();
            int idx1 = pair.first;
            int idx2 = pair.second;
            emit_line(std::format("float {} = min({}, {});", temp_min,
                                  values[idx1], values[idx2]));
            emit_line(std::format("float {} = max({}, {});", temp_max,
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
    case TokenType::LABEL_DEF:
    case TokenType::JUMP:
        // These are no-ops during token processing
        break;

    // Variables
    case TokenType::VAR_STORE: {
        const auto& payload = std::get<TokenPayload_Var>(token.payload);
        std::string val = pop();
        emit_line(std::format("u_{} = {};", payload.name, val));
        break;
    }
    case TokenType::VAR_LOAD: {
        const auto& payload = std::get<TokenPayload_Var>(token.payload);
        push(std::format("u_{}", payload.name));
        break;
    }

    // Pixel access
    case TokenType::CLIP_CUR: {
        const auto& payload = std::get<TokenPayload_ClipAccess>(token.payload);
        std::string idx_temp = new_temp();
        emit_line(std::format("uint {} = gid;", idx_temp));
        std::string val_temp = new_temp();
        emit_line(std::format("float {} = src{}.data[{}];", val_temp,
                              payload.clip_idx, idx_temp));
        push(val_temp);
        break;
    }
    case TokenType::CLIP_REL: {
        const auto& payload = std::get<TokenPayload_ClipAccess>(token.payload);
        bool use_mirror =
            payload.has_mode ? payload.use_mirror : mirror_boundary;

        std::string x_expr = std::format("X + {}", payload.rel_x);
        std::string y_expr = std::format("Y + {}", payload.rel_y);

        std::string final_x =
            emit_final_coord(x_expr, "int(pc.width)", use_mirror);
        std::string final_y =
            emit_final_coord(y_expr, "int(pc.height)", use_mirror);
        std::string idx = emit_pixel_index(final_x, final_y);

        std::string val_temp = new_temp();
        emit_line(std::format("float {} = src{}.data[{}];", val_temp,
                              payload.clip_idx, idx));
        push(val_temp);
        break;
    }
    case TokenType::CLIP_ABS: {
        const auto& payload = std::get<TokenPayload_ClipAccess>(token.payload);
        std::string coord_y = pop();
        std::string coord_x = pop();
        bool use_mirror =
            payload.has_mode ? payload.use_mirror : mirror_boundary;

        // Round to nearest even integer (matches CPU rint behavior)
        std::string x_int = new_temp();
        std::string y_int = new_temp();
        emit_line(std::format("int {} = int(roundEven({}));", x_int, coord_x));
        emit_line(std::format("int {} = int(roundEven({}));", y_int, coord_y));

        push(emit_pixel_load(payload.clip_idx, x_int, y_int, use_mirror));
        break;
    }

    // Property access
    case TokenType::PROP_ACCESS: {
        const auto& payload = std::get<TokenPayload_PropAccess>(token.payload);
        auto key = std::make_pair(payload.clip_idx, payload.prop_name);
        int prop_idx = prop_map.at(key);
        std::string temp = new_temp();
        emit_line(
            std::format("float {} = propsData.props[{}];", temp, prop_idx));
        push(temp);
        break;
    }
    case TokenType::PROP_EXISTS: {
        const auto& payload = std::get<TokenPayload_PropAccess>(token.payload);
        auto key = std::make_pair(payload.clip_idx, payload.prop_name);
        if (prop_map.contains(key)) {
            int prop_idx = prop_map.at(key);
            std::string temp = new_temp();
            emit_line(std::format(
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
    case TokenType::ARRAY_ALLOC_STATIC:
        // This is a no-op at token processing time
        break;
    case TokenType::ARRAY_LOAD: {
        const auto& payload = std::get<TokenPayload_ArrayOp>(token.payload);
        std::string idx = pop();
        std::string temp = new_temp();
        emit_line(
            std::format("float {} = a_{}[int({})];", temp, payload.name, idx));
        push(temp);
        break;
    }
    case TokenType::ARRAY_STORE: {
        const auto& payload = std::get<TokenPayload_ArrayOp>(token.payload);
        std::string idx = pop();
        std::string val = pop();
        emit_line(std::format("a_{}[int({})] = {};", payload.name, idx, val));
        break;
    }

    case TokenType::STORE_ABS: {
        std::string coord_y = pop();
        std::string coord_x = pop();
        std::string val = pop();
        std::string x_int = new_temp();
        std::string y_int = new_temp();
        emit_line(std::format("int {} = int({});", x_int, coord_x));
        emit_line(std::format("int {} = int({});", y_int, coord_y));
        std::string idx = emit_pixel_index(x_int, y_int);
        emit_line(std::format("dst.data[{}] = {};", idx, val));
        break;
    }

    case TokenType::EXIT_NO_WRITE: {
        push("uintBitsToFloat(0x7FC0E71Fu)");
        break;
    }

    default:
        throw std::runtime_error(
            std::format("GLSLGenerator: unhandled token type {}",
                        static_cast<int>(token.type)));
    }
}
