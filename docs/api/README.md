# Mingol API 레퍼런스

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only. 모듈마다 문서 한 편. 시그니처는 **실제 헤더를 읽고** 작성했다 — 헤더가 정본이므로 불일치가 보이면 헤더가 맞고 문서가 틀린 것이다(이슈/PR 환영). 코드 블록 중 "스케치"라고 표시된 것은 컴파일하지 않은 설명용 예시다(실제로 컴파일되는 사용 예는 `tests/<모듈>/`).

## 공통 규약
- 오류: `Result<T>`(`[[nodiscard]]`)와 `Error{ErrorCode, message}`. 예외를 오류 전달에 쓰지 않는다. 매크로 `MINGOL_TRY`, `MINGOL_TRY_ASSIGN`, `MINGOL_VERIFY`(항상 검사), `MINGOL_ASSERT`(디버그 전용), `MINGOL_LOG_*` → [core.md](core.md).
- 네임스페이스 `mingol::<모듈>`, CMake 타깃 `Mingol::<Module>`, 헤더 `mingol/<module>/*.hpp`.
- 스레딩은 문서마다 명시한다. 기본값은 "단일 스레드".

## 모듈 의존 (아래 → 위로 쌓임)
`core` ← `platform` · `core` ← `rhi` ← `renderer` ← `scene` ← `runtime` · `shader`→`rhi` · `assets` · `physics` · `audio` · `script` ← `scripting`(+`scene`) · `package` · `sceneio`→`core+scene+runtime+scripting` · `gui` ← `gui_gpu`(+`rhi`) · `editor`/`player` → 다수.

| 문서 | 모듈 | 요약 |
|---|---|---|
| [core.md](core.md) | `core` | 에러, 어서션, 로그, 수학, JSON, UUID, 해시, 파일 시스템, 잡 시스템, 메모리 |
| [platform.md](platform.md) | `platform` | 창, 이벤트, 입력, 키 목록 |
| [rhi.md](rhi.md) | `rhi` | Vulkan 위의 GPU 추상화(디바이스, 리소스, 커맨드 리스트, 스왑체인) |
| [shader.md](shader.md) | `shader` | GLSL/HLSL → SPIR-V 컴파일 |
| [renderer.md](renderer.md) | `renderer` | Forward PBR, 메시/머티리얼, 그림자, 스키닝, 애니메이션 |
| [scene.md](scene.md) | `scene` | GameObject/Component, 트랜스폼, 내장 컴포넌트, `buildRenderScene` |
| [runtime.md](runtime.md) | `runtime` | 게임 루프, 물리·오디오 컴포넌트, glTF 인스턴스화 |
| [physics.md](physics.md) | `physics` | Bullet 뒤의 강체 물리 월드 |
| [audio.md](audio.md) | `audio` | 소프트웨어 믹서, 출력 장치 |
| [assets.md](assets.md) | `assets` | PNG/JPEG, WAV/Ogg, glTF/GLB 디코딩 |
| [script.md](script.md) | `script` | Mingol Script 컴파일러, VM, 플러그인 C ABI |
| [scripting.md](scripting.md) | `scripting` | 씬 ↔ 스크립트 다리, 핫 리로드, 엔진 스크립트 API |
| [package.md](package.md) | `package` | `.mindev`, `.mingol` 아카이브, 암호 프리미티브 |
| [sceneio.md](sceneio.md) | `sceneio` | `.minseen` 저장/로드, 컴포넌트 레지스트리, 표준 에셋 이름 |
| [gui.md](gui.md) | `gui`, `gui_gpu` | 즉시 모드 GUI, 도킹, 소프트웨어/GPU 렌더러 |
| [editor.md](editor.md) | `editor` | 에디터 문서 모델, 앱, 프로퍼티, 뷰포트 수학 |
| [player.md](player.md) | `player` | 콘텐츠 소스, `GameApp` |

도구/실행 파일: `mingol-editor`, `mingol-player`, `mingol-pack`(`tools/`), `mingol-bench`(`tests/bench`) — 사용법은 [../EDITOR.md](../EDITOR.md), [../PLAYER.md](../PLAYER.md), [../MINGOL.md](../MINGOL.md).

## 문서를 고칠 때
헤더를 바꾸면 해당 문서를 같은 PR에서 갱신한다([../../CONTRIBUTING.md](../../CONTRIBUTING.md)). 예시를 추가할 때는 컴파일해 본 것만 "검증됨"으로 적고, 아니면 "스케치"로 표시한다.
