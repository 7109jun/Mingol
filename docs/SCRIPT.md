# Mingol Script (`.mins`) 언어·VM 명세

문서 라이선스: CC BY 4.0. 구현: `engine/script` (컴파일러 + 바이트코드 VM + 플러그인 ABI). 이 문서는 **구현된 동작**만 기술한다.

## 1. 파이프라인

`렉서 → 파서(AST) → 선언 패스(클래스/멤버 심볼) → 검사기(타입 검사, AST 주석) → CodeGen(바이트코드) → VM`

- 진입점: `compile(sources, diags)` → `Program`(바이트코드, 클래스/메서드 심볼, 리플렉션 테이블). 오류가 있으면 `nullptr` + 위치가 있는 진단(`파일:줄:열: error: ...`).
- 모든 스크립트에는 내장 프렐류드(`preludeSource()`)가 앞에 붙는다: `Object, String 관련, Exception 계층, Type, FieldInfo, MethodInfo, Coroutine, IntBox/FloatBox/StringBox ...`.
- 컴파일러는 어떤 입력에서도 크래시/무한 재귀하지 않는다(중첩 깊이 제한, 변이 퍼징 ASan/UBSan 통과 — 한계 참조).

## 2. 언어 요약

| 항목 | 규칙 |
|---|---|
| 타입 | `int`(64비트, 래핑), `float`(64비트), `bool`, `string`, 배열 `T[]`, `new int[3][]`, 함수형 `fn(int): int`, 배열의 함수 `(fn():int)[]`, 포인터 `T*` (`&x`, `*p`) |
| 선언 | `module a.b;` `import x;` `class`/`abstract class`/`interface`/`enum`, `extends`/`implements`, `final`, `static`, `override`, 접근 제어 `public/protected/private` |
| 문장 | `if/else for while do/while switch(case 1, 2: …; 폴스루 없음) break continue return throw try/catch/finally yield` |
| foreach | `for (T v : xs) { }` |
| 캐스트 | `expr as T` (실패 시 `CastError`, 산술에서는 괄호 필요: `(x as int) + 1`) |
| 람다 | `(int x) => x * k` (값 캡처된 변수는 박스로 공유) |
| 연산자 오버로드 | `static T operator+(A a, B b)`, 인덱서: getter 2 매개변수 / setter 3 매개변수 |
| `ref` 매개변수 | 선언에 `ref`, 호출부는 암시적 |
| 코루틴 | `coroutine void f() { yield 1; }` — 호출하면 `Coroutine` 객체 생성, `resume()`로 진행, `yield`는 코루틴 함수 본문에서만 |
| 예외 | `throw`는 `Exception` 파생 객체만. `catch`는 선언 순서대로 첫 일치. `finally` 지원. 스택 트레이스는 `Exception.stackTrace` 필드 |
| 네이티브 | `native class`/`public static native int f(int);` — 구현은 호스트 또는 플러그인이 이름으로 바인딩 |
| 리플렉션 | `typeof(T)`, `Type.forName`, `getFields/getMethods/getField/getMethod/newInstance`, `FieldInfo.get*/set*`, `MethodInfo.invoke(Object, Object[])` |

### 의미론 (놓치기 쉬운 것)
- 문자열은 **UTF-8 바이트열**: `length`와 인덱싱은 바이트 단위(문자 단위 아님).
- `int` 오버플로는 래핑, 시프트 양은 63으로 마스크, 0으로 나누기는 `DivideByZeroError`, `float→int`는 포화(NaN→0).
- `float → string`은 왕복 가능한 최단 표현, 지수는 선행 0 없이(`1e-7`).
- `switch`의 빈 비-default `case`는 오류. 도달 불가 코드는 오류. 반환 누락은 오류.
- `Type`, `Coroutine` 등 프렐류드 타입은 스크립트에서 `new`로 만들 수 없다.
- 함수형 시그니처의 정규 표기는 `fn(int): int`(콜론 뒤 공백) — 네이티브 이름 매칭에 쓰인다.

## 3. VM

- 스택 기반 바이트코드, 명령 8바이트(`op, pad, a:u16, b:i32`). 값은 16바이트 태그 값(`Null/Int/Float/Bool/Object`).
- 힙 객체: `Instance/String/Array/Closure/Box/Ref`. **정밀 mark-sweep GC**, 안전점(호출·역방향 점프·할당 지점)에서만 수행, 네이티브 실행 중에는 절대 실행되지 않는다. 순환 참조 수거됨. `VMOptions::gcThresholdBytes`, `maxHeapBytes`(초과 시 `OutOfMemoryError`).
- 스택은 고정 사전 할당(재할당 없음), 한도 초과 시 `StackOverflowError`.
- 코루틴 = 중단된 단일 프레임(지역 변수·pc 저장). 버려진 코루틴의 `finally`는 실행되지 않는다(한계).
- 예외 해제는 함수별 핸들러 테이블(안쪽 우선).

