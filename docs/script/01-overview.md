# Mingol Script 소개

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only. 이 문서는 Mingol Script 문서 시리즈의 1편이다.

| 편 | 문서 | 내용 |
|---|---|---|
| 1 | **소개** (이 문서) | 설계 목표, 특징, 실행 모델, 도구 체인, 적합한 용도 |
| 2 | [언어 기초](02-language-basics.md) | 어휘, 타입, 연산자, 제어 흐름, 문자열·배열, 모듈 |
| 3 | [타입과 객체](03-types-and-objects.md) | 클래스, 상속, 인터페이스, 열거형, 연산자 오버로딩, 초기화 순서 |
| 4 | [고급 기능](04-advanced-features.md) | 클로저, `ref`/포인터, 예외, 코루틴, 리플렉션, 메모리 모델 |
| 5 | [엔진 연동과 확장](05-engine-integration.md) | 스크립트 컴포넌트, 엔진 API, 핫 리로드, C++ 임베딩, 네이티브 플러그인 |

참고 문서: 구현된 동작의 간결한 명세는 [../SCRIPT.md](../SCRIPT.md), 컴파일러/VM의 C++ 시그니처는 [../api/script.md](../api/script.md)와 [../api/scripting.md](../api/scripting.md).

> **이 문서의 코드에 대하여.** 모든 코드 블록은 실제 컴파일러와 VM으로 컴파일·실행해 출력을 확인한 것이다(출력은 코드 아래에 그대로 붙였다). 예제의 `Out.print`는 검증용 호스트가 바인딩한 네이티브 함수일 뿐이며, 엔진 안에서는 `Log.info`를 쓴다. 표시된 "미검증" 항목은 실행해 보지 않은 것이다.

---

## 1. Mingol Script란

Mingol Script(`.mins`)는 Mingol 엔진에 내장된 **정적 타입, 클래스 기반, 바이트코드 컴파일 스크립트 언어**다. 엔진과 같은 저장소의 **자체 구현**(렉서 → 파서 → 타입 검사기 → 코드 생성기 → 스택 기반 VM)이며, 외부 스크립트 엔진(Lua, QuickJS, Mono 등)에 의존하지 않는다.

게임 로직을 C++ 재컴파일 없이 에디터 안에서 작성·수정하고, **저장하는 즉시 컴파일해 실행 중인 씬에 핫 리로드**하는 것이 목적이다.

### 설계 목표

| 목표 | 구체적 선택 |
|---|---|
| **컴파일 시점에 실수를 잡는다** | 정적 타입. 반환 누락, 도달 불가 코드, 잘못된 타입 사용, 존재하지 않는 이름·메서드는 모두 **컴파일 오류**이며 `파일:줄:열` 진단으로 보고된다. |
| **게임 개발자에게 익숙한 문법** | 중괄호, `class`/`interface`/`enum`, `extends`/`implements`, `override`, 접근 제어, `try/catch/finally`, 람다. |
| **예측 가능한 실행** | 정밀 mark-sweep GC가 **세이프포인트에서만** 동작, 고정 크기 스택, 정수 오버플로는 래핑, 모든 런타임 오류는 **잡을 수 있는 예외**. |
| **호스트가 통제한다** | 스크립트는 기본 라이브러리 없이 시작하며, 호스트(엔진)가 노출한 API와 `native` 선언으로만 외부와 통신한다. |
| **게임에 맞는 동시성** | 스레드 대신 **코루틴**(중단 가능한 함수). VM은 단일 스레드. |
| **도구 친화적** | 리플렉션(`typeof`, 필드/메서드 열거)으로 인스펙터·직렬화를 구현한다. |

### 한눈에 보는 언어

| 항목 | 내용 |
|---|---|
| 타입 | `int`(64비트 정수, 래핑), `float`(64비트 실수), `bool`, `string`(UTF-8), 배열 `T[]`, 함수 타입 `fn(int): int`, 포인터 `T*`, 클래스/인터페이스/열거형 |
| 객체지향 | 단일 상속, 다중 인터페이스, 추상 클래스, `final`, `static`, 가상 디스패치, 생성자 체인, 연산자 오버로딩·인덱서 |
| 함수형 | 람다(변수를 **참조로** 캡처), 메서드 값(`obj.method`), 고차 함수 |
| 오류 처리 | `throw`/`try`/`catch`/`finally`, `Exception` 계층(`IndexOutOfRangeError` 등) |
| 동시성 | `coroutine` + `yield` + `Coroutine.resume()` |
| 메타 | `typeof(T)`, `Type.forName`, `FieldInfo`, `MethodInfo.invoke` |
| 모듈 | `module a.b;` / `import a.b;` |
| 확장 | `native` 메서드 + C ABI 플러그인(`.dll`/`.so`) |

