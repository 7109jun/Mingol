# API: editor (`mingol/editor/*.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/editor` · 타깃 `Mingol::Editor` · 네임스페이스 `mingol::editor`. 의존: `gui`, `runtime`, `sceneio`, `scripting`, `package`, `renderer`, `rhi` 등. 사용자 안내는 [../EDITOR.md](../EDITOR.md).
GUI 에디터의 **로직 전부**. 창 시스템과 무관하다: 플랫폼 이벤트를 `feed`하고, `frame()`을 호출하고, 뷰포트를 렌더하고, 반환된 `DrawList`를 그리면 된다. 실행 파일 `mingol-editor`(`apps/mingol_editor`)가 GLFW 창 + Vulkan에 연결하고, 테스트는 합성 이벤트로 구동한다.
**스레딩**: 에디터 메인 스레드 하나. (예외: `ConsoleLog`는 스레드 안전.)

## 구성
| 헤더 | 내용 |
|---|---|
| `document.hpp` | `EditorDocument` — UI 없는 편집 모델과 모든 연산(프로젝트·씬·선택·되돌리기·플레이 모드·스크립트) |
| `editor_app.hpp` | `EditorApp` — 메뉴, 도킹 레이아웃, 패널, 다이얼로그, 뷰포트 상호작용 |
| `properties.hpp` | 인스펙터용 프로퍼티 리플렉션 |
| `viewport_math.hpp` | 에디터 카메라, 화면↔월드 레이, 피킹, 기즈모 수학 |
| `code_editor.hpp` | `.mins` 코드 에디터 위젯 |
| `console.hpp` | 로그 캡처 모델 |
| `profiler.hpp` | 프레임 프로파일러 |
| `euler.hpp` | 오일러 각 변환 |

## euler.hpp
규약: **도(degree)**, 회전 = `Ry(y) · Rx(x) · Rz(z)`(Z가 먼저, 다음 X, 다음 Y), 오른손 좌표계, +Y 위.
```cpp
Quat fromEulerDegrees(Vec3);
Vec3 toEulerDegrees(Quat);      // x ∈ [-90, 90], y,z ∈ (-180, 180]. ±90° 짐벌 극에서 z = 0
```

## console.hpp
`ConsoleEntry{level, category, message, seconds(콘솔 생성 후), serial}`, `ConsoleFilter{minLevel = Trace, text(카테고리/메시지의 대소문자 무시 부분 문자열, 빈 문자열 = 전부)}`.
`ConsoleLog final : LogSink`(`mingol/core/log.hpp`): **스레드 안전** — 어떤 스레드에서든 기록 가능. 용량이 있는 환형 목록(기본 5000).
`write(record)`, `add(level, category, message)`(에디터 메시지·스크립트 진단), `clear()`, `size()`, `totalAdded()`(단조 증가; UI가 새 항목을 감지), `count(level)`(정확히 그 레벨인 현재 항목 수), `filtered(filter)`, `install(self)`(전역 `Logger`에 등록, 멱등), `static uninstall(self)`(소멸자도 해제). 스크립트 `Log.*` 출력도 같은 Logger를 거쳐 여기에 들어온다.

## profiler.hpp
단일 스레드. `Profiler::kHistory = 240`. `beginFrame()`/`endFrame()`(프레임 시간과 모든 스코프 합계를 이력에 push), `add(scope, ms)`, `setCounter(name, value)`(드로 콜·객체 수 등 임의의 프레임별 숫자), `Scoped(profiler, name)`(RAII 타이머), `frameMs()`, `fps()`(평균 프레임 시간 기준), `frameHistory()`(오래된 것부터, 최대 240), `scopes()` → `ProfilerScopeStats{name, lastMs, avgMs, maxMs}`, `scopeHistory(name)`(없으면 null), `counter(name)`, `frames()`.

