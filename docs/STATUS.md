# Mingol 현황 (STATUS)

> 이 문서 + 프로젝트 zip만 있으면 현재 상태를 파악할 수 있도록 쓴 요약이다. 작성 시점: 2026-10-26.
> 전체 문서 색인: [README.md](README.md). 문서 라이선스 CC BY 4.0 · 코드 GPL-2.0-only. 상세 설계/결정은 [ROADMAP.md](ROADMAP.md), 형식 명세는 [MINDEV](MINDEV.md) · [MINSEEN](MINSEEN.md) · [MINGOL](MINGOL.md) · [SCRIPT](SCRIPT.md), 도구는 [EDITOR](EDITOR.md) · [PLAYER](PLAYER.md).

## 한눈에 보기

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | Core + CMake | ✅ 완료 |
| 2 | Vulkan 렌더러 + 씬 | ✅ 완료 (lavapipe 기준) |
| 3 | 런타임·물리·오디오·에셋·스크립트 | ✅ 완료 |
| 4 | 패키징 (`.mindev` `.minseen` `.mingol`) | ✅ 완료 (압축 없음) |
| 5 | GUI 에디터 | ✅ 완료 (제한 있음, 아래) |
| 6 | 통합·벤치마크·문서·릴리스 | 🟡 대부분 완료 — 실제 GPU/Windows 검증과 릴리스 산출물이 남음 |

**검증된 환경은 Ubuntu 24.04 + GCC 13 + 소프트웨어 Vulkan(lavapipe)뿐이다.** 실제 GPU, 실제 오디오 장치, Windows(MSVC)에서는 한 번도 실행해 보지 못했다.

규모: 엔진/앱/도구 소스 약 3.3만 줄, 테스트 약 1.7만 줄, 테스트 케이스 약 660개(CTest 28개 실행 파일).

## 구현된 기능

**엔진 코어** — `Result`/에러 처리, 로깅, 수학(Vulkan 클립 공간), 메모리/핸들, 시그널, 잡 시스템, 원자적 파일 저장, 해시/UUID, 엄격한 JSON, 자체 테스트 프레임워크.

**렌더링** — 백엔드 중립 RHI + Vulkan 1.3 구현, GLSL/HLSL→SPIR-V(glslang), Forward PBR(GGX), 방향광 그림자맵(PCF), 점/스포트광 최대 8개, GPU 스키닝, 알파 블렌딩, 스켈레톤/애니메이션(블렌딩·크로스페이드).

**씬/런타임** — GameObject+Component 계층, 생명주기, 지연 파괴, 세대 핸들, 고정 스텝 게임 루프, Bullet 물리(충돌/트리거 이벤트·레이캐스트·2D 평면 모드), 소프트웨어 오디오 믹서(공간화) + miniaudio 출력, 에셋 로더(PNG/JPEG/WAV/OGG/glTF·GLB).

**Mingol Script (`.mins`)** — 자체 컴파일러 + 바이트코드 VM(GC·코루틴·예외·리플렉션), 엔진 API, 핫 리로드(필드 이름+타입으로 상태 이전), C 플러그인 ABI v1.

**파일 형식** — `.mindev`(JSON 프로젝트), `.minseen`(JSON 씬), `.mingol`(아카이브, 선택적 ChaCha20-Poly1305 암호화·무결성, 단일 EXE 내장), `mingol-pack` CLI.

**GUI/에디터** — 즉시 모드 GUI(도킹·위젯·텍스트 편집, 소프트웨어 래스터라이저 기준 + Vulkan 렌더러), `mingol-editor`: 뷰포트(선택·이동/회전/크기 기즈모·편집 카메라), 계층/인스펙터(리플렉션), 스크립트 편집기(문법 강조·진단·핫 리로드), 콘솔/에셋/프로파일러, 스냅샷 기반 실행 취소·재생 모드, 프로젝트 관리, 빌드(아카이브·암호화·단일 EXE).

**플레이어** — `mingol-player`: 폴더(.mindev)/`.mingol`/내장 아카이브 실행, 키 파일/환경변수, 헤드리스·스크린샷 모드.

**검증 도구** — Debug/Release/ASan+UBSan/TSan 프리셋, 선택적 valgrind, 변이 테스트·골든 이미지·실제 X11 입력 테스트, `mingol-bench`.

## 검증 결과 (이 환경)

- 4개 프리셋(linux-debug/release/asan/tsan) 모두 CTest 28/28 통과(최종 코드 기준).
- 에디터 GUI는 xvfb + lavapipe에서 실제로 띄워 스크린샷으로 확인했고, 프로젝트→패키징→플레이어 실행·렌더가 테스트로 연결되어 있다(단일 EXE 포함).
- 벤치마크(release, 2코어, 소프트웨어 렌더링 — 절대값은 참고용): 아카이브 읽기/쓰기 평문 ~175 MiB/s·암호화 ~98 MiB/s, 스크립트 2000개 `onUpdate` 0.57 ms/프레임, 박스 100개 640×360 렌더 ~34 ms/프레임.

