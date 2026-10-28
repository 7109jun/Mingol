# 언어 기초

문서 라이선스: CC BY 4.0. Mingol Script 문서 시리즈 2편. 이전: [1편 소개](01-overview.md) · 다음: [3편 타입과 객체](03-types-and-objects.md).

이 편은 문법과 기본 타입, 제어 흐름, 문자열·배열, 모듈을 다룬다. 모든 코드는 실제 컴파일러/VM에서 실행해 출력을 확인했다.

## 1. 한 번에 보는 예제

```mins
public native class Out { public static native void print(string s); }

// Mingol Script: a statically typed, class-based language.
class Main {
  static int square(int x) { return x * x; }

  static int main() {
    // variables and types
    int hp = 100;
    float speed = 2.5;
    bool alive = true;
    string name = "Hero";
    var gold = 30;                    // type inferred
    Out.print(name + ": hp=" + hp + " speed=" + speed + " alive=" + alive + " gold=" + gold);

    // arithmetic: int is 64-bit and wraps; float is 64-bit
    int big = 9223372036854775807;
    Out.print("wrap: " + (big + 1));
    Out.print("div: " + (7 / 2) + " mod: " + (7 % 3) + " fdiv: " + (7.0 / 2.0));
    Out.print("shift: " + (1 << 70));
    Out.print("f2i: " + (3.99 as int) + " i2f: " + (3 as float));

    // control flow
    int sum = 0;
    for (int i = 1; i <= 10; i++) {
      if (i % 2 == 0) continue;
      sum += i;
    }
    Out.print("odd sum = " + sum);

    int n = 0;
    while (n < 3) { n++; }
    do { n--; } while (n > 0);
    Out.print("n = " + n);

    // arrays
    int[] a = [5, 3, 8];
    int[] b = new int[4];
    b[2] = square(a[1]);
    int total = 0;
    for (int v : a) { total += v; }
    Out.print("a.length=" + a.length + " b[2]=" + b[2] + " total=" + total);
    int[][] grid = new int[2][];
    grid[0] = [1, 2];
    Out.print("grid[1] is null: " + (grid[1] == null));

    // strings are UTF-8 byte sequences
    string s = "abc";
    Out.print("len=" + s.length + " s[1]=" + s[1] + " same=" + (s == "ab" + "c"));
    string hangul = "\u{AC00}";
    Out.print("hangul bytes=" + hangul.length);

    // switch has no fallthrough; several values per case are allowed
    int code = 3;
    switch (code) {
      case 1: Out.print("one"); break;
      case 2, 3: Out.print("two or three"); break;
      default: Out.print("other");
    }

    // ternary and compound assignment
    int hpAfter = hp > 50 ? hp - 10 : 0;
    hpAfter *= 2;
    Out.print("hpAfter=" + hpAfter);
    return sum;
  }
}
```

실행 출력:
```
Hero: hp=100 speed=2.5 alive=true gold=30
wrap: -9223372036854775808
div: 3 mod: 1 fdiv: 3.5
shift: 64
f2i: 3 i2f: 3.0
odd sum = 25
n = 0
a.length=3 b[2]=9 total=16
grid[1] is null: true
len=3 s[1]=98 same=true
hangul bytes=3
two or three
hpAfter=180
=> 25
```

(반환값 `25`는 `Main.main()`의 `return sum`.) `Out.print`는 검증용 네이티브이며 엔진에서는 `Log.info`.

---

## 2. 프로그램 구조

- 소스 파일은 UTF-8 텍스트(`.mins`). 최상위에는 `class`, `interface`, `enum` 선언만 둘 수 있다(전역 함수·변수는 없다: `int g = 1;`은 `expected 'class', 'interface' or 'enum'` 오류). 진입점은 언어 차원에서 정해져 있지 않다 — 호스트가 어떤 클래스의 메서드를 호출할지 정한다(엔진에서는 `Script`를 상속한 클래스의 콜백).
- 주석: `// 한 줄`, `/* 여러 줄 */`.
- 문장은 `;`로 끝나고 블록은 `{ }`. 식별자는 대소문자를 구분한다.
- 여러 파일은 하나의 컴파일 단위 집합으로 함께 컴파일되며, 파일 사이의 순서에 의존하지 않는다(클래스 선언은 앞뒤 어디서든 참조 가능).

