# 엔진 연동과 확장

문서 라이선스: CC BY 4.0. Mingol Script 문서 시리즈 5편(마지막). 이전: [4편 고급 기능](04-advanced-features.md) · 처음: [1편 소개](01-overview.md).

이 편은 스크립트를 **게임에 붙이는 방법**과 **호스트(C++)가 스크립트를 임베딩·확장하는 방법**을 다룬다. 독자는 둘로 나뉜다.

- **게임 스크립터**: 1~3절, 7~8절.
- **엔진/도구 개발자**: 4~6절, 9절(C++ 시그니처는 [../api/scripting.md](../api/scripting.md), [../api/script.md](../api/script.md)).

---

## 1. 스크립트 컴포넌트 작성

`Script`를 상속한 클래스가 곧 컴포넌트다. 조건은 **구체 클래스(abstract 아님), `Script` 파생, public 인자 없는 생성자**(생성자를 쓰지 않으면 기본 생성자가 있으므로 충족)다. 이 조건을 만족하지 않으면 `addScript`가 오류를 돌려준다.

```mins
enum Mode { Idle, Patrol }

class Hero extends Script {
  public int hp = 10;
  public float speed = 2.0;
  public string label = "hero";
  public Mode mode = Mode.Patrol;
  public int[] waypoints = [1, 2, 3];

  float elapsed = 0.0;               // not public, but still a data field: saved and shown in the inspector
  Coroutine blink;

  override void onStart() {
    Log.info(label + " starts at " + gameObject.getPosition().x);
    blink = pulse();
  }

  coroutine void pulse() {
    while (true) { yield 1; yield 2; }
  }

  override void onUpdate(float dt) {
    elapsed += dt;
    Vec3 p = gameObject.getPosition();
    gameObject.setPosition(p + new Vec3(speed * dt, 0.0, 0.0));
    blink.resume();
    if (hp <= 0) gameObject.destroy();
  }

  override void onDestroy() { Log.warn("hero destroyed after " + elapsed); }
}
```

### 생명주기 콜백

씬의 `Component` 콜백과 **같은 순서**다. 필요한 것만 `override`한다(override하지 않은 콜백은 호출 비용도 없다).

| 콜백 | 시점 |
|---|---|
| `onAwake()` | 오브젝트가 계층에서 **처음 활성**이 될 때 한 번 |
| `onEnable()` | 활성화될 때마다 |
| `onStart()` | 첫 `fixedUpdate`/`update` 직전 한 번 |
| `onFixedUpdate(float dt)` | 고정 스텝(기본 1/60초)마다, 한 프레임에 0~8번 |
| `onUpdate(float dt)` | 프레임마다 (`dt` = 프레임 시간. 0.25초로 **클램프**되고 타임 스케일이 곱해진다) |
| `onLateUpdate(float dt)` | `onUpdate` 이후 프레임마다 |
| `onDisable()` | 비활성화될 때 |
| `onDestroy()` | 파괴될 때 |

- 한 프레임 중에 만든 오브젝트/컴포넌트는 **다음 단계부터** 콜백을 받는다.
- 오브젝트 파괴(`destroy`)는 **현재 단계가 끝난 뒤** 실제로 일어난다. 콜백 도중에 `gameObject.destroy()`를 불러도 그 콜백은 끝까지 실행된다.
- **잡히지 않은 예외는 그 컴포넌트를 비활성화**하고 스택 트레이스를 로그에 남긴다. 다른 오브젝트는 영향받지 않는다.

### 필드: 저장과 인스펙터

