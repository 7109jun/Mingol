// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "mingol/core/error.hpp"

namespace mingol::shader {

enum class Language { Glsl, Hlsl };
enum class Stage { Vertex, Fragment, Compute };

// Resolves an #include request. `path` is the string written in the directive and `includer` is
// the name of the including source. Return the file contents, or an Error (reported as a
// diagnostic). GLSL sources must enable `#extension GL_GOOGLE_include_directive : require`.
using IncludeResolver = std::function<Result<std::string>(std::string_view path, std::string_view includer)>;

struct CompileOptions {
  Language language = Language::Glsl;
  Stage stage = Stage::Vertex;
  std::string entryPoint = "main";
  std::string sourceName = "<shader>";  // used in diagnostics
  bool optimize = true;
  bool debugInfo = false;
  IncludeResolver includeResolver;  // optional; without it every #include is an error
  int maxIncludeDepth = 16;
};

using SpirV = std::vector<std::uint32_t>;

// Compiles GLSL or HLSL to SPIR-V 1.6 for Vulkan 1.3. On failure the Error message contains the
// compiler's diagnostics (file:line: message). Thread-safe.
Result<SpirV> compile(std::string_view source, const CompileOptions& options);

// Cheap structural check used when loading precompiled shaders: magic number, size alignment.
Result<void> validateSpirvHeader(const void* data, std::size_t sizeBytes);

}  // namespace mingol::shader
