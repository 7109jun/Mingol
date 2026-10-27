# API: platform (`mingol/platform/*.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/platform` · 타깃 `Mingol::Platform` · 네임스페이스 `mingol::platform`. 의존: `core`.
**RHI를 모른다**(창 핸들만 노출). 백엔드: GLFW(실제 창) / Headless(OS 창 없음, 이벤트 주입).

## 목차
[window.hpp](#windowhpp) · [event.hpp](#eventhpp) · [input.hpp](#inputhpp--키마우스액션) · [key_list.hpp](#key_listhpp)

---

## window.hpp
### 타입
| 타입 | 설명 |
|---|---|
| `Extent2{width,height}` | 픽셀/논리 크기 |
| `WindowBackend` | `Glfw`(실제 창; GLFW로 빌드되고 디스플레이가 있어야 함) · `Headless`(주입된 이벤트만; 테스트·서버·CI) |
| `CursorMode` | `Normal`, `Hidden`, `Disabled`(캡처, 무한 이동) |
| `CursorShape` | `Arrow, IBeam, Crosshair, Hand, ResizeH, ResizeV` |
| `WindowDesc` | `title="Mingol"`, `width=1280`, `height=720`(논리 크기), `resizable`, `visible`, `decorated`, `fullscreen`(주 모니터), `backend=Glfw` |
| `NativeHandle{kind, display, window}` | `Kind`: `None/Win32/Xlib/Wayland`. `display` = `Display*`/`wl_display*`/`HINSTANCE`, `window` = X11 `Window`/`wl_surface*`/`HWND`. **스왑체인은 창보다 먼저 파괴**해야 한다 |

### `class Window`
```cpp
static Result<std::unique_ptr<Window>> create(const WindowDesc&);   // Unsupported: 백엔드 미포함 / Internal·Io: 디스플레이 없음 등
static bool backendAvailable(WindowBackend) noexcept;
```
**스레드 규칙**: `injectEvent()`를 제외한 모든 멤버는 창을 만든 스레드에서만 호출한다(GLFW는 프로세스 **메인 스레드** 필수). 창은 여러 개 가능.

| 멤버 | 설명 |
|---|---|
| `pollEvents()` | 한 프레임: 프레임 단위 입력 상태 초기화 → OS(또는 주입된 이벤트) 펌프 → 대기 중인 모든 이벤트를 **순서대로** `events()` 핸들러와 입력 상태에 전달 |
| `waitEvents(timeoutSeconds = -1)` | 이벤트가 올 때까지(또는 타임아웃, 음수=무제한) 블록한 뒤 `pollEvents()`. 다른 스레드가 `injectEvent`로 깨울 수 있다 |
| `injectEvent(Event)` | 합성 이벤트를 큐에 넣는다(**스레드 안전**), 다음 poll/wait에서 처리 |
| `shouldClose()` / `requestClose()` | 닫기 플래그. `CloseRequestedEvent`를 디스패치할 때 **자동으로 켜진다**(핸들러가 그 전에 호출됨); 코드에서 닫으려면 `requestClose()` |
| `input()` | `const InputState&` |
| `events()` | `Signal<const Event&>&` — 모든 이벤트가 디스패치될 때 호출. 핸들러는 `InputState`가 갱신되기 **전에** 실행되지만, 창이 캐시하는 `size()/framebufferSize()/focused()/iconified()` 같은 값은 **핸들러 호출 전에** 이미 새 값이다 |
| `size()` / `framebufferSize()` / `contentScale()` | 논리 크기 / 픽셀 크기(고DPI에서 다름) / 스케일 |
| `focused()`, `iconified()`, `title()`, `cursorMode()`, `fullscreen()` | 상태 |
| `setTitle`, `setSize`(요청일 뿐; 실제 변경은 `ResizedEvent`로 통지)`, setCursorMode, setCursorShape, setFullscreen` | |
| `clipboardText()` / `setClipboardText(utf8)` | |
| `nativeHandle()` | 표면 생성용 |
| `backendName()` | `"glfw"`/`"headless"` 등 |

```cpp
platform::WindowDesc wd; wd.title = "Game"; wd.backend = platform::WindowBackend::Headless;
auto w = platform::Window::create(wd);
auto conn = w.value()->events().connect([](const platform::Event& e) {
  if (std::holds_alternative<platform::CloseRequestedEvent>(e)) { /* ... */ }
});
while (!w.value()->shouldClose()) { w.value()->pollEvents(); /* 프레임 */ }
```
**좌표 주의**: GLFW의 마우스 좌표는 논리 단위다. 프레임버퍼 픽셀로 쓰려면 `framebufferSize()/size()` 비율을 곱한다(`mingol-editor`가 이렇게 한다).

---

## event.hpp
`using Event = std::variant<…>` — 구성:
| 이벤트 | 필드 |
|---|---|
| `CloseRequestedEvent` | — |
| `ResizedEvent` | `width,height`(논리) |
| `FramebufferResizedEvent` | `width,height`(**픽셀** — 스왑체인이 따라가는 값) |
| `FocusEvent` | `focused` |
| `IconifyEvent` | `iconified` |
| `KeyEvent` | `key`, `scancode`(플랫폼 의존, 한 기기에서만 안정), `action`, `mods` |
| `CharEvent` | `codepoint`(char32_t) |
| `MouseButtonEvent` | `button`, `action`(Press/Release), `mods` |
| `MouseMoveEvent` | `x,y` |
| `ScrollEvent` | `dx,dy` |
| `CursorEnterEvent` | `entered` |
| `FilesDroppedEvent` | `paths`(UTF-8) |

### `EventQueue(capacity = 4096)`
유한 FIFO. 고빈도 이벤트는 **병합**: 연속된 `MouseMove`는 마지막만, 연속된 `Resized/FramebufferResized`는 마지막만, 연속된 `Scroll`은 합산. 그래도 가득 차면 새 이벤트를 버리고 `droppedCount()`에 센다. **키/버튼/포커스 이벤트는 절대 버리지 않는다**(Release가 사라지면 입력이 눌린 채 고정되므로) — 대신 가장 오래된 버려도 되는 이벤트(이동/스크롤/문자/Enter)를 먼저 내보낸다. `push/pop/size/empty/clear`.
`void applyToInput(InputState&, const Event&)` — 입력 관련이 아닌 이벤트는 무시.

---

## input.hpp — 키·마우스·액션
- `enum class Key : uint16_t` — `Unknown`, `Space`, `A…Z`, `Num0…9`, `F1…F12`, 방향키, 편집키, 키패드, 수정키 등(목록은 `key_list.hpp`), `Count`. `keyName(Key)`("A","Space","LeftShift", 미지정 "Unknown")/`keyFromName(name)`(대소문자 구분, 없으면 `nullopt`) — **스크립트·직렬화 이름은 안정적**.
- `enum class MouseButton { Left, Right, Middle, Button4, Button5, Count }`, `mouseButtonName`.
- `enum class Modifiers : uint8_t { None, Shift, Control, Alt, Super, CapsLock, NumLock }` + `MINGOL_ENUM_FLAGS`(`hasFlag(mods, Modifiers::Control)`).
- `enum class KeyAction { Release, Press, Repeat }`.
- `void appendUtf8(std::string&, char32_t)` — 서로게이트·U+10FFFF 초과는 U+FFFD로 대체(항상 유효한 UTF-8).

### `class InputState`
`beginFrame()`(프레임당 1회, 이벤트 펌프 전)과 `onKey/onMouseButton/onMouseMove/onScroll/onChar/onFocusLost`로 갱신된다(`Window`가 해 주지만 테스트·리플레이·에디터 입력 라우팅이 같은 상태 기계를 직접 구동하도록 공개).
| 질의 | 의미 |
|---|---|
| `keyDown(k)` / `mouseDown(b)` | 지금 눌려 있음 |
| `keyPressed(k)` / `mousePressed(b)` | **이번 프레임에** 눌림(키 반복은 제외) |
| `keyReleased(k)` / `mouseReleased(b)` | 이번 프레임에 뗌 |
| `mousePosition()` | 창 좌표, 원점 좌상단 |
| `mouseDelta()` | `beginFrame` 이후 이동량 |
| `scroll()` | `beginFrame` 이후 누적 |
| `modifiers()` | 현재 수정키 |
| `text()` | `beginFrame` 이후 입력된 UTF-8 텍스트(IME/배열 반영 — 키 입력과 다름) |
`onFocusLost()`는 눌려 있던 모든 키·버튼을 해제한다(다른 창으로 Release가 가서 눌림 고착되는 것을 방지; 이 해제는 "이번 프레임에 뗌"으로 집계).

### `class ActionMap`
이름 붙은 액션 ↔ 물리 입력. `bind(action, Key)`, `bind(action, MouseButton)`(여러 개 바인딩 가능), `unbindAll(action)`, `clear()`, `down/pressed/released(const InputState&, action)`, `axis(in, negative, positive)`(−1..1, 둘 다 눌리면 0).
```cpp
platform::ActionMap map;
map.bind("Left", platform::Key::A); map.bind("Left", platform::Key::Left);
map.bind("Right", platform::Key::D);
float x = map.axis(win.input(), "Left", "Right");
```

## key_list.hpp
`MINGOL_KEY_LIST(MKEY)` X-매크로 — `MKEY(식별자, GLFW_KEY_ 접미사, "스크립트에 보이는 이름")`. `Key` 열거형, 이름 표, GLFW 변환 표의 **단일 출처**다. 키를 추가하려면 여기에 한 줄만 추가한다.

## 한계
- Wayland 네이티브는 미지원(null 디스플레이면 `Unsupported`), 게임패드 없음, 고DPI·다중 모니터·Win32 창은 실제로 검증하지 못했다([../KNOWN-LIMITS.md](../KNOWN-LIMITS.md)).
- 전체화면은 주 모니터의 현재 비디오 모드만.
