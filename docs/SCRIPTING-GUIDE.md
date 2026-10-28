# Mingol Script 작성 가이드

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

언어 명세는 [SCRIPT.md](SCRIPT.md). 본격적인 문서 시리즈는 [script/01-overview.md](script/01-overview.md)(소개·언어 기초·타입과 객체·고급 기능·엔진 연동). 이 문서는 **게임 스크립트를 쓰는 방법**을 다룬다. 예제는 테스트에서 실제로 컴파일·실행되는 형태를 기준으로 한다.

## 첫 스크립트
`scripts/hero.mins`:
```
class Hero extends Script {
  public int hp = 10;
  public float speed = 2.5;
  public int ticks = 0;
  override void onUpdate(float dt) { ticks++; }
}
```
- `Script`를 상속한 클래스가 **컴포넌트**가 된다. 에디터 인스펙터의 *Add Component* 버튼에서 `Script: <클래스>`를 골라 붙이거나, C++에서 `host.addScript(gameObject, "Hero")`.
- **정적이 아닌 데이터 필드**는 접근 수정자와 무관하게 인스펙터에 노출되고 `.minseen`에 저장된다(int/float/bool/string/enum과 그 배열; 객체 타입 필드는 저장되지 않음).
- `gameObject`로 자신의 GameObject에 접근한다.

## 콜백 (Scene 생명주기와 동일)
`onAwake, onEnable, onStart, onFixedUpdate, onUpdate(float dt), onLateUpdate, onDisable, onDestroy` — 필요한 것만 `override`.
스크립트에서 던져진 예외가 잡히지 않으면 **그 컴포넌트만 비활성화**되고 스택 트레이스가 로그에 남는다(게임은 계속 진행).

## 엔진 API (현재 제공되는 전부)
| 이름 | 기능 |
|---|---|
| `Log.info/warn/error(string)` | 로그 → 에디터 콘솔 |
| `Vec3` | 벡터, 연산자 +, -, * |
| `GameObject` | exists, getName/setName, isActive/setActive, get·setPosition, getWorldPosition, get·setScale, rotate, setRotation, getParent, same, destroy, 정적 find/create |
물리·오디오·입력·렌더링 컴포넌트 접근은 **아직 스크립트 API에 없다**(C++ 컴포넌트로만 사용).

## 언어 팁
- 정적 타입, `int`는 64비트 래핑, 문자열은 UTF-8 바이트 단위.
- 코루틴: `coroutine void blink() { yield 1; }` → 호출하면 `Coroutine` 객체, `resume()`으로 진행.
- 예외는 `Exception` 파생 객체만 던질 수 있다. `try/catch/finally` 지원.
- 람다 `(int x) => x * k`는 바깥 변수를 **값 복사가 아니라 공유(참조)** 로 캡처한다. 자세한 규칙은 [script/04-advanced-features.md](script/04-advanced-features.md).
- 리플렉션: `typeof(T)`, `getFields()` 등(인스펙터가 이것을 쓴다).

## 에디터에서의 흐름
1. Scripts 탭에서 `.mins` 편집 → **Ctrl+B**(Scripts → Compile & Reload)로 컴파일. 저장하지 않은 편집 내용도 컴파일에 사용된다(디스크 저장은 Scripts → Save Script). 오류는 `파일:줄:열` 진단으로 에디터에 표시되고 **기존 실행 상태는 바뀌지 않는다**.
2. 성공하면 **핫 리로드**: 모든 스크립트 컴포넌트가 필드 이름+타입 기준으로 값을 이전한다. 정적 변수는 초기화되고, 객체 참조 필드와 코루틴 상태는 이전되지 않는다.
3. 재생(Ctrl+P)은 씬 스냅샷을 찍고 정지 시 복원한다.

## C++ 플러그인으로 확장
기본 라이브러리가 없으므로 파일/네트워크 같은 기능은 `native` 선언 + C ABI 플러그인(`mingol_plugin_abi`, `mingol_plugin_init`)으로 제공한다. 플러그인은 샌드박스 없이 프로세스 권한으로 실행된다 → [SECURITY.md](SECURITY.md), 시그니처 규칙은 [SCRIPT.md](SCRIPT.md) 4절.
