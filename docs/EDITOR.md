# Mingol 에디터 (`mingol-editor`)

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

자체 즉시 모드 GUI(`engine/gui`) 위에 만든 에디터. 모든 UI는 CPU `DrawList`로 환원되고 `GuiRenderer`(Vulkan)가 그린다. 소프트웨어 래스터라이저가 기준 구현이며 GPU 결과와 픽셀 비교 테스트가 있다.

## 실행

```sh
build/linux-release/apps/mingol_editor/mingol-editor [--project 프로젝트.mindev] [--player mingol-player 경로]
                                                     [--headless --frames N --screenshot out.ppm --width W --height H]
```
`--headless`는 OS 창 없이 오프스크린 스왑체인으로 전체 GPU 경로를 실행한다(CI·스크린샷용).

## 화면 구성

기본 도킹 배치: 가운데 **Viewport**(탭: Scripts), 왼쪽 **Hierarchy**, 오른쪽 **Inspector**, 아래 **Console / Assets / Profiler**. 배치는 `<프로젝트>/.mingol-editor-layout.json`에 저장되고 View 메뉴에서 초기화한다.

| 패널 | 기능 |
|---|---|
| Viewport | 씬 렌더(편집 카메라), 클릭 선택, 기즈모(이동/회전/크기, Esc 취소), Alt+좌클릭 오빗, 우클릭+WASD 플라이/시점, 가운데 드래그 팬, 휠 줌, F로 선택 대상에 맞춤 |
| Hierarchy | 트리, 생성(빈 객체·Box/Sphere/Plane·카메라·조명), 복제, 삭제, 이름 변경, 재부모화(드래그) |
| Inspector | 리플렉션 기반 속성 편집(Transform·내장 컴포넌트·스크립트 필드), 컴포넌트 추가/제거, 에셋 선택 |
| Scripts | `.mins` 편집기: 문법 강조, 줄 번호, 진단(오류 위치 표시), 저장 시 핫 리로드 |
| Console | 엔진/스크립트 로그(최근 500건), 레벨 필터 |
| Assets | 프로젝트 에셋 목록, 모델 인스턴스화 |
| Profiler | 프레임 시간, 편집/재생 상태 |

단축키: Ctrl+S 저장, Ctrl+Z/Y 실행 취소/다시, Ctrl+D 복제, Ctrl+B 스크립트 컴파일·리로드, Ctrl+P 재생/정지(빌드는 File → Build... 메뉴), Delete 삭제, 뷰포트에서 W/E/R(기즈모)·F(맞춤).

## 동작 모델

- **실행 취소/다시 실행**: 씬을 `.minseen` JSON 스냅샷으로 저장(단계 수·바이트 상한). 선택은 DFS 인덱스로 복원한다.
- **재생 모드**: 재생 시작 시 스냅샷을 찍고 정지하면 복원한다. 재생 중에는 저장·실행 취소가 막힌다.
- **스크립트**: 항상 `ScriptHost`가 존재하고, 저장/컴파일 때 진단을 먼저 구조화해서 보여 준 뒤 성공하면 핫 리로드한다(필드는 이름+타입으로 이전).
- **프로젝트**: 새 프로젝트/열기/저장, 씬 목록, 다른 이름으로 저장.
- **빌드 대화상자**: `.mingol` 아카이브 생성, 선택적 암호화(키 파일 생성), 선택적 단일 실행 파일(`mingol-player`에 아카이브 내장). 위협 모델 한계는 [MINGOL.md](MINGOL.md).
- 파일 대화상자는 OS 네이티브가 아니라 경로 텍스트 입력이다.

## 알려진 한계

- 내장 폰트는 ASCII 전용(한글 글리프 없음) — UI·스크립트 편집기 모두 해당.
- 단일 선택만 지원, 형제 순서 변경 없음, 소프트웨어 AA 없음.
- 기즈모는 이동/회전에 월드 축, 크기에 로컬 축을 쓴다. 회전 기즈모는 마우스 X 기반이다.
- VM 줄 단위 디버거(중단점/스텝) 없음. 편집 모드에서 컴포넌트를 만들 때 스크립트의 Awake/Enable이 실행되고, Update는 재생 중에만 실행된다.
- 실제 GPU·실제 디스플레이 환경에서는 검증하지 못했다(lavapipe + xvfb만). Windows는 미검증.
