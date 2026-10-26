# 아키텍처 개요

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

## 한 문장 요약
Mingol은 **정적 라이브러리 묶음**(`engine/*`) 위에 **실행 파일 3개**(`mingol-editor`, `mingol-player`, `mingol-pack`)를 얹은 구조다. 라이브러리는 아래 방향으로만 의존한다(CMake 링크로 강제).

## 모듈 의존 그래프
```
core ◄─ platform                       창/입력/이벤트 (RHI를 모름)
core ◄─ rhi ◄─ renderer ◄─ scene ◄─ runtime      GPU → 렌더 → 씬 → 게임 루프
core ◄─ shader ──► rhi                 GLSL/HLSL → SPIR-V
core ◄─ assets                         CPU 디코더 (PNG/JPEG/WAV/OGG/glTF)
core ◄─ physics, audio                 Bullet / 믹서+miniaudio (엔진 자체 API 뒤에 숨김)
core ◄─ script (컴파일러+VM) ◄─ scripting (+scene)   .mins와 씬 연결
core ◄─ package                        .mindev / .mingol / 암호
scene+runtime+scripting ◄─ sceneio     .minseen 직렬화, 컴포넌트 레지스트리
platform ◄─ gui ◄─ gui_gpu(+rhi)       즉시 모드 GUI와 Vulkan 렌더러
gui+runtime+sceneio+scripting+package+... ◄─ editor, player ◄─ apps
```
규칙: 상위는 하위를 알 수 있고 반대는 불가. 서드파티 타입(Bullet, miniaudio, glslang, Vulkan 핸들)은 공개 헤더에 나오지 않는다.

## 데이터 흐름 (게임 한 프레임)
1. `platform` 이벤트 → (에디터는 `gui`로 전달)
2. `Runtime::tick(dt)` — 고정 스텝(fixedUpdate → 물리 → 포즈 반영 → 접촉 콜백) → update → lateUpdate. 스크립트 콜백은 `ScriptComponent`가 VM을 호출한다.
3. `scene::buildRenderScene()` — 활성 씬을 평평한 `RenderScene`으로 변환(카메라 1, 방향광 1, 가까운 점/스포트광 최대 8, 메시 항목).
4. `Renderer::render(scene, target, swapchain?)` — 그림자 패스 → 메인 패스 → (스왑체인이면 Present 상태로) 제출.

## 파일 흐름 (제작 → 배포)
```
에디터(.mindev + scenes/*.minseen + scripts/*.mins + assets/*)
   └─ packProject ─► .mingol (선택 암호화) ─ packExecutable ─► 단일 EXE (= mingol-player + 아카이브)
mingol-player  ◄─ 폴더(.mindev) | .mingol | 내장 아카이브   (모두 ContentSource 뒤에서 같은 경로로 로드)
```

## 설계 원칙
- 에러는 예외가 아니라 `Result<T>`로 전파한다(자세한 규칙은 [CODING-GUIDE.md](CODING-GUIDE.md)).
- 단일 스레드 규약: Scene·ScriptHost·Renderer는 한 스레드에서만 쓴다. RHI의 자원 생성/제출만 다중 스레드 허용.
- 에디터와 플레이어는 같은 로딩 경로(표준 에셋 이름, `sceneio`)를 공유해서 "에디터에서 되던 것이 배포에서 안 됨"을 구조적으로 막는다 → [ASSETS.md](ASSETS.md).
- 검증하지 못한 것은 문서에 그대로 적는다 → [KNOWN-LIMITS.md](KNOWN-LIMITS.md).