### 모듈과 import

기본 모듈(선언 없음)과 `module` 선언 파일이 있다. 다른 모듈의 클래스는 **`public`** 으로 선언되어야 한다.
```mins
module game.util;
public class MathUtil {
  public static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}
```
```mins
import game.util;
public native class Out { public static native void print(string s); }
class Main { static int main() { Out.print("" + MathUtil.clamp(15, 0, 10) + " " + game.util.MathUtil.clamp(-3, 0, 10)); return 0; } }
```
(`modules.mins`, `modmain.mins`를 함께 컴파일해 실행)
```
10 0
```
`import game.util;` 후에는 `MathUtil`로, 임포트 없이도 정규화된 이름 `game.util.MathUtil`로 접근할 수 있다. 프렐류드(모듈 `mingol`)의 `Object`, `Exception` 등은 항상 보인다.

---

## 3. 타입

### 기본 타입

| 타입 | 설명 | 기본값 |
|---|---|---|
| `int` | **64비트 부호 있는 정수**. 오버플로는 **래핑**(`9223372036854775807 + 1` → `-9223372036854775808`) | `0` |
| `float` | **64비트** IEEE 754 배정밀도 | `0.0` |
| `bool` | `true` / `false` | `false` |
| `string` | 불변 **UTF-8 바이트열**. **`null`이 될 수 없다**(`string s = null;`은 컴파일 오류) | `""` |
| `T[]` | 배열(참조 타입, `null` 가능) | `null` |
| 클래스/인터페이스 | 참조 타입, `null` 가능 | `null` |
| `fn(A, B): R` | 함수 타입 | `null` |
| `T*` | 포인터(`&x`로 얻음). 4편 참조 | `null` |

`void`는 반환 타입으로만 쓴다. 필드, 배열 요소, **초기화 없이 선언한 지역 변수**는 모두 위 기본값을 가진다(확인: `int x; float y; bool z; string s;` → `0 0.0 false ""`). 선언과 함께 초기화하는 것이 관례다.

> 실제로 확인한 기본값: `new D()` 의 `int/float/bool/string` 필드 → `0 0.0 false ""`, 배열·객체 필드 → `null`.

### 리터럴

| 종류 | 예 | 비고 |
|---|---|---|
| 정수 | `42`, `0xFF`, `0b101`, `1_000` | 자릿수 구분 `_` 가능 (`0xFF + 0b101 + 1_000` = 1260) |
| 실수 | `2.5`, `1e3`, `1.5e3` | **`.5`처럼 앞의 `0` 생략은 불가** — `0.5`로 쓴다 |
| 불리언 | `true`, `false` | |
| 문자열 | `"text"` | 이스케이프 `\n \t \" \\`, 유니코드 `\u{AC00}` |
| 배열 | `[1, 2, 3]`, `[]` | |
| null | `null` | 문자열에는 쓸 수 없다 |

**문자 리터럴(`'a'`)은 없다.** 문자 하나는 문자열(`"a"`)이거나, 바이트 값(`s[1]` → `int`)이다.

### 타입 변환

| 변환 | 방법 | 비고 |
|---|---|---|
| `int` → `float` | **암시적** (`float f = 3;`) | |
| `float` → `int` | `x as int` | **0 방향 절단**, 범위를 넘으면 포화, `NaN` → `0` (`1.0 / 0.0 as int` → `9223372036854775807`, `0.0 / 0.0 as int` → `0`) |
| 숫자/불리언 → `string` | `n as string`, 또는 `"" + n` | `+`에 문자열이 있으면 다른 피연산자를 문자열로 변환해 이어붙임 |
| 참조 → 하위 타입 | `obj as T` | 실패 시 `CastError`. `null`은 그대로 `null` |
| 타입 검사 | `obj is T` | `null`이면 `false` |

`as`는 산술 안에서는 괄호가 필요하다: `(x as int) + 1`.

### 타입 추론

`var`로 지역 변수의 타입을 초기화식에서 추론한다: `var gold = 30;`(`int`). 초기화식이 필수다.

---

## 4. 연산자

