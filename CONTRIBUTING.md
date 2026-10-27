# Mingol 기여 가이드

코드: GPL-2.0-only · 문서: CC BY 4.0. 이 문서는 **기여자가 먼저 읽는 한 장짜리 지도**다. 자세한 내용은 `docs/`에 링크한다. 현재 상태와 한계는 [docs/STATUS.md](docs/STATUS.md), [docs/KNOWN-LIMITS.md](docs/KNOWN-LIMITS.md).

## 0. 원칙 (이 프로젝트의 규칙)
1. **가짜 구현·TODO 금지.** 있는 기능은 실제로 동작하고 테스트가 있다. 없는 기능은 문서의 "한계"에 적는다.
2. **실행해 보지 않은 것을 "검증됨"이라 쓰지 않는다.** (예: Windows 창/GPU는 아직 미검증)
3. **외부 입력은 항상 검증하고 `Result`로 오류를 돌려준다.** 파일·아카이브·씬·스크립트·glTF가 크래시나 무한 루프를 만들면 버그다.
4. 버그를 고치면 **그 버그를 잡는 테스트**를 같이 넣는다.

## 1. 시작하기
```sh
sudo apt install ninja-build libvulkan-dev glslang-dev glslang-tools libglfw3-dev libbullet-dev \
                 mesa-vulkan-drivers vulkan-validationlayers xvfb xdotool
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug --timeout 300
```
- 프리셋: `linux-debug`(경고=오류) · `linux-release` · `linux-asan` · `linux-tsan` · `windows-msvc`(vcpkg 툴체인을 직접 지정). 자세히는 [docs/BUILD.md](docs/BUILD.md).
- 새 소스/하위 디렉터리를 추가하면 **`cmake --preset ...`을 다시 실행**한다(소스 목록이 CMake에 하드코딩).
- 개별 테스트: `build/linux-debug/tests/<모듈>/mingol_<모듈>_tests [이름 부분 문자열]`, 목록은 `--list`.
- GPU 없이도 lavapipe(`mesa-vulkan-drivers`)로 GPU 테스트가 돈다. 디스플레이가 없으면 `--headless`.

## 2. 설계도

### 2.1 모듈과 의존 방향 (아래로만 의존)
```
core ◄─ platform                                 창/입력/이벤트
core ◄─ rhi ◄─ renderer ◄─ scene ◄─ runtime      GPU → 렌더 → 씬 → 게임 루프(물리·오디오)
core ◄─ shader ► rhi                             GLSL/HLSL → SPIR-V
core ◄─ assets                                   PNG/JPEG/WAV/OGG/glTF 디코더 (CPU 데이터)
core ◄─ script(컴파일러+VM) ◄─ scripting(+scene)  .mins ↔ 씬
core ◄─ package                                  .mindev/.mingol/암호
scene+runtime+scripting ◄─ sceneio               .minseen, 컴포넌트 레지스트리, 표준 에셋 이름
platform ◄─ gui ◄─ gui_gpu(+rhi)                 즉시 모드 GUI
… ◄─ editor, player ◄─ apps(mingol-editor, mingol-player), tools(mingol-pack)
```
- 상위는 하위를 알 수 있고 **반대는 불가**(CMake 링크로 강제). 순환 금지.
- 서드파티 타입(Bullet, miniaudio, glslang, Vulkan 핸들)은 **공개 헤더에 노출하지 않는다.**
- 각 모듈: `engine/<m>/include/mingol/<m>/*.hpp`(공개), `engine/<m>/src/*`(비공개), 타깃 `mingol_<m>` + 별칭 `Mingol::<M>`.

### 2.2 한 프레임
`platform` 이벤트 → `Runtime::tick(dt)`(고정 스텝: fixedUpdate → 물리 → 포즈 반영 → 접촉 콜백, 그다음 update → lateUpdate) → `buildRenderScene()` → `Renderer::render()`. 에디터는 사이에 GUI(`DrawList` → `GuiRenderer`)가 끼어든다.

