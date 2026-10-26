# Mingol

C++20 게임 엔진 (2D/3D 공통 코어, Vulkan, 자체 스크립트 `.mins`, 자체 GUI 에디터·플레이어 포함).

- 코드: GPL-2.0-only (`LICENSE`)  ·  문서: CC BY 4.0 (`docs/LICENSE-DOCS.md`)
- 설계 결정과 단계별 계획: [`docs/ROADMAP.md`](docs/ROADMAP.md)

## 현재 상태

| 단계 | 상태 |
|---|---|
| 1. Core + CMake | **완료** (Linux GCC 13에서 빌드·테스트·새니타이저 검증) |
| 2. Vulkan 렌더러 + 씬 | **완료** (Linux/lavapipe 기준) — 셰이더 컴파일러(GLSL/HLSL→SPIR-V), Vulkan RHI(장치·자원·파이프라인·스왑체인·동기화) Window/Input(GLFW + 헤드리스), Forward+PBR 렌더러(방향광 그림자맵·점/스포트광·GPU 스키닝·알파 블렌딩), 스켈레톤/애니메이션, GameObject·Component 씬(Transform·생명주기·Animator·RenderScene 빌더) |
| 3. 런타임·물리·오디오·스크립트 | **진행 중** — 에셋 로더, 물리(Bullet), 오디오 믹서, 런타임(게임 루프·컴포넌트) 완료. Mingol Script 컴파일러·VM·리플렉션·플러그인 ABI 완료([docs/SCRIPT.md](docs/SCRIPT.md)). `engine/scripting`의 ScriptComponent·핫 리로드 완료. 남음: 3단계 보고서 정리 |
| 4. 패키징 | **완료** — `.mindev`(JSON, [MINDEV.md](docs/MINDEV.md)), `.minseen`(JSON 씬, 컴포넌트 레지스트리, [MINSEEN.md](docs/MINSEEN.md)), `.mingol`(ChaCha20-Poly1305/HKDF-SHA256 암호화·무결성·버전·단일 EXE 내장, `mingol-pack` CLI, [MINGOL.md](docs/MINGOL.md)). 압축 미구현 |
| 5. GUI 에디터 | **완료** — 즉시 모드 GUI(`engine/gui`, 소프트웨어 래스터라이저 기준), `mingol-editor`(도킹·뷰포트·기즈모·인스펙터·스크립트 편집기·실행 취소·재생 모드·빌드, [EDITOR.md](docs/EDITOR.md)) |
| 6. 통합·문서·릴리스 | **진행 중** — 플레이어(`mingol-player`, [PLAYER.md](docs/PLAYER.md)), 벤치마크(`mingol-bench`) 완료. 현황 문서: [STATUS.md](docs/STATUS.md). 남음: [STATUS.md](docs/STATUS.md)의 "미완성" 참고 |

Windows(MSVC)는 GitHub Actions에서 빌드와 CPU 테스트가 통과한다(vcpkg 의존성). 창/GPU/에디터·플레이어 실제 실행은 **아직 검증되지 않았다.**

## 빌드 (Linux)

필요 도구: GCC 13+ (또는 Clang 16+), CMake 3.20+, Ninja.
Ubuntu 24.04 패키지: `libvulkan-dev glslang-dev glslang-tools libglfw3-dev` (GPU가 없으면 `mesa-vulkan-drivers`,
검증 레이어 `vulkan-validationlayers`, 누수 검사 `valgrind`, 실제 창 테스트 `xvfb xdotool`). VMA(MIT)는 `third_party/vma`에 포함.

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

프리셋: `linux-debug`, `linux-release`, `linux-asan`(ASan+UBSan), `linux-tsan`, `windows-msvc`.
옵션: `-DMINGOL_VALGRIND_TESTS=ON`으로 RHI valgrind 누수 테스트(약 2분)를 추가 등록한다.
프리셋 없이: `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build`.

GLFW가 없으면 플랫폼 모듈은 헤드리스 전용으로 빌드된다(`-DMINGOL_WITH_GLFW=OFF`로 강제 가능).
실제 창 테스트는 디스플레이가 필요하다: xvfb-run이 있으면 `*_xvfb` 테스트가 자동 등록되고, 디스플레이 없이 단독 실행하면 해당 테스트는 SKIP으로 보고된다.

개별 테스트 실행: `build/linux-debug/tests/core/mingol_core_tests [--list] [부분문자열 필터]`

## 구조

```
engine/core/   Error/Result, 로깅, 수학, 메모리, Signal, 잡 시스템, 파일 I/O, 해시, UUID, JSON(엄격한 파서/라이터)
engine/script/ Mingol Script 컴파일러(렉서·파서·검사기·CodeGen), 바이트코드 VM(GC·코루틴·예외·리플렉션), C 플러그인 ABI
engine/scripting/ ScriptHost·ScriptComponent(씬 연동), 엔진 API, 핫 리로드
engine/package/ .mindev 프로젝트 파일, .mingol 아카이브(암호화·무결성), 암호 프리미티브(SHA-256/HKDF/PBKDF2/ChaCha20-Poly1305)
engine/sceneio/ .minseen 씬 직렬화(컴포넌트 레지스트리, AssetTable)
tools/mingol_pack/ mingol-pack CLI
engine/shader/ GLSL/HLSL → SPIR-V 1.6 (glslang), include 처리, 진단
engine/platform/ Window/Input/이벤트 (GLFW 백엔드 + 헤드리스 백엔드)
engine/renderer/ Forward PBR, 그림자, 스켈레탈 애니메이션 (RHI 위)
engine/assets/ PNG/JPEG/WAV/Ogg 디코더, glTF/GLB 로더 (CPU 데이터)
engine/physics/ Bullet을 감싼 엔진 자체 물리 API (바디·충돌/트리거 이벤트·레이캐스트)
engine/audio/ 소프트웨어 믹서(공간화·보이스 관리)와 miniaudio 출력 장치
engine/runtime/ 게임 루프, Rigidbody/Collider/AudioSource 컴포넌트, 모델 GPU 업로드·인스턴스화
engine/scene/  GameObject 계층 + Component 생명주기, Transform, Animator/MeshRenderer/Camera/Light, RenderScene 빌더
engine/gui/    즉시 모드 GUI(Context·위젯·도킹·DrawList) + Vulkan GuiRenderer(gui_gpu)
engine/editor/ 에디터(EditorDocument·EditorApp·패널·뷰포트·스크립트 편집기)
engine/player/ ContentSource(폴더/아카이브/내장) + GameApp
apps/         mingol-editor, mingol-player 실행 파일
engine/rhi/    백엔드 중립 GPU 인터페이스 + Vulkan 1.3 구현 (dynamic rendering, timeline 세마포어, VMA)
third_party/   vma (MIT)
tests/         자체 테스트 프레임워크 + 모듈별 테스트, tests/bench(mingol-bench 벤치마크)
docs/          로드맵, 형식 명세, 에디터/플레이어 문서, STATUS.md(현황), 라이선스 고지
```