### 호스트 API (`mingol::script::VM`)
`initialize`, `call(MethodSym, receiver, args, result, ex)`, `construct`, `callClosure`, `getField/setField/getStatic`, `newString/stringValue`, `classOf`, `collectGarbage`, `heapStats`, `Rooted`(GC 핀 RAII), `bindNative`, `unboundNatives`, `loadPlugin`.
실패는 예외가 아니라 `false + ScriptException{type,message,stackTrace}`로 보고된다.

## 4. 플러그인 ABI v1 (`plugin_abi.h`, 순수 C)

공유 라이브러리가 `mingol_plugin_abi()`(= `MS_ABI_VERSION`)와 `mingol_plugin_init(api, registrar)`를 내보내고, **이름**으로 네이티브를 등록한다:
`"<정규화된 클래스>.<메서드>(<매개변수 타입, ...>)"` 예: `Native.addInts(int, int)`, `Native.applyTwice(fn(int): int, int)`.

- 프로그램이 선언하지 않은 이름을 등록하면 플러그인 로드가 실패한다. ABI 버전 불일치·심볼 없음·파일 없음도 실패(오류 반환, 크래시 없음).
- 호스트 API: 문자열, 예외 던지기, 네이티브 데이터+finalizer, 객체/필드, 배열, 핀, 클로저 호출, 로그.
- **샌드박스 없음**: 플러그인은 프로세스 전체 권한으로 실행된다. C++ 예외/longjmp가 경계를 넘으면 안 된다.
- 구조체 레이아웃 변경은 `MS_ABI_VERSION` 상승이 필요하다.

## 5. 검증된 것 / 한계

검증(Linux GCC 13): 렉서·파서·VM·예외·코루틴·리플렉션·호스트 API·GC 스트레스·플러그인(C로 작성한 공유 라이브러리)·진단·변이 퍼징 — `tests/script`, debug/release/asan 통과. 퍼징은 시드 6개×4000회 변이를 ASan+UBSan에서 실행해 충돌 0(이 과정에서 오류 인자 타입이 null일 때의 검사기 크래시 1건을 발견·수정).

한계:
- 핫 리로드·ScriptComponent는 `engine/scripting`에 구현됨(아래 6절). 한계: 정적 변수는 리로드 시 초기화, 객체 타입 필드는 이전되지 않음, 코루틴 상태는 이전되지 않음, 리로드 중 실행 중인 호출 스택이 있으면 안 됨(콜백 밖에서만 호출).
- `ref` 매개변수는 리플렉션(`invoke`)으로 호출할 수 없다. 버려진 코루틴의 `finally` 미실행. 문자열은 바이트 단위.
- 플러그인 ABI는 Linux에서만 실행 검증했다(Windows `LoadLibrary` 경로는 컴파일·실행 미검증).
- 컴파일러는 크래시/ASan 오류에 대해서만 퍼징했고, 의미적 정확성의 변이 비교(차분 테스트)는 하지 않았다.
- 성능 벤치마크는 아직 없다(fib(20) 정확성만 확인).

## 6. 씬 연동과 핫 리로드 (`engine/scripting`, Mingol::Scripting)

- `ScriptHost::create(sources)` — 엔진 API 소스(`engineApiSource()`)와 사용자 소스를 함께 컴파일하고 VM을 만든다. 컴파일 오류는 `파일:줄:열` 진단 문자열로 반환.
- 엔진 API(스크립트에서 보임): `Log.info/warn/error`, `Vec3`(연산자 +,-,*), `GameObject`(핸들: exists/getName/setName/isActive/setActive/get·setPosition/getWorldPosition/get·setScale/rotate/setRotation/getParent/same/destroy, 정적 find/create), 기반 클래스 `Script`(`gameObject` + `onAwake/onEnable/onStart/onFixedUpdate/onUpdate/onLateUpdate/onDisable/onDestroy`).
- `host.addScript(gameObject, "ClassName")` → `ScriptComponent`. 콜백은 Scene의 생명주기 순서를 따른다. 스크립트 예외는 해당 컴포넌트만 비활성화하고 `Scene::callbackFailures()`에 집계된다(스택 트레이스 로그).
- `host.reload(sources)` — 새 프로그램을 컴파일하고 새 VM을 만든 뒤 **모든 ScriptComponent를 필드 이름+타입 기준으로 이전**한다(int/float/bool/string/enum, 이들의 배열). 컴파일/정적 초기화 오류 시 아무것도 바뀌지 않는다. 클래스가 사라졌거나 생성자가 던지면 그 컴포넌트만 비활성화. `ReloadReport`(이전/비활성화/복원·건너뜀 필드 수, 메시지).
- 호스트는 씬보다 오래 살아야 한다. 단일 스레드.
- 한계: Windows 미검증, 엔진 API가 작다(물리·오디오·입력·렌더링 컴포넌트 접근 없음), 객체 필드(다른 스크립트/GameObject 참조) 이전 안 됨, 디버거 없음.
