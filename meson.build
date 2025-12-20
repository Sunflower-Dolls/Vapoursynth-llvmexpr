# Copyright (C) 2025 yuygfgg

# This file is part of Vapoursynth-llvmexpr.

# Vapoursynth-llvmexpr is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# Vapoursynth-llvmexpr is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with Vapoursynth-llvmexpr.  If not, see <https://www.gnu.org/licenses/>.

project('llvmexpr', 'cpp', 'c',
  default_options: ['buildtype=release', 'warning_level=2', 'b_ndebug=if-release', 'cpp_std=c++23', 'c_gnu_symbol_visibility=hidden', 'cpp_gnu_symbol_visibility=hidden'],
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.4.0',
  version: '4.3'
)

cxx = meson.get_compiler('cpp')
cc = meson.get_compiler('c')

if cxx.get_id() != 'clang'
  error('This project requires Clang C++ compiler. Current compiler: ' + cxx.get_id())
endif

if cc.get_id() != 'clang'
  error('This project requires Clang C compiler. Current compiler: ' + cc.get_id())
endif

buildtype = get_option('buildtype')
if buildtype != 'debug' and buildtype != 'debugoptimized'
  add_project_link_arguments('-flto', language: ['cpp', 'c'])
  add_project_arguments('-flto', language: ['cpp', 'c'])
endif

enable_sanitizers = get_option('enable-sanitizers')
if enable_sanitizers
  sanitizer_args = [
    '-fno-omit-frame-pointer',
    '-fno-optimize-sibling-calls',
    '-O1'
  ]
  add_project_arguments(sanitizer_args, language: ['cpp', 'c'])
endif

vapoursynth_dep = dependency('vapoursynth', version: '>=55').partial_dependency(compile_args: true, includes: true)
install_dir = vapoursynth_dep.get_variable(pkgconfig: 'libdir') / 'vapoursynth'

ctre_dep = dependency('ctre', fallback: ['ctre', 'ctre_dep'], required: true)

static_llvm = get_option('static-llvm')
llvm_dep = dependency('llvm', version: '>=20.0.0', method: 'config-tool', modules: ['core', 'orcjit', 'native', 'all-targets'], static: static_llvm)

llvm_inc_dir = include_directories(llvm_dep.get_variable('includedir'), is_system: true)
llvm_link_dep = llvm_dep.partial_dependency(link_args: true)
llvm_dep = declare_dependency(include_directories: llvm_inc_dir, dependencies: llvm_link_dep)

dependencies = [vapoursynth_dep, llvm_dep, ctre_dep]
link_args = []

if static_llvm
  zlib_dep = dependency('zlib', static: true, required: false)
  if zlib_dep.found()
    dependencies += zlib_dep
  endif

  zstd_dep_check = dependency('libzstd', static: true, required: false)
  if zstd_dep_check.found() and host_machine.system() == 'linux'
    link_args += ['-Wl,-Bstatic', '-lzstd', '-Wl,-Bdynamic']
  elif zstd_dep_check.found() and host_machine.system() == 'windows'
    # On Windows, we need to link zstd statically without the .dll suffix
    link_args += ['-lzstd']
    dependencies += zstd_dep_check
  elif zstd_dep_check.found()
    dependencies += zstd_dep_check
  endif

  xml2_dep = dependency('libxml-2.0', static: true, required: false)
  if xml2_dep.found()
    dependencies += xml2_dep
  endif
endif


if static_llvm and host_machine.system() == 'windows'
  link_args += '-static'
endif

sources = [
  'llvmexpr/llvmexpr.cpp',
  'llvmexpr/frontend/Tokenizer.cpp',
  'llvmexpr/frontend/InfixConverter.cpp',
  'llvmexpr/frontend/infix2postfix/Preprocessor.cpp',
  'llvmexpr/frontend/infix2postfix/StandardLibrary.cpp',
  'llvmexpr/frontend/infix2postfix/Builtins.cpp',
  'llvmexpr/frontend/infix2postfix/Tokenizer.cpp',
  'llvmexpr/frontend/infix2postfix/Parser.cpp',
  'llvmexpr/frontend/infix2postfix/AnalysisEngine.cpp',
  'llvmexpr/frontend/infix2postfix/SemanticAnalyzer.cpp',
  'llvmexpr/frontend/infix2postfix/SymbolTable.cpp',
  'llvmexpr/frontend/infix2postfix/CodeGenerator.cpp',
  'llvmexpr/frontend/infix2postfix/PostfixBuilder.cpp',
  'llvmexpr/frontend/infix2postfix/PostfixHelper.cpp',
  'llvmexpr/analysis/framework/AnalysisManager.cpp',
  'llvmexpr/analysis/ExpressionAnalyzer.cpp',
  'llvmexpr/analysis/passes/BuildCFGPass.cpp',
  'llvmexpr/analysis/passes/BlockAnalysisPass.cpp',
  'llvmexpr/analysis/passes/ConstPropPass.cpp',
  'llvmexpr/analysis/passes/StaticArrayOptPass.cpp',
  'llvmexpr/analysis/passes/DynamicArrayAllocOptPass.cpp',
  'llvmexpr/analysis/passes/VarInitPass.cpp',
  'llvmexpr/analysis/passes/StaticAllocReachabilityPass.cpp',
  'llvmexpr/analysis/passes/ValidationPass.cpp',
  'llvmexpr/analysis/passes/StackSafetyPass.cpp',
  'llvmexpr/analysis/passes/StructurizeCFGPass.cpp',
  'llvmexpr/analysis/passes/PropWriteTypeSafetyPass.cpp',
  'llvmexpr/analysis/passes/RelAccessAnalysisPass.cpp',
  'llvmexpr/analysis/passes/CoordinateUsagePass.cpp',
  'llvmexpr/analysis/passes/VariableUsagePass.cpp',
  'llvmexpr/codegen/llvm/ExprIRGenerator.cpp',
  'llvmexpr/codegen/llvm/SingleExprIRGenerator.cpp',
  'llvmexpr/codegen/llvm/IRGeneratorBase.cpp',
  'llvmexpr/runtime/llvm/Compiler.cpp',
  'llvmexpr/runtime/llvm/Jit.cpp',
  'llvmexpr/codegen/llvm/Diagnostics.cpp',
  'llvmexpr/codegen/glsl/GLSLGenerator.cpp',
  'llvmexpr/runtime/vulkan/VulkanContext.cpp',
  'llvmexpr/runtime/vulkan/VulkanMemory.cpp',
  'llvmexpr/runtime/vulkan/VulkanComputePipeline.cpp',
  'llvmexpr/runtime/vulkan/VkExprExecutor.cpp',
]

