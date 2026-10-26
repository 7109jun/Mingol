# Mingol 개발 로드맵 및 설계 결정

문서 라이선스: CC BY 4.0 (`docs/LICENSE-DOCS.md` 참고). 코드 라이선스: GPL-2.0-only.

설계 질문 단계는 종료된 것으로 간주하고, 미정 항목은 아래 "결정"으로 확정해 기록한다. 이후 벤치마크나 라이선스 검토에서 근거가 바뀌면 이 문서를 개정한다.

## 1. 개발 환경 (실측)

| 항목 | 값 |
|---|---|
| 빌드 호스트 | Ubuntu 24.04 LTS, GCC 13.3, Clang 18, CMake 3.28, Ninja |
| 자원 | 2 코어, 7 GB RAM, GPU 없음 (Vulkan은 소프트웨어 드라이버 lavapipe만 가능) |
| Windows x64 (MSVC) | **이 환경에서 실행 불가.** CMake/코드는 이식성을 고려해 작성하되, Windows 빌드·테스트 결과는 검증되지 않은 상태로 표기한다. |

검증 한계: GPU와 디스플레이가 없으므로 렌더링 테스트는 lavapipe + 헤드리스(오프스크린) 경로로 한정한다. 창 표시, 실제 GPU 성능, Windows 동작은 검증하지 못한다.

## 2. 결정 사항

| ID | 주제 | 결정 | 근거 |
|---|---|---|---|
| D1 | 언어 표준 | C++20 | std::format, span, concepts 사용. GCC 13+, MSVC 19.34+ |
| D2 | 창/입력 | GLFW 3.x (zlib) | 플랫폼 계층 인터페이스 뒤에 숨김 |
| D3 | 그래픽 API | Vulkan 1.3 (로더 직접 링크), 렌더 하드웨어 인터페이스(RHI) 경계 분리 | 향후 API 확장 |
| D4 | 셰이더 | GLSL/HLSL → SPIR-V, glslang으로 빌드 시/에디터 런타임 컴파일 | 런타임 배포물은 SPIR-V만 로드 |
| D5 | 물리 | Bullet3 (zlib). 2D는 평면 제약(Z축 고정)을 건 3D 바디로 구현 | 단일 백엔드, GPL-2.0 호환. Box2D(MIT)는 벤치마크 후 재평가 |
| D6 | 오디오 | miniaudio (MIT-0/PD) + stb_vorbis (PD) | 헤더 단일 파일, 단일 EXE 패키징에 유리. OpenAL Soft(LGPL)는 동적 링크 요구로 제외 |
| D7 | 이미지/모델 | stb_image (PD), cgltf (MIT) | PNG/JPEG, glTF/GLB |
| D8 | 직렬화 | 개발용 텍스트(JSON 계열) + 배포용 바이너리, 자체 파서 | 외부 의존 최소화 |
| D9 | 스크립트 VM | 스택 기반 바이트코드, 코루틴은 코루틴별 프레임 스택 | 구현·핫 리로드 단순성 |
| D10 | 플러그인 ABI | C ABI(함수 포인터 테이블), `abiVersion` 협상, 오류는 반환 코드 + 마지막 오류 문자열 | 컴파일러/CRT 독립 |
| D11 | 핫 리로드 | 객체 필드를 리플렉션으로 이름 기준 직렬화 → 클래스 교체 → 객체 재생성 후 복원 | 요구사항의 "관련 객체 재생성" |
| D12 | `.mingol` 암호화 | ChaCha20-Poly1305(AEAD) 항목별 암호화, 키 파생은 HKDF-SHA256, 헤더/목차는 별도 인증 | 암호화와 무결성 검증을 동시에 제공 |
| D13 | `.mindev` 형식 | JSON 텍스트(사용자 요구), 무압축·무암호화, 정규형 CRC-32 `integrity`, `formatVersion` | 사람이 읽고 diff/merge 가능, 손상 탐지 |

### `.mingol` 위협 모델 (명시)

