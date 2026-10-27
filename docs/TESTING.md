# 테스트 가이드

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

## 프레임워크 (`tests/mingol_test.hpp`)
외부 의존 없는 자체 프레임워크.
- `TEST(Suite, Name) { ... }`로 등록, 실행 파일마다 하나의 `main`(`tests/test_main.cpp`).
- 단언: `CHECK(expr)`, `CHECK_EQ(a,b)`, `CHECK_NEAR(a,b,eps)`, `CHECK_MSG(expr, std::string)`(실패 시 메시지 포함 — 값이 필요한 검사에 사용), `REQUIRE(expr)`(실패 시 그 테스트 중단), `SKIP_TEST(reason)`.
- CLI: 인자 하나 = 이름 부분 문자열 필터, `--list`로 목록. 예: `build/linux-debug/tests/core/mingol_core_tests Json`.
- 새 테스트 대상: `mingol_add_test(이름 SOURCES ... LIBS ...)` (`tests/CMakeLists.txt`).

## 스위트 구성 (약 660 케이스)
| 대상 | 내용 |
|---|---|
| core 78, package 42, shader 12, rhi 21, platform 43(+xvfb) | 기반 모듈 |
| renderer 15(CPU) + 23(GPU) | CPU 참조 BRDF와 픽셀 비교, 그림자 위치 기하 검증, 골든 이미지 |
| scene 28 + 1, runtime 28 + 5, physics 23, audio 22, assets 34 | |
| script 103, scripting 23, sceneio 22 + 2 | |
| gui 67 + 7(GPU), editor 47 + 5(GPU), player 3 + 2(GPU) | |
| `mingol_pack_cli` | CLI 종단 테스트 |

GPU 테스트는 `GPU_OR_SKIP()`(`tests/renderer/render_test_util.hpp`)로 시작한다: Vulkan 장치가 없으면 SKIP, 있으면 검증 레이어를 켜고 **검증 오류가 1건이라도 나오면 실패**한다.

## 이 프로젝트가 쓰는 검증 기법
1. **독립 참조 구현과 비교** — 렌더러는 별도 CPU BRDF, GUI는 소프트웨어 래스터라이저, 암호는 RFC/FIPS 벡터 + Python `cryptography`.
2. **변이 테스트** — 구현의 한 줄을 일부러 망가뜨려 테스트가 실패하는지 확인(스키닝 제거, 그림자 v 반전, 해시 검사 제거 등). 살아남은 변이는 테스트를 추가해 잡는다.
3. **퍼징형 입력** — 모든 길이의 절단, 비트 반전 전수, 랜덤 손상을 ASan/UBSan 하에서 실행.
4. **종단 테스트** — 에디터가 만든 프로젝트를 패키징해 실제 `mingol-player` 프로세스를 띄운다(`tests/player`).

## 새니타이저와 한계
- ASan+UBSan: RHI/렌더 테스트는 LeakSanitizer를 끈다(Vulkan 로더가 드라이버를 `dlclose`해 누수 스택을 억제할 수 없음). 누수는 `-DMINGOL_VALGRIND_TESTS=ON`의 valgrind 테스트가 담당.
- TSan: lavapipe·검증 레이어·**시스템 libLLVM**(lavapipe의 JIT, 비계측) 내부 경합은 `tests/tsan.supp`에서 모듈 단위로 억제한다. Mingol 프레임이 낀 경합은 그대로 실패한다. TSan 실패 시 종료 코드 66.
- 병렬 실행은 `ctest -j4 --timeout 300` 권장. 임시 폴더는 `TempDir`(시간 기반 접미사)로 프로세스 간 충돌을 피한다.

## 알려진 간헐 문제
TSan `-j6` 반복 중 1회 장시간 멈춤, release에서 빌드 직후 병렬 실행 시 1회 실패 — 둘 다 원인 미규명, 이후 재현 안 됨. 재현되면 `ctest --output-on-failure`로 로그를 저장해 조사할 것.

CI에서 asan 단계가 한 번 실패했다가(어느 테스트인지 로그를 받지 못함) 같은 코드로 재실행하자 통과한 적이 있다. 이후 `.github/ci-annotate.sh`가 실패 로그를 어노테이션으로 남기므로 재발하면 원인을 확인할 수 있다.

## 벤치마크
`build/linux-release/tests/bench/mingol-bench` — 아카이브 처리량, 스크립트 호출 비용, 100박스 렌더 시간. ctest에는 등록되지 않는다.
