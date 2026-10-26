# GUI 시스템 (`engine/gui`, `engine/gui_gpu`)

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

에디터를 위한 **즉시 모드 GUI**. 외부 GUI 라이브러리를 쓰지 않는다.

## 프레임 프로토콜
```cpp
gui::Context ctx;
ctx.feed(event);            // platform 이벤트(마우스/키/문자/스크롤)
ctx.beginFrame(size, time, dt);
  // 위젯 호출: beginPanel/endPanel, button, checkbox, dragFloat/dragFloat3, inputText, combo,
  //            selectable, treeNode, textArea, 메뉴/팝업/모달 ...
ctx.endFrame();
const gui::DrawList& dl = ctx.drawList();   // GUI의 유일한 출력
```
- 위젯 ID = 라벨(`"텍스트##숨김ID"`) + ID 스택. 스크롤·트리 열림·텍스트 편집 상태는 Context가 ID로 보관한다.
- 내용 크기에 의존하는 레이아웃(스크롤바, 팝업 크기)은 **한 프레임 늦게** 수렴한다. 테스트는 안정될 때까지 프레임을 더 돌린다.

## 렌더링
모든 UI는 CPU `DrawList`(정점/인덱스/클립 사각형/텍스처 ID)로 환원된다.
- `gui::raster`: **소프트웨어 래스터라이저 = 기준 구현**. 단위 테스트가 픽셀을 직접 검사한다.
- `gui_gpu`의 `GuiRenderer`(Vulkan): 같은 DrawList를 그리며 GPU 테스트가 래스터라이저 결과와 비교한다.
- 텍스처 ID: 0 = 없음, 1 = 내장 폰트, ≥16 = 사용자(`kTextureUserBase` = 에디터 뷰포트). `GuiRenderer::setTexture(id, tex, ...)`. 사용자 텍스처는 `ShaderRead` 상태여야 한다.

## 도킹 (`dock.hpp`)
`DockLayout`(트리: 분할/탭) + `dockSpace()`. 레이아웃은 JSON으로 저장/복원된다(에디터: `<프로젝트>/.mingol-editor-layout.json`).

## 쌍을 맞춰야 하는 호출 (자주 틀리는 부분)
| 항상 쌍 | begin이 true일 때만 end |
|---|---|
| `beginMainMenuBar/endMainMenuBar`, `beginPanel/endPanel`, `beginChild/endChild` | `beginMenu`, `beginPopup`, `beginModal`, `beginContextMenu` |
- **`openModal(id)`는 ID 스택이 비었을 때 호출**해야 한다(ID가 스택과 함께 해시되므로). 에디터는 `modalPending_`에 요청을 모아 `drawDialogs()`에서 연다.

## 텍스트
- `TextEdit`: UTF-8 편집 모델(커서·선택·복사/붙여넣기·실행 취소), `Context::textArea`는 여러 줄 편집(스크립트 편집기가 사용).
- **내장 폰트는 ASCII 전용**이다(한글 등은 표시되지 않음). 유니코드 폰트 아틀라스는 후속 작업.
- 클립보드/커서 모양은 `Context`의 콜백(`clipboard.get/set`, `setCursorShape`)으로 창 계층과 연결한다.

## 스타일
`gui::Style`: 색상 팔레트(어두운 테마)와 간격/라운딩/`fontScale`(기본 2). 테마 파일은 없다(코드에서 수정).