### 2.3 제작 → 배포
에디터(`.mindev` + `scenes/*.minseen` + `scripts/*.mins` + `assets/*`) → `packProject` → `.mingol` → (선택) `packExecutable`로 `mingol-player`에 붙여 단일 EXE. 플레이어는 폴더/아카이브/내장 아카이브를 모두 `ContentSource` 뒤에서 **같은 경로**로 로드한다. 에디터와 플레이어는 **표준 에셋 이름**(`builtin/box`, `<경로>#mesh0.0` …)을 공유한다 — 이름 규칙을 바꾸면 기존 `.minseen`이 깨진다([docs/ASSETS.md](docs/ASSETS.md)).

### 2.4 핵심 불변식
- **Scene/ScriptHost/Renderer는 단일 스레드.** RHI의 자원 생성·`submit`만 다중 스레드 허용.
- 씬을 건드리는 모든 작업은 `ScriptHost::Scope`로 감싼다(스크립트 컴포넌트가 VM을 호출하기 때문). `ScriptHost`는 씬보다 오래 산다.
- GameObject는 **세대 핸들**(`GameObjectId`)로 참조하고 파괴는 지연된다. 콜백 중 생성/파괴는 안전해야 한다.
- 에디터 되돌리기와 재생 모드는 **씬 전체 `.minseen` 스냅샷**이다. 씬에 저장되지 않는 상태는 되돌려지지 않는다.
- RHI: 미제출 `CommandList`가 참조하는 자원은 호출자가 살려 둔다. 텍스처 상태는 텍스처 전체 단위.
- GUI는 CPU `DrawList`로 환원되고 **소프트웨어 래스터라이저가 기준 구현**이다(GPU 결과가 그것과 일치해야 한다).

## 3. 코드 스타일
(자동 포맷터 설정은 아직 없다. 기존 코드를 보고 맞춘다.)
- C++20, GCC 13에서 `-Wall -Wextra -Wpedantic -Wshadow -Werror`에 **경고 0**. MSVC는 `/W4 /WX`(`/wd4127` 예외). C++ 예외로 에러를 전달하지 않는다(**에러는 `Result<T>`**).
- 첫 줄에 `// SPDX-License-Identifier: GPL-2.0-only`. 헤더는 `#pragma once`.
- 들여쓰기 2칸, 한 줄 약 120자 이내, `public:`/`private:`는 1칸 들여쓰기.
- 이름: 타입 `PascalCase` · 함수/변수 `camelCase` · 멤버 변수 `name_`(끝에 `_`) · 상수 `kName` · 열거형 `enum class` + `PascalCase` · 네임스페이스 소문자 `mingol::<모듈>` · 매크로 `MINGOL_*`. 순수 데이터 `struct`의 필드는 `_` 없이.
- 주석은 "무엇"이 아니라 **왜/불변식/함정**을 쓴다. 공개 헤더 맨 위에 모듈의 계약을 적는다(기존 헤더 참고).
- 포함 순서: 자기 헤더 → 표준 라이브러리 → `mingol/...`. 공개 헤더는 포함을 최소화(전방 선언).
- 에러: `Result<T>`(`[[nodiscard]]`), `MINGOL_TRY(expr)`, `MINGOL_TRY_ASSIGN(auto x, expr)`, `Error{ErrorCode::..., msg}`. `.value()`는 `ok()` 확인 후에만. 무시해야 하면 `(void)`.
- 불변식 검사는 `MINGOL_VERIFY`(항상) / `MINGOL_ASSERT`(디버그만). **사용자 입력 검사에 쓰지 않는다.**
- 로그: `MINGOL_LOG_INFO("카테고리", "형식 {}", 값)`.
- 네이티브 이식: 플랫폼 코드는 `#if defined(_WIN32)`로 가르고, `/proc`, `/tmp`, `/dev/null`, `unistd.h` 같은 POSIX 가정은 코드·**테스트 모두**에서 피한다(`std::filesystem::temp_directory_path()` 사용).
- 더 자세히: [docs/CODING-GUIDE.md](docs/CODING-GUIDE.md).