- `Script`의 **정적이 아닌 데이터 필드**(`int`, `float`, `bool`, `string`, 열거형, 그리고 이들의 배열)는 **`.minseen`에 저장**되고 **에디터 인스펙터에 나타난다.** 접근 수정자(`public`/`private`)는 이 규칙에 영향을 주지 않는다 — 위 `Hero`의 `private` 성격의 `elapsed`도 저장된다(확인: 직렬화 결과 `{"hp": 10, "speed": 2.0, "label": "hero", "mode": 1, "waypoints": [1, 2, 3], "elapsed": 0.0}`).
- 저장되지 **않는** 것: 객체 타입 필드(다른 스크립트·`Coroutine`·`GameObject` 등), `gameObject` 핸들, 정적 필드, 함수 값.
- 열거형은 **정수 값**으로 저장된다(`mode: 1`). 인스펙터에서는 `int`/`float`/`bool`/`string` 필드를 편집할 수 있고, 배열과 열거형은 읽기 전용 텍스트로 표시된다.
- 로드할 때 파일에 있지만 클래스에 없는 필드, 정적 필드, 타입이 어긋난 필드는 **건너뛰고 경고**한다. 한 필드 때문에 객체가 반쯤 쓰인 상태가 되지는 않는다.
- 초기값은 필드 선언의 초기화식이며, 씬 파일의 값이 그것을 덮어쓴다. 씬 로드는 오브젝트를 **비활성 상태로 모두 구성한 뒤** 활성화하므로 콜백은 완성된 상태에서 시작한다(로드 도중 실패하면 만들어진 것을 전부 파괴한다).

### 엔진 API (스크립트에서 보이는 전부)

`ScriptHost`가 모든 프로그램 앞에 컴파일하는 모듈 없는 클래스들이다.

| 이름 | 내용 |
|---|---|
| `Log` (native) | `static info(string)`, `warn(string)`, `error(string)` → 에디터 콘솔 / 로그 |
| `Vec3` | `float x, y, z`; `Vec3()`, `Vec3(x, y, z)`; 연산자 `+`, `-`(이항), `*`(`Vec3 * float`) |
| `GameObject` (native 핸들) | `exists()`, `getName()/setName()`, `isActive()/setActive(bool)`, `getPosition()/setPosition(Vec3)`(**로컬**), `getWorldPosition()`, `getScale()/setScale(Vec3)`, `rotate(Vec3 axis, float degrees)`, `setRotation(Vec3 axis, float degrees)`, `getParent()`, `same(GameObject)`, `destroy()`, 정적 `find(string)`, `create(string)` |
| `Script` | `GameObject gameObject;` 와 위 콜백 8개 |

- `GameObject`는 **핸들**이다. 오브젝트가 파괴된 뒤에도 값으로 남아 있으며 `exists()`가 `false`가 되고, 그 뒤의 다른 호출은 `NullReferenceError`를 던진다. 다른 오브젝트를 오래 들고 있다면 쓰기 전에 `exists()`를 검사한다.
- `rotate`/`setRotation`은 **축-각(도)** 로 회전을 지정한다.
- **아직 없는 것**: 물리(리지드바디/레이캐스트), 오디오 재생, 입력(키보드·마우스), 렌더링/머티리얼, 다른 컴포넌트 접근, 씬 전환. 이 기능들은 C++ 컴포넌트로 존재하지만 스크립트 API로는 노출되지 않았다. 필요하면 6절의 플러그인으로 `native` 함수를 추가한다.

### 다른 오브젝트 찾기

```mins
class Follower extends Script {
  GameObject target;                 // 객체 필드: 저장되지 않으므로 onStart에서 찾는다
  float distance = 3.0;

  override void onStart() {
    target = GameObject.find("Player");
    if (target == null) Log.warn("Player not found");
  }

  override void onUpdate(float dt) {
    if (target == null || !target.exists()) return;
    Vec3 p = target.getWorldPosition();
    gameObject.setPosition(new Vec3(p.x - distance, p.y, p.z));
  }
}
```
*(위 스니펫은 이 문서용 스케치이며 컴파일은 `Follower` 그대로 확인하지 않았다. 사용한 API는 위 표와 같다.)*

---

## 2. 에디터 작업 흐름

