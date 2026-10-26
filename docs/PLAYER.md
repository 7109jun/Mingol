# Mingol 플레이어 (`mingol-player`)

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

게임을 실행하는 실행 파일. 라이브러리는 `engine/player`(`Mingol::Player`), 실행 파일은 `apps/mingol_player`.

## 실행

```sh
mingol-player 프로젝트.mindev            # 개발용: 프로젝트 폴더에서 직접 실행
mingol-player 게임.mingol [--key-file f | --key-env VAR]   # 아카이브 실행(암호화 시 키 필요)
./내게임                                 # 아카이브가 내장된 단일 실행 파일(인자 없음)
옵션: --scene 이름  --headless  --frames N  --screenshot out.ppm  --width W  --height H
```
키는 명령줄 값으로 받지 않는다(프로세스 목록에 노출되므로) — 파일 또는 환경변수만 지원한다.
단일 실행 파일 만들기: 에디터 빌드 대화상자, 또는 `mingol-pack pack` 후 `mingol-pack embed mingol-player 게임.mingol -o 내게임`.

## 구조

- `ContentSource`: `openDirectorySource(.mindev)`, `openArchiveSource(.mingol, key?)`, `openEmbeddedSource(exe, key?)`. 이름은 프로젝트 상대 경로이고 `..`·절대 경로는 거부된다.
- `GameApp::create(source, {device, renderer, runtime, scene})`: project.mindev 읽기 → `scriptDirs`의 `.mins` 전부 컴파일 → `assetDirs`의 `.wav/.ogg`/`.glb/.gltf` 로드(실패한 에셋은 경고로 보고하고 계속) → 표준 에셋 이름 등록(`builtin/*`, `<경로>#mesh…`, 에디터와 동일) → 진입 씬 로드.
- `tick(dt)`, `render(target, swapchain?)`(활성 카메라 기준), `loadScene(name)`(실패 시 기존 씬 유지).
- `--headless`는 고정 프레임 시간(1/60)을 쓰므로 결과가 기계 속도와 무관하다.

## 검증된 것 / 한계

- 테스트(`tests/player`): 폴더·평문 아카이브·암호화 아카이브(키 없음/틀린 키 거부)에서 같은 게임이 실행되고 스크립트 카운터가 맞음, 깨진/없는 씬 거부 후 기존 씬 유지, GPU로 렌더한 이미지가 배경과 구별되는 픽셀을 가짐, 아카이브를 실행 파일에 내장해 실제로 프로세스를 띄워 `--headless --screenshot` 결과를 검사.
- 한계: 입력 이벤트를 스크립트에 전달하는 API는 아직 없음(창 닫기만 처리). 실제 오디오 장치는 미검증(Null 백엔드만). 실제 GPU·창 표시는 미검증. 모델 로드는 시작 시 전부 수행(스트리밍 없음). Windows 미검증.
