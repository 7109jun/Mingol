// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "mingol/shader/shader_compiler.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::shader;

namespace {

constexpr const char* kVert = R"(#version 450
layout(location = 0) in vec3 inPos;
layout(set = 0, binding = 0) uniform Camera { mat4 viewProj; } cam;
void main() { gl_Position = cam.viewProj * vec4(inPos, 1.0); }
)";

constexpr const char* kFrag = R"(#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0, 0.5, 0.25, 1.0); }
)";

constexpr const char* kComp = R"(#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba8) uniform writeonly image2D img;
void main() { imageStore(img, ivec2(gl_GlobalInvocationID.xy), vec4(1.0)); }
)";

constexpr const char* kHlslPs = R"(
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  return float4(uv, 0.0, 1.0);
}
)";

CompileOptions opts(Stage stage, Language lang = Language::Glsl) {
  CompileOptions o;
  o.stage = stage;
  o.language = lang;
  return o;
}

bool hasMagic(const SpirV& s) { return !s.empty() && s[0] == 0x07230203u; }

}  // namespace

TEST(ShaderCompiler, GlslVertexFragmentCompute) {
  for (auto [src, stage] : {std::pair{kVert, Stage::Vertex}, std::pair{kFrag, Stage::Fragment},
                            std::pair{kComp, Stage::Compute}}) {
    auto r = compile(src, opts(stage));
    REQUIRE(r.ok());
    CHECK(hasMagic(r.value()));
    CHECK(validateSpirvHeader(r.value().data(), r.value().size() * 4).ok());
  }
}

TEST(ShaderCompiler, HlslPixelShader) {
  auto r = compile(kHlslPs, opts(Stage::Fragment, Language::Hlsl));
  if (!r.ok()) std::fprintf(stderr, "%s\n", r.error().message.c_str());
  REQUIRE(r.ok());
  CHECK(hasMagic(r.value()));
}

TEST(ShaderCompiler, SyntaxErrorReportsLocationAndName) {
  CompileOptions o = opts(Stage::Fragment);
  o.sourceName = "broken.frag";
  auto r = compile("#version 450\nlayout(location=0) out vec4 c;\nvoid main() { c = vec4(1.0) }\n", o);
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::InvalidArgument);
  CHECK(r.error().message.find("broken.frag") != std::string::npos);
  CHECK(r.error().message.find("ERROR") != std::string::npos);
}

TEST(ShaderCompiler, UndeclaredIdentifierIsAnError) {
  auto r = compile("#version 450\nvoid main() { gl_Position = nope; }\n", opts(Stage::Vertex));
  REQUIRE(!r.ok());
  CHECK(r.error().message.find("nope") != std::string::npos);
}

TEST(ShaderCompiler, MissingEntryPointFails) {
  CompileOptions o = opts(Stage::Fragment);
  o.entryPoint = "";
  CHECK(!compile(kFrag, o).ok());
}

TEST(ShaderCompiler, EmptySourceFailsCleanly) {
  CHECK(!compile("", opts(Stage::Vertex)).ok());
}

TEST(ShaderCompiler, OptimizerChangesOutputButKeepsValidity) {
  CompileOptions a = opts(Stage::Fragment);
  a.optimize = false;
  CompileOptions b = opts(Stage::Fragment);
  b.optimize = true;
  const char* src = R"(#version 450
layout(location=0) out vec4 c;
void main() { float x = 1.0; float unused = x * 3.0; c = vec4(x); })";
  auto ra = compile(src, a);
  auto rb = compile(src, b);
  REQUIRE(ra.ok());
  REQUIRE(rb.ok());
  CHECK(rb.value().size() <= ra.value().size());
}

TEST(ShaderCompiler, IncludeResolution) {
  std::map<std::string, std::string> files = {
      {"common.glsl", "vec4 tint() { return vec4(0.1, 0.2, 0.3, 1.0); }\n"},
  };
  CompileOptions o = opts(Stage::Fragment);
  o.includeResolver = [&](std::string_view path, std::string_view) -> Result<std::string> {
    auto it = files.find(std::string(path));
    if (it == files.end()) return makeError(ErrorCode::NotFound, "no such include: {}", path);
    return it->second;
  };
  const char* src = R"(#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location=0) out vec4 c;
void main() { c = tint(); })";
  auto r = compile(src, o);
  if (!r.ok()) std::fprintf(stderr, "%s\n", r.error().message.c_str());
  CHECK(r.ok());

  const char* missing = R"(#version 450
#extension GL_GOOGLE_include_directive : require
#include "absent.glsl"
void main() {})";
  auto bad = compile(missing, o);
  REQUIRE(!bad.ok());
  CHECK(bad.error().message.find("no such include: absent.glsl") != std::string::npos);
}

TEST(ShaderCompiler, IncludeWithoutResolverIsAnError) {
  const char* src = R"(#version 450
#extension GL_GOOGLE_include_directive : require
#include "x.glsl"
void main() {})";
  auto r = compile(src, opts(Stage::Fragment));
  REQUIRE(!r.ok());
  CHECK(r.error().message.find("no include resolver") != std::string::npos);
}

TEST(ShaderCompiler, RecursiveIncludeIsStopped) {
  CompileOptions o = opts(Stage::Fragment);
  o.maxIncludeDepth = 4;
  o.includeResolver = [](std::string_view, std::string_view) -> Result<std::string> {
    return std::string("#include \"self.glsl\"\n");
  };
  const char* src = R"(#version 450
#extension GL_GOOGLE_include_directive : require
#include "self.glsl"
void main() {})";
  auto r = compile(src, o);
  REQUIRE(!r.ok());
  CHECK(r.error().message.find("too deep") != std::string::npos);
}

TEST(ShaderCompiler, ConcurrentCompilationIsSafe) {
  std::vector<std::thread> threads;
  std::vector<int> okCount(4, 0);
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 10; ++i) {
        if (compile(kFrag, opts(Stage::Fragment)).ok()) ++okCount[t];
      }
    });
  }
  for (auto& th : threads) th.join();
  for (int c : okCount) CHECK_EQ(c, 10);
}

TEST(ShaderCompiler, SpirvHeaderValidation) {
  std::uint32_t good[5] = {0x07230203u, 0x00010600u, 0, 1, 0};
  CHECK(validateSpirvHeader(good, sizeof(good)).ok());
  CHECK(!validateSpirvHeader(nullptr, 100).ok());
  CHECK(!validateSpirvHeader(good, 8).ok());    // too small
  CHECK(!validateSpirvHeader(good, 22).ok());   // not 4-aligned
  std::uint32_t bad[5] = {0xDEADBEEFu, 0, 0, 0, 0};
  CHECK(!validateSpirvHeader(bad, sizeof(bad)).ok());
}
