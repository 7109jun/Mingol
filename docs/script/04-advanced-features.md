# 고급 기능

문서 라이선스: CC BY 4.0. Mingol Script 문서 시리즈 4편. 이전: [3편 타입과 객체](03-types-and-objects.md) · 다음: [5편 엔진 연동과 확장](05-engine-integration.md).

클로저와 함수 값, `ref`/포인터, 예외, 코루틴, 리플렉션, `native` 선언, 실행·메모리 모델을 다룬다. 코드는 모두 실제 VM에서 실행해 출력을 확인했다.

## 1. 종합 예제

```mins
public native class Out { public static native void print(string s); }

class InsufficientFunds extends Exception {
  public int missing;
  InsufficientFunds(string msg, int missing) { super(msg); this.missing = missing; }
}

class Wallet {
  int gold = 10;
  void pay(int cost) {
    if (cost > gold) throw new InsufficientFunds("need more gold", cost - gold);
    gold -= cost;
  }
}

class Main {
  static void swap(ref int a, ref int b) { int t = a; a = b; b = t; }
  static fn(int):int makeAdder(int k) { return (int x) => x + k; }
  static int apply(fn(int,int):int op, int a, int b) { return op(a, b); }

  static coroutine void countdown(int from) {
    for (int i = from; i > 0; i--) { yield i; }
  }

  static int main() {
    // closures: captured variables are shared, not copied
    int counter = 0;
    fn():void tick = () => { counter++; };
    tick(); tick();
    fn(int):int add5 = makeAdder(5);
    Out.print("counter=" + counter + " add5(1)=" + add5(1) + " mul=" + apply((a, b) => a * b, 6, 7));

    // ref parameters (call site is implicit) and pointers
    int x = 1; int y = 2;
    swap(x, y);
    int* p = &x;
    *p += 40;
    Out.print("x=" + x + " y=" + y);

    // exceptions
    Wallet w = new Wallet();
    try {
      w.pay(4);
      w.pay(50);
    } catch (InsufficientFunds e) {
      Out.print("caught: " + e.message + " (missing " + e.missing + ")");
    } catch (Exception e) {
      Out.print("other");
    } finally {
      Out.print("gold=" + w.gold);
    }
    try {
      int[] arr = new int[2];
      arr[5] = 1;
    } catch (IndexOutOfRangeError e) {
      Out.print("runtime error: " + e.message);
    }

    // coroutines: resume() runs until the next yield
    Coroutine c = countdown(3);
    string seq = "";
    while (true) {
      c.resume();
      if (c.isDone()) break;
      seq += c.yieldedInt() + " ";
    }
    Out.print("countdown: " + seq);

    // reflection
    Type t = typeof(Wallet);
    Out.print("type " + t.getName() + " fields=" + t.getFields().length);
    Object o = t.newInstance();
    t.getField("gold").setInt(o, 99);
    Out.print("gold via reflection = " + (o as Wallet).gold);
    return 0;
  }
}
```
실행 출력:
```
counter=2 add5(1)=6 mul=42
x=42 y=1
caught: need more gold (missing 44)
gold=6
runtime error: index 5 is out of range for length 2
countdown: 3 2 1 
type Wallet fields=1
gold via reflection = 99
```

---

## 2. 함수 값, 람다, 클로저

### 함수 타입
`fn(int, int): int`는 "정수 두 개를 받아 정수를 돌려주는 함수"다. 콜론 뒤에 공백을 둔 형태가 **정규 표기**이며 네이티브 이름 매칭(5편)에 그대로 쓰인다. 반환이 없으면 `fn(): void`. 변수·필드·매개변수·배열 요소·반환값에 쓸 수 있다. 배열 요소 타입일 때는 괄호가 필요하다: `(fn(): int)[]`.

### 람다
```mins
fn(int): int add5 = (int x) => x + 5;          // 매개변수 타입 명시
fn(int, int): int mul = (a, b) => a * b;       // 대상 타입에서 추론되면 생략 가능
fn(): void tick = () => { counter++; };        // 본문이 블록
```
람다는 **다른 함수로 전달**하거나(`apply((a, b) => a * b, 6, 7)` → 42) 반환할 수 있다. 중첩 람다(`(int b) => (int c) => a + b + c`)도 된다. 자기 자신을 부르려면 먼저 변수를 선언하고 대입한다: `fn(int): int fact = null; fact = (int n) => n <= 1 ? 1 : n * fact(n - 1);`.