우선순위는 C 계열과 같다: 후위(`a.b`, `a[i]`, `f()`, `x++`) → 단항(`-`, `!`, `~`, 전위 `++/--`, `*p`, `&x`) → `* / %` → `+ -` → `<< >>` → `< <= > >= is` → `== !=` → `&` → `^` → `|` → `&&` → `||` → `?:` → 대입(`= += -= *= /= %= &= |= ^= <<= >>=`). **비교/등치가 비트 연산자보다 먼저 결합**하므로 `1 | 2 == 2`는 `1 | (2 == 2)`가 되어 타입 오류다 — 괄호를 쓴다. `as`는 산술 안에서 괄호가 필요하다. (확인: `1 + 2 * 3` = 7, `7 - 2 - 1` = 4, `1 << 1 + 1` = 4)

| 영역 | 동작 (확인됨) |
|---|---|
| 정수 나눗셈 | **0 방향으로 절단**: `7 / 2` = `3`, `-7 / 2` = `-3`, `-7 % 3` = `-1`. **0으로 나누기는 `DivideByZeroError`** |
| 실수 | `7.0 / 2.0` = `3.5`, `7.5 % 2.0` = `1.5`. 0.0으로 나누면 `inf`/`nan`(예외 아님) |
| 시프트 | 시프트 양은 **63으로 마스크**: `1 << 70` = `1 << 6` = `64` |
| 비트 | `a & 3`, `a | 8`, `a ^ 1`, `~a`, `a >> 1`(산술 시프트) |
| 논리 | `&&`, `||`는 **단락 평가** |
| 비교 | `int`/`float`/`string`(사전식 바이트 비교)/참조 동일성. 문자열 `==`는 **내용 비교** |
| 증감 | `i++`, `++i`, `i--`, `--i`는 `int` 변수/필드/배열 요소에 |

실행 확인(`a = 5`):
```mins
public native class Out { public static native void print(string s); }
class Main {
  static int f() {
    int a = 5; bool b = a > 3 && a < 10 || false;
    Out.print("" + (a & 3) + (a | 8) + (a ^ 1) + (~a) + (a >> 1) + (!b));
    int i = 0; i++; ++i; i--;
    Out.print("" + (-7 / 2) + " " + (-7 % 3) + " " + (7.5 % 2.0));
    var q = (2 as float) / 3;
    Out.print("" + q);
    Out.print("" + (1.0 / 0.0 as int) + " " + (0.0/0.0 as int));
    return 0;
  }
}
```
```
1134-62false
-3 -1 1.5
0.6666666666666666
9223372036854775807 0
```
(`"" + (a & 3) + (a | 8) + (a ^ 1) + (~a) + (a >> 1) + (!b)` → `1`,`13`,`4`,`-6`,`2`,`false`)

`float → string` 변환은 **왕복 가능한 최단 표현**을 쓰며 지수에 선행 0이 없다(`0.1 + 0.2` → `0.30000000000000004`, `-1.5e-7`). `nan`, `inf`, `-inf`가 출력된다.

---

## 5. 제어 흐름

```mins
if (cond) { ... } else if (other) { ... } else { ... }
while (cond) { ... }
do { ... } while (cond);
for (int i = 0; i < n; i++) { ... }       // 초기화/조건/증분 각각 생략 가능: for (;;) { ... }
for (T item : array) { ... }              // foreach: 배열 전용
break;  continue;  return value;
```

- 조건식은 반드시 `bool`이다(정수를 조건으로 쓸 수 없다).
- **`for` 초기화부에 변수를 둘 이상 선언할 수 없다.** 언어 전체에서 **한 선언문에 변수 하나**(`int a = 1, b = 2;`는 오류: `declare one variable per statement`).
- 같은 함수 안에서 같은 이름을 다시 선언하면 오류(`already declared in this function`). **중첩 블록에서도 바깥 지역 변수를 가릴 수(shadowing) 없다.**
- 루프 안에서 선언한 변수를 클로저가 캡처하면 **반복마다 새 변수**를 얻는다(4편).

### switch

```mins
switch (code) {
  case 1: ...; break;
  case 2, 3: ...; break;      // 한 case에 값 여러 개
  default: ...
}
```
- **폴스루가 없다.** 각 case 본문이 끝나면 switch를 빠져나간다.
- 대상은 `int`, `string`, `enum`. `case` 라벨은 **리터럴**(정수/문자열) 또는 열거형 상수여야 한다(변수는 `case label must be an integer literal` 오류).
- 본문이 빈 비-`default` case는 오류다(`case 1: case 2:` 대신 `case 1, 2:`). `switch` 안의 `continue`는 바깥 루프에 적용된다.