**의도적으로 없는 것**: 제네릭, 컬렉션 라이브러리(리스트/맵), 문자 리터럴(`'a'`), 한 문장 안의 다중 변수 선언, 문자열 `null`(문자열은 항상 값이며 기본값은 `""`), 스레드, 파일·네트워크 표준 라이브러리. 필요한 기능은 엔진 API나 플러그인으로 호스트가 제공한다.

---

## 2. 첫 프로그램

게임 스크립트는 `Script`를 상속한 클래스다. 에디터에서 GameObject에 붙이면 씬 생명주기 콜백이 호출된다.

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

이 한 파일에서 볼 수 있는 것:
- `Script` 기반 클래스, `override`한 생명주기 콜백(`onStart`, `onUpdate`, `onDestroy`).
- `gameObject`로 자신의 GameObject 접근, `Vec3` 연산자(`+`).
- 코루틴(`pulse`)을 필드에 보관하고 매 프레임 `resume()`.
- 클래스의 **데이터 필드**(`hp`, `speed`, `label`, `mode`, `waypoints`, `elapsed`)는 `.minseen`에 저장되고 에디터 인스펙터에 나타난다([5편](05-engine-integration.md)).

실제로 씬에 붙여 3프레임 실행한 로그:
```
[INFO] hero starts at 0.0
[WARN] hero destroyed after 0.05000000260770321
```

---

## 3. 실행 모델

```
 .mins 소스 ─▶ 렉서 ─▶ 파서(AST) ─▶ 선언 패스 ─▶ 검사기(타입) ─▶ 코드 생성 ─▶ Program(불변)
                                                                              │
                                                  프렐류드(module mingol)와 함께 컴파일
                                                                              ▼
                                                              VM (Program당 여러 개 가능, 스레드당 하나)
```

- **Program**은 불변이다. 컴파일 오류가 있으면 `Program`은 만들어지지 않는다(부분 결과 없음).
- **VM**은 16바이트 태그 값(`Null/Int/Float/Bool/Object`)을 쓰는 스택 머신이다. 호출 깊이(기본 8192 프레임)와 힙(기본 512 MiB)에 상한이 있으며, 넘으면 `StackOverflowError`/`OutOfMemoryError`가 **스크립트 예외로** 던져진다(프로세스는 죽지 않는다).
- **GC**는 정밀 mark-sweep이며 순환 참조도 수거한다. 수집은 호출·뒤로 가는 점프·할당 지점(세이프포인트)에서만 일어나고 **네이티브 함수 실행 중에는 절대 일어나지 않는다**.
- 모든 스크립트에는 **프렐류드**(모듈 `mingol`)가 자동으로 컴파일에 포함된다: `Object`, `Exception`과 오류 클래스들, `Coroutine`, 리플렉션 클래스(`Type`, `FieldInfo`, `MethodInfo`), 박스 클래스(`IntBox` 등).
- 엔진에 올릴 때는 `ScriptHost`가 **엔진 API 소스**(`Log`, `Vec3`, `GameObject`, `Script`)를 사용자 소스 앞에 함께 컴파일한다.

### 컴파일 오류 예

진단은 한 번에 여러 개(최대 100개) 보고되며 위치가 정확하다:
```mins
class A { }
class Main {
  static int f() {
    int x = 5;
    undefinedName = 3;
    A a = new A();
    a.missing();
    yield 3;
    return 1;
    x = 2;
  }
}
```
```
diag2.mins:5:5: error: unknown name 'undefinedName'
diag2.mins:7:6: error: 'A' has no method 'missing'
diag2.mins:8:5: error: 'yield' is only allowed directly inside a coroutine method
diag2.mins:10:5: error: unreachable code
diag2.mins:11:3: error: method 'f' must return a value on every path
```

### 런타임 오류와 스택 트레이스

