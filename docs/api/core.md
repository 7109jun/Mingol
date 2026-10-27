# API: core (`mingol/core/*.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/core` · CMake 타깃 `Mingol::Core` · 네임스페이스 `mingol`(JSON은 `mingol::json`, 파일은 `mingol::fs`).
의존: 표준 라이브러리 + Threads. **다른 모든 모듈의 기반**이며 어떤 모듈에도 의존하지 않는다.

## 목차
[error.hpp](#errorhpp--에러와-result) · [assert.hpp](#asserthpp) · [platform.hpp](#platformhpp) · [flags.hpp](#flagshpp) · [fs.hpp](#fshpp) · [hash.hpp](#hashhpp) · [uuid.hpp](#uuidhpp) · [json.hpp](#jsonhpp) · [log.hpp](#loghpp) · [signal.hpp](#signalhpp) · [memory.hpp](#memoryhpp) · [job_system.hpp](#job_systemhpp) · [math.hpp](#mathhpp) · [math_decompose.hpp](#math_decomposehpp)

---

## error.hpp — 에러와 `Result`
엔진 전체의 에러 전달 규약. 예외를 쓰지 않는다.

### `enum class ErrorCode : uint32_t`
`Ok, InvalidArgument, NotFound, AlreadyExists, Io, Corrupt`(구조 검증 실패)`, VersionMismatch`(데이터/ABI 버전 미지원)`, AuthFailed`(태그/키 불일치)`, OutOfRange, OutOfMemory, Unsupported, Timeout, OutOfDate`(표면이 바뀌어 스왑체인 재생성 필요)`, Internal`.
`const char* toString(ErrorCode) noexcept`.

### `struct Error`
| 멤버 | 설명 |
|---|---|
| `ErrorCode code` | 기본 `Internal` |
| `std::string message` | 사람이 읽는 설명 |
| `Error(ErrorCode, std::string)` | |
| `std::string describe() const` | `"Code: message"` |

`Error makeError(ErrorCode, std::format_string<Args...>, Args&&...)` — `std::format` 문법으로 메시지 생성.

### `template <class T> class [[nodiscard]] Result`
값 또는 `Error`. `Result<void>` 특수화 있음(값 없음).
| 멤버 | 설명 |
|---|---|
| `Result(T)`, `Result(Error)` | 암시적 변환 — `return value;` / `return makeError(...);` |
| `bool ok() const`, `explicit operator bool` | 성공 여부 |
| `T& value()` (`&`, `const&`, `&&` 오버로드) | **`ok()`가 아니면 abort**(`MINGOL_VERIFY`) |
| `const Error& error() const&` | **`ok()`이면 abort** |
| `T valueOr(T fallback) const&` | 실패 시 대체값 |

### 전파 매크로
```cpp
Result<Config> load(const std::filesystem::path& p) {
  MINGOL_TRY_ASSIGN(std::string text, fs::readTextFile(p));   // 실패하면 Error를 그대로 반환
  MINGOL_TRY(validate(text));                                  // Result<void>
  return parse(text);                                          // Result<Config> → 그대로
}
```
- `MINGOL_TRY(expr)` — `expr`이 실패면 그 `Error`를 호출 함수의 `Result<U>`로 반환(어떤 `U`든 가능).
- `MINGOL_TRY_ASSIGN(decl, expr)` — 성공이면 `decl = std::move(값)`. `decl`은 선언문(`auto x`, `std::string s` 등).
- 주의: 두 매크로 모두 **여러 문장으로 확장**되므로 `if (...) MINGOL_TRY(...);`처럼 중괄호 없는 분기 본문에 쓰지 말 것(`MINGOL_TRY`는 `do/while`이라 안전하지만 `MINGOL_TRY_ASSIGN`은 선언을 만든다).

---

## assert.hpp
- `MINGOL_VERIFY(expr)` — **항상 검사**. 실패하면 로그를 남기고 `abort`(`[[noreturn]] detail::assertFail`).
- `MINGOL_VERIFY_MSG(expr, msg)` — 메시지 포함.
- `MINGOL_ASSERT(expr)` — 디버그(`!NDEBUG`)에서만 `VERIFY`, 릴리스에서는 아무것도 안 함.
용도: 위반이 곧 미정의 동작인 **내부 불변식**. 사용자/파일 입력 검사에는 쓰지 말고 `Result`로 돌려준다.

## platform.hpp
컴파일 타임 매크로: `MINGOL_PLATFORM_WINDOWS` / `MINGOL_PLATFORM_LINUX`(그 외는 `#error`), `MINGOL_COMPILER_MSVC` / `MINGOL_COMPILER_GNU`, `MINGOL_NOINLINE`, `MINGOL_EXPORT`(`extern "C"` + dllexport/visibility), `MINGOL_CONCAT(a,b)`, `MINGOL_UNIQUE(name)`(`__COUNTER__`로 고유 이름).

## flags.hpp
`MINGOL_ENUM_FLAGS(E)` — `enum class E`가 정의된 네임스페이스에서 한 번 호출하면 `|`, `&`, `|=`, `hasFlag(value, flag)`(flag의 **모든 비트**가 켜졌는지; flag가 0이면 false), `anyFlag(value)`가 생긴다.
```cpp
enum class BufferUsage : uint32_t { None = 0, Vertex = 1, Index = 2 };
MINGOL_ENUM_FLAGS(BufferUsage)
auto u = BufferUsage::Vertex | BufferUsage::Index;  hasFlag(u, BufferUsage::Index);  // true
```

## fs.hpp — `mingol::fs`
| 함수 | 설명 |
|---|---|
| `readFile(path, maxSize = 4 GiB)` → `Result<vector<uint8_t>>` | 실패: `NotFound` / `Io` / `OutOfRange`(maxSize 초과) |
| `readTextFile(path, maxSize)` → `Result<string>` | |
| `writeFileAtomic(path, span<const uint8_t>)` → `Result<void>` | **크래시 안전 저장**: 같은 폴더에 임시 파일을 쓰고 디스크에 flush한 뒤 rename. 실패 시 원본 유지, 임시 파일 삭제 |
| `writeTextFileAtomic(path, string_view)` | |
Windows에서는 대상이 다른 프로세스/핸들에 의해 열려 있으면 rename이 실패할 수 있다.

## hash.hpp
- `uint32_t crc32(span<const uint8_t>, uint32_t seed = 0)` — IEEE 802.3(역방향, 0xEDB88320). 이전 결과를 `seed`로 넘기면 이어서 계산.
- `constexpr uint64_t fnv1a64(string_view)` — 컴파일 타임 이름 해시 가능. **암호학적 해시가 아님.**

## uuid.hpp
`struct Uuid { array<uint8_t,16> bytes; }` — RFC 4122 v4(랜덤). `Uuid::generate()`, `Uuid::parse("xxxxxxxx-xxxx-…")`→`optional<Uuid>`(대소문자 무시), `toString()`, `isNil()`, `==`/`<=>`, `std::hash<Uuid>`.

## json.hpp — `mingol::json`
엄격한 RFC 8259 JSON 파서/라이터. `.mindev`/`.minseen`이 사용한다.
설계: **정수와 실수를 구분**(`.`/`e`/`E`가 없고 int64에 들어가면 `Int`) · 객체는 **삽입 순서 유지**(저장 파일이 안정적) · **중복 키, 후행 쉼표, 주석, 짝 없는 서로게이트, 잘못된 UTF-8은 오류** · 중첩 깊이 제한(기본 256).

### `class Value`
생성: `null`, `bool`, `int`, `int64_t`, `double`, 문자열류, `Array`(= `vector<Value>`), `Object`.
판별: `isNull/isBool/isInt/isFloat/isNumber/isString/isArray/isObject`.
접근: `asBool/asInt/asFloat/toDouble`(정수도 double로)`/asString/asArray/asObject` — **타입이 다르면 abort**, 먼저 `is*()`로 확인.
`operator==`: 구조 비교, **`1 != 1.0`**, 객체 키 순서도 비교에 포함.

### `class Object`
`size/empty/keyAt(i)/valueAt(i)/find(key)`(없으면 `nullptr`)`/contains/set(key, value)`(있으면 위치 유지하며 교체)`/erase/appendUnique`(파서 전용; 중복 키가 없다고 호출자가 보장). 조회는 선형 탐색(작은 문서용).

### 함수
```cpp
Result<Value> parse(std::string_view text, const ParseOptions& = {});   // maxDepth=256
Result<std::string> write(const Value&, const WriteOptions& = {});      // pretty=true, maxDepth=256
```
- `parse`: 문서 하나만(앞뒤 공백 허용), UTF-8 BOM 무시. 오류는 `Corrupt`, 메시지 `"줄:열: 이유"`.
- `write`: 결정적 출력. `NaN`/`Infinity`, 잘못된 UTF-8, 깊이 초과는 `InvalidArgument`. pretty는 2칸 들여쓰기 + 끝 개행.

```cpp
auto v = json::parse(R"({"name":"Demo","size":3})");
if (!v.ok()) return v.error();
const json::Object& o = v.value().asObject();
std::string name = o.find("name")->asString();
```

## log.hpp
`enum class LogLevel { Trace, Debug, Info, Warn, Error, Fatal, Off }`.
- `Logger::get()` — 프로세스 전역. 기본: `ConsoleSink` + 레벨 `Info`. `setLevel/level/enabled`, `addSink/removeSink/clearSinks/flush`, `log(level, category, message)`, `logf(level, category, format, args...)`.
- 싱크(`LogSink::write(const LogRecord&)`는 **로거 뮤텍스를 잡은 채** 호출 → 싱크 내부에 락 불필요): `ConsoleSink`, `FileSink(path, append=true)`(`isOpen()`), `RingBufferSink(capacity)`(`snapshot()`/`count()` 스레드 안전; 에디터 콘솔이 사용).
- `formatLogRecord` → `"12:34:56.789 [INFO ] [category] message"`(UTC).
- 매크로 `MINGOL_LOG_TRACE|DEBUG|INFO|WARN|ERROR|FATAL(category, fmt, args...)` — 레벨이 꺼져 있으면 **인자를 평가하지 않는다**. `Fatal`은 로그만 하고 종료하지 않는다.
```cpp
MINGOL_LOG_INFO("player", "loaded {} files", n);
```

## signal.hpp
단일 스레드 멀티캐스트 델리게이트.
- `Signal<Args...>`: `connect(fn)`→`Connection`, `emit(args...)`, `slotCount()`. **emit 중 connect/disconnect(자기 자신 포함) 가능**; emit 중 연결된 핸들러는 다음 emit부터 호출. `Signal`이 먼저 파괴돼도 `Connection::disconnect()`는 안전.
- `Connection`: 복사 가능, `disconnect()`(멱등), `connected()`.
- `ScopedConnection`: 이동 전용, 소멸 시 자동 해제(`Connection`에서 암시적 변환).
```cpp
Signal<int> onHit;
ScopedConnection c = onHit.connect([](int dmg) { /* ... */ });
onHit.emit(5);
```
스레드 안전하지 않음(플랫폼 이벤트 `win.events()`가 이것을 사용).

## memory.hpp
- `LinearAllocator(capacity)`: 범프 할당기, 스레드 안전 아님. `allocate(size, align)`(안 맞으면 `nullptr`; align은 2의 거듭제곱)`/create<T>(args)`(**소멸자가 자명한 타입만**)`/reset()/used()/capacity()`. 개별 해제 없음.
- `Handle{index, generation}`: 세대 핸들(`valid()`, `==`). 슬롯이 지워지면 이전 핸들은 **새 객체와 절대 겹치지 않고** 무효가 된다.
- `SlotMap<T>`: `insert(T)`→`Handle`, `erase(h)`→bool, `get(h)`→`T*`(무효면 `nullptr`)`/contains/size/clear()`(모든 핸들 무효화)`/forEach(f(Handle, T&))`(**콜백 안에서 insert/erase 금지**). `get()` 포인터는 `insert`(저장소 확장)나 그 요소의 `erase` 후 무효.
- `ScopeGuard<F>`: 스코프 종료 시 `f()` 실행, `dismiss()`로 취소. 이동 가능.

## job_system.hpp
고정 크기 워커 풀 `JobSystem(threadCount = 0)`(0 = `max(1, hw_concurrency-1)`). 소멸자는 큐를 모두 처리하고 join.
- `submit(f)` → `std::future<R>` (`[[nodiscard]]`; 예외는 future로 전파).
- `parallelFor(begin, end, grain, body(chunkBegin, chunkEnd))` — 워커 + 호출 스레드가 함께 처리하고 **끝날 때까지 블록**. job 안에서 호출해도 안전(호출자가 항상 진행). 첫 예외를 다시 던진다.
- `waitIdle()` — 큐가 비고 실행 중인 job이 없을 때까지.

## math.hpp
**규약**: 오른손 좌표계(+Y 위, **−Z 전방**), 열 우선(column-major) `Mat4`, 열 벡터(`v' = M * v`), Vulkan 클립 공간(깊이 0..1, NDC에서 +Y 아래).
- 상수/함수: `kPi`, `kEpsilon`, `radians`, `degrees`, `lerp`, `clamp`.
- `Vec2/Vec3/Vec4`(float 멤버 `x,y,z,w`): 연산자 `+ - *`(스칼라·성분곱) `/` `+= -= *=` `==`. 자유 함수: `dot`, `cross`, `lengthSq`, `length`, `normalize`(영벡터면 영벡터 — NaN 없음), `lerp`, `nearlyEqual`.
- `Quat{x,y,z,w}`(기본 항등): `fromAxisAngle(axis, rad)`, `*`(합성), `conjugate`, `dot`, `normalize`(영이면 항등), `rotate(q, v)`, `slerp(a, b, t)`(짧은 경로, 거의 평행하면 nlerp).
- `Mat4`: 원소 `(row r, col c)` = `m[c*4 + r]`, `at(row,col)`. 정적 생성: `identity`, `translation`, `scale`, `rotation(Quat)`, `trs(t, q, s)`(= T·R·S), `lookAt(eye, target, up)`(오른손, 뷰 공간에서 −Z를 봄), `perspective(fovY_rad, aspect, near, far)`(**Y 반전, 깊이 0..1**), `orthographic(l, r, b, t, near, far)`(Y 반전). 연산: `Mat4*Mat4`, `Mat4*Vec4`, `transformPoint`(w=1), `transformDirection`(w=0), `transpose`, `inverse(in, out)`→**bool**(특이 행렬이면 false, `out` 불변).
```cpp
Mat4 proj = Mat4::perspective(radians(60), 16.0f/9.0f, 0.1f, 100.0f);
// 화면 y = (ndc.y * 0.5 + 0.5) * height   (추가 뒤집기 없음)
```

## math_decompose.hpp
- `decompose(const Mat4&, Vec3& t, Quat& r, Vec3& s)` — 아핀 행렬 분해. 거울 반사는 X 스케일에 흡수, **전단(shear)은 버림**, 스케일 0이면 항등 회전.
- `Quat quatFromMatrix(const Mat4&)` — 상단 3×3(직교 가정) → 정규화된 쿼터니언.
