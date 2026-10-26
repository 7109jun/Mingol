// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/shader/shader_compiler.hpp"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <cstring>
#include <memory>

namespace mingol::shader {

namespace {

// glslang requires process-wide initialization before any compile.
struct GlslangProcess {
  GlslangProcess() { glslang::InitializeProcess(); }
  ~GlslangProcess() { glslang::FinalizeProcess(); }
};

void ensureGlslang() {
  static const GlslangProcess process;
  (void)process;
}

EShLanguage toGlslangStage(Stage stage) {
  switch (stage) {
    case Stage::Vertex: return EShLangVertex;
    case Stage::Fragment: return EShLangFragment;
    case Stage::Compute: return EShLangCompute;
  }
  return EShLangVertex;
}

struct IncludeData {
  std::string content;
};

class ResolverIncluder final : public glslang::TShader::Includer {
 public:
  ResolverIncluder(const IncludeResolver* resolver, int maxDepth) : resolver_(resolver), maxDepth_(maxDepth) {}

  IncludeResult* includeLocal(const char* headerName, const char* includerName, std::size_t depth) override {
    return resolve(headerName, includerName, depth);
  }
  IncludeResult* includeSystem(const char* headerName, const char* includerName, std::size_t depth) override {
    return resolve(headerName, includerName, depth);
  }
  void releaseInclude(IncludeResult* result) override {
    if (result != nullptr) {
      delete static_cast<IncludeData*>(result->userData);
      delete result;
    }
  }

 private:
  IncludeResult* fail(const std::string& message) {
    // glslang convention: empty name + message in the data field signals an error.
    auto* data = new IncludeData{message};
    return new IncludeResult("", data->content.data(), data->content.size(), data);
  }

  IncludeResult* resolve(const char* headerName, const char* includerName, std::size_t depth) {
    if (static_cast<int>(depth) > maxDepth_) {
      return fail(std::string("#include nesting too deep (limit ") + std::to_string(maxDepth_) + ") at '" +
                  headerName + "'");
    }
    if (resolver_ == nullptr || !*resolver_) {
      return fail(std::string("#include is not available: no include resolver configured for '") + headerName + "'");
    }
    auto content = (*resolver_)(headerName, includerName != nullptr ? includerName : "");
    if (!content.ok()) {
      return fail(content.error().message);
    }
    auto* data = new IncludeData{std::move(content).value()};
    return new IncludeResult(headerName, data->content.data(), data->content.size(), data);
  }

  const IncludeResolver* resolver_;
  int maxDepth_;
};

}  // namespace

Result<SpirV> compile(std::string_view source, const CompileOptions& options) {
  ensureGlslang();

  if (options.entryPoint.empty()) {
    return makeError(ErrorCode::InvalidArgument, "entry point must not be empty");
  }

  const EShLanguage stage = toGlslangStage(options.stage);
  const bool hlsl = options.language == Language::Hlsl;

  glslang::TShader shader(stage);
  const char* strings[1] = {source.data()};
  const int lengths[1] = {static_cast<int>(source.size())};
  const char* names[1] = {options.sourceName.c_str()};
  shader.setStringsWithLengthsAndNames(strings, lengths, names, 1);
  shader.setEntryPoint(options.entryPoint.c_str());
  shader.setSourceEntryPoint(options.entryPoint.c_str());
  shader.setEnvInput(hlsl ? glslang::EShSourceHlsl : glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
  if (hlsl) {
    shader.setAutoMapBindings(true);
    shader.setAutoMapLocations(true);
  }

  auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
  if (hlsl) {
    messages = static_cast<EShMessages>(messages | EShMsgReadHlsl);
  }

  ResolverIncluder includer(&options.includeResolver, options.maxIncludeDepth);
  if (!shader.parse(GetDefaultResources(), 450, false, messages, includer)) {
    return makeError(ErrorCode::InvalidArgument, "shader compile failed ({}):\n{}{}", options.sourceName,
                     shader.getInfoLog(), shader.getInfoDebugLog());
  }

  glslang::TProgram program;
  program.addShader(&shader);
  if (!program.link(messages)) {
    return makeError(ErrorCode::InvalidArgument, "shader link failed ({}):\n{}{}", options.sourceName,
                     program.getInfoLog(), program.getInfoDebugLog());
  }

  glslang::TIntermediate* intermediate = program.getIntermediate(stage);
  if (intermediate == nullptr) {
    return makeError(ErrorCode::Internal, "no intermediate representation for {}", options.sourceName);
  }

  SpirV spirv;
  spv::SpvBuildLogger logger;
  glslang::SpvOptions spvOptions;
  spvOptions.disableOptimizer = !options.optimize;
  spvOptions.generateDebugInfo = options.debugInfo;
  spvOptions.validate = true;
  glslang::GlslangToSpv(*intermediate, spirv, &logger, &spvOptions);

  if (spirv.empty()) {
    return makeError(ErrorCode::Internal, "SPIR-V generation produced no output for {}: {}", options.sourceName,
                     logger.getAllMessages());
  }
  return spirv;
}

Result<void> validateSpirvHeader(const void* data, std::size_t sizeBytes) {
  constexpr std::uint32_t kMagic = 0x07230203u;
  if (data == nullptr || sizeBytes < 20) {
    return makeError(ErrorCode::Corrupt, "SPIR-V blob too small ({} bytes)", sizeBytes);
  }
  if (sizeBytes % 4 != 0) {
    return makeError(ErrorCode::Corrupt, "SPIR-V size {} is not a multiple of 4", sizeBytes);
  }
  std::uint32_t magic = 0;
  std::memcpy(&magic, data, sizeof(magic));
  if (magic != kMagic) {
    return makeError(ErrorCode::Corrupt, "bad SPIR-V magic number 0x{:08x}", magic);
  }
  return {};
}

}  // namespace mingol::shader