### 캡처 규칙 (중요)
**캡처한 변수는 값 복사가 아니라 공유된다.** 클로저가 만들어진 뒤 바깥에서 변수가 바뀌면 클로저에서도 바뀐 값이 보이고, 반대도 성립한다. 변수가 재대입되는 경우 VM이 힙 박스에 올려 공유한다.
```mins
int n = 1;
fn(): int get = () => n;
n = 5;
get();           // 5  (복사였다면 1)

int c = 0;
fn(): void inc = () => { c += 2; };
fn(): int read = () => c;
inc(); inc();
read();          // 4  — 두 클로저가 같은 c를 본다
```
(위 동작은 VM 테스트 `tests/script/test_vm_lang.cpp`가 검증한다. 종합 예제의 `counter`도 `tick()`을 두 번 호출해 `counter=2`가 된다.)

- **반복문 안에서 선언한 변수는 반복마다 새 변수**다. 따라서 `for (int i = 0; i < 3; i++) { int j = i * 10; fs[i] = () => j; }`의 세 클로저는 각각 `0`, `10`, `20`을 돌려준다. `foreach`의 루프 변수도 마찬가지다. 단, `for`의 **카운터 `i` 자체**는 하나의 변수이므로 `() => i`는 루프가 끝난 뒤의 값을 본다 — 반복마다 값을 고정하려면 안쪽에 `int j = i;`를 만들어 캡처한다.
```mins
public native class Out { public static native void print(string s); }
class Main {
  static coroutine void one() { yield 1; }
  static int main() {
    (fn(): int)[] fs = new (fn(): int)[3];
    for (int i = 0; i < 3; i++) { fs[i] = () => i; }
    Out.print("loop counter captured: " + fs[0]() + " " + fs[1]() + " " + fs[2]());
    (fn(): int)[] gs = new (fn(): int)[3];
    for (int i = 0; i < 3; i++) { int j = i; gs[i] = () => j; }
    Out.print("copy inside: " + gs[0]() + " " + gs[1]() + " " + gs[2]());
    Coroutine c = one();
    c.resume(); c.resume(); Out.print("done=" + c.isDone());
    c.resume(); Out.print("resume after done ok, done=" + c.isDone());
    return 0;
  }
}
```
```
loop counter captured: 3 3 3
copy inside: 0 1 2
done=true
resume after done ok, done=true
```
- 클로저는 `this`도 캡처한다(`(int x) => x + base`에서 `base`는 필드; 이후 필드가 바뀌면 클로저가 변경된 값을 본다).
- 캡처된 변수는 클로저가 살아 있는 한 GC가 보존한다.

### 메서드 값
메서드를 이름만으로 함수 값으로 쓸 수 있다.
```mins
fn(int): int f = Util.square;      // 정적 메서드
fn(int): int g = obj.add;          // 인스턴스 메서드: obj에 바인딩됨
fn(): int h = animal.speak;        // 가상 메서드: 바인딩된 객체의 실제 타입으로 디스패치
```

---

## 3. `ref` 매개변수와 포인터

### `ref` 매개변수
호출자의 변수를 직접 바꾸는 매개변수. **선언에만 `ref`를 쓰고 호출부는 평범하게 쓴다.**
```mins
static void swap(ref int a, ref int b) { int t = a; a = b; b = t; }
...
int x = 1; int y = 2;
swap(x, y);          // x == 2, y == 1
```
- 인자는 **변수여야 한다**: 지역 변수, 필드, 정적 필드, 배열 요소(`inc(arr[1])`)가 가능하다. 리터럴이나 식은 불가(`argument 1 must be a variable of type 'int' (passed by reference)`). **타입이 정확히 같아야** 한다.
- `ref` 매개변수를 다른 `ref`로 그대로 넘길 수 있다(`forward(ref int v) { bump(v); }`).
- **연산자 매개변수는 `ref`가 될 수 없다**. 람다도 `ref` 매개변수를 가질 수 있다(`fn(ref int): void`).
- 제약: **리플렉션 `MethodInfo.invoke`로 `ref` 매개변수를 가진 메서드를 호출할 수 없다.** 네이티브 함수는 `ref` 인자를 읽을 수 없다.