게임 실행 파일이 복호화 키를 가져야 하므로, 이 암호화는 **캐주얼한 추출 방지 및 변조 검출** 수준이다. 실행 파일을 분석하는 공격자로부터 에셋을 보호하는 DRM이 아니다. 문서와 에디터 빌드 대화상자에 이 한계를 표시한다.

## 3. 단계별 계획과 완료 기준

각 단계는 빌드 + 테스트 통과 후 다음으로 넘어간다.

### 1단계: Core + CMake 기반 (현재)
- 구성: 플랫폼 매크로, Error/Result, assert, 로깅(콘솔/파일/링버퍼), 수학(Vec/Quat/Mat4, Vulkan 클립 공간), 메모리(선형 할당자, 세대 핸들 SlotMap), Signal(이벤트), 잡 시스템, 파일 I/O(원자적 저장), CRC32/FNV 해시, UUID, 자체 테스트 프레임워크
- 완료 기준: GCC에서 경고 없이 빌드, 전체 테스트 통과, ASan/UBSan 빌드 통과

### 2단계: Vulkan 렌더러 + 씬
- Window/Input(GLFW), RHI, 스왑체인, Forward + PBR, 그림자 맵, 스켈레탈 애니메이션, 셰이더 컴파일러
- GameObject/Component 계층, 씬 그래프
- 완료 기준: lavapipe 헤드리스 렌더 결과를 기준 이미지와 비교하는 테스트

### 3단계: 런타임 (완료)
- 에셋 관리(PNG/JPEG/WAV/OGG/glTF), 물리(Bullet), 오디오, 게임 루프
- Mingol Script 컴파일러/VM, 플러그인 ABI, 핫 리로드
- `.minseen` 직렬화 (`.mindev`는 4단계 package 모듈에서 JSON 텍스트로 구현 완료)
- 완료 기준: 스크립트 테스트 스위트, 씬 로드/해제 누수 테스트(valgrind/ASan)

### 4단계: 패키징 및 배포
- `.mindev` = JSON 기반 텍스트 프로젝트 파일 (사용자 결정 D13: 사람이 읽고 diff/merge 가능, 압축·암호화 없음) — **완료**
- `.minseen` JSON 씬 직렬화 + 컴포넌트 레지스트리 — **완료** ([MINSEEN.md](MINSEEN.md))
- `.mingol` 작성/읽기, 암호화·무결성, 버전 호환, 단일 EXE 패키징 — **완료** ([MINGOL.md](MINGOL.md)); 압축은 미구현
- 완료 기준: 변조·절단·잘못된 키 입력에 대한 퍼징형 테스트 — 충족

### 5단계: 자체 GUI 에디터 (완료)
- 위젯/레이아웃/입력/도킹/테마, 패널, 스크립트 편집기(문법 강조·진단), 빌드/재생 — [EDITOR.md](EDITOR.md)
- 미구현으로 남은 것: 자동 완성·검색/치환, VM 줄 디버거, 한글 폰트, 네이티브 파일 대화상자

### 6단계: 통합·최적화·문서·릴리스 (진행 중)
- 플레이어 실행 파일(폴더/`.mingol`/단일 EXE 실행) — **완료** ([PLAYER.md](PLAYER.md))
- 통합 테스트(에디터로 프로젝트 생성 → 패키징 → 플레이어 실행·렌더) — **완료**
- 벤치마크 `mingol-bench` — **완료**(측정만; 성능 목표는 실제 GPU 없이 정할 수 없어 미설정)
- 문서: README, EDITOR/PLAYER, [STATUS.md](STATUS.md) — 완료. CC BY 4.0 전문 파일·Windows CI 단계는 네트워크/호스트 제약으로 미완

## 4. 모듈 의존 규칙 (순환 방지)