1. **Scripts 패널**에서 `.mins`를 편집한다(줄 번호, 구문 강조, 진단 표시, 자동 들여쓰기).
2. **Ctrl+B** (Scripts → Compile & Reload): 프로젝트의 `scriptDirs` 아래 모든 `.mins`를 **저장하지 않은 편집 내용까지 포함해** 컴파일한다. 디스크 저장은 Scripts → Save Script.
3. **오류가 있으면**: `파일:줄:열` 진단이 에디터에 표시되고, **기존 실행 상태는 전혀 바뀌지 않는다.**
4. **성공하면** 핫 리로드: 이미 씬에 있는 모든 스크립트 컴포넌트가 새 프로그램으로 이전된다(4절).
5. 인스펙터 **Add Component**에서 `Script: <클래스>`로 오브젝트에 붙이고 필드 값을 편집한다.
6. **재생(Ctrl+P)**: 씬 스냅샷을 찍고 실행하며, 정지하면 스냅샷으로 복원한다. 재생 중 스크립트가 바꾼 필드 값은 정지 시 되돌아간다.

---

## 3. 진단 읽기

컴파일 오류는 한 번에 최대 100개, 위치는 `파일:줄:열`(열은 **바이트** 단위, 1부터)이다.

| 메시지(실제) | 의미 |
|---|---|
| `unknown name 'x'` | 선언되지 않은 변수/클래스 |
| `'A' has no method 'm'` | 없는 메서드 호출 |
| `cannot use 'string' as 'int' in return value` | 타입 불일치 |
| `method 'g' must return a value on every path` | 반환 누락 |
| `unreachable code` | `return`/`throw` 뒤 문장 |
| `'yield' is only allowed directly inside a coroutine method` | 코루틴 밖의 `yield` |
| `'x' is already declared in this function` | 중복 선언 (중첩 블록의 가리기 포함) |
| `declare one variable per statement` | `int a, b;` 형태 |

런타임 예외는 **타입 + 메시지 + 스택 트레이스**로 로그에 나온다(예: `mingol.IndexOutOfRangeError: index 3 is out of range for length 2`, `at Main.inner(int[]) (trace.mins:2)`).

---

## 4. 핫 리로드의 정확한 의미

`ScriptHost::reload(sources)`는 **새 프로그램을 컴파일하고 새 VM을 만든 뒤**, 살아 있는 모든 `ScriptComponent`를 **필드 이름 + 타입**을 기준으로 이전한다. 아래는 실제 실행 결과다.

이전 소스(`hero_v1.mins`):
```mins
class Hero extends Script {
  public int hp = 10;
  public string label = "hero";
  public float speed = 2.0;
  override void onUpdate(float dt) { hp += 0; }
}
```
새 소스(`hero_v2.mins`) — `speed`의 타입을 바꾸고 필드 `armor`를 추가:
```mins
class Hero extends Script {
  public int hp = 10;
  public string label = "hero";
  public bool speed = true;            // retyped: float -> bool
  public int armor = 3;                // new field
  override void onUpdate(float dt) { hp += 1; }
}
```
`hp`를 `77`, `label`을 `"veteran"`으로 바꾼 컴포넌트를 두고 `reload` 호출:
```
migrated=1 disabled=0 restored=2 skipped=1
hp=77 label=veteran
bad reload rejected; hp still 77
script compilation failed:
  hero.mins:1:38: error: expected an expression, found ';'
```
- `restored=2`: `hp`, `label`이 값을 유지했다. `skipped=1`: `speed`는 **타입이 바뀌어** 이전하지 않고 새 초기값을 가진다. 새 필드 `armor`는 선언의 초기값(`3`)이다.
- **컴파일 오류가 있으면 아무것도 바뀌지 않고** 진단만 반환된다(마지막 줄: `bad reload rejected; hp still 77`).
- 이전되지 **않는** 것: 정적 변수(**초기화**됨), 객체 타입 필드(다른 스크립트·GameObject 참조 등), 코루틴 상태. 클래스가 사라졌거나 비호환으로 바뀌었거나 새 생성자가 예외를 던지면 **그 컴포넌트만 비활성화**된다. 결과는 `ReloadReport{componentsMigrated, componentsDisabled, fieldsRestored, fieldsSkipped, messages}`.
- **호출 시점**: 실행 중인 호출 스택이 없는 곳(콜백 밖)에서만 호출한다. 에디터의 Ctrl+B가 이를 지킨다.
- 팁: 객체 참조가 리로드 뒤 사라지는 것이 문제라면 `onStart`에서 `GameObject.find(...)`로 다시 찾는 습관을 들이면 리로드와 씬 로드를 같은 코드로 처리할 수 있다. 이전 소스에서 코루틴을 돌리고 있었다면 리로드 뒤에는 다시 시작해야 한다.