### 포인터
```mins
int x = 1;
int* p = &x;       // x의 주소 (변수, 필드, 배열 요소)
*p += 40;          // 역참조 읽기/쓰기
// x == 41
```
- 포인터는 **GC가 관리하는 안전한 참조**다(주소 연산이나 포인터 산술 없음). 가리키는 변수는 포인터가 살아 있는 한 보존되고, 클로저와 같은 변수를 공유한다.
- `null` 포인터를 역참조하면 `NullReferenceError`. `==`/`!=`로 같은 대상인지 비교할 수 있다.
- 용도: `ref` 없이 변수에 대한 핸들을 저장해야 할 때. 일반적인 게임 로직에서는 `ref`나 반환값이 더 단순하다.

---

## 4. 예외

```mins
try { ... }
catch (LoadError e) { ... }          // 위에서부터 첫 일치
catch (Exception e) { ... }
finally { ... }                      // 정상/예외/return 어느 경우에도 실행
```

- `throw`는 **`Exception`을 상속한 객체만** 던질 수 있다. 사용자 예외는 `Exception`(또는 `RuntimeError` 등)을 상속한다.
- `catch`는 **선언 순서대로 첫 번째로 일치하는 것**이 잡는다. 하위 타입을 먼저, 상위 타입을 나중에 둔다.
- `finally`는 `return`으로 빠져나갈 때도 실행된다.
- `Exception`은 `message`, `stackTrace` 필드와 `getMessage()`, `getStackTrace()`를 가진다. **스택 트레이스는 던져질 때 채워진다**(`  at 클래스.메서드(시그니처) (파일:줄)`, 가장 안쪽이 먼저).

```mins
public native class Out { public static native void print(string s); }

class AppError extends Exception { AppError(string m) { super(m); } }
class LoadError extends AppError { LoadError(string m) { super(m); } }

class Main {
  static int tryFinally() {
    try { return 1; } finally { Out.print("finally runs before the return completes"); }
  }

  int fieldDummy; void field() { fieldDummy++; }
  static void level3() { throw new LoadError("file missing"); }
  static void level2() {
    try { level3(); }
    catch (LoadError e) { throw new AppError("level2 wrapped: " + e.message); }
  }

  static int main() {
    Out.print("tryFinally=" + tryFinally());
    try { level2(); }
    catch (LoadError e) { Out.print("not here"); }
    catch (AppError e) {
      Out.print("caught " + e.message);
      Out.print("trace:\n" + e.stackTrace);
    }
    try { Object o = null; Main m2 = o as Main; m2.field(); }
    catch (RuntimeError e) { Out.print("RuntimeError family: " + e.message); }
    int zero = 0;
    try { Out.print("" + (1 / zero)); } catch (DivideByZeroError e) { Out.print("div0: " + e.message); }
    return 0;
  }
}
```
```
finally runs before the return completes
tryFinally=1
caught level2 wrapped: file missing
trace:
  at Main.level2() (exc2.mins:15)
  at Main.main() (exc2.mins:20)

RuntimeError family: null reference
div0: integer division by zero
```

> 위 출력에서 `level2()`가 새로 던진 `AppError`의 스택은 `level2` → `main`이다. 원래 던져진 곳(`level3`)의 정보는 새 예외에 자동으로 이어지지 않는다 — 필요하면 메시지에 넣는다.

### 내장 예외 계층
```
Exception
└─ RuntimeError
   ├─ NullReferenceError      null 역참조, null 배열 length 등
   ├─ IndexOutOfRangeError    배열/문자열 범위 밖 (index 5 is out of range for length 2)
   ├─ DivideByZeroError       정수 0 나누기 (integer division by zero)
   ├─ CastError               잘못된 `as`, 정의되지 않은 enum 값
   ├─ StackOverflowError      호출 깊이 초과
   ├─ OutOfMemoryError        힙 한도 초과
   ├─ ArgumentError           잘못된 인자(음수 길이 등)
   └─ NativeError             구현이 바인딩되지 않은 native 호출 등
```
모든 이름은 모듈 `mingol`에 있으며 완전 이름은 `mingol.IndexOutOfRangeError`(호스트 보고의 `ScriptException::type`에 이 형태로 나온다).

### 잡히지 않은 예외
- VM 호출에서 **호스트로 보고**된다(`ScriptException{type, message, stackTrace}`, C++ 예외가 아니다).
- 엔진에서는 **그 스크립트 컴포넌트만 비활성화**되고 로그에 남으며 게임은 계속된다(`Scene::callbackFailures()` 증가).

---

## 5. 코루틴

코루틴은 **실행을 중단했다가 이어 가는 함수**다. 여러 프레임에 걸친 연출·대기·상태 머신에 쓴다. 스레드가 아니다: 호출한 쪽이 `resume()`할 때만 진행한다.