## 4. 주의점 (실제로 겪은 함정)
| 영역 | 함정 |
|---|---|
| CMake | 파일 추가 후 재구성 필요. MSVC용 옵션은 `cmake/MingolCompileOptions.cmake`(`/Zc:preprocessor`, `_CRT_SECURE_NO_WARNINGS`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`을 **전역으로 정의**하니 소스에서 다시 `#define` 하지 말 것) |
| Windows | 열린 파일은 삭제/덮어쓰기 불가 → 테스트 정리는 `remove_all(dir, ec)`. DLL은 기본으로 심볼을 내보내지 않음 → 플러그인은 `MS_PLUGIN_EXPORT`. 실행 파일은 `.exe` 확장자 필요 |
| 수학/좌표 | `Mat4::perspective`는 Vulkan식(Y 반전, 깊이 0..1). 화면 y = (ndc.y·0.5+0.5)·h (추가 뒤집기 없음) |
| GPU 텍스처 상태 | `readbackTexture` 후엔 TransferSrc → 샘플링 전에 ShaderRead로 전환. `Renderer::render(…, target)`은 타깃을 ColorAttachment로 남김 |
| GUI | `beginPanel/endPanel`, `beginMainMenuBar/endMainMenuBar`, `beginChild/endChild`는 항상 쌍. `beginMenu/Popup/Modal/ContextMenu`는 **true일 때만** end. `openModal(id)`는 ID 스택이 비었을 때 호출. 레이아웃은 한 프레임 늦게 수렴. 내장 폰트는 ASCII 전용 |
| 에디터 | 편집 모드에서 컴포넌트를 만들면 스크립트 `onAwake/onEnable`이 즉시 실행됨. 에셋(오디오/모델)은 리스캔을 넘어 유지되어야 씬의 포인터가 유효 |
| 스크립트 | 문자열은 UTF-8 **바이트** 단위. 핫 리로드는 객체 필드·코루틴·정적 변수를 이전하지 않음. 플러그인은 샌드박스 없음 |
| 씬 로드 | 알 수 없는 컴포넌트는 기본 오류(`skipUnknownComponents`일 때만 경고) |
| 테스트 | `[[nodiscard]]` 결과는 `REQUIRE(x.ok())`로. 병렬 ctest에서 임시 폴더가 겹치지 않게 `TempDir` 사용. GPU 테스트는 `GPU_OR_SKIP()`로 시작 |
| CI | 느린 Linux 잡은 커밋 메시지에 `[win-only]`가 있으면 건너뜀(Windows 반복 수정용). **푸시 전 최종 커밋은 태그 없이** 전체 CI를 돌릴 것 |

## 5. 파일 확장자와 형식
| 확장자 | 무엇 | 형식/문서 |
|---|---|---|
| `.mindev` | 프로젝트 파일 | JSON 텍스트, 정규형 CRC-32 `integrity` — [MINDEV.md](docs/MINDEV.md) |
| `.minseen` | 씬 | JSON 텍스트 — [MINSEEN.md](docs/MINSEEN.md) |
| `.mins` | Mingol Script 소스 | 자체 언어 — [SCRIPT.md](docs/SCRIPT.md), [SCRIPTING-GUIDE.md](docs/SCRIPTING-GUIDE.md) |
| `.mingol` | 배포 아카이브(선택 암호화) | 바이너리, 매직 `89 4D 49 4E 47 4F 4C 0A` — [MINGOL.md](docs/MINGOL.md) |
| (단일 EXE) | 실행 파일 + `.mingol` + 32바이트 트레일러 | `packExecutable` |
| `.mingol-editor-layout.json` | 에디터 도킹 배치(프로젝트 폴더) | JSON |
| `.hpp` / `.cpp` | C++ 헤더/소스 (공개 헤더는 `include/mingol/<모듈>/`) | |
| `.h` / `.c` | 순수 C: 플러그인 ABI(`plugin_abi.h`)와 테스트 플러그인 | |
| `.cmake`, `CMakeLists.txt`, `CMakePresets.json` | 빌드 | |
| `.supp` | 새니타이저/valgrind 억제 목록(`tests/tsan.supp`, `valgrind.supp`) | 모듈 단위로 좁게 |
| `.rgba`, `.ppm` | 골든 이미지(원시 RGBA), 스크린샷(P6) | 테스트 전용 |
| 문서 `.md` | CC BY 4.0 | |
| 셰이더 | 별도 파일 없이 `engine/renderer/src/shaders.cpp`에 GLSL 문자열로 내장, 실행 시 glslang으로 SPIR-V 컴파일 | |

경로 규칙: 프로젝트 안의 이름은 **상대 경로, `/` 구분**, 절대 경로·`..` 금지(`isValidRelativePath`).

## 6. 자주 하는 작업
- **새 모듈**: `engine/<m>/{include/mingol/<m>,src}` + `CMakeLists.txt`(`add_library`, `Mingol::<M>` 별칭, `mingol_target_options`) → `engine/CMakeLists.txt`에 `add_subdirectory` → 의존 방향 확인 → `tests/<m>/` 추가.
- **새 테스트 실행 파일**: `mingol_add_test(이름 SOURCES ... LIBS ...)`. 본문은 `TEST(Suite, Name) { CHECK/CHECK_EQ/CHECK_NEAR/REQUIRE/SKIP_TEST }`.
- **새 컴포넌트**: 클래스 추가 → `sceneio`의 `ComponentRegistry`에 직렬화 등록 → 에디터 인스펙터(`describeComponent`와 추가 가능 목록) → 직렬화 왕복 테스트 → 에셋 참조는 **표준 에셋 이름**으로.
- **스크립트에 엔진 API 추가**: `engine/scripting`의 `engineApiSource()`(스크립트 쪽 선언)와 네이티브 바인딩을 함께 수정 → `tests/scripting`.
- **에디터 패널/동작**: `engine/editor/src/editor_panels.cpp`, 문서 상태는 `EditorDocument`(되돌리기 단위는 `commit("라벨")`). 새 대화상자는 `modalPending_` 패턴 사용.
- **렌더 기능**: 정답은 독립 CPU 참조와 비교하고(기하로 예측 가능한 것은 기하로), 골든 이미지는 회귀 가드로만 쓴다. 변이(일부러 망가뜨리기)로 테스트의 민감도를 확인한다.

## 7. 테스트와 검증 기대치
- 정상 경로 + 잘못된 입력 + 경계값. 파일 형식은 **절단/비트 반전/랜덤 손상** 입력을 포함한다.
- 새니타이저: `linux-asan`(ASan+UBSan), `linux-tsan`(억제 목록은 서드파티 모듈 한정 — 우리 코드의 경합은 그대로 실패해야 함). 자세히는 [docs/TESTING.md](docs/TESTING.md).
- GPU 테스트는 검증 레이어 오류 0건이어야 통과.
- 알려진 간헐 실패(원인 미규명)는 [docs/TESTING.md](docs/TESTING.md)에 있다. 재현되면 로그를 남겨 조사하고, **억제로 덮지 말 것.**

## 8. 변경 제출
1. 작은 단위로 나눠 커밋(한 커밋 = 한 의도). 메시지는 "무엇을/왜".
2. 로컬에서 최소 `linux-debug` 전체 ctest 통과. 스레드/수명 변경은 `linux-tsan`/`linux-asan`도.
3. 동작이 바뀌면 `docs/`와 [docs/STATUS.md](docs/STATUS.md)·[docs/KNOWN-LIMITS.md](docs/KNOWN-LIMITS.md)를 같이 고친다.
4. 새 서드파티는 **GPL-2.0 호환 라이선스만**, `third_party/THIRD_PARTY.md`에 기록. 벤더링하면 업스트림 파일을 수정하지 않는다.
5. PR을 올리면 CI(Linux 4프리셋 + valgrind, Windows MSVC)가 돈다. 두 OS 모두 통과해야 한다.
6. 취약점 신고 채널은 아직 정해지지 않았다([docs/SECURITY.md](docs/SECURITY.md) 5절).