---

## 5. C++ 호스트에서 임베딩

### 5.1 게임/에디터 수준: `ScriptHost`

```cpp
// 검증된 흐름: tests/scripting 과 같은 API (아래는 실제로 컴파일해 실행한 코드를 줄여 옮긴 것)
std::vector<script::SourceInput> src = {{"scripts/hero.mins", heroText}};
auto host = scripting::ScriptHost::create(src);              // 엔진 API와 함께 컴파일
if (!host.ok()) { /* host.error().message 에 "파일:줄:열" 진단 목록 */ }

scene::Scene scene;                                           // host가 scene보다 오래 살아야 한다
auto* go = scene.createObject("Player");
auto script = host.value()->addScript(*go, "Hero");           // 클래스 이름 검증 포함
script.value()->setInt("hp", 77);                             // 필드 접근 (원시형/문자열)

{
  scripting::ScriptHost::Scope scope(*host.value(), &scene);  // 엔진 API가 현재 호스트/씬을 찾는 데 필요
  scene.fixedUpdate(1.0F / 60); scene.update(1.0F / 60); scene.lateUpdate(1.0F / 60);
}
auto rep = host.value()->reload(newSources);                  // 4절의 핫 리로드
```
- `ScriptComponent`의 콜백은 자체적으로 `Scope`를 잡는다. **호스트 코드가 VM을 직접 호출할 때는 직접 `Scope`를 잡아야** `GameObject.find` 같은 엔진 API가 동작한다.
- `HostDesc{vm, plugins}`: VM 한도(`VMOptions`)와 엔진 네이티브가 바인딩된 뒤 로드할 플러그인 목록(리로드 때 다시 로드된다).
- 직렬화는 `ScriptComponent::serializeFields()` / `deserializeFields(json, warnings)` — 씬 저장/로드([../api/sceneio.md](../api/sceneio.md))와 인스펙터가 사용한다.

### 5.2 저수준: 컴파일러와 VM 직접 사용 (엔진 없이)

엔진과 무관하게 스크립트를 단독으로 실행할 수 있다(도구, 테스트, 서버).
```cpp
script::Diagnostics diags;
auto prog = script::compile({{"main.mins", text}}, diags);     // 프렐류드 자동 포함. 오류가 있으면 nullptr
if (!prog) { for (auto& d : diags.errors()) std::cerr << d.format() << "\n"; return 1; }

script::VM vm(*prog);                                           // prog가 vm보다 오래 살아야 한다
vm.bindNative("Out.print(string)", &outPrint);                 // 호스트 네이티브 바인딩
for (auto& n : vm.unboundNatives()) std::cerr << "unbound: " << n << "\n";

script::ScriptException ex;
if (!vm.initialize(ex)) { std::cerr << ex.describe(); return 1; }   // 정적 초기화. 반드시 먼저 한 번

const script::MethodSym* m = prog->findMethod("Main", "main");
script::Value result;
if (!vm.call(*m, script::makeNull(), {}, result, ex)) std::cerr << "EXC " << ex.describe() << "\n";
```
이 코드(와 `outPrint`)로 이 문서의 모든 예제를 실행해 출력을 얻었다. 실패는 C++ 예외가 아니라 `false + ScriptException`이다.

- **값 보존**: 호스트가 VM 호출 사이에 `Value`(객체)를 들고 있으려면 `VM::pin`/`Rooted`로 고정해야 한다. 고정하지 않은 값은 다음 GC 때 사라질 수 있다.
- **스레딩**: VM은 단일 스레드, 스레드당 VM 하나.
- **한도**: `VMOptions`(`maxStackValues`, `maxFrames`, `maxNativeDepth`, `gcThresholdBytes`, `maxHeapBytes`). 신뢰할 수 없는 스크립트를 돌린다면 `maxHeapBytes`와 `maxFrames`를 낮춘다. **그러나 무한 루프를 시간으로 중단하는 장치는 없다**(실행 시간 제한/워치독이 없음) — 호스트가 별도 스레드/프로세스 격리로 처리해야 한다.