## properties.hpp — 인스펙터 프로퍼티 리플렉션
컴포넌트(또는 트랜스폼)를 **이름·타입·getter/setter가 있는 평평한 프로퍼티 목록**으로 바꾼다. UI와 테스트가 같은 표를 쓴다.
**수명 주의**: `Property`는 만들어진 객체를 가리키는 raw 포인터를 가지므로 **`describe*()`를 부른 같은 프레임 안에서만**, 객체를 파괴할 수 있는 편집 전에 사용한다.
- `PropKind{Bool, Int, Float, Vec3, Color3(0..1, 색으로 편집), Color4(RGBA 0..1), Enum(Int 인덱스 → enumNames), String, Asset(assetChoices 중 이름, ""=없음), Label(읽기 전용 텍스트)}`.
- `PropValue = variant<bool, int64_t, double, Vec3, Vec4, std::string>`.
- `Property{name, kind, get(), set(const PropValue&) -> Result<void>(비어 있으면 읽기 전용), min, max(둘이 같으면 무제한), speed = 0.1, enumNames, assetChoices, tooltip, readOnly()}`.
- `PropertyEnv{const sceneio::AssetTable* assets}` — 에셋 피커용(null이면 "none"만 제공).
- `describeTransform(GameObject&)` — Position, Rotation(오일러 도), Scale(**로컬**). `describeComponent(Component&, env)` — 내장 컴포넌트를 `typeName()`으로 식별; **Script는 직렬화 가능한 필드를 나열**; 알 수 없는 타입은 타입 이름을 담은 읽기 전용 `Label` 하나. `addableComponentTypes()`("Add Component" 메뉴의 내장 타입; Script는 클래스별로 별도 추가). `toDisplayString(PropValue)`.

## viewport_math.hpp
화면 공간: 픽셀, **원점 좌상단**(Vulkan Y 뒤집기를 하는 `Mat4::perspective`와 일치).
- `Ray{origin, dir(정규화)}`.
- `EditorCamera{target, yaw, pitch(±89°로 제한), distance, fovY, nearZ, farZ}`: `position()`, `forward()`(눈 → 타깃), `right()`, `toCamera(aspect) -> renderer::Camera`, `orbit(dx, dy)`(타깃 주위), `look(dx, dy)`(눈 고정, 타깃 이동 = 플라이 시점), `pan(dx, dy, viewportHeight)`(뷰 평면에서 타깃과 눈을 함께 이동), `zoom(wheelSteps)`(돌리), `fly(localDir, seconds, speed)`(x 오른쪽, y 위, z 앞), `frame(center, radius)`(구가 시야에 맞도록).
- `screenRay(cam, pixel, size)`, `projectToScreen(cam, world, size) -> optional<Vec2>`(카메라 뒤면 nullopt).
- `rayAabb(ray, bmin, bmax, tOut)`(슬랩 테스트; 원점이 안이면 t=0), `rayLocalAabb(ray, world, localAabb, tOut)`(월드 행렬의 로컬 AABB, 비균일 스케일 지원, t는 월드 단위), `raySphere`, `closestOnAxis(ray, axisOrigin, axisDir)`(축 직선 위 레이에 가장 가까운 점의 매개변수 s; 평행이면 0), `distancePointSegment2D`.

## code_editor.hpp
`.mins`용 멀티라인 에디터 위젯: 줄 번호, 구문 강조, 진단, 자동 들여쓰기, 선택, 스크롤. 텍스트 모델은 `gui::TextEdit`.
- `SyntaxKind{Plain, Keyword, Type, Number, String, Comment, Punct}`, `SyntaxSpan{begin, end(줄 안 바이트 오프셋), kind}`.
- `highlightScript(text) -> vector<vector<SyntaxSpan>>` — 줄 단위 강조(블록 주석은 줄을 넘어 이어짐). **순수 함수**(위젯과 테스트가 공용).
- `EditorDiagnostic{line(1부터), col(1부터, 바이트), message}`.
- `CodeEditor`: `setText`(되돌리기 이력과 modified 초기화), `text()`, `modified()`, `markSaved()`, `setDiagnostics/clearDiagnostics/diagnostics`, `gotoLine(line1)`(다음 draw에서 이동·스크롤), `cursorLine()/cursorColumn()`(1부터), `edit()`, `focused()`, `draw(ctx, id, size = {0,0}) -> bool`(현재 영역의 남은 공간에 그림; size ≤ 0 = 채움; **텍스트가 바뀌었으면 true**).

## document.hpp — `EditorDocument`
편집 대상 전부와 모든 연산(UI 없음). **Runtime(씬 + 물리 + 오디오), 에셋 테이블, 스크립트 호스트, 프로젝트를 소유**한다. 되돌리기는 **`.minseen` 텍스트 스냅샷** 방식(단순하고 정확하며 메모리는 바이트 상한으로 제한). 플레이 모드 = 스냅샷 → 실행 → 복원.