```mins
coroutine void waves(int count) {
  for (int i = 1; i <= count; i++) { yield "wave " + i; }
  yield;
}
```
- 선언: `coroutine` 수정자, 반환 타입은 **반드시 `void`**(`a coroutine must return void`). 인스턴스/정적 메서드 모두 가능하고, 가상 메서드로 override할 수 있다.
- 호출하면 본문이 실행되지 않고 **`Coroutine` 객체**가 만들어진다. 인자와 지역 변수는 그 객체가 보관한다(여러 코루틴이 각자 상태를 가진다).
- `Coroutine`의 메서드: `resume()`(다음 `yield` 또는 끝까지 실행), `isDone()`, `yieldKind()`(`0` 값 없음, `1` int, `2` float, `3` bool, `4` string, `5` object), `yieldedInt/Float/Bool/String/Object()`.
- `yield 값;`은 호출자에게 값을 내보내고, `yield;`는 값 없이 멈춘다. `yield`는 **코루틴 메서드 본문에서 직접**만 쓸 수 있다(`'yield' is only allowed directly inside a coroutine method` — 람다 안이나 일반 메서드에서는 불가).
- 끝난 뒤에는 `isDone()`이 `true`. 이미 끝난 코루틴에 `resume()`을 다시 불러도 **오류 없이 아무 일도 하지 않는다**(확인됨). 그래도 `isDone()`을 먼저 확인하는 것이 의도가 분명하다.
- 코루틴 안의 `try/catch`가 동작한다. 코루틴에서 예외가 **밖으로 새면** `resume()` 호출자에게 전파되고 그 코루틴은 끝난다.

```mins
public native class Out { public static native void print(string s); }

class Spawner {
  int spawned = 0;
  coroutine void waves(int count) {
    for (int i = 1; i <= count; i++) {
      spawned += i;
      yield "wave " + i;          // yield a value: string
    }
    yield;                        // plain yield, no value
  }
}

class Main {
  static int main() {
    Spawner sp = new Spawner();
    Coroutine c = sp.waves(2);
    int steps = 0;
    while (true) {
      c.resume();
      if (c.isDone()) break;
      steps++;
      Out.print("step " + steps + " kind=" + c.yieldKind() + (c.yieldKind() == 4 ? " value=" + c.yieldedString() : ""));
    }
    Out.print("spawned=" + sp.spawned + " done=" + c.isDone());
    return 0;
  }
}
```
```
step 1 kind=4 value=wave 1
step 2 kind=4 value=wave 2
step 3 kind=0
spawned=3 done=true
```
세 번째 `resume()`은 값 없는 `yield;`(`kind=0`), 네 번째 `resume()`에서 본문이 끝나 `isDone()`이 `true`가 된다.

### 엔진에서 쓰는 패턴
엔진은 코루틴을 자동으로 진행하지 않는다 — **`Script`가 자신의 `Coroutine`을 보관하고 `onUpdate`에서 `resume()`** 한다(1편의 `Hero` 예제).
```mins
override void onUpdate(float dt) {
  if (routine != null && !routine.isDone()) routine.resume();
}
```
시간 대기가 필요하면 `yield` 사이에서 `dt`를 누적하는 식으로 직접 구현한다(기본 제공 `WaitForSeconds` 같은 것은 없다).

### 한계
- **버려진 코루틴의 `finally`는 실행되지 않는다**(끝까지 진행하지 않고 참조를 놓으면 그대로 수거된다).
- 코루틴은 **중단된 단일 프레임**(지역 변수 + 실행 위치)이다. 다른 함수를 부른 안쪽에서 바깥 코루틴을 `yield`할 수는 없다.
- 핫 리로드 시 **코루틴 상태는 이전되지 않는다**.

---

## 6. 리플렉션

스크립트와 도구(인스펙터, 직렬화)가 타입 정보를 런타임에 다룬다.

| API | 설명 |
|---|---|
| `typeof(T)` | 타입 객체(`Type`) |
| `Type.forName("정규화된 이름")` | 이름으로 조회 |
| `Type`: `getName`, `getBase`, `isClass`, `isInterface`, `isEnum`, `isAbstract`, `isAssignableFrom(Type)`, `getFields`, `getMethods`, `getField(name)`, `getMethod(name)`, `newInstance()` | |
| `FieldInfo`: `getName`, `getTypeName`, `isStatic`, `getInt/Float/Bool/String/Object(target)`, `setInt/Float/Bool/String/Object(target, value)` | |
| `MethodInfo`: `getName`, `getReturnTypeName`, `getParameterCount`, `isStatic`, `invoke(target, args)` | |