```
core  <-  platform  (창/입력/이벤트; RHI를 모른다)
core  <-  rhi  <-  renderer  <-  scene  <-  runtime  <-  editor
platform  <-  renderer/runtime (렌더러 이상에서 창과 스왑체인을 연결)
core  <-  script (VM)  <-  scripting (+scene)
core  <-  package (.mingol)
core  <-  gui  <-  gui_gpu(+rhi)
gui + runtime + sceneio + scripting + package  <-  editor, player  <-  apps
```
화살표 방향으로만 의존한다. 상위 모듈은 하위 모듈을 알 수 있고, 반대는 불가능하다. CMake 타깃 링크 방향으로 강제하고 CI에서 순환을 검사한다.

## 5. 진행 기록과 검증 한계

### 1단계 (완료)
Core 62개 테스트. Debug/Release/ASan+UBSan/TSan/valgrind 통과.

### 2단계 (완료) — 구성 요소
- **셰이더 컴파일러**: GLSL·HLSL → SPIR-V 1.6(Vulkan 1.3). 12개 테스트(진단 위치, include 해석·재귀 차단, 동시 컴파일 포함).
- **Vulkan RHI**: 장치 선택, VMA 메모리, 버퍼/텍스처(밉·큐브·깊이)/샘플러/바인드 그룹/파이프라인, 텍스처 상태 추적과 배리어(synchronization2), 지연 파괴(타임라인 시리얼 기반), 스왑체인(헤드리스 포함). 21개 테스트.

- **Platform**: `Window`(GLFW 백엔드 + 헤드리스 백엔드), 이벤트 큐(마우스 이동·스크롤·리사이즈 병합, 키/포커스 이벤트는 절대 유실 안 함), `InputState`(눌림/뗌/델타/텍스트, 포커스 상실 시 전체 해제), `ActionMap`, 클립보드, 커서 모드/모양, 전체화면, 스레드 안전 `injectEvent`/`waitEvents`. 테스트 43개 + 창-RHI 2개.
  - 실제 X11 입력(xdotool/XTEST)으로 키 번역·마우스·휠·포커스 상실을 검증했고, 실제 창에 Vulkan(lavapipe) 스왑체인으로 프레젠트한 뒤 X 서버가 보여주는 픽셀을 읽어 비교한다(리사이즈 포함). 기대 픽셀을 틀리게 바꾸면 실패함을 확인했다.
  - Debug/Release/ASan+UBSan/TSan 전부 통과, Xvfb 40회 반복 실패 0, TSan 10회·ASan 10회 실패 0, valgrind 통과. 반복 중 발견된 간헐 실패 1건은 Xvfb가 마지막 클라이언트 종료 시 리셋하는 서버 동작이 원인이어서 `-noreset`로 해결했다(엔진 결함 아님).
  - 한계: Wayland 네이티브(GLFW 3.4+)는 미지원(null 디스플레이면 Unsupported 반환), Win32 경로는 컴파일·실행 미검증, 게임패드 미구현, 전체화면은 창 관리자 없는 Xvfb에서만 확인, 고DPI(contentScale≠1)는 검증 못 함.