### 생성
```cpp
struct EditorConfig { runtime::RuntimeDesc runtime; size_t maxUndoBytes = 64 MiB; size_t maxUndoSteps = 200; };
static Result<std::unique_ptr<EditorDocument>> create(rhi::Device*, renderer::Renderer*, const EditorConfig& = {});
```
`device`/`renderer`는 **null일 수 있다**(GPU 없음): 이때 메시/머티리얼 에셋이 없으며 그것을 참조하는 씬은 분명한 오류와 함께 로드 실패한다.
서비스 접근자: `runtime()`, `scene()`, `assets()`, `scriptHost()`(없으면 null), `console()`, `consolePtr()`, `propertyEnv()`.

### 프로젝트
`createProject(dir, name)`(`<dir>/<name>.mindev`, `assets/`, `scripts/`, `scenes/`, 빈 `scenes/main.minseen` 생성 후 열기; **.mindev가 이미 있으면 실패**), `openProject(mindev)`, `saveProject()`, `project()`(없으면 null), `projectRoot()`, `projectFile()`, `hasProject()`.
에셋: `rescanAssets()`(내장 에셋(렌더러 필요) + `assetDirs` 아래 glTF 모델·오디오로 테이블을 재구성; **개별 파일 문제는 로그로 남기고 건너뜀**), `assetList()` → `AssetEntry{path(프로젝트 상대, '/'), type, size}`(`AssetType{Model, Audio, Texture, Script, Scene, Other}`, `toString(type)`), `registerModel(path, ModelData)`(`path`가 모든 하위 에셋 이름의 접두), `instantiateModelAsset(path, parent)`(**되돌리기 가능**).

### 씬
`newScene(withDefaults = true)`(Main Camera + Directional Light 추가), `openScene(relPath)`, `saveScene()`(씬 경로 필요), `saveSceneAs(relPath)`(프로젝트의 씬 목록에 추가), `scenePath()`, `dirty()`.

### 선택
`selected()`, `select(object)`, `objectIndex(object)`(저장 순서 = 부모 먼저·깊이 우선 기준 인덱스, 없으면 −1), `objectAtIndex(i)`, `allObjects()`.

### 씬 연산 — **각각 되돌리기 한 단계**, 영향받은 객체를 반환
`createEmpty(name, parent)`, `createPrimitive(Box|Sphere|Plane, parent)`, `createCamera(parent)`, `createLight(type, parent)`, `duplicate(obj)`, `deleteObject(obj)`, `reparent(obj, newParent, keepWorld = true)`, `rename(obj, name)`, `setActive(obj, active)`, `addComponent(obj, typeName)`, `addScript(obj, className)`, `removeComponent(obj, comp)`.

### 프로퍼티 편집 / 히스토리
- `setProperty(const Property&, const PropValue&)` — **즉시 적용하되 히스토리 항목은 만들지 않는다**. 편집이 끝나면 `commit(label)`을 호출한다(GUI의 `itemEditFinished()`와 짝).
- `commit(label) -> bool`: 마지막 기록 상태와 **다르면** 새 되돌리기 단계로 기록.
- `canUndo()`, `canRedo()`, `undo()`, `redo()`, `undoLabel()`/`redoLabel()`(없으면 ""), `historySize()`, `historyBytes()`.

### 플레이 모드
`playing()`, `paused()`, `play()`(스냅샷 저장 후 실행), `stop()`(스냅샷 복원), `setPaused(bool)`, `tick(dt)`(플레이 중이고 일시정지가 아닐 때만 진행), `stepFrame(dt = 1/60)`(일시정지 중 정확히 한 프레임), `playTime()`.

### 스크립트
```cpp
struct ScriptDiagnostics { bool ok = true; std::vector<script::Diagnostic> errors; std::string failure; /* 컴파일 외 실패(정적 초기화, 파일 없음) */ };
ScriptDiagnostics compileScripts(const std::map<std::string, std::string>& overrides = {});   // overrides: 저장 안 된 텍스트(프로젝트 상대 경로 키)
std::vector<std::string> scriptFiles() const;       // 정렬된 .mins 상대 경로
```
`scriptDirs` 아래 모든 `.mins`(+overrides)를 컴파일; **성공하면 호스트를 만들거나 핫 리로드**, **실패하면 아무것도 바뀌지 않는다**.
`report(Result<T>, what)` — 실패를 콘솔에 기록하고 그대로 반환하는 표준 보고 헬퍼.

