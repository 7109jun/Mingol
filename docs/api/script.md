# API: script (`mingol/script/*`)

문서 라이선스: CC BY 4.0. 모듈: `engine/script` · 타깃 `Mingol::Script` · 네임스페이스 `mingol::script`. 의존: `core`.
**Mingol Script(`.mins`) 컴파일러 + 바이트코드 VM + 네이티브 플러그인 C ABI.** 엔진·씬을 모르는 독립 모듈이다(씬 연결은 [scripting.md](scripting.md)). 언어 자체는 [../SCRIPTING-GUIDE.md](../SCRIPTING-GUIDE.md).

## 목차
[파이프라인](#파이프라인) · [source.hpp](#sourcehpp-진단) · [compile.hpp](#compilehpp) · [lexer/parser](#lexer--parser) · [program.hpp](#programhpp) · [symbols/types](#symbolstypes) · [vm.hpp](#vmhpp) · [plugin_abi.h](#plugin_abih-네이티브-플러그인-c-abi) · [사용 예](#사용-예)

## 파이프라인
`lex` → `parse`(AST) → 검사(이름·타입 해석, `checker`) → 코드 생성(`codegen`) → 불변 `Program`(바이트코드 + 메타데이터). `compile()` 한 번 호출로 전 과정을 수행한다. 내부 헤더(`src/*.hpp`: checker, codegen, vm_impl)는 공개 API가 아니다.

## source.hpp (진단)
- `SourceLoc{file(파일 테이블 인덱스), line(1부터), col(1부터, **바이트** 단위)}`.
- `Diagnostic{file, line, col, message}`; `format()` → `파일:줄:열: error: 메시지`.
- `Diagnostics`: 첫 오류에서 멈추지 않고 **수집**한다. 병적 입력 방어로 **최대 `kMaxErrors` = 100개**(초과 시 `truncated()`).
  `setFiles`, `addFile`, `error(loc, msg)`, `hasErrors()`, `truncated()`, `errors()`, `fileName(i)`.

## compile.hpp
```cpp
struct SourceInput { std::string name /*진단에 표시(보통 경로)*/; std::string text; };
const char* preludeSource() noexcept;     // 모듈 `mingol`: Object, Exception과 런타임 오류 클래스, Coroutine, 리플렉션(Type/FieldInfo/MethodInfo), 박스 클래스
std::unique_ptr<Program> compile(const std::vector<SourceInput>& sources, Diagnostics& diags);
```
- 모든 프로그램에 **프렐류드가 함께 컴파일**된다.
- 오류가 있으면 **`nullptr`** 을 반환하고 `diags`에 보고한다. 성공했는지는 반환값(또는 `!diags.hasErrors()`)으로 판단.

## lexer / parser
```cpp
std::vector<Token> lex(std::string_view source, std::uint32_t fileIndex, Diagnostics&);
std::unique_ptr<CompilationUnit> parse(std::string_view source, std::uint32_t fileIndex, Diagnostics&);
```
- `lex`: 예외를 던지지 않고 `source` 밖을 읽지 않는다. 어휘 오류는 보고 후 해당 바이트를 건너뛰며 계속한다. 결과는 **항상 `Tok::End`로 끝난다**.
- `parse`: 구문 오류에서 문장·멤버 경계로 복구해 한 번에 여러 오류를 보고한다. 반환 유닛은 항상 non-null이지만 **`diags.hasErrors()`이면 코드 생성에 쓰면 안 된다**. 중첩 깊이 **200 초과는 오류**(컴파일러 자신의 스택 보호).
- 토큰(`Tok`): 키워드(`module import class interface enum extends implements static public private protected final abstract override native new null this super true false if else while do for switch case default break continue return throw try catch finally yield coroutine var is as typeof operator fn ref int float bool string void`), 연산자(`=>` 포함), 리터럴. `Token{kind, loc, text, ival, fval}`; `tokName(Tok)`.

## program.hpp
컴파일 결과 `Program`(복사 불가, **불변**). 여러 `VM`이 하나의 `Program`을 공유해도 된다.
| 멤버 | 설명 |
|---|---|
| `types` | `TypeTable` (아래) |
| `units`, `files` | AST 유닛, 파일 이름 |
| `classes` | `vector<unique_ptr<ClassSym>>` (인덱스 = `ClassSym::id`) |
| `funcs` | `vector<FuncProto>` (인덱스 = `MethodSym::funcId`) |
| `constants` | `Constant{kind(Int/Float/String), i, f, s}` |
| `staticCount`, `staticFields`, `staticInitFuncs` | 정적 필드와 로드 시 실행 순서 |
| `interfaceKeyCount`, `typeDescs` | 인터페이스 디스패치 키 수, `typeof(...)`가 참조하는 타입 |
| `objectClass`, `exceptionClass`, `coroutineClass`, `typeClass` | 프렐류드의 잘 알려진 클래스(프렐류드와 컴파일되면 null 아님) |
| `findClass(qualifiedName)` | 클래스 조회(없으면 null) |
| `findMethod(qualifiedClass, name)` | 메서드 조회(`const MethodSym*`) |
`disassemble(const Program&, int funcId)` — 바이트코드 목록 문자열(테스트/디버깅용).

`Instr`는 **8바이트 고정**(`op`, `pad`, `a: u16`, `b: i32`; `static_assert`). `Op` 목록은 `program.hpp`의 enum이 정본이며 `opName(op)`로 이름을 얻는다. 분류: 상수/스택, 로컬·캡처, 필드·정적, 객체·배열·문자열, 산술/비교(정수·실수·문자열·참조), 변환(`I2F F2I ToStr EnumFromInt`), 제어 흐름과 호출(`Call CallVirtual CallInterface CallClosure`), 클로저(`MakeClosure MakeBound…`), 타입(`IsType CastType`), `Throw`, `ref`/포인터(`MakeRef*`, `LoadRef/StoreRef`), 코루틴(`Yield StartCoroutine*`), 타입 객체(`TypeOfStatic/Value`).
`FuncProto`: `name`(예: `Player.update(float)`, 람다는 `…$lambda0`), `numParams`(this 포함), `numLocals`, `numCaptures`, `isCoroutine/isNative/isLambda/hasThis`, `nativeName`, `code`, `handlers`(`Handler{startPc,endPc,handlerPc,classId(-1=전부),slot}`), `lines`(`LineEntry{pc,line}`), `file`, `method`, `maxStack`.
`ElemKind{Int=0, Float, Bool, String, Ref}`.

## symbols/types
- `Type`은 `TypeTable`에서 **인턴**되므로 포인터로 비교한다. `TypeKind{Void, Int, Float, Bool, String, Null, Class, Interface, Enum, Array, Pointer, Func}`. `TypeTable`: `voidType()…nullType()`, `arrayOf`, `pointerTo`, `func(params, ret)`, `classType(ClassSym*)`.
- 선언 심볼은 컴파일된 `Program` 안에 살아 있고 **리플렉션과 네이티브 바인딩의 런타임 메타데이터**를 겸한다: `ClassSym`(`qualifiedName`, `moduleName`, `kind`, `base`, `interfaces`, `fields`, `methods`, `enumConsts`, `instanceFieldCount`, `vtable`, `itable`, `isSubclassOf`, `implements`, `isAssignableTo`, `findField`…), `FieldSym`(`isStatic`, `access`, `index`), `MethodSym`(`mkind{Method,Constructor,Operator}`, `mods`, `params`, `ret`, `funcId`, `vtableSlot`, `signature()` → `"name(int, ref float)"`, `isStatic()`, `isAbstract()`), `EnumConstSym`, `VarSym`(`boxed()`: 클로저 간 공유되거나 주소가 새는 변수는 힙 박스에 산다), `FuncInfo`.
- `scripting` 모듈은 이 메타데이터로 `onUpdate` 같은 메서드와 필드를 찾는다.

## vm.hpp
**스레딩**: VM은 단일 스레드. **스레드당 VM 하나**, `Value`를 VM 사이에 공유하지 않는다.
**메모리**: 정밀 mark-sweep GC. 수집은 **VM 세이프포인트(호출, 뒤로 가는 점프, 할당 지점)** 또는 명시적 `collectGarbage()`에서만 일어나고 **네이티브 함수 실행 중에는 절대 일어나지 않는다**. 호스트가 VM 호출을 가로질러 `Value`를 들고 있으려면 **고정(pin)** 해야 한다(`VM::pin` 또는 `Rooted`).

### 값
`using Value = MsValue` — 16바이트 `{tag(NULL/INT/FLOAT/BOOL/OBJECT), reserved, union{i, f, p}}`.
`makeNull()`, `makeInt(i64)`, `makeFloat(double)`, `makeBool(bool)`, `isNull(v)`, `isObject(v)`.

### 구조체
- `ScriptException{type("mingol.NullReferenceError" 등), message, stackTrace("  at Class.method(sig) (file:line)", 안쪽부터), describe()}` — **잡히지 않은 스크립트 예외를 호스트용으로 평탄화**.
- `VMOptions`: `maxStackValues = 1<<18`(16바이트 값), `maxFrames = 8192`(스크립트 호출 깊이), `maxNativeDepth = 64`(네이티브→스크립트→네이티브 재진입), `gcThresholdBytes = 8 MiB`(첫 수집 시점), `maxHeapBytes = 512 MiB`(**하드 한도** — 더 큰 할당은 `OutOfMemoryError`), `log`(`api->log`의 싱크, 기본은 무시).
- `HeapStats{liveObjects, liveBytes, collections}`.

### `class VM`
```cpp
explicit VM(const Program&, VMOptions = {});            // Program이 VM보다 오래 살아야 한다
const Program& program() const;
// 네이티브
Result<void> bindNative(const std::string& name, MsNativeFn fn);   // "mingol.Type.getName()", "game.Audio.play(string, float)"
std::vector<std::string> unboundNatives() const;                   // 구현이 아직 없는 native 메서드
Result<void> loadPlugin(const std::string& path);                  // 공유 라이브러리를 로드해 네이티브를 등록
// 실행 (성공=true, 실패=false이고 ex에 예외 설명)
bool initialize(ScriptException& ex);                              // 정적 초기화 실행. 다른 어떤 호출보다 먼저 1회
bool call(const MethodSym&, Value receiver, std::span<const Value> args, Value& result, ScriptException& ex);
bool callClosure(Value closure, std::span<const Value> args, Value& result, ScriptException& ex);
bool construct(const ClassSym&, const MethodSym& ctor, std::span<const Value> args, Value& result, ScriptException& ex);
Value allocate(const ClassSym&);                                   // 생성자 없이 인스턴스만 할당(핫 리로드 마이그레이션용)
// 값
Value newString(std::string_view);  bool isString(Value) const;
std::string_view stringValue(Value) const;                         // 다음 수집 전까지만 유효
const ClassSym* classOf(Value) const;                              // 인스턴스가 아니면 null
Value getField(Value obj, const FieldSym&) const;  void setField(Value obj, const FieldSym&, Value);
Value getStatic(const FieldSym&) const;            void setStatic(const FieldSym&, Value);
// 코루틴 (`coroutine` 메서드 호출이 돌려주는 객체)
bool isCoroutine(Value) const;  bool coroutineDone(Value) const;
bool resumeCoroutine(Value co, ScriptException& ex);   // 다음 yield 또는 끝까지 실행. false = 예외를 던졌고 이미 종료됨
int  coroutineYieldKind(Value) const;                  // 0 없음, 1 int, 2 float, 3 bool, 4 string, 5 object
Value coroutineYielded(Value) const;
// 메모리
std::uint64_t pin(Value);  void unpin(std::uint64_t id);  Value pinned(std::uint64_t id) const;
void collectGarbage();  HeapStats heapStats() const;
```
- `call`: 인스턴스 메서드는 `receiver`에 대해 **가상 디스패치**한다.
- `class Rooted(VM&, Value)`: RAII — 스코프 동안 값을 살려 둔다. `get()`으로 (이동하지 않는 핸들 기준의) 현재 값을 읽는다. 이동 가능, 복사 불가.
- `const MsHostApi* hostApi()` — 플러그인에 건네는 호스트 함수 표(호스트 프로세스 안의 네이티브도 사용 가능).

## plugin_abi.h (네이티브 플러그인 C ABI)
`MS_ABI_VERSION = 1`. 플러그인은 `native class`의 `native` 메서드를 구현하는 공유 라이브러리(`.dll`/`.so`)이며 **정확히 두 개의 C 심볼**을 내보낸다(`MS_PLUGIN_EXPORT`를 앞에 붙일 것 — Windows는 기본으로 아무것도 내보내지 않는다):
```c
MS_PLUGIN_EXPORT uint32_t mingol_plugin_abi(void);                                          // MS_ABI_VERSION을 반환해야 함
MS_PLUGIN_EXPORT int32_t  mingol_plugin_init(const MsHostApi* api, const MsRegistrar* reg); // 0 = 성공
```
`init`에서 **네이티브 이름**으로 등록한다: `"<qualified class>.<method>(<param types>)"`, 예 `"game.Audio.play(string, float)"`. `reg->registerNative(reg->ctx, name, fn)`은 `name`이 로드된 프로그램의 native 메서드가 아니면 **0이 아닌 값**을 반환.

`MsNativeFn = void(*)(MsVM*, MsValue* args, int32_t argc, MsValue* ret)`.
**규칙**
- `args`는 호출 인자(인스턴스 메서드는 `this`가 첫째). 결과는 `*ret`에 쓴다(void면 건드리지 않음). **`ref` 매개변수는 포인터 값으로 오며 플러그인이 읽을 수 없다**.
- 네이티브 실행 중에는 **GC가 돌지 않으므로** `api`에서 얻은 값은 함수가 반환할 때까지 유효하다. 호출 사이에 스크립트 객체를 들고 있으려면 `api->pin`.
- 오류는 `api->throwError(vm, "mingol.ArgumentError", "...")` 후 return; VM이 **함수가 반환될 때** 예외를 일으킨다. **C++ 예외나 longjmp가 이 경계를 넘으면 안 된다.**
- **샌드박스 없음** — 플러그인은 프로세스의 모든 권한으로 실행된다. 신뢰할 수 있는 플러그인만 로드할 것([../SECURITY.md](../SECURITY.md)).
- 구조체 레이아웃은 ABI의 일부다. 바꾸려면 `MS_ABI_VERSION`을 올려야 한다. `MsHostApi::structSize` 안의 멤버만 사용할 것.

`MsHostApi` 함수: `newString`, `stringData`(NUL 종료 보장 없음), `throwError`, `setNativeData/getNativeData`(+finalizer), `newObject`(생성자 미실행; 실패 시 null + 보류 오류), `getField/setField`, `newArray/arrayLength/arrayGet/arraySet`(`MS_ELEM_INT/FLOAT/BOOL/STRING/OBJECT`), `pin/unpin/pinned`, `callClosure`(0=성공, 0이 아니면 예외가 보류 중이므로 **즉시 return**), `log`(`MS_LOG_DEBUG/INFO/WARN/ERROR`).
최소 플러그인(스케치):
```c
#include "mingol/script/plugin_abi.h"
static const MsHostApi* g;
static void add(MsVM* vm, MsValue* a, int32_t n, MsValue* ret) { (void)vm; (void)n; ret->tag = MS_TAG_INT; ret->u.i = a[0].u.i + a[1].u.i; }
MS_PLUGIN_EXPORT uint32_t mingol_plugin_abi(void) { return MS_ABI_VERSION; }
MS_PLUGIN_EXPORT int32_t mingol_plugin_init(const MsHostApi* api, const MsRegistrar* reg) {
  g = api; return reg->registerNative(reg->ctx, "game.Math.add(int, int)", add);
}
```
(실제 컴파일되는 예: `tests/script/plugin/test_plugin.c`)

## 사용 예 (스케치)
```cpp
script::Diagnostics diags;
auto prog = script::compile({{"main.mins", src}}, diags);
if (!prog) { for (auto& d : diags.errors()) MINGOL_LOG_ERROR("script", "{}", d.format()); return; }

script::VM vm(*prog);
// vm.bindNative("game.Log.print(string)", &myPrint);  — unboundNatives()가 빌 때까지
script::ScriptException ex;
if (!vm.initialize(ex)) { MINGOL_LOG_ERROR("script", "{}", ex.describe()); return; }

const script::MethodSym* m = prog->findMethod("Main", "run");
script::Value result;
if (!vm.call(*m, script::makeNull(), {}, result, ex)) MINGOL_LOG_ERROR("script", "{}", ex.describe());
```

## 한계
인터프리터(JIT 없음). 샌드박스 없음(플러그인은 완전 권한; 스크립트 자체는 VM 한도 `maxFrames/maxHeapBytes`로만 제한). 스레드 안전하지 않은 VM. 벤치: 2000개 스크립트 `onUpdate`가 약 0.57 ms/프레임(릴리스, 2코어)([../STATUS.md](../STATUS.md)).