- **Renderer**: 정적/스키닝 메시, PBR 머티리얼(GGX/Smith/Schlick, 텍스처 4종, 알파 블렌드, 양면), 방향광(그림자맵 3x3 PCF, 노멀 오프셋 + 슬로프 바이어스) + 점/스포트광 최대 8개, GPU 스키닝(조인트 128), 프레임 슬롯 링(2프레임 동시 진행), 파이프라인 캐시, 장면 검증(잘못된 입력은 오류 반환). 스켈레톤/클립/포즈 블렌딩은 CPU 순수 코드(15개 테스트).
  - 렌더 테스트 23개: 독립 CPU 참조 BRDF와 픽셀 비교(방향광 각도·금속·거칠기 여러 조합, 점/스포트광 감쇠, 노멀맵, 비균일 스케일 법선), 그림자 위치를 기하로 예측해 비교(z 비대칭으로 좌우/상하 반전도 검출), 스키닝 결과를 기하로 예측, 블렌드 정렬, 프레임 겹침(9프레임 연속 제출 후 일괄 판독), 골든 이미지(회귀 가드; 눈으로 확인한 뒤 확정, 정답 증명은 아님).
  - 변이 테스트 4종(스키닝 제거, 그림자 v 반전, 블렌드 정렬 제거/역전, 법선 행렬을 모델 행렬로 교체)이 모두 해당 테스트를 실패시킴을 확인. 첫 시도에서 법선 변이가 살아남아 비균일 스케일 테스트를 추가했다.
  - 한계: IBL/환경광 없음(금속은 점·방향광 하이라이트 외에는 어둡게 보임), 단일 방향광 그림자(캐스케이드·점광 그림자 없음), 스키닝 법선은 mat3(skin)이라 비균일 스케일 조인트에서 근사, 알파 테스트(컷아웃)·HDR 중간 타깃·블룸 없음, 셰이더는 실행 시 glslang으로 컴파일(배포용 SPIR-V 캐시 미구현), 성능은 GPU 없이 측정 불가. 머티리얼은 생성 후 불변.
  - valgrind: 렌더러 테스트에서 lavapipe JIT 내부의 "uninitialised" 보고가 2회 중 1회 나타났다(스택 전부 libvulkan_lvp.so). 모듈 단위 억제를 추가했으며, 이는 우리가 드라이버에 넘긴 미초기화 값도 가릴 수 있다는 한계가 있다.

- **Scene**: `GameObject`/`Component` 계층. Transform(지연 갱신되는 월드 행렬, 반복적 dirty 전파로 깊이 1만 계층도 스택 안전), 세대 번호 핸들(`GameObjectId`: 파괴 후 슬롯이 재사용돼도 옛 핸들은 해석되지 않음), 지연 파괴(콜백 중 파괴/생성 안전, 자손 포함), 순환 부모 설정 거부, `setParent(keepWorld)`, 활성/비활성 계층. 콜백 순서: onAwake → onEnable → onStart(1회) → onFixedUpdate/onUpdate/onLateUpdate → onDisable → onDestroy. 콜백이 예외를 던지면 로그를 남기고 해당 컴포넌트만 비활성화(onDisable 1회 호출)한다. 내장 컴포넌트: `MeshRenderer`, `Animator`(클립 재생·루프·크로스페이드·속도), `CameraComponent`(우선순위), `LightComponent`. `buildRenderScene()`이 활성 장면을 렌더러용 평면 `RenderScene`으로 변환한다(방향광 1개 + 카메라에 가까운 점/스포트광 8개, 카메라 스케일은 뷰 행렬에서 제거, 그릴 수 없는 항목은 컴포넌트당 1회 경고 후 건너뜀).
  - 테스트: CPU 27개(계층 수학, dirty 전파, 월드 setter 왕복, lookAt, 분해 왕복, 재부모화/순환 거부, 핸들 무효화, 1만 깊이, 생명주기 순서, 비활성/비활성화 건너뜀, 콜백 예외 격리, 업데이트 중 생성·파괴, clear/소멸자 콜백, 50회 로드/언로드 누수 카운터, Animator 루프·클램프·크로스페이드, 카메라/조명 선택) + GPU 1개(부모를 움직이면 예측 픽셀 위치에 실제로 그려지고, 비활성화·파괴하면 사라짐). 변이 테스트(dirty 전파 제거)가 3개 테스트를 실패시킴을 확인. 4개 프리셋 통과, CPU 테스트 valgrind 누수 0.
  - 테스트 작성 중 발견·수정: `lookAt`이 아래로 향하는 방향광(up 힌트와 평행)에서 아무 일도 하지 않던 결함 → 대체 up 축 선택. 테스트의 `Log`가 `Scene`보다 먼저 파괴되는 수명 순서 오류(테스트 버그).
  - 한계: Scene은 단일 스레드 전용. 컴포넌트 레지스트리(이름→팩토리)와 직렬화는 4단계(.minseen)에서 추가한다. 컬링·인스턴싱 없음(전 항목 제출). `Animator`는 루트 모션·이벤트·레이어 블렌딩 미지원.

