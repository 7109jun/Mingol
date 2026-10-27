# API: shader (`mingol/shader/shader_compiler.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/shader` · 타깃 `Mingol::Shader` · 네임스페이스 `mingol::shader`. 의존: `core`, glslang(시스템 패키지, `find_package(glslang CONFIG)`).
GLSL·HLSL 소스를 **SPIR-V 1.6(Vulkan 1.3)** 으로 컴파일한다. 스레드 안전.

## 타입
| 타입 | 설명 |
|---|---|
| `enum class Language { Glsl, Hlsl }` | 소스 언어 |
| `enum class Stage { Vertex, Fragment, Compute }` | 셰이더 스테이지 |
| `using SpirV = std::vector<std::uint32_t>` | 결과 |
| `using IncludeResolver = std::function<Result<std::string>(std::string_view path, std::string_view includer)>` | `#include` 해석기. `path` = 지시문에 쓴 문자열, `includer` = 포함하는 소스 이름. 오류를 반환하면 진단으로 보고된다 |
| `CompileOptions` | `language = Glsl`, `stage = Vertex`, `entryPoint = "main"`, `sourceName = "<shader>"`(진단용), `optimize = true`, `debugInfo = false`, `includeResolver`(없으면 모든 `#include`가 오류), `maxIncludeDepth = 16` |

## 함수
```cpp
Result<SpirV> compile(std::string_view source, const CompileOptions& options);
Result<void>  validateSpirvHeader(const void* data, std::size_t sizeBytes);
```
- `compile`: 실패하면 `Error::message`에 컴파일러 진단이 `파일:줄: 메시지` 형태로 들어 있다.
- `validateSpirvHeader`: 미리 컴파일된 셰이더를 로드할 때 쓰는 **얕은 구조 검사**(매직 넘버, 4바이트 정렬 크기). 내용의 올바름은 검증하지 않는다.

## 사용
```cpp
shader::CompileOptions o;
o.language = shader::Language::Glsl;
o.stage = shader::Stage::Fragment;
o.sourceName = "lit.frag";
auto spv = shader::compile(R"(#version 450
layout(location = 0) out vec4 color;
void main() { color = vec4(1, 0, 0, 1); })", o);
if (!spv.ok()) { MINGOL_LOG_ERROR("shader", "{}", spv.error().message); return; }
rhi::ShaderModuleDesc d; d.spirv = spv.value(); d.stage = rhi::ShaderStage::Fragment;   // RHI로 전달
```
`#include` 사용 (GLSL은 확장 활성화 필수):
```cpp
o.includeResolver = [&](std::string_view path, std::string_view includer) -> Result<std::string> {
  return fs::readTextFile(shaderDir / std::string(path));   // 경로 검증은 호출자의 책임
};
// 소스 맨 위: #extension GL_GOOGLE_include_directive : require
```
재귀 포함은 `maxIncludeDepth`로 차단되고 진단으로 보고된다.

## 한계 / 주의
- 모든 컴파일은 **런타임**에 한다. 렌더러의 내장 셰이더도 시작 시 컴파일된다(SPIR-V 캐시·사전 컴파일 미구현). 사용자 머티리얼 셰이더를 렌더러에 연결하는 고수준 API는 아직 없다([renderer.md](renderer.md)).
- `Result<void> validateSpirvHeader`는 악의적 SPIR-V를 막지 못한다. 신뢰할 수 없는 SPIR-V는 로드하지 말 것.
- `includeResolver`가 파일 시스템을 읽을 때 프로젝트 밖 경로를 막는 것은 호출자 책임이다.
