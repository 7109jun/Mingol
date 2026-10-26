# 처음 시작하기 (빌드 → 게임 제작 → 배포)

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

## 1. 빌드
[BUILD.md](BUILD.md)의 패키지를 설치하고:
```sh
cmake --preset linux-release && cmake --build --preset linux-release
```

## 2. 에디터 실행
```sh
build/linux-release/apps/mingol_editor/mingol-editor --player build/linux-release/apps/mingol_player/mingol-player
```
(`--player`는 단일 EXE 빌드에 쓰인다. 디스플레이가 없으면 `--headless`.)

## 3. 프로젝트 만들기
- File → New Project...: 폴더와 이름을 입력(경로는 텍스트 입력). `<이름>.mindev`, `scenes/main.minseen`(카메라+조명 포함), `scripts/`, `assets/`가 생긴다.
- 기존 프로젝트는 File → Open Project...에 `.mindev` 경로 입력.

## 4. 씬 꾸미기
1. 상단 GameObject 메뉴 → Create Empty / 3D(Box·Sphere·Plane) / Camera / Light(Directional·Point·Spot). Hierarchy 빈 곳 우클릭으로도 Create Empty.
2. 뷰포트: 오른쪽 버튼 누른 채 마우스+WASD = 시점/비행, Alt+왼쪽 드래그 = 오빗, 가운데 드래그 = 팬, 휠 = 줌, **F** = 선택 대상으로 이동. 클릭하면 선택.
3. **W/E/R**로 이동/회전/크기 기즈모(드래그 중 Esc = 취소). 값은 Inspector에서 직접 입력해도 된다.
4. 모델은 `assets/`에 `.glb`를 넣고 Assets 패널에서 인스턴스화.
5. Ctrl+Z / Ctrl+Y 실행 취소·다시. Ctrl+S 저장.

## 5. 스크립트 붙이기
Scripts 메뉴 → New Script...로 새 스크립트를 만들고([SCRIPTING-GUIDE.md](SCRIPTING-GUIDE.md)), 객체 Inspector에서 Add Component 버튼 → `Script: <클래스>` 항목 선택. **Ctrl+P**로 재생하면 `onUpdate`가 돈다. 재생 중 변경은 정지하면 되돌아간다.

## 6. 배포
- 에디터: **File → Build...** 대화상자. 출력 경로, (선택) 암호화 키 파일 생성, (선택) 단일 실행 파일 만들기.
- 명령줄:
```sh
mingol-pack genkey > key.txt                          # 64자리 hex 키
mingol-pack pack game.mindev -o game.mingol --key-file key.txt
mingol-pack embed mingol-player game.mingol -o mygame # 단일 EXE (인자 없이 실행하면 내장 게임 실행)
mingol-player game.mingol --key-file key.txt          # 아카이브 직접 실행
mingol-player game.mindev                             # 개발 중 폴더 실행
```
암호화는 캐주얼 추출 방지용이다 → [SECURITY.md](SECURITY.md). `--key-file` 대신 `--key-env 변수명`도 가능(키를 명령줄 값으로 받지 않음).

## 7. 문제 해결
| 증상 | 확인 |
|---|---|
| "no usable Vulkan device" | Vulkan 드라이버 설치(GPU 없으면 `mesa-vulkan-drivers`) |
| "cannot create a window" | 디스플레이가 없다 → `--headless` 또는 xvfb |
| 한글이 안 보임 | 내장 폰트가 ASCII 전용(알려진 한계) |
| 스크립트 오류 | Scripts 탭/콘솔의 `파일:줄:열` 진단 확인 |
| 플레이어가 에셋을 못 찾음 | 프로젝트의 `assetDirs`/`scriptDirs` 아래에 있어야 패키징된다 |
