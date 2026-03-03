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

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "VSHelper4.h"
#include "VapourSynth4.h"

#include "analysis/AnalysisResults.hpp"
#include "analysis/ExpressionAnalyzer.hpp"
#include "analysis/passes/DynamicArrayAllocOptPass.hpp"
#include "analysis/passes/StaticArrayOptPass.hpp"
#include "codegen/glsl/GLSLGenerator.hpp"
#include "frontend/InfixConverter.hpp"
#include "frontend/Tokenizer.hpp"
#include "runtime/llvm/Compiler.hpp"
#include "runtime/llvm/Jit.hpp"
#include "runtime/vulkan/VkExprExecutor.hpp"

constexpr uint32_t PROP_READ_NAN_PAYLOAD =
    0x7FC0BEEF; // qNaN with payload 0xBEEF
constexpr uint32_t PROP_WRITE_NAN_PAYLOAD =
    0x7FC0DEAD; // qNaN with payload 0xDEAD
constexpr uint32_t PROP_DELETE_NAN_PAYLOAD =
    0x7FC0DE1E; // qNaN with payload DE1E

namespace {

enum class PlaneOp : std::uint8_t { PoProcess, PoCopy };

struct BaseExprData {
    std::vector<VSNode*> nodes;
    VSVideoInfo vi = {};
    int num_inputs = 0;
    bool mirror_boundary = false;
    std::string dump_ir_path;
    int opt_level = 5; // NOLINT(cppcoreguidelines-avoid-magic-numbers)
    int approx_math = 2;
    std::vector<std::pair<int, std::string>> required_props;
    std::map<std::pair<int, std::string>, int> prop_map;
};

struct ExprData : BaseExprData {
    std::array<PlaneOp, 3> plane_op = {};
    std::array<CompiledFunction, 3> compiled;
    std::array<std::vector<Token>, 3> tokens;
    std::array<std::unique_ptr<analysis::AnalysisManager>, 3> analysis_managers;
    int tile_x = 0;
    int tile_y = 0;
};

struct SingleExprData : BaseExprData {
    CompiledFunction compiled;
    std::vector<std::pair<std::string, PropWriteType>> output_props;
    std::map<std::string, int> output_prop_map;
    std::vector<Token> tokens;
    std::unique_ptr<analysis::AnalysisManager> analysis_manager;
};

struct SingleExprFrameData {
    struct DynamicArray {
        std::vector<float> buffer;
    };
    std::map<std::string, DynamicArray> dynamic_arrays;
};

thread_local SingleExprFrameData
    g_frame_data; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

std::unordered_map<std::string, std::pair<int, int>> expr_autotune_cache;
std::mutex expr_autotune_cache_mutex;

template <bool check_dimensions>
void validate_and_init_clips(BaseExprData* d, const VSMap* in,
                             const VSAPI* vsapi) {
    int err = 0;
    d->num_inputs = vsapi->mapNumElements(in, "clips");
    if (d->num_inputs == 0) {
        throw std::runtime_error("At least one clip must be provided.");
    }

    d->nodes.resize(d->num_inputs);
    for (int i = 0; i < d->num_inputs; ++i) {
        d->nodes[i] = vsapi->mapGetNode(in, "clips", i, &err);
    }

    std::vector<const VSVideoInfo*> vi(d->num_inputs);
    for (int i = 0; i < d->num_inputs; ++i) {
        vi[i] = vsapi->getVideoInfo(d->nodes[i]);
        if (!vsh::isConstantVideoFormat(vi[i])) {
            throw std::runtime_error(
                "Only constant format clips are supported.");
        }
    }

    if constexpr (check_dimensions) {
        for (int i = 1; i < d->num_inputs; ++i) {
            if (vi[i]->width != vi[0]->width ||
                vi[i]->height != vi[0]->height) {
                throw std::runtime_error(
                    "All clips must have the same dimensions.");
            }
        }
    }

    d->vi = *vi[0];
}

void parse_format_param(BaseExprData* d, const VSMap* in, const VSAPI* vsapi,
                        VSCore* core) {
    int err = 0;
    const int format_id =
        static_cast<int>(vsapi->mapGetInt(in, "format", 0, &err));
    if (err == 0) {
        VSVideoFormat temp_format;
        if (vsapi->getVideoFormatByID(&temp_format, format_id, core) != 0) {
            if (d->vi.format.numPlanes != temp_format.numPlanes) {
                throw std::runtime_error("The number of planes in the "
                                         "inputs and output must match.");
            }
            VSVideoFormat new_format;
            if (vsapi->queryVideoFormat(&new_format, d->vi.format.colorFamily,
                                        temp_format.sampleType,
                                        temp_format.bitsPerSample,
                                        d->vi.format.subSamplingW,
                                        d->vi.format.subSamplingH, core) != 0) {
                d->vi.format = new_format;
            } else {
                throw std::runtime_error("Failed to query new format.");
            }
        }
    }
}

void parse_common_params(BaseExprData* d, const VSMap* in, const VSAPI* vsapi) {
    int err = 0;

    const char* dump_path = vsapi->mapGetData(in, "dump_ir", 0, &err);
    if ((err == 0) && (dump_path != nullptr)) {
        d->dump_ir_path = dump_path;
    }

    d->opt_level = static_cast<int>(vsapi->mapGetInt(in, "opt_level", 0, &err));
    if (err != 0) {
        d->opt_level = 5; // NOLINT(cppcoreguidelines-avoid-magic-numbers)
    }
    if (d->opt_level <= 0) {
        throw std::runtime_error("opt_level must be greater than 0.");
    }

    d->approx_math =
        static_cast<int>(vsapi->mapGetInt(in, "approx_math", 0, &err));
    if (err != 0) {
        d->approx_math = 2; // Default to auto mode
    }
    if (d->approx_math < 0 || d->approx_math > 2) {
        throw std::runtime_error(
            "approx_math must be 0 (disabled), 1 (enabled), or 2 (auto).");
    }
}

void parse_expr_tiling_params(ExprData* d, const VSMap* in,
                              const VSAPI* vsapi) {
    int err = 0;
    d->tile_x = static_cast<int>(vsapi->mapGetInt(in, "tile_x", 0, &err));
    if (err != 0) {
        d->tile_x = -1;
    }

    d->tile_y = static_cast<int>(vsapi->mapGetInt(in, "tile_y", 0, &err));
    if (err != 0) {
        d->tile_y = -1;
    }

    if (d->tile_x < -1) {
        throw std::runtime_error("tile_x must be -1 or >= 0.");
    }
    if (d->tile_y < -1) {
        throw std::runtime_error("tile_y must be -1 or >= 0.");
    }
}

void read_frame_properties(
    std::vector<float>& props, const std::vector<const VSFrame*>& src_frames,
    const std::vector<std::pair<int, std::string>>& required_props, int n,
    const VSAPI* vsapi) {

    props[0] = static_cast<float>(n);

    for (size_t i = 0; i < required_props.size(); ++i) {
        const auto& prop_info = required_props[i];
        int clip_idx = prop_info.first;
        const std::string& prop_name = prop_info.second;
        int prop_array_idx = static_cast<int>(i) + 1;

        const VSMap* props_map =
            vsapi->getFramePropertiesRO(src_frames[clip_idx]);
        int err = 0;
        int type = vsapi->mapGetType(props_map, prop_name.c_str());

        if (type == ptInt) {
            props[prop_array_idx] = static_cast<float>(
                vsapi->mapGetInt(props_map, prop_name.c_str(), 0, &err));
        } else if (type == ptFloat) {
            props[prop_array_idx] = static_cast<float>(
                vsapi->mapGetFloat(props_map, prop_name.c_str(), 0, &err));
        } else if (type == ptData) {
            if (vsapi->mapGetDataSize(props_map, prop_name.c_str(), 0, &err) >
                    0 &&
                (err == 0)) {
                props[prop_array_idx] = static_cast<float>(
                    *vsapi->mapGetData(props_map, prop_name.c_str(), 0, &err));
            } else {
                err = 1;
            }
        } else {
            err = 1;
        }

        if (err != 0) {
            props[prop_array_idx] = std::bit_cast<float>(PROP_READ_NAN_PAYLOAD);
        }
    }
}

// NOLINTBEGIN(readability-identifier-naming)
template <typename T>
void genericFree(void* instanceData, [[maybe_unused]] VSCore* core,
                 const VSAPI* vsapi) {
    // NOLINTEND(readability-identifier-naming)
    std::unique_ptr<T> d(static_cast<T*>(instanceData));
    for (auto* node : d->nodes) {
        vsapi->freeNode(node);
    }
}

std::string generate_cache_key(
    const std::string& expr, const VSVideoInfo* vo, const VSAPI* vsapi,
    const std::vector<const VSVideoInfo*>& vi, bool mirror,
    const std::map<std::pair<int, std::string>, int>& prop_map, int plane_width,
    int plane_height, const std::vector<std::string>& output_props = {},
    int tile_x = 0, int tile_y = 0) {
    auto get_vf_name = [&](const VSVideoFormat* vf) {
        std::array<char, 32> // NOLINT(cppcoreguidelines-avoid-magic-numbers)
            vf_name_buffer{};
        if (!vsapi->getVideoFormatName(vf, vf_name_buffer.data())) {
            throw std::runtime_error("Failed to get video format name");
        }
        return std::string(vf_name_buffer.data());
    };
    std::string result =
        std::format("expr={}|mirror={}|out={}|w={}|h={}", expr, mirror,
                    get_vf_name(&vo->format), plane_width, plane_height);

    for (size_t i = 0; i < vi.size(); ++i) {
        result += std::format("|in{}={}", i, get_vf_name(&vi[i]->format));
    }

    for (const auto& [key, val] : prop_map) {
        result += std::format("|prop{}={}.{}", val, key.first, key.second);
    }

    for (const auto& prop : output_props) {
        result += std::format("|out_prop={}", prop);
    }

    result += std::format("|tile_x={}|tile_y={}", tile_x, tile_y);

    return result;
}

// NOLINTBEGIN(readability-identifier-naming)
const VSFrame*
    VS_CC // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    exprGetFrame(int n, int activationReason, void* instanceData,
                 [[maybe_unused]] void** frameData, VSFrameContext* frameCtx,
                 VSCore* core, const VSAPI* vsapi) {
    // NOLINTEND(readability-identifier-naming)
    auto* d = static_cast<ExprData*>(instanceData);

    if (activationReason == arInitial) {
        for (int i = 0; i < d->num_inputs; ++i) {
            vsapi->requestFrameFilter(n, d->nodes[i], frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        std::vector<const VSFrame*> src_frames(d->num_inputs);
        for (int i = 0; i < d->num_inputs; ++i) {
            src_frames[i] = vsapi->getFrameFilter(n, d->nodes[i], frameCtx);
        }

        std::array<const VSFrame*, 3> plane_src = {
            d->plane_op.at(0) == PlaneOp::PoCopy ? src_frames[0] : nullptr,
            d->plane_op.at(1) == PlaneOp::PoCopy ? src_frames[0] : nullptr,
            d->plane_op.at(2) == PlaneOp::PoCopy ? src_frames[0] : nullptr};
        std::array<int, 3> planes = {0, 1, 2};
        VSFrame* dst_frame = vsapi->newVideoFrame2(
            &d->vi.format, d->vi.width, d->vi.height, plane_src.data(),
            planes.data(), src_frames[0], core);

        std::vector<uint8_t*> rwptrs(d->num_inputs + 1);
        std::vector<int> strides(d->num_inputs + 1);
        std::vector<float> props(1 + d->required_props.size());

        read_frame_properties(props, src_frames, d->required_props, n, vsapi);

        for (int plane = 0; plane < d->vi.format.numPlanes; ++plane) {
            if (d->plane_op.at(plane) == PlaneOp::PoProcess) {
                rwptrs[0] = vsapi->getWritePtr(dst_frame, plane);
                strides[0] =
                    static_cast<int>(vsapi->getStride(dst_frame, plane));
                for (int i = 0; i < d->num_inputs; ++i) {
                    rwptrs[i + 1] =
                        const_cast< // NOLINT(cppcoreguidelines-pro-type-const-cast)
                            uint8_t*>(vsapi->getReadPtr(src_frames[i], plane));
                    strides[i + 1] = static_cast<int>(
                        vsapi->getStride(src_frames[i], plane));
                }

                if (d->compiled.at(plane).func_ptr == nullptr) {
                    int width = vsapi->getFrameWidth(dst_frame, plane);
                    int height = vsapi->getFrameHeight(dst_frame, plane);

                    std::vector<const VSVideoInfo*> vi(d->num_inputs);
                    for (int i = 0; i < d->num_inputs; ++i) {
                        vi[i] = vsapi->getVideoInfo(d->nodes[i]);
                    }

                    std::string expr_str;
                    for (const auto& token : d->tokens.at(plane)) {
                        if (!expr_str.empty()) {
                            expr_str += " ";
                        }
                        expr_str += token.text;
                    }

                    auto get_or_compile = [&](int resolved_tile_x,
                                              int resolved_tile_y) {
                        const std::string key = generate_cache_key(
                            expr_str, &d->vi, vsapi, vi, d->mirror_boundary,
                            d->prop_map, width, height, {}, resolved_tile_x,
                            resolved_tile_y);

                        std::lock_guard<std::mutex> lock(cache_mutex);
                        if (!jit_cache.contains(key)) {
                            size_t key_hash = std::hash<std::string>{}(key);
                            std::string func_name = std::format(
                                "process_plane_{}_{}", plane, key_hash);

                            try {
                                analysis::ExpressionAnalysisResults results(
                                    *d->analysis_managers.at(plane));
                                Compiler compiler(
                                    std::vector<Token>(d->tokens.at(plane)),
                                    &d->vi, vi, width, height,
                                    d->mirror_boundary, d->dump_ir_path,
                                    d->prop_map, func_name, d->opt_level,
                                    d->approx_math, results, resolved_tile_x,
                                    resolved_tile_y);
                                jit_cache[key] = compiler.compile();
                            } catch (...) {
                                for (const auto& frame : src_frames) {
                                    vsapi->freeFrame(frame);
                                }
                                vsapi->freeFrame(dst_frame);
                                throw;
                            }
                        }
                        return jit_cache.at(key);
                    };

                    const bool auto_tile_x = d->tile_x == -1;
                    const bool auto_tile_y = d->tile_y == -1;

                    if (!auto_tile_x && !auto_tile_y) {
                        d->compiled.at(plane) =
                            get_or_compile(d->tile_x, d->tile_y);
                    } else {
                        const std::string autotune_key = generate_cache_key(
                            expr_str, &d->vi, vsapi, vi, d->mirror_boundary,
                            d->prop_map, width, height, {}, d->tile_x,
                            d->tile_y);

                        int best_tile_x = 0;
                        int best_tile_y = 0;
                        bool has_autotuned = false;
                        {
                            std::lock_guard<std::mutex> lock(
                                expr_autotune_cache_mutex);
                            auto it = expr_autotune_cache.find(autotune_key);
                            if (it != expr_autotune_cache.end()) {
                                best_tile_x = it->second.first;
                                best_tile_y = it->second.second;
                                has_autotuned = true;
                            }
                        }

                        if (!has_autotuned) {
                            constexpr std::array<int, 8> AUTO_TILE_CANDIDATES = {
                                1,  4,   8,  16, 32,
                                64, 128, 256}; // NOLINT(cppcoreguidelines-avoid-magic-numbers)

                            std::vector<std::pair<int, int>> candidates;
                            if (auto_tile_x && auto_tile_y) {
                                candidates.reserve(AUTO_TILE_CANDIDATES.size() *
                                                   AUTO_TILE_CANDIDATES.size());
                                for (int tx : AUTO_TILE_CANDIDATES) {
                                    for (int ty : AUTO_TILE_CANDIDATES) {
                                        candidates.emplace_back(tx, ty);
                                    }
                                }
                            } else if (auto_tile_x) {
                                candidates.reserve(AUTO_TILE_CANDIDATES.size());
                                for (int tx : AUTO_TILE_CANDIDATES) {
                                    candidates.emplace_back(tx, d->tile_y);
                                }
                            } else {
                                candidates.reserve(AUTO_TILE_CANDIDATES.size());
                                for (int ty : AUTO_TILE_CANDIDATES) {
                                    candidates.emplace_back(d->tile_x, ty);
                                }
                            }

                            double best_time_ns =
                                std::numeric_limits<double>::max();
                            CompiledFunction best_compiled;

                            for (const auto& [candidate_tile_x,
                                              candidate_tile_y] : candidates) {
                                CompiledFunction candidate = get_or_compile(
                                    candidate_tile_x, candidate_tile_y);

                                // Warm-up once before measuring.
                                candidate.func_ptr(nullptr, rwptrs.data(),
                                                   strides.data(),
                                                   props.data());

                                constexpr int MEASURED_RUNS =
                                    2; // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                                const auto start =
                                    std::chrono::steady_clock::now();
                                for (int run = 0; run < MEASURED_RUNS; ++run) {
                                    candidate.func_ptr(nullptr, rwptrs.data(),
                                                       strides.data(),
                                                       props.data());
                                }
                                const auto end =
                                    std::chrono::steady_clock::now();

                                const double avg_time_ns =
                                    static_cast<double>(
                                        std::chrono::duration_cast<
                                            std::chrono::nanoseconds>(end -
                                                                      start)
                                            .count()) /
                                    static_cast<double>(MEASURED_RUNS);
                                if (avg_time_ns < best_time_ns) {
                                    best_time_ns = avg_time_ns;
                                    best_tile_x = candidate_tile_x;
                                    best_tile_y = candidate_tile_y;
                                    best_compiled = candidate;
                                }
                            }

                            if (best_compiled.func_ptr == nullptr) {
                                throw std::runtime_error(
                                    "Auto tile benchmark failed to select a "
                                    "candidate.");
                            }

                            {
                                std::lock_guard<std::mutex> lock(
                                    expr_autotune_cache_mutex);
                                expr_autotune_cache[autotune_key] = {
                                    best_tile_x, best_tile_y};
                            }
                            d->compiled.at(plane) = best_compiled;
                        } else {
                            d->compiled.at(plane) =
                                get_or_compile(best_tile_x, best_tile_y);
                        }
                    }
                }

                d->compiled.at(plane).func_ptr(nullptr, rwptrs.data(),
                                               strides.data(), props.data());
            }
        }

        for (const auto& frame : src_frames) {
            vsapi->freeFrame(frame);
        }
        return dst_frame;
    }

    return nullptr;
}

// NOLINTBEGIN(readability-identifier-naming)
void VS_CC // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
exprFree(void* instanceData, [[maybe_unused]] VSCore* core,
         const VSAPI* vsapi) {
    // NOLINTEND(readability-identifier-naming)
    genericFree<ExprData>(instanceData, core, vsapi);
}

// NOLINTBEGIN(readability-identifier-naming)
void VS_CC // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
exprCreate(const VSMap* in, VSMap* out, [[maybe_unused]] void* userData,
           VSCore* core, const VSAPI* vsapi) {
    // NOLINTEND(readability-identifier-naming)
    auto d = std::make_unique<ExprData>();
    int err = 0;

    try {
        validate_and_init_clips<true>(d.get(), in, vsapi);
        parse_format_param(d.get(), in, vsapi, core);

        d->mirror_boundary = vsapi->mapGetInt(in, "boundary", 0, &err) != 0;

        const int nexpr = vsapi->mapNumElements(in, "expr");
        if (nexpr == 0) {
            throw std::runtime_error(
                "At least one expression must be provided.");
        }

        bool use_infix = vsapi->mapGetInt(in, "infix", 0, &err) != 0;

        std::array<std::string, 3> expr_strs;
        for (int i = 0; i < nexpr; ++i) {
            std::string input_expr = vsapi->mapGetData(in, "expr", i, &err);
            if (use_infix && !input_expr.empty()) {
                std::map<std::string, std::string> macros;
                macros["__EXPR__"] = "";
                macros["__NUM_PLANES__"] =
                    std::to_string(d->vi.format.numPlanes);
                macros["__WIDTH__"] = std::to_string(d->vi.width);
                macros["__HEIGHT__"] = std::to_string(d->vi.height);
                macros["__INPUT_NUM__"] = std::to_string(d->num_inputs);
                macros["__OUTPUT_BITDEPTH__"] =
                    std::to_string(d->vi.format.bitsPerSample);
                macros["__OUTPUT_COLORFAMILY__"] =
                    std::to_string(d->vi.format.colorFamily);
                macros["__SUBSAMPLE_W__"] =
                    std::to_string(d->vi.format.subSamplingW);
                macros["__SUBSAMPLE_H__"] =
                    std::to_string(d->vi.format.subSamplingH);
                macros["__PLANE_NO__"] = std::to_string(i);
                macros["__OUTPUT_SAMPLETYPE__"] = std::to_string(
                    (d->vi.format.sampleType == stFloat) ? 1 : 0);

                for (int j = 0; j < d->num_inputs; ++j) {
                    const VSVideoInfo* input_vi =
                        vsapi->getVideoInfo(d->nodes[j]);
                    macros[std::format("__INPUT_BITDEPTH_{}__", j)] =
                        std::to_string(input_vi->format.bitsPerSample);
                    macros[std::format("__INPUT_COLORFAMILY_{}__", j)] =
                        std::to_string(input_vi->format.colorFamily);
                    macros[std::format("__INPUT_NUM_PLANES_{}__", j)] =
                        std::to_string(input_vi->format.numPlanes);
                    macros[std::format("__INPUT_SAMPLETYPE_{}__", j)] =
                        std::to_string(
                            (input_vi->format.sampleType == stFloat) ? 1 : 0);
                }

                expr_strs.at(i) = convert_infix_to_postfix(
                    input_expr, d->num_inputs, infix2postfix::Mode::Expr,
                    &macros);
            } else {
                expr_strs.at(i) = input_expr;
            }
        }
        for (int i = nexpr; i < d->vi.format.numPlanes; ++i) {
            expr_strs.at(i) = expr_strs.at(nexpr - 1);
        }

        for (int i = 0; i < d->vi.format.numPlanes; ++i) {
            if (expr_strs.at(i).empty()) {
                d->plane_op.at(i) = PlaneOp::PoCopy;
                continue;
            }
            d->plane_op.at(i) = PlaneOp::PoProcess;
            d->tokens.at(i) =
                tokenize(expr_strs.at(i), d->num_inputs, ExprMode::Expr);

            for (const auto& token : d->tokens.at(i)) {
                if (token.type == TokenType::PropAccess ||
                    token.type == TokenType::PropExists) {
                    const auto& payload =
                        std::get<TokenPayloadPropAccess>(token.payload);
                    auto key =
                        std::make_pair(payload.clip_idx, payload.prop_name);
                    if (!d->prop_map.contains(key)) {
                        d->prop_map[key] = static_cast<int>(
                            1 + d->required_props
                                    .size()); // 0 is for frame number N
                        d->required_props.push_back(key);
                    }
                }
            }

            auto analyser = std::make_unique<analysis::AnalysisManager>(
                d->tokens.at(i), d->mirror_boundary);
            analysis::ExpressionAnalyzer expr_analyzer(*analyser);
            expr_analyzer.analyze();
            d->analysis_managers.at(i) = std::move(analyser);
        }

        parse_common_params(d.get(), in, vsapi);
        parse_expr_tiling_params(d.get(), in, vsapi);

    } catch (const std::exception& e) {
        for (auto* node : d->nodes) {
            if (node != nullptr) {
                vsapi->freeNode(node);
            }
        }
        vsapi->mapSetError(out, std::format("Expr: {}", e.what()).c_str());
        return;
    }

    std::vector<VSFilterDependency> deps;
    deps.reserve(d->nodes.size());
    for (auto* node : d->nodes) {
        deps.push_back({node, rpStrictSpatial});
    }

    VSVideoInfo* vi_ptr = &d->vi;

    vsapi->createVideoFilter(out, "Expr", vi_ptr, exprGetFrame, exprFree,
                             fmParallel, deps.data(),
                             static_cast<int>(deps.size()), d.release(), core);
}

// NOLINTBEGIN(readability-identifier-naming)
void VS_CC // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
singleExprFree(void* instanceData, [[maybe_unused]] VSCore* core,
               const VSAPI* vsapi) {
    // NOLINTEND(readability-identifier-naming)
    genericFree<SingleExprData>(instanceData, core, vsapi);
}

// NOLINTBEGIN(readability-identifier-naming)
const VSFrame*
    VS_CC // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    singleExprGetFrame(int n, int activationReason, void* instanceData,
                       [[maybe_unused]] void** frameData,
                       VSFrameContext* frameCtx, VSCore* core,
                       const VSAPI* vsapi) {
    // NOLINTEND(readability-identifier-naming)
    auto* d = static_cast<SingleExprData*>(instanceData);

    if (activationReason == arInitial) {
        for (int i = 0; i < d->num_inputs; ++i) {
            vsapi->requestFrameFilter(n, d->nodes[i], frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        g_frame_data.dynamic_arrays.clear();

        std::vector<const VSFrame*> src_frames(d->num_inputs);
        for (int i = 0; i < d->num_inputs; ++i) {
            src_frames[i] = vsapi->getFrameFilter(n, d->nodes[i], frameCtx);
        }

        std::array<const VSFrame*, 3> plane_src = {src_frames[0], src_frames[0],
                                                   src_frames[0]};
        std::array<int, 3> planes = {0, 1, 2};
        VSFrame* dst_frame = vsapi->newVideoFrame2(
            &d->vi.format, d->vi.width, d->vi.height, plane_src.data(),
            planes.data(), src_frames[0], core);

        int num_planes = d->vi.format.numPlanes;
        std::vector<uint8_t*> rwptrs((d->num_inputs + 1) * num_planes);
        std::vector<int> strides((d->num_inputs + 1) * num_planes);
        std::vector<float> props(1 + d->required_props.size() +
                                 d->output_props.size());

        read_frame_properties(props, src_frames, d->required_props, n, vsapi);

        for (size_t i = 0; i < d->output_props.size(); ++i) {
            props[1 + d->required_props.size() + i] =
                std::bit_cast<float>(PROP_WRITE_NAN_PAYLOAD);
        }

        for (int i = 0; i <= d->num_inputs; ++i) {
            for (int p = 0; p < num_planes; ++p) {
                rwptrs[(i * num_planes) + p] =
                    (i == 0)
                        ? vsapi->getWritePtr(dst_frame, p)
                        : const_cast< // NOLINT(cppcoreguidelines-pro-type-const-cast)
                              uint8_t*>(
                              vsapi->getReadPtr(src_frames[i - 1], p));
                strides[(i * num_planes) + p] = static_cast<int>(
                    (i == 0) ? vsapi->getStride(dst_frame, p)
                             : vsapi->getStride(src_frames[i - 1], p));
            }
        }

        if (d->compiled.func_ptr == nullptr) {
            std::vector<const VSVideoInfo*> vi(d->num_inputs);
            for (int i = 0; i < d->num_inputs; ++i) {
                vi[i] = vsapi->getVideoInfo(d->nodes[i]);
            }

            std::string expr_str;
            for (const auto& token : d->tokens) {
                if (!expr_str.empty()) {
                    expr_str += " ";
                }
                expr_str += token.text;
            }

            std::vector<std::string> output_prop_names;
            output_prop_names.reserve(d->output_props.size());
            for (const auto& p : d->output_props) {
                output_prop_names.push_back(p.first);
            }

            const std::string key = generate_cache_key(
                expr_str, &d->vi, vsapi, vi, d->mirror_boundary, d->prop_map,
                d->vi.width, d->vi.height, output_prop_names);

            std::lock_guard<std::mutex> lock(cache_mutex);
            if (!jit_cache.contains(key)) {
                size_t key_hash = std::hash<std::string>{}(key);
                std::string func_name =
                    std::format("process_single_expr_{}", key_hash);

                try {
                    analysis::ExpressionAnalysisResults results(
                        *d->analysis_manager);
                    Compiler compiler(
                        std::vector<Token>(d->tokens), &d->vi, vi, d->vi.width,
                        d->vi.height, d->mirror_boundary, d->dump_ir_path,
                        d->prop_map, func_name, d->opt_level, d->approx_math,
                        results, 0, 0, ExprMode::SingleExpr, output_prop_names);
                    jit_cache[key] = compiler.compile();
                } catch (const std::exception& e) {
                    for (const auto& frame : src_frames) {
                        vsapi->freeFrame(frame);
                    }
                    vsapi->freeFrame(dst_frame);
                    throw;
                }
            }
            d->compiled = jit_cache.at(key);
        }

        d->compiled.func_ptr(d, rwptrs.data(), strides.data(), props.data());

        // Resolve prop types and write to output frame
        enum class ResolvedPropWriteType : std::uint8_t { Int, Float };
        std::vector<ResolvedPropWriteType> resolved_types;
        resolved_types.reserve(d->output_props.size());
        const VSMap* src_props = vsapi->getFramePropertiesRO(src_frames[0]);

        for (const auto& prop_info : d->output_props) {
            const auto& prop_name = prop_info.first;
            const auto prop_write_type = prop_info.second;

            switch (prop_write_type) {
            case PropWriteType::Int:
                resolved_types.push_back(ResolvedPropWriteType::Int);
                break;
            case PropWriteType::Float:
                resolved_types.push_back(ResolvedPropWriteType::Float);
                break;
            case PropWriteType::Delete:
                // The prop will be deleted so anything is fine.
                resolved_types.push_back(ResolvedPropWriteType::Float);
                break;
            case PropWriteType::AutoInt:
            case PropWriteType::AutoFloat:
                int existing_type =
                    vsapi->mapGetType(src_props, prop_name.c_str());
                if (existing_type == ptInt) {
                    resolved_types.push_back(ResolvedPropWriteType::Int);
                } else if (existing_type == ptFloat) {
                    resolved_types.push_back(ResolvedPropWriteType::Float);
                } else {
                    if (prop_write_type == PropWriteType::AutoInt) {
                        resolved_types.push_back(ResolvedPropWriteType::Int);
                    } else {
                        resolved_types.push_back(ResolvedPropWriteType::Float);
                    }
                }
                break;
            }
        }

        VSMap* dst_props = vsapi->getFramePropertiesRW(dst_frame);
        for (size_t i = 0; i < d->output_props.size(); ++i) {
            const auto& prop_name = d->output_props[i].first;
            float value = props[1 + d->required_props.size() + i];

            if (std::bit_cast<uint32_t>(value) == PROP_WRITE_NAN_PAYLOAD) {
                continue;
            }

            if (std::bit_cast<uint32_t>(value) == PROP_DELETE_NAN_PAYLOAD) {
                vsapi->mapDeleteKey(dst_props, prop_name.c_str());
                continue;
            }

            if (resolved_types[i] == ResolvedPropWriteType::Int) {
                auto int_value = static_cast<int64_t>(lroundf(value));
                vsapi->mapSetInt(dst_props, prop_name.c_str(), int_value,
                                 maReplace);
            } else { // FLOAT
                vsapi->mapSetFloat(dst_props, prop_name.c_str(), value,
                                   maReplace);
            }
        }

        for (const auto& frame : src_frames) {
            vsapi->freeFrame(frame);
        }
        return dst_frame;
    }

    return nullptr;
}

// NOLINTBEGIN(readability-identifier-naming)
void VS_CC // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
singleExprCreate(const VSMap* in, VSMap* out, [[maybe_unused]] void* userData,
                 VSCore* core, const VSAPI* vsapi) {
    // NOLINTEND(readability-identifier-naming)
    auto d = std::make_unique<SingleExprData>();
    int err = 0;

    try {
        validate_and_init_clips<false>(d.get(), in, vsapi);
        parse_format_param(d.get(), in, vsapi, core);

        d->mirror_boundary = vsapi->mapGetInt(in, "boundary", 0, &err) != 0;

        const char* expr_str = vsapi->mapGetData(in, "expr", 0, &err);
        if (err != 0) {
            throw std::runtime_error("An expression must be provided.");
        }

        bool use_infix = vsapi->mapGetInt(in, "infix", 0, &err) != 0;

        std::string processed_expr;
        if (use_infix) {
            std::map<std::string, std::string> macros;
            macros["__SINGLEEXPR__"] = "";
            macros["__NUM_PLANES__"] = std::to_string(d->vi.format.numPlanes);
            macros["__WIDTH__"] = std::to_string(d->vi.width);
            macros["__HEIGHT__"] = std::to_string(d->vi.height);
            macros["__INPUT_NUM__"] = std::to_string(d->num_inputs);
            macros["__OUTPUT_BITDEPTH__"] =
                std::to_string(d->vi.format.bitsPerSample);
            macros["__OUTPUT_COLORFAMILY__"] =
                std::to_string(d->vi.format.colorFamily);
            macros["__SUBSAMPLE_W__"] =
                std::to_string(d->vi.format.subSamplingW);
            macros["__SUBSAMPLE_H__"] =
                std::to_string(d->vi.format.subSamplingH);
            macros["__OUTPUT_SAMPLETYPE__"] =
                std::to_string((d->vi.format.sampleType == stFloat) ? 1 : 0);

            for (int i = 0; i < d->num_inputs; ++i) {
                const VSVideoInfo* input_vi = vsapi->getVideoInfo(d->nodes[i]);
                macros[std::format("__INPUT_BITDEPTH_{}__", i)] =
                    std::to_string(input_vi->format.bitsPerSample);
                macros[std::format("__INPUT_COLORFAMILY_{}__", i)] =
                    std::to_string(input_vi->format.colorFamily);
                macros[std::format("__INPUT_NUM_PLANES_{}__", i)] =
                    std::to_string(input_vi->format.numPlanes);
                macros[std::format("__INPUT_SAMPLETYPE_{}__", i)] =
                    std::to_string(
                        (input_vi->format.sampleType == stFloat) ? 1 : 0);
                macros[std::format("__INPUT_WIDTH_{}__", i)] =
                    std::to_string(input_vi->width);
                macros[std::format("__INPUT_HEIGHT_{}__", i)] =
                    std::to_string(input_vi->height);
                macros[std::format("__INPUT_SUBSAMPLE_W_{}__", i)] =
                    std::to_string(input_vi->format.subSamplingW);
                macros[std::format("__INPUT_SUBSAMPLE_H_{}__", i)] =
                    std::to_string(input_vi->format.subSamplingH);
            }

            processed_expr = convert_infix_to_postfix(
                expr_str, d->num_inputs, infix2postfix::Mode::Single, &macros);
        } else {
            processed_expr = expr_str;
        }

        d->tokens =
            tokenize(processed_expr, d->num_inputs, ExprMode::SingleExpr);

        // Array optimization passes
        {
            analysis::AnalysisManager temp_am(d->tokens, d->mirror_boundary, 0);
            analysis::StaticArrayOptPass static_opt_pass;
            static_opt_pass.run(d->tokens, temp_am);
            analysis::DynamicArrayAllocOptPass dyn_opt_pass;
            dyn_opt_pass.run(d->tokens, temp_am);
        }

        for (const auto& token : d->tokens) {
            if (token.type == TokenType::ConstantPlaneWidth ||
                token.type == TokenType::ConstantPlaneHeight) {
                const auto& payload =
                    std::get<TokenPayloadPlaneDim>(token.payload);
                if (payload.plane_idx < 0 ||
                    payload.plane_idx >= d->vi.format.numPlanes) {
                    throw std::runtime_error(
                        std::format("Invalid plane index {} in token '{}'",
                                    payload.plane_idx, token.text));
                }
            } else if (token.type == TokenType::PropAccess ||
                       token.type == TokenType::PropExists) {
                const auto& payload =
                    std::get<TokenPayloadPropAccess>(token.payload);
                auto key = std::make_pair(payload.clip_idx, payload.prop_name);
                if (!d->prop_map.contains(key)) {
                    d->prop_map[key] = static_cast<int>(
                        1 +
                        d->required_props.size()); // 0 is for frame number N
                    d->required_props.push_back(key);
                }
            } else if (token.type == TokenType::PropStore) {
                const auto& payload =
                    std::get<TokenPayloadPropStore>(token.payload);
                if (!d->output_prop_map.contains(payload.prop_name)) {
                    d->output_prop_map[payload.prop_name] =
                        static_cast<int>(d->output_props.size());
                    d->output_props.emplace_back(payload.prop_name,
                                                 payload.type);
                }
            }
        }

        auto analyser = std::make_unique<analysis::AnalysisManager>(
            d->tokens, d->mirror_boundary, 0);
        analysis::ExpressionAnalyzer expr_analyzer(*analyser);
        expr_analyzer.analyze();
        d->analysis_manager = std::move(analyser);

        parse_common_params(d.get(), in, vsapi);

    } catch (const std::exception& e) {
        for (auto* node : d->nodes) {
            if (node != nullptr) {
                vsapi->freeNode(node);
            }
        }
        vsapi->mapSetError(out,
                           std::format("SingleExpr: {}", e.what()).c_str());
        return;
    }

    std::vector<VSFilterDependency> deps;
    deps.reserve(d->nodes.size());
    for (auto* node : d->nodes) {
        deps.push_back({node, rpStrictSpatial});
    }

    VSVideoInfo* vi_ptr = &d->vi;

    vsapi->createVideoFilter(out, "SingleExpr", vi_ptr, singleExprGetFrame,
                             singleExprFree, fmParallel, deps.data(),
                             static_cast<int>(deps.size()), d.release(), core);
}

struct VkExprData : BaseExprData {
    std::array<PlaneOp, 3> plane_op = {};
    std::array<std::vector<std::vector<Token>>, 3> tokens_stages;
    std::array<std::vector<std::unique_ptr<analysis::AnalysisManager>>, 3>
        analysis_managers;

    int device_id = -1;
    int num_streams = 8; // NOLINT(cppcoreguidelines-avoid-magic-numbers)
    std::unique_ptr<vkexpr::VkExprExecutor> executor;

    std::string dump_glsl_path;
};

// NOLINTBEGIN(readability-identifier-naming)
const VSFrame*
    VS_CC // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    vkExprGetFrame(int n, int activationReason, void* instanceData,
                   [[maybe_unused]] void** frameData, VSFrameContext* frameCtx,
                   VSCore* core, const VSAPI* vsapi) {
    // NOLINTEND(readability-identifier-naming)
    auto* d = static_cast<VkExprData*>(instanceData);

    if (activationReason == arInitial) {
        for (int i = 0; i < d->num_inputs; ++i) {
            vsapi->requestFrameFilter(n, d->nodes[i], frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        std::vector<const VSFrame*> src_frames(d->num_inputs);
        for (int i = 0; i < d->num_inputs; ++i) {
            src_frames[i] = vsapi->getFrameFilter(n, d->nodes[i], frameCtx);
        }

        std::array<const VSFrame*, 3> plane_src = {
            d->plane_op.at(0) == PlaneOp::PoCopy ? src_frames[0] : nullptr,
            d->plane_op.at(1) == PlaneOp::PoCopy ? src_frames[0] : nullptr,
            d->plane_op.at(2) == PlaneOp::PoCopy ? src_frames[0] : nullptr};
        std::array<int, 3> planes = {0, 1, 2};
        VSFrame* dst_frame = vsapi->newVideoFrame2(
            &d->vi.format, d->vi.width, d->vi.height, plane_src.data(),
            planes.data(), src_frames[0], core);

        std::vector<float> props(1 + d->required_props.size());
        read_frame_properties(props, src_frames, d->required_props, n, vsapi);

        for (int plane = 0; plane < d->vi.format.numPlanes; ++plane) {
            if (d->plane_op.at(plane) != PlaneOp::PoProcess) {
                continue;
            }

            try {
                d->executor->processPlane(plane, n, src_frames, dst_frame,
                                          props, vsapi);

            } catch (const std::exception& e) {
                for (const auto& frame : src_frames) {
                    vsapi->freeFrame(frame);
                }
                vsapi->freeFrame(dst_frame);
                vsapi->setFilterError(
                    std::format("VkExpr: GPU error: {}", e.what()).c_str(),
                    frameCtx);
                return nullptr;
            }
        }

        for (const auto& frame : src_frames) {
            vsapi->freeFrame(frame);
        }
        return dst_frame;
    }

    return nullptr;
}

// NOLINTBEGIN(readability-identifier-naming)
void VS_CC // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
vkExprFree(void* instanceData, [[maybe_unused]] VSCore* core,
           const VSAPI* vsapi) {
    // NOLINTEND(readability-identifier-naming)
    auto* raw = static_cast<VkExprData*>(instanceData);
    raw->executor.reset();
    std::unique_ptr<VkExprData> d(raw);
    for (auto* node : d->nodes) {
        vsapi->freeNode(node);
    }
}

// NOLINTBEGIN(readability-identifier-naming)
void VS_CC // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
vkExprCreate(const VSMap* in, VSMap* out, [[maybe_unused]] void* userData,
             VSCore* core, const VSAPI* vsapi) {
    // NOLINTEND(readability-identifier-naming)
    auto d = std::make_unique<VkExprData>();
    int err = 0;

    try {
        // Validate and initialize clips
        validate_and_init_clips<true>(d.get(), in, vsapi);

        parse_format_param(d.get(), in, vsapi, core);

        const int nexpr = vsapi->mapNumElements(in, "expr");
        if (nexpr == 0) {
            throw std::runtime_error(
                "At least one expression must be provided.");
        }

        std::array<std::string, 3> expr_strs;
        for (int i = 0; i < nexpr && i < 3; ++i) {
            expr_strs.at(i) = vsapi->mapGetData(in, "expr", i, &err);
        }
        for (int i = nexpr; i < d->vi.format.numPlanes; ++i) {
            expr_strs.at(i) = expr_strs.at(nexpr - 1);
        }

        d->mirror_boundary = vsapi->mapGetInt(in, "boundary", 0, &err) != 0;

        const char* dump_glsl_path =
            vsapi->mapGetData(in, "dump_glsl", 0, &err);
        if ((err == 0) && (dump_glsl_path != nullptr)) {
            d->dump_glsl_path = dump_glsl_path;
        }

        bool use_infix = vsapi->mapGetInt(in, "infix", 0, &err) != 0;

        std::array<std::vector<std::string>, 3> processed_stages;
        for (int i = 0; i < nexpr && i < 3; ++i) {
            std::string raw_expr = expr_strs.at(i);
            std::vector<std::string> stages;
            constexpr std::string_view POSTFIX_STAGE_SEPARATOR = "##";
            constexpr std::string_view INFIX_STAGE_SEPARATOR = "---";
            const std::string_view stage_separator =
                use_infix ? INFIX_STAGE_SEPARATOR : POSTFIX_STAGE_SEPARATOR;
            size_t pos = 0;
            while ((pos = raw_expr.find(stage_separator)) !=
                   std::string::npos) {
                stages.push_back(raw_expr.substr(0, pos));
                raw_expr.erase(0, pos + stage_separator.size());
            }
            stages.push_back(raw_expr);

            if (use_infix) {
                std::map<std::string, std::string> macros;
                macros["__GPU__"] = "";
                macros["__EXPR__"] = "";
                macros["__NUM_PLANES__"] =
                    std::to_string(d->vi.format.numPlanes);
                macros["__WIDTH__"] = std::to_string(d->vi.width);
                macros["__HEIGHT__"] = std::to_string(d->vi.height);
                macros["__INPUT_NUM__"] = std::to_string(d->num_inputs);
                macros["__OUTPUT_BITDEPTH__"] =
                    std::to_string(d->vi.format.bitsPerSample);
                macros["__OUTPUT_COLORFAMILY__"] =
                    std::to_string(d->vi.format.colorFamily);
                macros["__SUBSAMPLE_W__"] =
                    std::to_string(d->vi.format.subSamplingW);
                macros["__SUBSAMPLE_H__"] =
                    std::to_string(d->vi.format.subSamplingH);
                macros["__PLANE_NO__"] = std::to_string(i);
                macros["__OUTPUT_SAMPLETYPE__"] = std::to_string(
                    (d->vi.format.sampleType == stFloat) ? 1 : 0);

                for (int j = 0; j < d->num_inputs; ++j) {
                    const VSVideoInfo* input_vi =
                        vsapi->getVideoInfo(d->nodes[j]);
                    macros[std::format("__INPUT_BITDEPTH_{}__", j)] =
                        std::to_string(input_vi->format.bitsPerSample);
                    macros[std::format("__INPUT_COLORFAMILY_{}__", j)] =
                        std::to_string(input_vi->format.colorFamily);
                    macros[std::format("__INPUT_NUM_PLANES_{}__", j)] =
                        std::to_string(input_vi->format.numPlanes);
                    macros[std::format("__INPUT_SAMPLETYPE_{}__", j)] =
                        std::to_string(
                            (input_vi->format.sampleType == stFloat) ? 1 : 0);
                }

                for (size_t stage_idx = 0; stage_idx < stages.size();
                     ++stage_idx) {
                    auto& stage = stages[stage_idx];
                    if (!stage.empty()) {
                        stage = convert_infix_to_postfix(
                            stage, d->num_inputs, infix2postfix::Mode::VkExpr,
                            &macros, static_cast<int>(stage_idx));
                    }
                }
            }
            processed_stages.at(i) = stages;
        }
        for (int i = nexpr; i < d->vi.format.numPlanes; ++i) {
            processed_stages.at(i) = processed_stages.at(nexpr - 1);
        }

        for (int i = 0; i < d->vi.format.numPlanes; ++i) {
            if (processed_stages.at(i).empty() ||
                (processed_stages.at(i).size() == 1 &&
                 processed_stages.at(i)[0].empty())) {
                d->plane_op.at(i) = PlaneOp::PoCopy;
            } else {
                d->plane_op.at(i) = PlaneOp::PoProcess;

                auto& plane_stages = processed_stages.at(i);
                d->tokens_stages.at(i).resize(plane_stages.size());
                d->analysis_managers.at(i).resize(plane_stages.size());

                for (size_t s = 0; s < plane_stages.size(); ++s) {
                    d->tokens_stages.at(i).at(s) =
                        tokenize(plane_stages.at(s), d->num_inputs,
                                 ExprMode::VkExpr, static_cast<int>(s));

                    for (const auto& token : d->tokens_stages.at(i).at(s)) {
                        if (token.type == TokenType::PropAccess ||
                            token.type == TokenType::PropExists) {
                            const auto& payload =
                                std::get<TokenPayloadPropAccess>(token.payload);
                            auto key = std::make_pair(payload.clip_idx,
                                                      payload.prop_name);
                            if (!d->prop_map.contains(key)) {
                                d->prop_map[key] = static_cast<int>(
                                    1 + d->required_props.size()); // 0 is for N
                                d->required_props.push_back(key);
                            }
                        }
                    }

                    auto analyser = std::make_unique<analysis::AnalysisManager>(
                        d->tokens_stages.at(i).at(s), d->mirror_boundary);
                    analysis::ExpressionAnalyzer expr_analyzer(*analyser);
                    expr_analyzer.analyze();
                    d->analysis_managers.at(i).at(s) = std::move(analyser);
                }
            }
        }

        d->num_streams =
            static_cast<int>(vsapi->mapGetInt(in, "num_streams", 0, &err));
        if (err != 0 || d->num_streams < 1) {
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
            d->num_streams = 8;
        }

        d->device_id =
            static_cast<int>(vsapi->mapGetInt(in, "device_id", 0, &err));
        if (err != 0) {
            d->device_id = -1;
        }
        if (d->device_id < -1) {
            throw std::runtime_error("device_id must be >= -1");
        }

        auto num_props_floats =
            static_cast<uint32_t>(1 + d->required_props.size()); // N + props

        std::array<std::vector<std::string>, 3> glsl_stages;
        for (int i = 0; i < d->vi.format.numPlanes; ++i) {
            if (d->plane_op.at(i) != PlaneOp::PoProcess) {
                continue;
            }

            auto num_stages = d->tokens_stages.at(i).size();
            glsl_stages.at(i).resize(num_stages);

            for (size_t s = 0; s < num_stages; ++s) {
                analysis::ExpressionAnalysisResults analysis_results(
                    *d->analysis_managers.at(i).at(s));
                GLSLGenerator generator(
                    d->tokens_stages.at(i).at(s), d->num_inputs,
                    static_cast<int>(s),
                    d->vi.width / (i == 0 ? 1 : d->vi.format.subSamplingW),
                    d->vi.height / (i == 0 ? 1 : d->vi.format.subSamplingH),
                    d->mirror_boundary, d->prop_map, analysis_results);

                glsl_stages.at(i).at(s) = generator.generate();

                if (!d->dump_glsl_path.empty()) {
                    std::string plane_specific_path = d->dump_glsl_path;
                    size_t dot_pos = plane_specific_path.rfind('.');
                    std::string suffix = std::format(".plane{}.stage{}", i, s);
                    if (dot_pos != std::string::npos) {
                        plane_specific_path.insert(dot_pos, suffix);
                    } else {
                        plane_specific_path += suffix;
                    }

                    std::ofstream glsl_file(plane_specific_path);
                    if (glsl_file.is_open()) {
                        glsl_file << glsl_stages.at(i).at(s);
                        glsl_file.close();
                    }
                }
            }
        }

        d->executor = std::make_unique<vkexpr::VkExprExecutor>(
            d->device_id, d->num_streams, d->num_inputs, std::move(glsl_stages),
            num_props_floats);

    } catch (const std::exception& e) {
        for (auto* node : d->nodes) {
            if (node != nullptr) {
                vsapi->freeNode(node);
            }
        }
        vsapi->mapSetError(out, std::format("VkExpr: {}", e.what()).c_str());
        return;
    }

    std::vector<VSFilterDependency> deps;
    deps.reserve(d->nodes.size());
    for (auto* node : d->nodes) {
        deps.push_back({node, rpStrictSpatial});
    }

    VSVideoInfo* vi_ptr = &d->vi;

    vsapi->createVideoFilter(out, "VkExpr", vi_ptr, vkExprGetFrame, vkExprFree,
                             fmParallel, deps.data(),
                             static_cast<int>(deps.size()), d.release(), core);
}

} // anonymous namespace

// Host API for JIT code to manage dynamic arrays
// TODO: Move this to a separate file.
// TODO: Optimize this.
extern "C" {

float* llvmexpr_ensure_buffer(const char* name, int64_t requested_size) {
    auto& array = g_frame_data.dynamic_arrays[std::string(name)];
    if (static_cast<size_t>(requested_size) > array.buffer.size()) {
        array.buffer.resize(requested_size);
    }
    return array.buffer.data();
}

int64_t llvmexpr_get_buffer_size(const char* name) {
    auto it = g_frame_data.dynamic_arrays.find(std::string(name));
    return (it != g_frame_data.dynamic_arrays.end())
               ? static_cast<int64_t>(it->second.buffer.size())
               : 0;
}

} // extern "C"

// NOLINTBEGIN(readability-identifier-naming)
VS_EXTERNAL_API(void)
VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
    // NOLINTEND(readability-identifier-naming)
    vspapi->configPlugin(
        "com.yuygfgg.llvmexpr", "llvmexpr", "LLVM JIT RPN Expression Filter",
        VS_MAKE_VERSION(4, 3), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction(
        "Expr",
        "clips:vnode[];expr:data[];format:int:opt;boundary:int:opt;"
        "dump_ir:data:opt;opt_level:int:opt;approx_math:int:opt;infix:int:opt;"
        "tile_x:int:opt;tile_y:int:opt;",
        "clip:vnode;", exprCreate, nullptr, plugin);
    vspapi->registerFunction("SingleExpr",
                             "clips:vnode[];expr:data;format:int:opt;boundary:"
                             "int:opt;dump_ir:data:opt;opt_"
                             "level:int:opt;approx_math:int:opt;infix:int:opt;",
                             "clip:vnode;", singleExprCreate, nullptr, plugin);

    vspapi->registerFunction("VkExpr",
                             "clips:vnode[];expr:data[];format:int:opt;"
                             "boundary:int:opt;num_streams:int:opt;device_id:"
                             "int:opt;dump_glsl:data:opt;infix:int:opt;",
                             "clip:vnode;", vkExprCreate, nullptr, plugin);
}