`invoke`의 인자와 반환값은 **박스 클래스**(`IntBox`, `FloatBox`, `BoolBox`, `StringBox`)나 객체다. 정적 메서드는 `target`에 `null`을 넘긴다.
```mins
public native class Out { public static native void print(string s); }
class Main {
  static int main() {
    Object[] args = [new IntBox(5), new IntBox(6)];
    Type t = typeof(Main);
    MethodInfo m = t.getMethod("add");
    Object r = m.invoke(null, args);
    Out.print("invoke -> " + (r as IntBox).value + " params=" + m.getParameterCount() + " static=" + m.isStatic() + " ret=" + m.getReturnTypeName());
    string names = "";
    for (MethodInfo mi : t.getMethods()) { names += mi.getName() + ","; }
    Out.print("methods: " + names);
    Type forName = Type.forName("Main");
    Out.print("forName: " + forName.getName() + " isClass=" + forName.isClass() + " base=" + forName.getBase().getName());
    return 0;
  }
  static int add(int a, int b) { return a + b; }
}
```
```
invoke -> 11 params=2 static=true ret=int
methods: main,add,
forName: Main isClass=true base=mingol.Object
```
`typeof(Wallet).getFields().length`, `getField("gold").setInt(obj, 99)`는 종합 예제에서 확인했다(출력: `type Wallet fields=1`, `gold via reflection = 99`).

- `Type`, `FieldInfo`, `MethodInfo`, `Coroutine`은 **스크립트에서 `new`할 수 없다**(생성자가 `private`).
- `ref` 매개변수를 가진 메서드는 `invoke`로 호출할 수 없다.
- 호스트(C++)는 같은 메타데이터를 `Program::findClass/findMethod`, `ClassSym`/`FieldSym`/`MethodSym`으로 읽는다([../api/script.md](../api/script.md)). 엔진의 스크립트 직렬화와 인스펙터가 이것으로 동작한다.

---

## 7. `native` 선언

스크립트 안에서 구현할 수 없는 기능은 `native`로 **선언만** 하고 호스트나 플러그인이 구현한다.
```mins
public native class Native {
  public static native int addInts(int a, int b);
  public static native int applyTwice(fn(int): int f, int x);
  public static native string greet(string name);
}
```
- `native class`의 `native` 메서드는 본문이 없다. 구현은 **이름 `"<클래스>.<메서드>(<매개변수 타입, ...>)"`** 로 바인딩된다(`Native.addInts(int, int)`, `Native.applyTwice(fn(int): int, int)`). 정규화된 클래스 이름·함수 타입 표기가 그대로 키가 된다.
- 바인딩되지 않은 네이티브를 호출하면 **런타임에** `mingol.NativeError`(`native method '...' is not bound to an implementation`)가 던져진다. 컴파일 오류가 아니다 — 호스트는 `VM::unboundNatives()`로 로드 직후 확인할 수 있다.
- 구현 방법(C ABI 플러그인)은 [5편](05-engine-integration.md#6-네이티브-플러그인-c-abi).

---

## 8. 실행과 메모리 모델 요약

| 주제 | 동작 |
|---|---|
| 정수 | 64비트, 오버플로 **래핑**, 시프트 양은 63으로 마스크, 0 나누기 예외 |
| 실수→정수 | 0 방향 절단, 범위 밖 **포화**, `NaN` → 0 |
| 문자열 | 불변 UTF-8 바이트열, 길이·인덱스는 바이트 단위 |
| 스택 | 사전 할당된 고정 크기(기본 `1<<18` 값, 호출 깊이 8192). 초과 시 `StackOverflowError` |
| 힙 | 기본 512 MiB 한도. 초과 시 `OutOfMemoryError`. 첫 GC는 8 MiB 할당 후 |
| GC | 정밀 mark-sweep, 세이프포인트에서만, 순환 참조 수거, **네이티브 실행 중에는 없음** |
| 예외 해제 | 함수별 핸들러 테이블, 안쪽 우선 |
| 스레딩 | VM은 **단일 스레드**. 스레드당 VM 하나, 값을 VM 사이에 공유하지 않는다 |
| 결정성 | 입력(프레임 시간, 호스트 API 결과)이 같으면 같은 결과 |

정밀한 한계 목록은 [../SCRIPT.md](../SCRIPT.md)의 "검증된 것 / 한계" 절.

다음: [5편 엔진 연동과 확장](05-engine-integration.md)