---

## 6. 네이티브 플러그인 (C ABI)

스크립트가 호출할 수 있는 기능(파일, 네트워크, 물리 확장 등)은 **C로 작성한 공유 라이브러리**(`.dll`/`.so`)로 추가한다. 순수 C ABI이므로 어떤 컴파일러/언어로도 만들 수 있다.

### 6.1 스크립트 쪽 선언
```mins
public native class Out { public static native void print(string s); }
public native class Native {
  public static native int addInts(int a, int b);
  public static native int applyTwice(fn(int): int f, int x);
  public static native string greet(string name);
}
class Main {
  static int main() {
    Out.print(Native.greet("Mingol"));
    return Native.applyTwice((int v) => v * 3, Native.addInts(1, 2));
  }
}
```

### 6.2 플러그인 구현 (실제로 컴파일·로드·실행됨)
```c
/* mathplugin.c — implements the `native class Native` declared in the script. Plain C. */
#include <mingol/script/plugin_abi.h>
#include <string.h>

static const MsHostApi* g_api;

static void addInts(MsVM* vm, MsValue* args, int32_t argc, MsValue* ret) {
  (void)vm; (void)argc;
  ret->tag = MS_TAG_INT;
  ret->reserved = 0;
  ret->u.i = args[0].u.i + args[1].u.i;
}

/* Calls a script closure twice: f(f(x)). callClosure != 0 means a script exception is pending. */
static void applyTwice(MsVM* vm, MsValue* args, int32_t argc, MsValue* ret) {
  MsValue tmp;
  (void)argc;
  if (g_api->callClosure(vm, args[0], &args[1], 1, &tmp) != 0) return;
  if (g_api->callClosure(vm, args[0], &tmp, 1, ret) != 0) return;
}

static void greet(MsVM* vm, MsValue* args, int32_t argc, MsValue* ret) {
  char buf[128];
  size_t len = 0;
  const char* name = g_api->stringData(vm, args[0], &len);
  (void)argc;
  if (len > 100) { g_api->throwError(vm, "mingol.ArgumentError", "name too long"); return; }
  memcpy(buf, "hello, ", 7);
  memcpy(buf + 7, name, len);
  *ret = g_api->newString(vm, buf, len + 7);
}

MS_PLUGIN_EXPORT uint32_t mingol_plugin_abi(void) { return MS_ABI_VERSION; }

MS_PLUGIN_EXPORT int32_t mingol_plugin_init(const MsHostApi* api, const MsRegistrar* reg) {
  g_api = api;
  if (reg->registerNative(reg->ctx, "Native.addInts(int, int)", addInts) != 0) return 1;
  if (reg->registerNative(reg->ctx, "Native.applyTwice(fn(int): int, int)", applyTwice) != 0) return 1;
  if (reg->registerNative(reg->ctx, "Native.greet(string)", greet) != 0) return 1;
  return 0;
}
```

빌드(Linux): `gcc -shared -fPIC -I<engine>/engine/script/include mathplugin.c -o libmathplugin.so` — Windows(MSVC)에서는 `cl /LD`, `MS_PLUGIN_EXPORT`(`__declspec(dllexport)`)가 두 진입점을 내보낸다.

로드·실행 결과:
```
hello, Mingol
=> 27
```
(`(1 + 2) → 3`, `applyTwice(v => v * 3, 3)` = `27`)

### 6.3 규칙 (위반하면 안 되는 것)