### 3단계 (완료)
- **Assets (3a, 완료)**: `decodeImage`(PNG 전 색상형·1~16비트·인터레이스, JPEG 베이스라인/프로그레시브; 헤더 단계에서 크기·메모리 한도 검사 후 할당), `decodeWav`(PCM 8/16/24/32, float 32/64, EXTENSIBLE, 스트리밍 크기 0/0xFFFFFFFF, 홀수 청크 패딩, 잘린 데이터 거부, NaN/Inf 무해화), `decodeOgg`(stb_vorbis, 잘린 스트림은 오류), `loadGltf`/`loadGltfFile`(GLB·glTF·data URI·외부 파일은 리졸버로, 경로 이탈 차단).
  - glTF → CPU 데이터: 메시(삼각형/스트립/팬 변환, 없는 법선·탄젠트 생성), 머티리얼(PBR 계수, emissive_strength, 이미지 인덱스), 노드 계층, 스킨(조인트를 부모 우선 순서로 재배열하고 정점 조인트 인덱스·역바인드 행렬을 함께 재매핑), 애니메이션(스킨별 클립, LINEAR/STEP).
  - 보고하되 조용히 넘기지 않는 한계(`ModelData::warnings`): alpha MASK(불투명으로 로드), CUBICSPLINE(탄젠트 버림), 모프 타깃, 카메라/라이트, 두 번째 UV, KHR_texture_transform, 스펙큘러-글로시니스, 조인트가 아닌 노드의 애니메이션, KTX2/WebP 텍스처.
  - 테스트 34개: 생성한 픽스처(Pillow/ImageMagick/ffmpeg; `tests/assets/gen_fixtures.py`)를 정확한 픽셀·주파수·채널 분리로 비교, 모든 길이의 잘림, 해상도 폭탄, 랜덤 손상 수천 회(ASan 하에서 크래시 없음), 손상된 glTF에서도 수락된 결과는 항상 내부적으로 일관됨. 변이 테스트 3종(조인트 재매핑 제거, 스트립 와인딩, 24비트 부호)과 stb 제로 초기화 제거가 각각 테스트를 실패시킴.
  - 검증으로 찾은 실제 문제 2건: ① stb_vorbis가 손상된 헤더에서 임시 메모리를 누수(valgrind) → 고정 아레나 모드로 전환(누수 불가능 + 악성 헤더의 메모리 상한 32MiB). ② stb_image가 손상된 JPEG에서 초기화되지 않은 버퍼를 읽어 힙 내용이 이미지에 섞일 수 있음 → 모든 할당을 0으로 초기화(같은 입력이면 항상 같은 출력임을 테스트).
  - 한계: 이미지·오디오는 전체를 메모리에 디코딩(스트리밍 없음), JPEG 색공간은 sRGB로 가정(ICC 무시), 16비트 PNG는 8비트로 축소, 텍스처 업로드와 씬 인스턴스화는 runtime 모듈(3b)에서 한다.