### 컴파일러가 거부하는 것
- **도달 불가 코드**(`return` 뒤 문장), **모든 경로에서 값을 반환하지 않는 메서드**, 존재하지 않는 이름/메서드, 타입 불일치 — 모두 컴파일 오류.

---

## 6. 문자열

- **불변**이며 **UTF-8 바이트열**이다. `length`와 인덱싱은 **바이트 단위**: `"\u{AC00}".length` = `3`. 한글 같은 비ASCII 문자의 개수가 필요하면 호스트 API/플러그인이 필요하다(기본 제공 없음).
- `s[i]` → `int`(바이트 값, 예: `"abc"[1]` = `98`). 범위를 벗어나면 `IndexOutOfRangeError`.
- `+` 연결, `+=`, `==`/`!=`/`<` 등 비교(바이트 사전식). 문자열 메서드(부분 문자열, 검색, 분할 등)는 **언어에 내장되어 있지 않다**. 필요하면 `native` 플러그인으로 추가한다.
- 문자열 `switch`/`case` 사용 가능.

---

## 7. 배열

```mins
int[] a = [5, 3, 8];          // 리터럴
int[] b = new int[4];         // 길이 4, 0으로 초기화
int[][] grid = new int[2][];  // 배열의 배열: 안쪽은 null
string[] names = new string[2];   // 요소는 ""
(fn():int)[] fs = new (fn():int)[3];   // 함수 타입의 배열은 괄호가 필요
```
- `a.length`, `a[i]`, `a[i] = v`, `for (int v : a)`.
- 범위 밖 접근은 `IndexOutOfRangeError`, 음수 길이로 생성하면 `ArgumentError`, 너무 큰 배열은 `OutOfMemoryError`, `null` 배열의 `length`는 `NullReferenceError`.
- 배열은 **참조 타입**이다: 대입·전달 시 복사되지 않는다. 크기 변경(`push` 등)은 없다 — 새 배열을 만들어 복사한다.
- 리스트·맵 같은 컬렉션 클래스는 기본 제공되지 않는다. 스크립트로 직접 작성하거나(예: 배열 기반 간단한 리스트 클래스) 플러그인으로 제공한다.

---

## 8. 함수(메서드)

모든 함수는 클래스의 메서드다.
```mins
class Util {
  static int square(int x) { return x * x; }
  static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}
```
- **오버로딩**: 이름이 같고 매개변수가 다른 메서드를 둘 수 있다. 가장 잘 맞는 후보를 고르며(`g(1)`→`int`, `g(1.5)`→`float`, `g("s")`→`string`, 객체 → `Object` 오버로드), 모호하면 컴파일 오류(`call to ... is ambiguous`).
- **기본 인자는 없다**(`f(int a = 1)`은 구문 오류; 오버로드로 대신한다).
- 값은 **복사로 전달**(기본 타입)되며 참조 타입은 참조가 전달된다. 호출자의 변수를 바꾸려면 `ref` 매개변수를 쓴다(4편).
- 재귀가 가능하며 깊이가 한도(기본 8192 프레임)를 넘으면 `StackOverflowError`.
- 함수는 값이다: `fn(int): int f = Util.square;`(4편).

---

## 9. 자주 하는 실수

| 증상 | 원인과 해결 |
|---|---|
| `declare one variable per statement` | `int a = 1, b = 2;` 불가 → 두 문장으로 |
| `.5` 에서 `expected an expression` | `0.5`로 쓴다 |
| `'a'` 에서 `unexpected character` | 문자 리터럴 없음 → `"a"` |
| `string s = null;` 오류 | 문자열은 `null`이 될 수 없다. 비어 있음은 `""`로 표현하고 `s.length == 0`으로 검사 |
| `operator '==' cannot be applied to 'string' and 'null'` | 같은 이유 |
| `cannot use 'float' as 'int'` | `(x as int)`로 명시 변환 |
| 반환 누락 / 도달 불가 코드 오류 | 모든 경로에서 `return`, `return` 뒤에 문장 없음 |
| `(x as int) + 1`의 괄호 생략 | `as`는 산술 안에서 괄호 필요 |

다음: [3편 타입과 객체](03-types-and-objects.md)
