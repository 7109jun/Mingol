# 타입과 객체

문서 라이선스: CC BY 4.0. Mingol Script 문서 시리즈 3편. 이전: [2편 언어 기초](02-language-basics.md) · 다음: [4편 고급 기능](04-advanced-features.md).

이 편은 클래스, 상속, 인터페이스, 열거형, 접근 제어, 생성·초기화 순서, 연산자 오버로딩, 타입 검사/캐스트를 다룬다.

## 1. 종합 예제

```mins
public native class Out { public static native void print(string s); }

enum Team { Neutral, Red = 5, Blue }

interface Damageable {
  void takeDamage(int amount);
  bool isDead();
}

abstract class Actor implements Damageable {
  protected int hp;
  public final string name;
  static int created = 0;

  Actor(string name, int hp) { this.name = name; this.hp = hp; created++; }

  public void takeDamage(int amount) { hp -= amount; if (hp < 0) hp = 0; }
  public bool isDead() { return hp == 0; }
  public abstract string describe();
}

class Soldier extends Actor {
  Team team;
  Soldier(string name, Team team) { super(name, 100); this.team = team; }
  override string describe() { return name + "[" + team + "] hp=" + hp; }
}

class Boss extends Soldier {
  Boss() { super("Boss", Team.Blue); hp = 500; }
  override string describe() { return "BOSS " + super.describe(); }
}

class Vec2 {
  float x; float y;
  Vec2(float x, float y) { this.x = x; this.y = y; }
  static Vec2 operator+(Vec2 a, Vec2 b) { return new Vec2(a.x + b.x, a.y + b.y); }
  static Vec2 operator*(Vec2 a, float k) { return new Vec2(a.x * k, a.y * k); }
  static float operator[](Vec2 v, int i) { return i == 0 ? v.x : v.y; }
  static void operator[](Vec2 v, int i, float val) { if (i == 0) v.x = val; else v.y = val; }
  string str() { return "(" + x + ", " + y + ")"; }
}

class Main {
  static int main() {
    Actor[] actors = [new Soldier("Ann", Team.Red), new Boss()];
    for (Actor a : actors) {
      a.takeDamage(120);
      Out.print(a.describe() + " dead=" + a.isDead());
    }
    Out.print("created=" + Actor.created);

    Object o = actors[1];
    Out.print("is Soldier: " + (o is Soldier) + ", is Damageable: " + (o is Damageable));
    Soldier s = o as Soldier;
    Out.print("team value: " + (s.team as int));

    Team t = Team.Red;
    switch (t) {
      case Team.Neutral: Out.print("neutral"); break;
      case Team.Red: Out.print("red"); break;
      default: Out.print("other");
    }

    Vec2 v = new Vec2(1, 2) + new Vec2(3, 4) * 2.0;
    v[1] += 0.5;
    Out.print("v=" + v.str() + " v[0]=" + v[0]);
    return Actor.created;
  }
}
```

실행 출력:
```
Ann[Red] hp=0 dead=true
BOSS Boss[Blue] hp=380 dead=false
created=2
is Soldier: true, is Damageable: true
team value: 6
red
v=(7.0, 10.5) v[0]=7.0
```

읽는 법: `Ann`은 `Soldier`로 `describe()`가 `Ann[Red] hp=0`; `Boss`는 `hp`를 500으로 덮고 `super.describe()`를 호출해 `BOSS ` 접두를 붙인다. `Actor.created`는 정적 필드(2). `s.team as int`는 `Team.Blue`의 값 `6`(`Red = 5`로 시작했으므로).

---

## 2. 클래스

```mins
class Name extends Base implements I1, I2 {
  // 필드, 생성자, 메서드, 연산자
}
```

- **단일 상속**: `extends`는 하나. 다중 상속은 구문 오류. 순환 상속은 `cyclic inheritance` 오류. 아무것도 상속하지 않으면 암묵적으로 `Object`를 상속한다.
- **인터페이스는 여러 개** `implements`.
- 수정자: `abstract`, `final`, `static`, `override`, `native`(5편에서 다룸), 접근 제어 `public`/`protected`/`private`.
- 클래스 본문의 선언 순서는 자유롭다.

### 접근 제어

| 수정자 | 의미 |
|---|---|
| `public` (기본값) | 어디서나 접근. **수정자를 생략하면 `public`이다.** |
| `protected` | 선언한 클래스와 그 하위 클래스 |
| `private` | 선언한 클래스 안에서만. **`private` 메서드는 가상이 아니다**(하위 클래스에 같은 이름이 있어도 상위 코드의 호출은 상위 것을 부른다) |

위반은 컴파일 오류다: `field 'p' is private in 'A'`, `field 'p' is protected in 'A'`, `constructor of 'A' is private in 'A'`(생성자를 `private`로 두어 팩터리로만 만들게 할 수 있다).

### 필드

