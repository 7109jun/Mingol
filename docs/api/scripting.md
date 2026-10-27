# API: scripting (`mingol/scripting/*.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/scripting` · 타깃 `Mingol::Scripting` · 네임스페이스 `mingol::scripting`. 의존: `script`, `scene`, `core`. 언어 문법은 [../SCRIPTING-GUIDE.md](../SCRIPTING-GUIDE.md), 컴파일러/VM은 [script.md](script.md).
**씬 ↔ Mingol Script 다리.** 컴파일된 프로그램과 VM을 소유하고, 엔진 API(`Log`, `Vec3`, `GameObject`, `Script`)를 스크립트에 노출하며, 스크립트 클래스를 GameObject에 `ScriptComponent`로 붙인다.
**스레딩**: `Scene`처럼 단일 스레드. **`ScriptHost`는 자신의 컴포넌트를 가진 모든 씬보다 오래 살아야 한다**(컴포넌트가 파괴되는 동안 VM을 호출하기 때문). 호스트가 먼저 파괴되면 남은 컴포넌트는 인스턴스를 놓아 **비활성 상태**가 된다(dangling 없음).

## 스크립트 쪽 엔진 API (`engineApiSource()`)
`engineApiSource()`가 모듈 없는(module-less) 클래스들의 소스를 돌려주며, 모든 프로그램에 `<engine-api>`라는 이름으로 함께 컴파일된다.
```
Log           (native)  static info(string) / warn(string) / error(string)
Vec3                    float x, y, z; Vec3(), Vec3(x, y, z); 연산자 + - (Vec3, Vec3), * (Vec3, float)
GameObject    (native)  int index, generation  — 씬 객체의 핸들
    bool exists()   string getName()   void setName(string)   bool isActive()   void setActive(bool)
    Vec3 getPosition()   void setPosition(Vec3)   Vec3 getWorldPosition()   Vec3 getScale()   void setScale(Vec3)
    void rotate(Vec3 axis, float degrees)   void setRotation(Vec3 axis, float degrees)
    GameObject getParent()   bool same(GameObject other)   void destroy()
    static GameObject find(string name)   static GameObject create(string name)
Script                  GameObject gameObject;  onAwake/onEnable/onStart/onFixedUpdate(float)/onUpdate(float)/onLateUpdate(float)/onDisable/onDestroy — 필요한 것만 override
```
- `GameObject` 핸들은 객체가 파괴된 뒤에도 **값으로는 유효**하다: `exists()`가 false가 되고 다른 호출은 `NullReferenceError`를 던진다.
- 스크립트의 **잡히지 않은 예외는 그 컴포넌트를 비활성화**하고 씬이 로그로 남긴다.
- `Script::gameObject`는 호스트가 채운다.
예시 스크립트:
```
public class Spin extends Script {
  public float speed = 90.0;
  public void onUpdate(float dt) { gameObject.rotate(new Vec3(0.0, 1.0, 0.0), speed * dt); }
}
```
*(문법 세부는 SCRIPTING-GUIDE 참조; 위 코드는 문서용 스케치이며 `tests/scripting`의 실제 소스를 따라 작성할 것.)*

## `ScriptHost` (`script_host.hpp`)
### 구조체
- `HostDesc{ script::VMOptions vm; std::vector<std::string> plugins; }` — `plugins`는 엔진 네이티브가 바인딩된 **뒤에** 로드되고 **리로드 때 다시** 로드된다.
- `ReloadReport{ componentsMigrated, componentsDisabled(클래스가 사라졌거나 비호환으로 바뀜, 또는 생성자가 던짐), fieldsRestored, fieldsSkipped(이름이 바뀌었거나 타입이 바뀌었거나 객체형인 필드는 새 초기값 유지), messages }`.

