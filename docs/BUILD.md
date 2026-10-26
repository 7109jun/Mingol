# 빌드 가이드

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

## 요구 사항
- 컴파일러: GCC 13+ (검증됨), Clang 16+ (미검증), MSVC 19.34+ (**미검증**)
- CMake 3.20+, Ninja
- 시스템 패키지(Ubuntu 24.04): `libvulkan-dev glslang-dev glslang-tools libglfw3-dev libbullet-dev`
- GPU가 없으면 `mesa-vulkan-drivers`(lavapipe), 검증 레이어 `vulkan-validationlayers`, 창 테스트 `xvfb xdotool`, 누수 검사 `valgrind`
- 벤더링: VMA, stb_image, stb_vorbis, cgltf, miniaudio (`third_party/`, 라이선스는 `third_party/THIRD_PARTY.md`)

## 프리셋
| 프리셋 | 용도 |
|---|---|
| `linux-debug` | 개발, 경고를 오류로(`-Wall -Wextra -Wpedantic -Wshadow -Werror`) |
| `linux-release` | 최적화, 벤치마크 |
| `linux-asan` | ASan + UBSan (LeakSanitizer는 꺼짐 — 이유는 [TESTING.md](TESTING.md)) |
| `linux-tsan` | ThreadSanitizer (`tests/tsan.supp` 적용) |
| `windows-msvc` | **미검증** |

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --timeout 300
```
빌드 디렉터리는 `build/<프리셋>`. **새 하위 디렉터리/소스를 추가하면 `cmake --preset ...`을 다시 실행**해야 한다.

## CMake 옵션
| 옵션 | 기본 | 의미 |
|---|---|---|
| `MINGOL_BUILD_TESTS` | ON | 테스트 빌드 |
| `MINGOL_SANITIZE` | OFF | ASan+UBSan (TSan과 동시 사용 불가) |
| `MINGOL_TSAN` | OFF | ThreadSanitizer |
| `MINGOL_WARNINGS_AS_ERRORS` | OFF (프리셋 debug는 ON) | 경고를 오류로 |
| `MINGOL_WITH_GLFW` | 자동 | OFF면 헤드리스 전용 플랫폼 계층 |
| `MINGOL_VALGRIND_TESTS` | OFF | valgrind 누수 테스트 등록(느림) |

## 산출물
| 경로 | 설명 |
|---|---|
| `apps/mingol_editor/mingol-editor` | 에디터 |
| `apps/mingol_player/mingol-player` | 플레이어 |
| `tools/mingol_pack/mingol-pack` | 패키징 CLI |
| `tests/bench/mingol-bench` | 벤치마크 |
| `tests/*/mingol_*_tests` | 테스트 실행 파일 |

## 디스플레이/GPU가 없을 때
- Vulkan: lavapipe가 설치되어 있으면 자동 사용(`Vulkan device: llvmpipe` 로그).
- 창: `mingol-editor --headless`, `mingol-player --headless`는 OS 창 없이 오프스크린 스왑체인으로 동작.
- `*_xvfb` 테스트는 xvfb-run이 있을 때만 등록된다.

## CI
`.github/workflows/ci.yml`: linux 잡(debug/asan/tsan/release + valgrind), windows 잡(vcpkg로 glslang·bullet3·glfw3·vulkan을 설치하고 MSVC로 빌드·테스트). 실패는 `.github/ci-annotate.ps1`가 워크플로 어노테이션으로도 남긴다. 커밋 메시지에 `[win-only]`를 넣으면 느린 Linux 잡을 건너뛴다. Windows는 `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`를 직접 넘겨야 한다.