- **Physics (3b, 완료)**: Bullet 3.24(zlib, GPL-2.0 호환)를 엔진 API 뒤에 숨김(`mingol::physics::World`; Bullet 타입이 공개 헤더에 나오지 않음). 정적/동적/키네마틱, 박스·구·캡슐·삼각형 메시, 레이어/마스크, 축 잠금, 2D 평면 모드, 자체 고정 스텝 누적기(백로그는 maxSubSteps에서 버림), 접촉 시작/종료·트리거 진입/이탈 이벤트(바디 파괴 시 종료 이벤트 먼저 발생), 레이캐스트(트리거 제외)·구 겹침. 테스트 23개, 변이 4종(법선 부호, 트리거 레이 필터, 백로그 버림, 축 인자) 모두 검출. 한계: 결정성은 한 기계에서만 확인, 연속 충돌 검출·조인트·차량·소프트바디 미노출, Windows는 vcpkg의 Bullet 필요.
- **Audio (3b, 완료)**: `audio::Mixer`(게임 스레드 명령 큐 + 오디오 스레드는 try-lock만, 해제는 게임 스레드에서) — 선형 보간 리샘플링, 등전력 팬, 거리 감쇠·좌우 공간화, 블록 단위 게인 램프, 하드 클립 카운터, 우선순위 기반 보이스 스틸링. `audio::Device`는 miniaudio(Default/Null 백엔드). 테스트 22개, 변이 5종 검출. **한계: 실제 사운드 장치가 없어 ALSA/Pulse/WASAPI 백엔드는 실행해 보지 못했다(Null 백엔드만 검증).** HRTF·리버브·스트리밍 디코딩 없음.
- **Runtime (3b, 완료)**: `Runtime::tick(dt)` = 고정 스텝(fixedUpdate → 물리 스텝 → 포즈 반영 → 접촉 콜백) 후 update/lateUpdate. 프레임 시간 클램프·스텝 상한(죽음의 나선 방지, 버린 시간 집계)·타임스케일. 컴포넌트: `Collider`, `Rigidbody`(첫 시작 시 바디 생성 → addComponent 직후 설정 가능, 스케일 반영, Transform 수정은 텔레포트, 동적 바디만 포즈 역반영), `ContactListener`(콜백 예외는 해당 컴포넌트만 비활성화), `AudioSource`/`AudioListener`, `uploadModel`(sRGB/선형 구분·밉맵)·`instantiateModel`(노드 순서 무관, 순환 거부, 스킨드 메시는 루트 아래 항등 변환 + Animator). 테스트 CPU 28 + GPU 5(텍스처 머티리얼이 노드 변환 위치에 실제로 그려짐). 4개 프리셋 통과, valgrind 누수 0, 변이 8종 검출(1종은 동치 변이).
  - 테스트로 찾은 설계 결함: 컴포넌트가 addComponent 즉시 활성화돼 설정값이 무시됨 → 바디 생성·자동 재생을 onStart로 이동.

- **Script (3c, 완료·부분)**: 컴파일러+VM+리플렉션+호스트 API+플러그인 ABI v1. 테스트 `tests/script` 약 110개(렉서/파서/VM/예외/코루틴/리플렉션/GC/플러그인/진단/변이 퍼징) debug·release·asan 통과. 명세와 한계는 [SCRIPT.md](SCRIPT.md). 

- **Scripting (3d, 완료)**: `engine/scripting` — ScriptHost/ScriptComponent/엔진 API/핫 리로드(필드 이름+타입 이전). 테스트 23개, debug/release/asan/tsan/valgrind 통과. [SCRIPT.md 6절](SCRIPT.md).

### 4단계 (완료)
- **`.minseen`/sceneio**: 11+11 CPU 테스트 + 2 GPU 테스트, debug/release/asan/tsan/valgrind. 구현 중 테스트가 찾은 결함: NaN 위치에서 저장 시 `integrityOf`가 오류 결과를 `.value()`로 열어 abort → 오류 전파로 수정. Animator 복원 시 재생 시간이 속도만큼 이중 적용되던 결함 수정.
- **`.mingol`/crypto**: 26+ 테스트. 한계와 보안 모델은 [MINGOL.md](MINGOL.md).

### RHI가 보장하는 계약
- `Device`의 자원 생성과 `submit()`은 여러 스레드에서 호출할 수 있다. `CommandList`는 한 번에 한 스레드만 기록한다.
- 자원의 마지막 참조를 놓는 것은 GPU가 아직 쓰는 중이어도 안전하다(파괴는 지연). 단, **기록했지만 아직 `submit()`하지 않은 리스트가 참조하는 자원은 제출이 끝날 때까지 호출자가 살려 둬야 한다.**
- 자원이 `Device` 객체보다 오래 살아도 된다. Vulkan 장치는 마지막 자원이 사라질 때 해제된다.
- 텍스처 상태는 텍스처 전체(모든 밉/레이어) 단위로 추적한다. 서브리소스별 상태가 필요하면 후속 개선 대상이다.