vulkan_headers_dep = dependency(
  'vulkan-headers',
  fallback: ['vulkan-headers', 'vulkan_headers_dep'],
  required: false,
)

# Some distros don't ship a standalone `vulkan-headers.pc`.
# Fall back to the `vulkan` package for include dirs without forcing a link to libvulkan.
if not vulkan_headers_dep.found()
  vulkan_dep = dependency('vulkan', required: false)
  if vulkan_dep.found()
    vulkan_headers_dep = vulkan_dep.partial_dependency(compile_args: true, includes: true)
  else
    error('Vulkan headers not found (need vulkan-headers or vulkan).')
  endif
endif

volk_dep = dependency('volk', fallback: ['volk', 'volk_dep'], static: true)

vma_dep = dependency(
  'vulkan-memory-allocator',
  fallback: ['vulkan-memory-allocator', 'vma_dep'],
  required: false,
)
if not vma_dep.found()
  if cc.has_header('vk_mem_alloc.h')
    vma_dep = declare_dependency()
  else
    error('vk_mem_alloc.h not found (need vulkan-memory-allocator or the subproject).')
  endif
endif

static_shaderc = get_option('static-shaderc')
shaderc_dep = dependency('shaderc', static: static_shaderc)

dependencies += [vulkan_headers_dep, volk_dep, vma_dep, shaderc_dep]

if static_shaderc
  # libshaderc_combined.a depends on glslang and SPIRV-Tools, but some pkg-config
  # files don't properly expose these transitive static dependencies
  glslang_dep = dependency('glslang', static: true, required: false)
  if glslang_dep.found()
    dependencies += glslang_dep
  else
    glslang_libs = [
      cxx.find_library('glslang', static: true, required: false),
      cxx.find_library('MachineIndependent', static: true, required: false),
      cxx.find_library('GenericCodeGen', static: true, required: false),
      cxx.find_library('glslang-default-resource-limits', static: true, required: false),
      cxx.find_library('OSDependent', static: true, required: false),
      cxx.find_library('SPIRV', static: true, required: false),
      cxx.find_library('SPVRemapper', static: true, required: false),
    ]
    foreach lib : glslang_libs
      if lib.found()
        dependencies += lib
      endif
    endforeach
  endif

  spirv_tools_dep = dependency('SPIRV-Tools', static: true, required: false)
  if spirv_tools_dep.found()
    dependencies += spirv_tools_dep
  else
    spirv_libs = [
      cxx.find_library('SPIRV-Tools', static: true, required: false),
      cxx.find_library('SPIRV-Tools-opt', static: true, required: false),
    ]
    foreach lib : spirv_libs
      if lib.found()
        dependencies += lib
      endif
    endforeach
  endif
endif

llvmexpr_module = shared_module('llvmexpr', sources,
  dependencies: dependencies,
  link_args: link_args,
  install: true,
  install_dir: install_dir
)

if host_machine.system() == 'darwin'
  custom_target('llvmexpr_dsym',
    input: llvmexpr_module,
    output: 'libllvmexpr.dylib.dSYM',
    command: ['dsymutil', '@INPUT@'],
    build_by_default: true
  )
endif

# Build infix2postfix tool
infix2postfix_sources = [
  'llvmexpr/frontend/infix2postfix/main.cpp',
  'llvmexpr/frontend/infix2postfix/Preprocessor.cpp',
  'llvmexpr/frontend/infix2postfix/StandardLibrary.cpp',
  'llvmexpr/frontend/infix2postfix/ASTPrinter.cpp',
  'llvmexpr/frontend/infix2postfix/Builtins.cpp',
  'llvmexpr/frontend/infix2postfix/Tokenizer.cpp',
  'llvmexpr/frontend/infix2postfix/Parser.cpp',
  'llvmexpr/frontend/infix2postfix/AnalysisEngine.cpp',
  'llvmexpr/frontend/infix2postfix/SemanticAnalyzer.cpp',
  'llvmexpr/frontend/infix2postfix/SymbolTable.cpp',
  'llvmexpr/frontend/infix2postfix/CodeGenerator.cpp',
  'llvmexpr/frontend/infix2postfix/PostfixBuilder.cpp',
  'llvmexpr/frontend/infix2postfix/PostfixHelper.cpp',
  'llvmexpr/frontend/Tokenizer.cpp',  # Need main project's tokenizer for PostfixHelper
]

infix2postfix_exe = executable('infix2postfix', infix2postfix_sources,
  dependencies: [llvm_dep, ctre_dep],
  install: false
)

if host_machine.system() == 'darwin'
  custom_target('infix2postfix_dsym',
    input: infix2postfix_exe,
    output: 'infix2postfix.dSYM',
    command: ['dsymutil', '@INPUT@'],
    build_by_default: true
  )
endif