### 메서드
```cpp
static Result<std::unique_ptr<ScriptHost>> create(const std::vector<script::SourceInput>& sources, HostDesc = {});
script::VM& vm();   const script::Program& program() const;
bool isScriptClass(const std::string& className) const;       // Script를 상속하고 public 무인자 생성자가 있는 구체 클래스
std::vector<std::string> scriptClasses() const;
Result<ScriptComponent*> addScript(scene::GameObject&, const std::string& className);
Result<ReloadReport> reload(const std::vector<script::SourceInput>& sources);
static ScriptHost* current() noexcept;    scene::Scene* currentScene() const noexcept;
class Scope { Scope(ScriptHost&, scene::Scene*); };    // RAII: 이 수명 동안 host/scene을 current로 설정(중첩 가능)
```
- `create`: 소스를 엔진 API와 함께 컴파일. 실패 시 `Error::message`에 **진단 목록**(`InvalidArgument`). 엔진 네이티브(`Log.*`, `GameObject.*`)가 바인딩되지 않았으면 `Internal`. 정적 초기화가 던져도 실패.
- `addScript`: `isScriptClass`가 거짓이거나 인스턴스를 만들지 못하면 오류(실패하면 컴포넌트를 되돌려 제거).
- **`reload`**: 재컴파일 → 새 VM → 살아 있는 모든 `ScriptComponent`를 **필드 이름과 타입으로 마이그레이션**. **컴파일 오류면 아무것도 바뀌지 않고** 진단이 반환된다. **정적 필드는 초기화되고 객체형 필드는 이전되지 않는다**(스칼라: int/float/bool/string/enum 및 그 배열만).
- `Scope`: 엔진 API 네이티브는 `current()`/`currentScene()`으로 활성 호스트·씬을 찾는다. `ScriptComponent`의 콜백은 자동으로 설정하지만, **호스트 코드가 VM을 직접 호출할 때는 직접 `Scope`를 잡아야** 한다.

## `ScriptComponent` (`script_component.hpp`, `typeName() == "Script"`)
직접 생성하지 말고 `ScriptHost::addScript`를 쓴다(클래스 이름을 먼저 검증).
| 멤버 | 설명 |
|---|---|
| `className()` | 스크립트 클래스 이름 |
| `hasInstance()`, `instance()` | 인스턴스를 만들지 못했거나 리로드에서 잃었으면 false |
| `setInt/Float/Bool/String(field, v)` / `getInt/Float/Bool/String(field)` | 스크립트 객체 필드를 이름으로 접근(**원시형과 문자열**). 없는 필드·타입 불일치는 `Result` 오류 |
| `serializeFields()` → `Result<json::Value>` | 데이터 필드(int/float/bool/string/enum 및 그 배열)를 **필드 이름 키의 JSON 객체**로. 객체형 필드와 `gameObject` 핸들은 **저장하지 않음**. NaN/Infinity면 실패 |
| `deserializeFields(json, warnings)` | `serializeFields` 결과를 적용. **알 수 없는 필드, 정적 필드, 타입 불일치는 건너뛰고 `warnings`에 기록**; 한 필드 때문에 객체가 반쯤 쓰인 상태가 되지는 않는다 |
| `call(method)` | 무인자 스크립트 메서드를 이름으로 호출(테스트/도구용) |
라이프사이클 콜백(`onAwake` … `onDestroy`)은 씬 콜백을 스크립트 메서드로 전달한다. 스크립트가 override하지 않은 콜백은 호출하지 않는다.

## 내부
`src/engine_api.hpp`의 `bindEngineNatives(VM&)`이 `Log.*`/`GameObject.*` 네이티브를 바인딩한다(공개 API 아님).

## 사용 예 (스케치)
```cpp
std::vector<script::SourceInput> src = {{"scripts/spin.mins", spinText}};
auto host = scripting::ScriptHost::create(src).value();

scene::Scene sc;                                          // host가 sc보다 오래 살도록 먼저 선언
auto* go = sc.createObject("Cube");
auto r = host->addScript(*go, "Spin");
if (!r.ok()) MINGOL_LOG_ERROR("scripting", "{}", r.error().message);
r.value()->setFloat("speed", 180.0);

{ scripting::ScriptHost::Scope scope(*host, &sc); sc.update(1.0F / 60); }   // 콜백은 자체 Scope를 잡지만 명시적으로 감싸도 무방

// 핫 리로드
auto rep = host->reload({{"scripts/spin.mins", editedText}});
if (!rep.ok()) { /* 컴파일 오류: rep.error().message에 진단, 기존 상태 유지 */ }
```

## 한계
`Script`로부터 물리·오디오·입력 API는 아직 노출되지 않는다(엔진 API는 위 목록이 전부). 스크립트 → 컴포넌트 접근은 `GameObject`의 이름/변환/계층 조작으로 제한된다. 리로드는 객체 참조 필드와 정적 상태를 보존하지 않는다.