| 규칙 | 설명 |
|---|---|
| **진입점 두 개** | `mingol_plugin_abi()`(= `MS_ABI_VERSION`, 현재 `1`)와 `mingol_plugin_init(api, registrar)`(0 = 성공). `MS_PLUGIN_EXPORT`를 앞에 붙인다 |
| **이름으로 등록** | `"클래스.메서드(타입, 타입)"` — 예: `Native.applyTwice(fn(int): int, int)`. 프로그램이 선언하지 않은 이름을 등록하면 `registerNative`가 0이 아닌 값을 돌려주고 로드가 실패한다 |
| **인자** | `args[0..argc)`. 인스턴스 메서드는 `this`가 첫째. 결과는 `*ret`에 쓰며 `void`면 건드리지 않는다. **`ref` 인자는 읽을 수 없다** |
| **GC 없음** | 네이티브 실행 중에는 GC가 돌지 않으므로 `api`로 얻은 값은 함수가 끝날 때까지 유효하다. 호출 사이에 객체를 들고 있으려면 `api->pin`/`unpin` |
| **오류 보고** | `api->throwError(vm, "mingol.ArgumentError", "...")` 후 **return**. 예외는 함수가 반환된 뒤 VM이 일으킨다 |
| **클로저 호출** | `api->callClosure` — **0이 아니면 스크립트 예외가 보류 중이므로 즉시 return** |
| **경계 규칙** | **C++ 예외나 `longjmp`가 경계를 넘으면 안 된다.** 네이티브 안에서 모두 잡는다 |
| **구조체 ABI** | `MsHostApi`, `MsValue`, `MsRegistrar` 레이아웃은 ABI의 일부다. 바꾸려면 `MS_ABI_VERSION`을 올려야 하고, 플러그인은 `api->structSize` 안의 멤버만 쓴다 |

호스트 API(`MsHostApi`): 문자열(`newString`, `stringData`), 예외(`throwError`), 네이티브 데이터+finalizer(`setNativeData/getNativeData`), 객체/필드(`newObject`, `getField`, `setField`), 배열(`newArray`, `arrayLength`, `arrayGet`, `arraySet`), 핀(`pin`, `unpin`, `pinned`), 클로저 호출(`callClosure`), 로그(`log`).

로드: `vm.loadPlugin(path)`(경로는 그대로 `dlopen`/`LoadLibrary`에 전달된다 — 확장자를 포함하고, Linux에서는 `./libx.so`처럼 슬래시가 있는 경로를 쓴다) 또는 `HostDesc::plugins`(엔진 네이티브를 바인딩한 뒤 로드되고 리로드 때 다시 로드됨).

> **현재 상태(중요)**: `.mindev`에는 `plugins` 필드가 정의되어 있고 파일 형식도 검증하지만, **기본 `mingol-editor`와 `mingol-player`는 이 항목을 읽어 플러그인을 로드하지 않는다**(소스에서 확인: 두 모듈 어디에도 `HostDesc::plugins`를 채우는 코드가 없다). 즉 플러그인은 현재 **자신의 호스트 코드에서 `HostDesc::plugins`/`loadPlugin`으로 로드**해야 하며, 에디터/플레이어에서 자동으로 쓰려면 이 연결을 추가해야 한다. 이는 알려진 공백이다. ABI 버전 불일치, 심볼 없음, 파일 없음, 선언되지 않은 이름 등록은 **오류를 반환하며 크래시하지 않는다**(테스트 `tests/script/test_plugin.cpp`).

### 6.4 보안

> **플러그인은 샌드박스 없이 프로세스 전체 권한으로 실행된다.** 로드하는 순간 임의의 네이티브 코드를 실행하는 것과 같다. 신뢰하는 출처의 플러그인만 프로젝트에 포함하고, `.mindev`의 `plugins` 항목은 기본 에디터/플레이어에서 아직 자동 로드되지 않지만, 로드를 구현하는 호스트에서는 프로젝트를 열기 전에 이 항목을 확인한다. 자세한 위협 모델은 [../SECURITY.md](../SECURITY.md).

---

## 7. 성능 가이드

참고 측정치(릴리스, CI급 2코어, 소프트웨어 렌더링 환경): **스크립트 2000개의 `onUpdate`가 프레임당 약 0.57 ms.** 실제 게임의 수치는 하드웨어와 스크립트 내용에 따라 다르다. 다음 습관이 도움이 된다.