## editor_app.hpp — `EditorApp`
```cpp
struct EditorAppDesc { rhi::Device* device = nullptr; renderer::Renderer* renderer = nullptr;   // 선택: GPU 없으면 뷰포트는 오버레이만
                       EditorConfig config; std::filesystem::path playerExe; /* "단일 exe" 빌드가 내장할 런타임 실행 파일(선택) */ };
enum class GizmoMode { Translate, Rotate, Scale };
static Result<std::unique_ptr<EditorApp>> create(const EditorAppDesc&);
EditorDocument& document();  gui::Context& gui();  Profiler& profiler();  gui::DockLayout& dock();
void feed(const platform::Event&);
const gui::DrawList& frame(Vec2 displaySize, double timeSeconds, float dt);    // UI 한 프레임 구성 (플레이 중이면 시뮬레이션도 진행)
Result<std::shared_ptr<rhi::Texture>> renderViewport();   // 마지막 frame()에서의 뷰포트 패널 크기로 렌더. gui::kTextureUserBase에 등록할 텍스처 반환(GPU 없음/패널 숨김이면 null)
const std::shared_ptr<rhi::Texture>& viewportTexture() const;   uint64_t viewportTextureGeneration() const;   // 텍스처가 다시 만들어질 때마다 증가 → 호스트가 GuiRenderer::setTexture를 다시 호출
bool quitRequested() const;   void requestQuit();     // 저장 안 된 변경이 있으면 확인 다이얼로그
```
호스트 루프(스케치):
```cpp
while (window->open()) {
  for (auto& e : window->pollEvents()) app->feed(e);
  const gui::DrawList& dl = app->frame({w, h}, now, dt);
  auto vp = app->renderViewport();                                   // GPU 있을 때
  if (vp.ok() && vp.value()) guiRenderer->setTexture(gui::kTextureUserBase, vp.value());
  guiRenderer->render(dl, *backbuffer, clear, swapchain.get());
  if (app->quitRequested()) break;
}
```
테스트/자동화용 상태: `gizmoMode()/setGizmoMode`, `camera()`, `viewportRect()`, `statusMessage()`, `dialogOpen()`, `dialogErrors()`, `activeScriptPath()`, `scriptEditor(path)`, `openScript(path)`, `saveActiveScript()`, `compileScripts()`(저장 안 된 에디터 텍스트로 컴파일하고 진단을 에디터에 표시), `saveLayout()`, `resetLayout()`, `pickObject(pixel, viewportSize)`(뷰포트 픽셀 아래 가장 가까운 객체, 없으면 null),
**`trackedRect(key)`** — 마지막 프레임에 그려진 UI 요소의 화면 사각형(`"btn:Play"`, `"menu:File"`, `"item:Save Scene"`, `"node:<객체 이름>"`, `"input:<라벨>"` …). 안 보이면 `nullopt`. 합성 이벤트 UI 테스트가 클릭 좌표를 얻는 데 쓴다.
패널: Hierarchy, Inspector, Viewport, Console, Assets, Scripts, Profiler(도킹 레이아웃은 `saveLayout`으로 프로젝트에 저장). 기본 조작·단축키는 [../EDITOR.md](../EDITOR.md).

## 사용 예 (UI 없이 문서 모델만, 스케치)
```cpp
auto doc = editor::EditorDocument::create(nullptr, nullptr).value();
doc->createProject("/tmp/proj", "demo").value();
auto* go = doc->createEmpty("Thing");
doc->commit("Rename");                         // 변경 없으면 no-op(false)
doc->undo();  doc->redo();
auto props = editor::describeTransform(*go);
doc->setProperty(props[0], Vec3{1, 2, 3});     // 라이브 편집
doc->commit("Move Thing");                     // 되돌리기 한 단계로 확정
```
## 한계
되돌리기는 스냅샷 방식이라 큰 씬에서는 단계당 비용이 크다(바이트 상한으로 오래된 단계를 버림). 플레이 모드에서의 변경은 `stop()`에서 버려진다. 다중 선택, 프리팹, 에셋 임포트 설정 UI 없음. 내장 폰트에 한글 글리프가 없어 에디터 UI는 ASCII 위주([gui.md](gui.md)).
