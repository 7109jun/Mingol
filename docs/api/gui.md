# API: gui (`mingol/gui/*.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/gui` · 타깃 **`Mingol::Gui`**(CPU: 컨텍스트·위젯·도킹·소프트웨어 래스터라이저; 의존 `core`, `platform`)와 **`Mingol::GuiGpu`**(`gui_renderer.hpp` 전용; 의존 `Gui`, `rhi`, `shader`) · 네임스페이스 `mingol::gui`. 개요는 [../GUI.md](../GUI.md).
**즉시 모드(immediate-mode) GUI**. 한 프레임은 `DrawList`(CPU 2D 드로 리스트)로 환원되고, 백엔드(소프트웨어 래스터라이저 / RHI)는 이 구조만 소비한다 — 위젯 코드는 그래픽 API를 건드리지 않는다.
**스레딩**: `Context` 하나당 윈도우 하나, 단일 스레드.

## 목차
[프레임 프로토콜](#프레임-프로토콜) · [Style/ItemState](#style--itemstate) · [Context](#context) · [DrawList](#drawh--drawlist) · [text_edit](#text_edithpp) · [font](#fonthpp) · [dock](#dockhpp) · [raster](#rasterhpp) · [gui_renderer](#gui_rendererhpp-guigpu) · [사용 예](#사용-예)

## 프레임 프로토콜
```
ctx.feed(event)…            // 프레임 사이에 누적 (platform::Event)
ctx.beginFrame(displaySize, timeSeconds);
  … 위젯 호출 …
ctx.endFrame();
const DrawList& dl = ctx.drawList();     // 렌더러에 전달
```
- 위젯은 **라벨(`"텍스트##숨김id"`) + ID 스택**으로 식별된다. 프레임을 넘어 유지돼야 하는 상태(스크롤, 트리 열림, 텍스트 편집 상태, 팝업 크기)는 **ID로 Context 내부에 보관**된다.
- 콘텐츠 크기에 의존하는 레이아웃(스크롤바, 팝업 크기)은 **한 프레임 지연**된다.
- `errors()`: 프레임을 만드는 중 발견된 문제(begin/end 불균형 등). **비어 있으면 정상**.

## Style / ItemState
- `Style`: 색(`bg panelBg panelHeader text textDim accent accentDim button buttonHover buttonActive frame frameHover border selection popupBg modalDim scrollTrack scrollThumb scrollThumbHover error warning success`), `fontScale = 2`, `spacing = 4`, `framePad = 3`, `indent = 14`, `scrollbar = 10`, `rounding = 3`, `regionPad = 4`, `labelFraction = 0.4`(라벨 열 폭 비율). `ctx.style`을 직접 수정한다.
- `ItemState{hovered, pressed(이 프레임에 눌림), held, clicked(항목 위에서 눌렀다 뗌), doubleClicked, rightClicked}`.
- `TreeFlags{None, Leaf, DefaultOpen, Selected}` (`|`, `hasFlag`).
- `using ID = uint32_t`.

## Context
### 프레임/입력
| 멤버 | 설명 |
|---|---|
| `feed(const platform::Event&)` | 입력 이벤트 주입 |
| `beginFrame(Vec2 displaySize, double timeSeconds)` / `endFrame()` | |
| `drawList()`, `errors()`, `displaySize()`, `time()`, `deltaTime()` | |
| `mousePos/mouseDelta/mouseDown(btn)/mouseClicked(btn)/mouseReleased(btn)/scrollY()` | beginFrame~endFrame 사이에서만 유효 |
| `keyPressed(Key, allowRepeat=false)`, `modifiers()`, `ctrl()`, `shift()` | |
| **`wantTextInput()`** | 텍스트 필드가 키보드 포커스를 가지면 true — **앱은 이때 키를 단축키로 처리하면 안 된다** |
| `anyItemActive()`, `anyPopupOpen()`, `clearFocus()` | |
| `clipboard`(선택), `setCursorShape`(선택 콜백) | 클립보드가 없으면 GUI 내부 버퍼 사용 |

### ID / 레이아웃
`pushID(string_view | int64)`, `popID()`, `getID(label)`, `sameLine(spacing=-1)`, `setNextWidth(w)`(다음 위젯 한 번), `spacing()`, `separator()`, `indent(amount)`/`unindent`, `availableWidth()/availableHeight()`, `itemHeight()`, `lineHeight()`, `textWidth(utf8)`, `lastItemRect()`, `lastItem()`, `itemEditFinished()`(드래그가 끝났거나 Enter/포커스 상실로 텍스트가 확정된 프레임 — **되돌리기 단계를 묶는 데** 사용).

### 영역
`beginPanel(name, bounds, scroll=true, background=true)` / `endPanel()`(최상위 패널, **항상 `endPanel` 호출**), `beginChild(id, size, border=false)` / `endChild()`(size ≤ 0 = 나머지 채움), `scrollOffset()`, `scrollMax()`, `setScrollOffset(y)`, `scrolledToBottom()`(콘솔 "맨 아래 붙이기").

### 위젯
`label`, `labelColored`, `labelDim`, `button(label, size) -> bool`, `smallButton`, `checkbox(label, bool&)`, `radio(label, int&, value)`, `selectable(label, selected, size)`, `progressBar(fraction, overlay, size)`, `image(textureId, size, uv0, uv1)`, `interactiveArea(id, size) -> ItemState`(3D 뷰포트), `dragFloat(label, float&, speed, min, max)`, `dragInt`, `dragFloat3(label, float[3], speed)`, `inputText(label, std::string&, bool* entered)`, `combo(label, int& index, span<const string>)`, `colorEdit(label, float rgba[4])`, `treeNode(label, flags)` + **`treePop()`**(true를 반환했을 때만), `collapsingHeader(label, defaultOpen)`.
반환값 `bool`은 일반적으로 "이번 프레임에 값이 바뀜/눌림"이다.

### 탭
`beginTabBar(id)`, `tabItem(label) -> bool`(이 탭이 선택된 것일 때 true), `endTabBar()`.

### 팝업 / 메뉴 / 모달
`openPopup(id, pos)`, `openPopupAtLastItem(id)`, `closeCurrentPopup()`, `isPopupOpen(id)`, `beginPopup(id)`/`endPopup()`(**true일 때만 endPopup**), `beginContextMenu(id)`(마지막 항목 위 우클릭 시 열고 begin), `beginMainMenuBar(bounds)`/`endMainMenuBar()`, `beginMenu(label, enabled)`/`endMenu()`, `menuItem(label, shortcut, selected, enabled) -> bool`(눌림), `menuSeparator()`, `openModal(id)`, `beginModal(id, title, size)`/`endModal()`, `closeModal()`, `isModalOpen()`, `tooltip(text)`(마지막 항목에 대해 약 0.4초 호버 후).

### 드래그 앤 드롭 (문자열 페이로드)
`dragSource(type, payload, preview) -> bool`, `dropTarget(type, std::string* payload) -> bool`(드롭이 일어난 프레임), `isDragging()`, `dragType()`, `dragPayload()`, `overlay()`(모든 것 위에 그려지는 `DrawList&`).

### 커스텀 위젯 도구
`draw()`(현재 영역 DrawList), `clipRect()`, `allocate(size)`(사각형 예약 + 마지막 항목으로 등록), `itemBehavior(rect, id)`, `fontScale()`, `hasFocus(id)`, `setFocus(id)`, `setHoverCursor(shape)`,
`textArea(frame, id, TextEdit&, byteAtMouse, onKey = {}, pageLines = 10) -> TextAreaResult{item, focused, changed}` — 멀티라인 텍스트 위젯(스크립트 에디터)용. 프레임을 항목으로 등록하고, 눌렀을 때 포커스, 마우스로 선택(더블클릭 = 단어), 포커스 중 문자 입력과 편집 키 적용. `onKey`는 문자가 아닌 모든 키를 먼저 보고 **true를 반환하면 소비**(자동 들여쓰기, Tab 처리 등).
`formatFloat(float)` — 소수 최대 3자리, 뒤의 0 제거(`"1"`, `"0.25"`).

## draw.hpp — `DrawList`
- `Color{r,g,b,a}`(8비트, **비곱셈 알파**; `withAlpha`, `mix(other, t)`), `Rect{x0,y0,x1,y1}`(`[x0,x1)×[y0,y1)`; `width/height/empty/contains/intersect/expanded`), `Vertex{pos, uv, color}`.
- 텍스처 id: `kTextureNone = 0`(단색), `kTextureFont = 1`(내장 폰트 아틀라스), **`kTextureUserBase = 16` 이상 = 사용자 텍스처**(뷰포트 등).
- `DrawCmd{clip, texture, firstIndex, indexCount}`.
- `DrawList`: `clear()`, `empty()`, `append(other)`(뒤에 붙임, 클립 스택은 병합 안 됨), `vertices()/indices()/commands()`, `pushClip(rect)`(현재 클립과 교집합)/`popClip()`/`currentClip()`, `addRect`, `addRectRounded(r, c, radius)`, `addRectOutline(r, c, thickness)`, `addRectGradientV`, `addLine`, `addTriangle`, `addCircle(center, radius, c, segments=24)`, `addImage(texture, rect, uv0, uv1, tint)`, `addText(pos, utf8, c, scale = 2)`.

## text_edit.hpp
- `Clipboard{std::function<std::string()> get; std::function<void(const std::string&)> set;}`.
- `TextEdit(bool multiline = false)`: 순수 텍스트 편집 모델(UTF-8, **바이트 오프셋은 항상 코드포인트 경계**). 그리기 없음.
  - `text()`, `setText(utf8)`(잘못된 UTF-8은 U+FFFD, CR 제거, 커서는 끝으로, **되돌리기 이력 초기화**), `cursor()`, `anchor()`, `hasSelection()`, `selection()`, `selectedText()`, `setCursor(byte, extend)`, `selectAll()`, `selectWordAt`, `selectLineAt`, `insert(utf8)`(선택 대체; 단일 행 모드에서 개행 제거), `key(Key, Modifiers, Clipboard, pageLines)`(방향키, Home/End, PageUp/Down, Backspace/Delete, Enter, Tab, Ctrl+A/C/X/V/Z/Y; **단일 행 모드의 Enter는 소비하지 않음**), `undo()`, `redo()`, `undoDepth()`.
  - 행: `lineCount()`, `lineStart/lineEnd(line)`, `lineOf(byte)`, `columnOf(byte)`(코드포인트), `offsetAt(line, col)`(둘 다 클램프), `revision()`(텍스트가 바뀔 때마다 증가), `static isBoundary`.
- `sanitizeUtf8(s)`: 잘못된 바이트마다 U+FFFD, `'\r'` 제거.

## font.hpp
내장 **5×8 비트맵 폰트(ASCII 32..126)**. 셀 6×9(글리프 + 1px 간격), 아틀라스 96×72 8비트 커버리지. **ASCII 밖의 코드포인트는 속이 빈 상자로 그려진다 — 한글/CJK 글리프가 없다(알려진 한계)**. `atlas()`, `cellFor(cp)`, `decode(utf8)`(잘못된 바이트마다 U+FFFD), `advance(scale)`, `lineHeight(scale)`, 상수 `kGlyphW/H`, `kCellW/H`, `kFirst/kLast`, `kAtlasCols/Rows/W/H`.

## dock.hpp
이진 분할 트리 + 탭 패널. 탭을 다른 리프에 끌어 놓으면 병합, 가장자리에 놓으면 분할. 스플리터 드래그 가능, **레이아웃은 JSON으로 왕복**한다.
- `DockZone{Center, Left, Right, Top, Bottom}`.
- `DockLayout`: `Node{leaf, sideBySide, ratio, a, b, tabs, active}`. 빌드: `addPanel(name)`, `dockRelative(panel, anchor, zone, ratio = 0.3) -> Result<void>`(앵커가 없거나 패널이 이미 있으면 실패), `removePanel(name)`(빈 리프는 접힘), `contains`, `panels()`, `activate(name)`, `movePanel(panel, target, zone)`(변화 없으면 false). 직렬화: `toJson()`, `static fromJson(json) -> Result<DockLayout>`. `valid()` — 구조 불변식(모든 분할은 자식 2개, 리프는 탭 ≥ 1(빈 루트 제외), 탭 이름 유일, 비율 ∈ (0,1)); 로드 후 검증에 사용.
- `DockSlot{name, content(탭 바 아래 본문 영역), active}`; `std::vector<DockSlot> dockSpace(Context&, DockLayout&, const Rect& area)` — 도크 크롬(탭 바, 스플리터, 드롭 존)을 그리고 사용자가 레이아웃을 재배치하게 한다. **각 리프의 활성 패널 영역**을 돌려주며, 패널을 제출하기 **전에** 호출한다.

## raster.hpp
`gui::Image{width, height, rgba, at(x,y), set(x,y,c)}`(RGBA8, **0행이 위**), `UserTextureLookup = function<const Image*(id)>`(없으면 해당 삼각형 건너뜀), `rasterize(list, Image& target, users = {})` — **헤드리스 렌더링(테스트·스크린샷·CI)과 GPU 백엔드의 기준 구현**. `writePpm(img, path)`(P6, 알파 버림). *(주의: `assets::Image`와 다른 타입.)*

## gui_renderer.hpp (`Mingol::GuiGpu`)
```cpp
struct GuiRendererDesc { rhi::Format targetFormat = BGRA8Unorm; uint32_t framesInFlight = 2; };   // targetFormat은 render()에 넘기는 텍스처와 일치해야 함
static Result<std::unique_ptr<GuiRenderer>> create(rhi::Device&, const GuiRendererDesc& = {});
Result<void> setTexture(uint32_t id, std::shared_ptr<rhi::Texture>, bool linear = true);   // id ≥ kTextureUserBase; null이면 제거; linear=쌍선형(뷰포트), 아니면 최근접
Result<uint64_t> render(const DrawList&, rhi::Texture& target, const float* clearColor = nullptr, rhi::Swapchain* presenting = nullptr);
uint64_t skippedCommands() const;       // 등록되지 않은 텍스처 id 때문에 건너뛴 커맨드 수(진단)
```
- `DrawList`의 색은 **최종 표시값**이므로 UNORM 타깃이 소프트웨어 래스터라이저와 일치한다; **sRGB 타깃은 셰이더에서 변환**해 지원.
- `clearColor`가 있으면 그 색으로 지우고, null이면 기존 내용 위에 그린다. `presenting`이 있으면 프리젠트용으로 전환하고 스왑체인을 present. 반환값 = 제출 시리얼.

## 사용 예 (스케치)
```cpp
gui::Context ctx;
// 매 프레임:
for (auto& e : events) ctx.feed(e);
ctx.beginFrame({1280, 720}, timeSeconds);
if (ctx.beginPanel("Inspector", {900, 0, 1280, 720})) {
  ctx.label("Position");
  float p[3] = {0, 1, 2};
  if (ctx.dragFloat3("pos", p)) { /* 변경됨 */ }
  if (ctx.itemEditFinished()) { /* 되돌리기 단계 확정 */ }
  if (ctx.button("Reset")) { /* ... */ }
}
ctx.endPanel();                                   // beginPanel이 false여도 호출
ctx.endFrame();
if (!ctx.errors().empty()) MINGOL_LOG_WARN("gui", "{}", ctx.errors().front());

gui::Image img(1280, 720);                        // 헤드리스
gui::rasterize(ctx.drawList(), img);  gui::writePpm(img, "ui.ppm");
// GPU:  guiRenderer->render(ctx.drawList(), *target, clear, swapchain.get());
```

## 한계
내장 폰트에 한글/CJK/기타 비ASCII 글리프가 없다. 접근성(스크린 리더)·IME 조합 입력 미지원. 레이아웃은 단순한 수직 흐름 + `sameLine`(그리드/플렉스 없음). 고DPI 스케일은 정수 `fontScale`로만.