런타임 오류는 예외로 전달되고, 잡히지 않으면 호스트에 **타입·메시지·스택 트레이스**로 보고된다:
```mins
class Main {
  static int inner(int[] a) { return a[3]; }
  static int outer() { return inner([1, 2]); }
}
```
```
EXC mingol.IndexOutOfRangeError: index 3 is out of range for length 2
  at Main.inner(int[]) (trace.mins:2)
  at Main.outer() (trace.mins:3)
```
엔진에서는 잡히지 않은 예외가 **그 컴포넌트만 비활성화**하고 같은 형식의 트레이스를 로그에 남긴다. 게임은 계속 진행된다.

---

## 4. 도구 체인에서의 위치

| 단계 | 도구 | 설명 |
|---|---|---|
| 편집 | 에디터의 Scripts 패널 | 줄 번호, 구문 강조, 진단 표시, 자동 들여쓰기. 저장하지 않은 편집도 컴파일에 사용된다 |
| 컴파일 + 핫 리로드 | **Ctrl+B** (Scripts → Compile & Reload) | 오류가 있으면 **기존 실행 상태는 바뀌지 않는다**. 성공하면 모든 스크립트 컴포넌트가 이전된다 |
| 인스펙터 | Add Component → `Script: <클래스>` | 데이터 필드를 인스펙터에서 편집 |
| 저장 | `.minseen` | 스크립트 컴포넌트는 클래스 이름 + 데이터 필드로 저장 |
| 배포 | `mingol-pack`, 에디터 Build | `scripts/` 아래 `.mins`가 `.mingol` 아카이브에 소스 그대로 포함되고, 플레이어가 시작할 때 컴파일한다 |
| 확장 | C ABI 플러그인 | 파일·네트워크 등 호스트가 허용하는 기능을 `native`로 제공(현재는 임베딩 코드에서 로드; 기본 에디터/플레이어는 `.mindev`의 `plugins`를 아직 로드하지 않음 — [5편 6절](05-engine-integration.md#6-네이티브-플러그인-c-abi)) |

> 배포본에는 **소스가 들어간다**(바이트코드 직렬화 없음). 암호화 아카이브는 우연한 추출을 막을 뿐 소스 비공개를 보장하지 않는다([../SECURITY.md](../SECURITY.md)).

---

## 5. 언제 쓰고, 언제 쓰지 말아야 하나

**적합**
- 게임플레이 로직: 캐릭터/적 AI, 퀘스트·상태 머신, 트리거, 이벤트 연출(코루틴), 도구성 스크립트.
- 빠른 반복이 중요한 곳: 핫 리로드로 재컴파일 없이 수정.
- 에디터가 속성을 자동 노출해야 하는 컴포넌트.

**부적합 / 주의**
- **프레임당 수만 번의 무거운 연산**(물리, 파티클, 경로 탐색의 내부 루프, 대량 메시 처리)은 C++ 컴포넌트나 플러그인으로 옮긴다. 이 VM은 인터프리터다(JIT 없음). 참고 측정치(릴리스, 2코어 CI급 머신): **스크립트 컴포넌트 2000개의 `onUpdate`가 프레임당 약 0.57 ms** — 일반적인 게임플레이 로직에는 충분하지만 숫자 연산 커널용은 아니다.
- **샌드박스가 없다.** 스크립트 언어 자체는 파일/네트워크 접근 수단이 없지만, 플러그인은 프로세스 전체 권한으로 실행된다. 신뢰할 수 없는 플러그인은 로드하지 않는다.
- **엔진 API가 작다**(`Log`, `Vec3`, `GameObject` 변환·계층·활성화). 물리·오디오·입력·렌더링 컴포넌트 접근은 **아직 스크립트 API에 없다**(C++ 컴포넌트로만). 필요하면 플러그인으로 `native` 바인딩을 추가한다.
- **디버거가 없다**. 로그와 스택 트레이스가 디버깅 수단이다.
- Windows에서는 컴파일러/VM/플러그인 로드가 CI에서 테스트되었으나 실사용 환경 검증은 아직 없다([../KNOWN-LIMITS.md](../KNOWN-LIMITS.md)).

---

## 6. 다음 읽을 것
- 문법과 기본 타입부터: [2편 언어 기초](02-language-basics.md)
- 바로 게임 스크립트를 쓰려면: [5편 엔진 연동](05-engine-integration.md)의 "스크립트 컴포넌트 작성"