## 미완성 · 알려진 한계

**검증 못 한 것 (코드는 있으나 실행 확인 없음)**
- Windows/MSVC 빌드와 CI의 Windows 잡 (Vulkan SDK·glslang 설치 단계가 없어 현재는 설정 단계에서 실패할 것임). CI 워크플로 전체가 한 번도 실행되지 않았다.
- 실제 GPU 성능·드라이버 동작, 실제 창 표시/프레젠트(Xvfb 외), Wayland, 고DPI.
- 실제 오디오 장치(ALSA/Pulse/WASAPI) — Null 백엔드만 검증. 플러그인 ABI는 Linux만.

**기능 공백**
- 렌더링: IBL/환경광, 캐스케이드·점광 그림자, 알파 컷아웃, HDR/후처리, 컬링·인스턴싱, 셰이더 SPIR-V 캐시(배포 시에도 런타임 glslang 컴파일).
- 에디터: 한글 등 비ASCII 글리프(내장 폰트가 ASCII 전용), 자동 완성·검색/치환, VM 줄 디버거, 다중 선택, 형제 순서 변경, 네이티브 파일 대화상자(경로 입력으로 대체), 소프트웨어 AA, 기즈모는 월드 축 기준(회전은 마우스 X 기반).
- 플레이어/스크립트: 스크립트로 입력 이벤트를 전달하는 API 없음(창 닫기만 처리), 에셋 스트리밍 없음.
- 패키징: `.mingol` 압축 없음. 암호화는 캐주얼 추출 방지/변조 검출 수준이며 DRM이 아니다. 암호 프리미티브는 자체 구현이고 외부 감사를 받지 않았다.
- 물리: 연속 충돌 검출·조인트·소프트바디 미노출. glTF: 모프 타깃, CUBICSPLINE 등은 경고 후 무시.

**문서/법적**
- CC BY 4.0 법적 전문 파일(`docs/LICENSE-CC-BY-4.0.txt`)이 없다(네트워크 제한으로 못 받음) — 릴리스 전 추가 필요.
- 성능 목표 수치는 정하지 않았다(실제 GPU 없이는 의미 없음).

**테스트 안정성**
- TSan에서 lavapipe의 시스템 libLLVM 내부 경합이 간헐 보고되어 `tests/tsan.supp`로 억제했다. TSan `-j6` 반복 중 1회 장시간 멈춤, release 프리셋에서 빌드 직후 병렬 실행 시 1회 원인 미상 실패가 있었다(이후 재현 안 됨). 병렬 실행에는 `ctest --timeout`을 권장한다.

## 다음에 해야 할 작업 (우선순위 순)

1. **실제 환경 검증**: 실제 GPU가 있는 Linux/Windows에서 전체 CTest, 에디터/플레이어 실행. Windows CI 잡에 Vulkan SDK + glslang(+Bullet vcpkg) 설치 단계를 추가하고 첫 실행 결과로 MSVC 오류를 수정.
2. **재현 안 되는 간헐 실패 추적**(위 테스트 안정성) — 원인을 찾아 억제가 아니라 수정.
3. **입력 API**: 플레이어→스크립트 입력(키/마우스/액션 맵) 연결, 에디터 재생 모드에도 적용.
4. **폰트/로컬라이제이션**: 유니코드 폰트 아틀라스(한글), 텍스트 입력(IME) 처리.
5. **렌더 품질**: IBL, 점광/캐스케이드 그림자, 후처리, 셰이더 SPIR-V 사전 컴파일과 배포물 포함.
6. **에디터 완성도**: 스크립트 자동 완성·검색, 디버거(중단점), 다중 선택, 네이티브 파일 대화상자, 프리팹.
7. **릴리스**: CC BY 4.0 전문 추가, 버전/체인지로그, 플랫폼별 바이너리 패키징, 실제 GPU 기준 성능 목표 설정과 최적화.

## 빠른 시작 (Linux)

```sh
sudo apt install ninja-build libvulkan-dev glslang-dev glslang-tools libglfw3-dev libbullet-dev \
                 mesa-vulkan-drivers vulkan-validationlayers xvfb xdotool
cmake --preset linux-release && cmake --build --preset linux-release && ctest --preset linux-release
build/linux-release/apps/mingol_editor/mingol-editor            # 에디터 (디스플레이 없으면 --headless)
build/linux-release/apps/mingol_player/mingol-player game.mindev
build/linux-release/tests/bench/mingol-bench                    # 벤치마크
```
모듈 지도는 [README.md](../README.md)의 "구조", 모듈 의존 방향은 [ROADMAP.md](ROADMAP.md) 4절 참고. 새 하위 디렉터리를 추가하면 `cmake --preset ...`을 다시 실행해야 한다.