### 검증 방법과 결과 (Ubuntu 24.04, lavapipe 소프트웨어 Vulkan 1.4, 검증 레이어 켬)
- 모든 렌더링 테스트는 검증 레이어 오류가 0건이어야 통과한다. 레이어가 실제로 동작하는지 고의 오류 테스트로 확인한다.
- 변이 테스트로 테스트의 민감도를 확인했다(깊이 비교·블렌딩·샘플러 필터를 망가뜨리면 해당 테스트가 실패).
- ASan+UBSan 통과. 단 RHI 테스트는 LeakSanitizer를 끈다(로더가 드라이버를 `dlclose`해 누수 스택을 억제할 수 없음). 대신 valgrind 누수 테스트가 담당하며, lavapipe 워커 스레드의 112바이트 누수 1건만 `tests/valgrind.supp`로 억제한다(valgrind `--keep-debuginfo`로 모든 프레임이 `libvulkan_lvp.so` 내부임을 확인).
- TSan 통과. 서드파티(lavapipe, 검증 레이어) 내부의 경합·락 순서 경고는 모듈 단위로 `tests/tsan.supp`에서 억제한다. 우리 코드의 경합은 여전히 실패로 잡힌다. 40회 반복에서 실패 0건.

### 알려진 한계 (정직하게)
- GPU가 없어 **실제 GPU 성능, 실제 창 표시/프레젠트, 드라이버별 동작은 검증하지 못했다.** 스왑체인은 헤드리스 표면에서만 테스트했다. Xlib/Xcb/Wayland/Win32 표면 생성 코드는 컴파일만 되고 실행 검증은 없다.
- Windows(MSVC) 빌드·테스트는 실행하지 못했다. CI의 Windows 잡에는 Vulkan SDK·glslang 설치 단계가 아직 없어 현 상태로는 구성 단계에서 실패할 것이다.
- 바인드 그룹은 그룹당 디스크립터 풀 1개를 쓴다. 정확하지만 머티리얼이 수천 개가 되면 풀 매니저로 교체해야 한다(렌더러 단계에서 벤치마크 후 결정).
- `copyBuffer`처럼 우리가 막지 않는 드라이버 수준의 잘못된 사용(예: 겹치는 복사)은 검증 레이어가 보고할 뿐 RHI가 막지 않는다.

### 5·6단계 기록 (Ubuntu 24.04, lavapipe)
- GUI/에디터: gui 테스트와 에디터 47 + GPU 5개, 플레이어 3 + GPU 2개. 4개 프리셋(debug/release/asan/tsan) 28개 CTest 전부 통과.
- 검증 중 발견·수정한 것: 병렬 ctest에서 테스트 임시 폴더 이름 충돌(TempDir가 프로세스 구분 없이 같은 이름) → 시간 기반 접미사. TSan에서 lavapipe가 쓰는 시스템 libLLVM(비계측) 내부 경합 보고가 간헐적으로 exit 66을 유발 → `tests/tsan.supp`에 모듈 단위 억제(우리 코드 프레임이 낀 경합은 여전히 실패).
- 알려진 간헐 문제: TSan 프리셋을 `-j6`으로 반복 실행하던 중 한 번 10분 이상 멈춘 적이 있었으나(원인 미규명, 락 순서 경고가 있는 lavapipe/검증 레이어 종료 경로 의심) 이후 15회 이상 재현되지 않았다. `ctest --timeout`을 쓰는 것을 권장한다.
- 벤치마크(release, 2코어 CPU, lavapipe — 절대값은 이 기계에서만 의미 있음): 아카이브 쓰기/읽기 평문 ~175 MiB/s·암호화 ~98 MiB/s, 스크립트 2000개 onUpdate 0.57 ms/프레임(호출당 ~285 ns), 박스 100개 640x360 렌더 ~34 ms/프레임(소프트웨어 렌더).
