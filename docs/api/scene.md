# API: scene (`mingol/scene/*.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/scene` · 타깃 `Mingol::Scene` · 네임스페이스 `mingol::scene`. 의존: `core`, `renderer`. 개요는 [../SCENE.md](../SCENE.md).
**GameObject + Component** 계층 씬, 라이프사이클 콜백, 내장 컴포넌트, 렌더러로 가는 다리(`buildRenderScene`).
**스레딩**: `Scene`은 단일 스레드(게임 루프 스레드에서만 사용).

## 목차
[소유권·수명](#소유권수명) · [GameObjectId](#gameobjectid) · [Component](#component) · [GameObject](#gameobject) · [Scene](#scene) · [Transform](#transform) · [내장 컴포넌트](#내장-컴포넌트-componentshpp) · [buildRenderScene](#buildrenderscene) · [사용 예](#사용-예)

## 소유권·수명
- `Scene`이 `GameObject`를 소유하고, `GameObject`가 `Component`를 소유한다.
- 객체 참조는 `GameObjectId` 핸들(파괴 후에는 단지 해석되지 않음) 또는 raw 포인터(파괴 전까지 유효).
- **파괴는 flush 지점에서** 일어난다. 콜백 도중 `destroy()`를 호출해도 그 단계 끝까지 포인터가 유효하다.

## GameObjectId
`{uint32 index(슬롯+1, 0=없음), uint32 generation}`, `valid()`, `operator==`. 슬롯이 재사용되면 generation이 달라져 오래된 핸들은 `scene.get(id) == nullptr`이 된다.

## Component
사용자 컴포넌트는 `Component`를 상속한다.
```cpp
class Spinner final : public scene::Component {
 public:
  const char* typeName() const override { return "Spinner"; }   // 직렬화/스크립팅용, 클래스당 유일해야 함
  float degPerSec = 90;
 protected:
  void onUpdate(float dt) override {
    auto& t = gameObject().transform();
    t.setLocalRotation(t.localRotation() * Quat::fromAxisAngle({0, 1, 0}, radians(degPerSec) * dt));
  }
};
```
| 멤버 | 설명 |
|---|---|
| `typeName()` (순수 가상) | 안정적인 이름. **직렬화는 이 이름으로 컴포넌트를 찾는다** |
| `gameObject()`, `scene()` | 소유자 / 씬 |
| `enabled()`, `setEnabled(bool)`, `activeAndEnabled()` | 활성(자신 enabled **그리고** 소유자가 계층에서 활성) |
| `static liveCount()` | 프로세스 내 살아 있는 Component 수(누수 테스트용) |

콜백(모두 `protected virtual`): `onAwake`, `onEnable`, `onStart`, `onFixedUpdate(dt)`, `onUpdate(dt)`, `onLateUpdate(dt)`, `onDisable`, `onDestroy`.
**순서**: `onAwake`(계층에서 처음 활성이 될 때 1회) → `onEnable` → `onStart`(첫 fixed/update 전 1회) → 매 프레임 `onFixedUpdate`/`onUpdate`/`onLateUpdate` → `onDisable` → `onDestroy`.
콜백에서 **예외가 던져지면 잡아 로그로 남기고 그 컴포넌트를 비활성화**한다(`Scene::callbackFailures()` 증가).

## GameObject
직접 생성할 수 없다(`Scene::createObject`). 복사 불가.
| 멤버 | 설명 |
|---|---|
| `id()`, `name()`, `setName()`, `scene()` | |
| `transform()` | 항상 존재 |
| `parent()` / `children()` | 부모(루트면 `nullptr`) / 자식 id 목록 |
| `Result<void> setParent(GameObject* parent, bool keepWorldTransform = true)` | 새 부모가 자신/자손이거나 **다른 씬**이면 실패. `keepWorldTransform=true`면 월드 위치 유지, `false`면 로컬 값 유지(부모와 함께 이동). `nullptr` = 루트로 |
| `activeSelf()`, `activeInHierarchy()`, `setActive(bool)` | 비활성화하면 하위 전체가 계층에서 비활성(컴포넌트는 `onDisable`) |
| `addComponent<T>(args...)` | `T*` 반환. 계층에서 활성이면 Awake/Enable이 즉시, 아니면 활성화될 때 |
| `getComponent<T>()` / `getComponents<T>()` | `dynamic_cast` 기반(파생 클래스도 일치). 파괴 예약된 것 제외 |
| `componentCount()`, `forEachComponent(fn)` | 부착 순서 |
| `removeComponent(Component*)` | 다음 flush 때 파괴. 즉시부터 콜백을 받지 않음 |
| `markedForDestroy()`, `static liveCount()` | |

## Scene
| 멤버 | 설명 |
|---|---|
| `createObject(name = "GameObject", parent = nullptr)` | 부모는 같은 씬 소속이어야 함 |
| `destroy(GameObject*)` | 객체와 모든 자손을 다음 flush 때 파괴 |
| `flushDestroyed()` | 보류 중인 파괴를 지금 실행(각 프레임 단계 끝에서 자동 호출; 콜백 밖에서 `destroy()`/`removeComponent()`를 부르면 그 안에서도 호출) |
| `clear()` | 전부 즉시 파괴(언로드). 소멸자도 같음(`onDisable`/`onDestroy` 실행) |
| `get(id)`, `find(name)` | `find`는 생성 순서상 첫 일치, 깊이 무관 |
| `rootObjects()`, `objectCount()`(파괴 보류 포함), `forEachObject(fn)`, `findComponents<T>()` | |
| `fixedUpdate(dt)`, `update(dt)`, `lateUpdate(dt)` | 프레임 단계. 단계 중 생성된 객체/컴포넌트는 **다음 단계**부터 콜백을 받는다 |
| `SceneEnvironment environment` | `ambient{0.03}`, `clearColor{0.05,0.05,0.07,1}` |
| `setService<T>(T*)` / `service<T>()` | 비소유 서비스 레지스트리(물리·오디오 등). 타입당 하나, `nullptr`이면 해제. **등록자가 씬이 호출하는 동안 수명을 보장**해야 한다 |
| `callbackFailures()` | 잡힌 콜백 예외 수 |

## Transform
부모 기준 position/rotation/scale + 월드 행렬 캐시. 불변식: 한 노드의 월드 캐시가 stale이면 모든 자손도 stale. 지역 값을 바꾸면 이미 stale인 노드를 만날 때까지만 내려가 표시하고, 월드 행렬을 읽을 때는 위쪽 stale 체인만 재계산한다.

| 메서드 | 설명 |
|---|---|
| `localPosition/Rotation/Scale()` 및 `setLocalPosition/Rotation(정규화됨)/Scale/setLocal(p,q,s)` | |
| `localMatrix()`, `worldMatrix()`(const&, 지연 계산), `inverseWorldMatrix()` | |
| `worldPosition()`, `worldRotation()`, `worldScale()` | `worldScale`은 **전단(shear)이 있으면 손실** (회전된 부모 + 비균일 스케일의 자식) |
| `setWorldPosition(p)`, `setWorldRotation(q)` | 부모를 고정한 채 로컬 값을 역산 |
| `forward()`(**−Z**), `right()`(+X), `up()`(+Y) | 월드 공간 축, 카메라 규약과 일치 |
| `lookAt(target, up = {0,1,0})` | forward가 target을 향하도록 회전. target이 같은 위치이거나 `up`이 시선과 평행이면 **아무 일도 안 함** |
| `worldStale()` | 테스트/씬용 |
`scene::decompose`, `scene::quatFromMatrix`는 core의 재노출([core.md](core.md)).

## 내장 컴포넌트 (`components.hpp`)
| 컴포넌트 (`typeName`) | 필드 / 동작 |
|---|---|
| `MeshRenderer` (`"MeshRenderer"`) | `shared_ptr<const Mesh> mesh`, `shared_ptr<const Material> material`, `bool castShadow = true`. 스킨드 메시면 같은 객체에 `Animator` 필요 |
| `Animator` (`"Animator"`) | `setSkeleton(skel)`(조인트 없으면 실패), `addClip(clip)`(이름이 비었거나 중복이거나 스켈레톤에 없는 조인트를 가리키면 실패), `play(name, loop = true, fadeSeconds = 0)`(알 수 없는 클립이면 실패; `fade>0`이면 교차 페이드), `stop()`, `speed = 1`, `playing()`, `looping()`, `clips()`, `currentClip()`, `currentTime()`, `fading()`, `fadeProgress()`, `finished()`(비루프 클립이 끝에 도달), `advance(dt)`(`onUpdate`가 호출), `skinMatrices()`(재생 중이 아니면 바인드 포즈), `skeleton()` |
| `CameraComponent` (`"Camera"`) | `fovYRadians = 60°`, `nearZ = 0.1`, `farZ = 200`, `priority = 0`. **활성 카메라 중 priority가 가장 높은 것**, 동률이면 먼저 만든 것 |
| `LightComponent` (`"Light"`) | `type = Directional`, `color`, `intensity = 1`, `range = 10`, `innerConeRadians = 0.4`, `outerConeRadians = 0.6`, `castShadow = false`. 방향/위치는 Transform(forward = 빛 방향)에서 온다 |

## buildRenderScene
```cpp
Result<renderer::RenderScene> buildRenderScene(Scene& scene, float aspect);
Result<renderer::RenderScene> buildRenderScene(Scene& scene, const renderer::Camera& camera);   // 에디터 뷰포트: Camera 컴포넌트 불필요
```
- 활성 씬을 평평한 `RenderScene`으로 수집(월드 행렬, 스킨 행렬, 라이트, 메인 카메라, `SceneEnvironment`).
- **실패**: 활성이고 enabled인 카메라가 없음(첫 번째 오버로드), 또는 애니메이션 평가 실패.
- **건너뜀(오류 아님)**: mesh/material이 없는 객체, 사용 가능한 `Animator`가 없는 스킨드 메시 → 컴포넌트당 **1회만** 로그.
- **라이트 한도 초과분은 버림**: 그림자 방향광 1 + 점/스폿 `kMaxPunctualLights`(8); 카메라에서 가까운 것을 남긴다.

## 사용 예
```cpp
scene::Scene sc;
auto* cam = sc.createObject("Camera");
cam->addComponent<scene::CameraComponent>();
cam->transform().setLocalPosition({0, 2, 6});
cam->transform().lookAt({0, 0, 0});

auto* cube = sc.createObject("Cube");
auto* mr = cube->addComponent<scene::MeshRenderer>();
mr->mesh = mesh; mr->material = mat;                  // renderer.md 참조
cube->addComponent<Spinner>();

auto* sun = sc.createObject("Sun");
auto* l = sun->addComponent<scene::LightComponent>(); l->castShadow = true;
sun->transform().setLocalRotation(Quat::fromAxisAngle({1, 0, 0}, radians(-50)));

sc.update(1.0F / 60); sc.lateUpdate(1.0F / 60);
auto rs = scene::buildRenderScene(sc, 16.0F / 9);
if (rs.ok()) renderer->render(rs.value(), *target);
```
*(스케치 — 실제 컴파일은 `tests/scene`의 테스트가 같은 API를 사용한다. `Quat::fromAxisAngle` 등 수학 이름은 [core.md](core.md)에서 확인할 것.)*

## 한계
- 컴포넌트는 **타입 이름 레지스트리**(`sceneio`/`runtime`)에 등록되어야 직렬화·스크립트에서 보인다 → [sceneio.md](sceneio.md).
- `getComponent<T>`는 `dynamic_cast` 선형 탐색이다(핫 루프에서는 캐시할 것).
- 스레드 안전하지 않다.