- **프레임마다 `new`를 남발하지 않는다.** 객체(`Vec3` 포함)는 힙 객체이고 GC가 수거한다. `gameObject.setPosition(p + new Vec3(...))` 같은 코드는 매 프레임 할당을 만든다 — 많은 오브젝트에서 문제가 될 수 있으므로 값을 필드에 재사용한다.
- 비싼 조회(`GameObject.find`)는 `onStart`에서 한 번 하고 필드에 저장한다.
- 콜백이 필요 없는 스크립트는 해당 `onUpdate`를 override하지 않는다.
- 숫자 계산 커널, 대량 데이터 처리는 플러그인(C)으로 옮긴다.
- 문자열 연결(`+`)은 새 문자열을 만든다. 프레임마다 로그를 만들지 않는다.

---

## 8. 문제 해결

| 증상 | 확인할 것 |
|---|---|
| Add Component에 내 클래스가 안 보임 | `Script`를 상속했는가, `abstract`가 아닌가, public 인자 없는 생성자가 있는가, 컴파일(Ctrl+B)이 성공했는가 |
| 컴포넌트가 갑자기 멈춤 | 잡히지 않은 예외로 **비활성화**되었을 수 있다. 콘솔의 스택 트레이스를 본다 |
| `native method '...' is not bound to an implementation` | 플러그인이 로드되지 않았거나 등록한 이름이 선언과 한 글자라도 다르다 (함수 타입의 `fn(int): int` 공백 포함) |
| 리로드 뒤 값이 초기값으로 돌아감 | 필드 이름/타입을 바꿨는가(건너뜀), 객체 필드인가(이전 안 됨), 정적 변수인가(초기화됨) |
| 리로드가 거부됨 | 컴파일 오류가 하나라도 있으면 전체가 거부된다. 진단을 확인 |
| `GameObject` 호출에서 `NullReferenceError` | 파괴된 오브젝트의 핸들. `exists()`로 검사 |
| 한글 문자열 길이가 이상함 | 길이는 **바이트**다(`"한".length` = 3). 화면의 에디터 내장 폰트에는 한글 글리프가 없어 로그/UI에서 네모로 보일 수 있다 |
| 코루틴이 진행되지 않음 | 엔진이 자동으로 `resume`하지 않는다. `onUpdate`에서 `resume()`을 호출하는가 |

---

## 9. 한계와 로드맵 (현재 상태)

- 엔진 API가 작다: `Log`, `Vec3`, `GameObject` 변환·계층·활성화뿐이다. 물리·오디오·입력·렌더링·다른 컴포넌트 접근, 씬 전환이 스크립트에서 불가능하다.
- 핫 리로드는 객체 필드·정적 변수·코루틴 상태를 이전하지 않는다.
- **`.mindev`의 `plugins`를 기본 에디터/플레이어가 로드하지 않는다**(6.3절). 플러그인은 임베딩 코드에서만 로드할 수 있다.
- 디버거(중단점, 변수 보기)가 없다. 실행 시간 제한(워치독)이 없다.
- 문자열 함수·컬렉션·수학 라이브러리가 기본 제공되지 않는다(제네릭 없음). 플러그인이나 스크립트로 직접 작성한다.
- 인터프리터(JIT 없음). 컴파일러는 크래시/ASan 관점에서만 퍼징했고, 의미적 정확성의 차분 테스트는 하지 않았다.
- Windows: 컴파일러·VM·플러그인 로드는 CI(MSVC)에서 테스트되었지만 실사용 환경 검증은 아직 없다.
- 전체 목록: [../SCRIPT.md](../SCRIPT.md)의 "검증된 것 / 한계", [../KNOWN-LIMITS.md](../KNOWN-LIMITS.md).

문서 시리즈 목차: [1편 소개](01-overview.md) · [2편 언어 기초](02-language-basics.md) · [3편 타입과 객체](03-types-and-objects.md) · [4편 고급 기능](04-advanced-features.md) · **5편 엔진 연동과 확장**