```mins
class Stats {
  int hp = 100;               // 인스턴스 필드, 초기화식 가능
  static int count;           // 정적 필드: 클래스 전체에서 하나
  final string name;          // 생성자에서 한 번만 대입 가능
}
```
- 초기화하지 않은 필드는 기본값(`0`, `0.0`, `false`, `""`, `null`).
- **`final` 필드**는 선언 위치 초기화 또는 **생성자 안에서 `this.`로 한 번만** 대입할 수 있다. 그 밖의 대입은 컴파일 오류(`cannot assign to final field 'q' here (only a constructor of 'A' may, once, via this)`).

### 메서드

```mins
class Counter {
  int n;
  void inc() { n++; }
  int get() { return n; }
  static Counter make() { return new Counter(); }
}
```
- **가상 디스패치**: 인스턴스 메서드는 기본적으로 가상이다. 하위 클래스가 같은 시그니처로 다시 정의하려면 **`override`를 반드시** 써야 한다(`'g()' overrides 'A.g()'; add the 'override' modifier`). 인터페이스 메서드의 구현은 `override`가 필요 없다.
- `final` 메서드는 override할 수 없다(`cannot override final method`), `final` 클래스는 상속할 수 없다(`cannot extend final class`).
- 정적 메서드에서 인스턴스 멤버를 쓰면 컴파일 오류(`instance method 'g' cannot be called from a static context`).
- 오버로딩과 호출 해석은 [2편](02-language-basics.md#8-함수메서드).

### 생성자와 초기화 순서

```mins
public native class Out { public static native void print(string s); }

class Trace {
  static string log = "";
  static int note(string tag) { log += tag + " "; return 1; }
}

class Animal {
  int a = Trace.note("Animal.field");
  Animal() { Trace.note("Animal.ctor"); }
  Animal(string kind) { this(); Trace.note("Animal(" + kind + ")"); }
}

class Dog extends Animal {
  int b = Trace.note("Dog.field");
  Dog() { super("dog"); Trace.note("Dog.ctor"); }
}

class Config {
  static int first = Trace.note("Config.first");
  static int second = Trace.note("Config.second");
}

class Main {
  static int main() {
    new Dog();
    Out.print(Trace.log);
    return 0;
  }
}
```
실행 출력:
```
Config.first Config.second Animal.field Animal.ctor Animal(dog) Dog.field Dog.ctor 
```

규칙(위 출력으로 확인):
1. **정적 초기화는 로드 시점에 한 번**, 선언 순서대로 모두 실행된다. 지연 초기화가 아니라서 사용하지 않는 `Config`의 정적 필드도 가장 먼저 실행되었다(호스트가 `VM::initialize`를 호출할 때).
2. `new Dog()`에서: **상위 클래스의 필드 초기화 → 상위 생성자 본문 → 하위 클래스의 필드 초기화 → 하위 생성자 본문** 순이다.
3. 생성자는 첫 문장으로 `this(...)`(같은 클래스의 다른 생성자) 또는 `super(...)`(상위 생성자)를 부를 수 있다. 생략하면 **상위의 인자 없는 생성자가 암묵적으로** 호출된다. 상위에 인자 없는 접근 가능한 생성자가 없으면 컴파일 오류(`class 'B' needs a constructor: base class 'A' has no accessible no-argument constructor`).
4. 생성자를 하나도 쓰지 않으면 기본 생성자가 만들어져 상위 기본 생성자를 부르고 필드 초기화식을 실행한다.
5. **생성자 안에서 가상 메서드를 호출하면 하위 클래스가 아직 초기화되기 전**이다(하위 필드 초기화가 상위 생성자 *뒤*이므로). 확인: 상위 생성자가 `v()`를 부르면 하위가 `x = 7`을 반환하도록 재정의했어도 `A ctor sees v=0`이 출력된다. 생성자에서는 재정의될 수 있는 메서드를 부르지 않는다.

### 추상 클래스

```mins
abstract class Shape {
  abstract int sides();                 // 본문 없음 — 하위 클래스가 override해야 한다
  int twice() { return sides() * 2; }
}
```
추상 클래스는 `new`할 수 없다(`cannot instantiate abstract class`).

---

## 3. 인터페이스

```mins
interface Damageable { void takeDamage(int amount); bool isDead(); }
interface Scored extends Named { int score(); }     // 인터페이스끼리 확장 가능
```
- **메서드 선언만** 가질 수 있다. 필드는 불가(`interfaces can only declare methods`). 기본 구현(default method)도 없다.
- 구현 클래스가 메서드를 빠뜨리면 컴파일 오류: `class 'C' does not implement 'I.m()'`.
- 인터페이스 타입의 변수로 호출하면 동적으로 디스패치된다(`CallInterface`). 타입 검사: `o is Damageable`, 캐스트: `o as Damageable`.

---

## 4. 열거형

```mins
enum Team { Neutral, Red = 5, Blue }      // 값을 지정하지 않으면 직전 값 + 1 → Blue = 6
```
- 값은 정수이며 `Team.Red`처럼 접근한다. **문자열로 변환하면 이름**이다(`"" + Team.Blue` → `Blue`).
- 정수 값은 `Team.Red as int`(→ `5`), 정수에서 열거형은 `6 as Team`. **정의되지 않은 값**(`99 as Team`)은 `CastError`(`99 is not a value of enum Team`).
- `switch`의 `case`에 `Team.Red`처럼 쓸 수 있다. `==`/`!=` 비교 가능.
- 스크립트 컴포넌트의 열거형 필드는 `.minseen`에 **정수 값**으로 저장된다(5편).

---

## 5. 타입 검사와 캐스트

| 구문 | 의미 |
|---|---|
| `x is T` | `x`가 `T`의 인스턴스(하위 클래스·구현 클래스 포함)인가. **`null`이면 `false`** |
| `x as T` | 참조 캐스트(클래스·인터페이스·열거형·`int`/`float`/`string` 변환). 실패하면 `mingol.CastError`. `null`은 그대로 `null`이 된다 |
| 업캐스트 | `Actor a = new Soldier(...)`는 **암시적**으로 허용 |
| 다운캐스트 | `Soldier s = a as Soldier;` — 반드시 `as`로 명시 |

```mins
Object o = actors[1];
bool ok = o is Soldier && o is Damageable;   // 위 예제의 출력: "is Soldier: true, is Damageable: true"
```

---

## 6. 연산자 오버로딩과 인덱서

클래스는 **`static` 메서드**로 연산자를 정의한다(위 예제의 `Vec2`).
```mins
static Vec2 operator+(Vec2 a, Vec2 b) { ... }
static Vec2 operator*(Vec2 a, float k) { ... }
static float operator[](Vec2 v, int i) { ... }              // 인덱서 getter: (객체, 인덱스)
static void  operator[](Vec2 v, int i, float val) { ... }   // 인덱서 setter: (객체, 인덱스, 값)
```
규칙(컴파일러가 강제하는 것):
- 연산자는 **`static`**이어야 하고 매개변수 중 **하나는 자기 클래스 타입**이어야 한다. `ref` 매개변수는 불가, `override`/`abstract` 등의 수정자 불가.
- 단항 `!`, `~`는 매개변수 1개, `+`/`-`는 1개(단항) 또는 2개(이항), 나머지 이항 연산자는 정확히 2개.
- 비교 연산자(`== != < > <= >=`)는 **`bool`을 반환**해야 한다. 그 외 연산자는 값을 반환해야 한다(`void` 불가). 인덱서 getter는 값을, setter는 `void`를 반환한다.
- 복합 대입(`v += w`, `v *= 3.0`)은 해당 이항 연산자로부터 자동으로 만들어진다. `!=`는 `==`를 정의하면 쓸 수 있다.
- 위 예제에서 확인: `new Vec2(1, 2) + new Vec2(3, 4) * 2.0` = `(7.0, 10.0)`, `v[1] += 0.5` 후 `(7.0, 10.5)`.

> 참조 타입의 `==`는 연산자를 정의하지 않으면 **참조 동일성**이다(확인: 서로 다른 `new P()` 둘은 `false`, 같은 객체를 가리키는 변수끼리는 `true`). 값처럼 비교하고 싶으면 `operator==`를 정의한다.

---

## 7. 객체와 메모리

- 객체는 힙에 할당되고 **GC가 수거**한다. 소멸자/`finalize`는 없다. 자원 정리는 `onDestroy` 같은 명시적 콜백이나 호스트 네이티브(`native` 데이터 + finalizer)로 한다.
- 변수 대입·인자 전달은 **참조 복사**다. 객체 복제 메서드는 기본 제공되지 않는다(필요하면 `clone()`을 직접 작성).
- 순환 참조도 GC가 수거한다.
- 같은 이름의 타입을 두 번 선언하면 오류(`type 'A' is already declared`).
- **`Object`는 클래스 인스턴스의 공통 기반**이다. 배열·`string`·`int` 등은 `Object`가 아니므로 `Object o = [1, 2];`는 컴파일 오류다(`cannot use 'int[]' as 'mingol.Object'`). 박스 클래스(`IntBox`, `FloatBox`, `BoolBox`, `StringBox`)로 감싸서 `Object`로 다룬다(리플렉션 `invoke`의 인자가 이 방식).

---

## 8. 요약: 컴파일러가 잡아 주는 객체지향 실수

| 실수 | 진단(실제 메시지) |
|---|---|
| `override` 누락 | `'g()' overrides 'A.g()'; add the 'override' modifier` |
| final 메서드 재정의 | `cannot override final method 'A.g()'` |
| final 클래스 상속 | `cannot extend final class 'A'` |
| 추상 클래스 인스턴스화 | `cannot instantiate abstract class 'A'` |
| 인터페이스 메서드 미구현 | `class 'C' does not implement 'I.m()'` |
| 상위에 기본 생성자 없음 | `class 'B' needs a constructor: base class 'A' has no accessible no-argument constructor` |
| private/protected 접근 | `field 'p' is private in 'A'` |
| 정적 문맥에서 인스턴스 호출 | `instance method 'g' cannot be called from a static context` |
| 순환 상속 | `cyclic inheritance involving 'A' (A -> A)` |
| 인터페이스에 필드 | `interfaces can only declare methods` |

다음: [4편 고급 기능](04-advanced-features.md)
