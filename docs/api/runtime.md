# API: runtime (`mingol/runtime/*.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/runtime` · 타깃 `Mingol::Runtime` · 네임스페이스 `mingol::runtime`. 의존: `scene`, `physics`, `audio`, `assets`, `renderer`, `rhi`. 개요는 [../RUNTIME.md](../RUNTIME.md).
게임 루프(`Runtime`), 물리·오디오 컴포넌트, glTF 모델 → GPU/GameObject 변환.
**스레딩**: 단일 스레드(게임 루프). 오디오 믹서만 오디오 스레드가 당겨 간다([audio.md](audio.md)).

## 목차
[Runtime](#runtime-runtimehpp) · [물리](#물리-physics_componentshpp) · [오디오](#오디오-audio_componentshpp) · [모델](#모델-model_instancehpp) · [사용 예](#사용-예)

## Runtime (`runtime.hpp`)
`Scene` + `PhysicsSystem` + `audio::Mixer`(+선택적 `audio::Device`)를 소유하고 프레임을 진행한다.

`RuntimeDesc`: `fixedStep = 1/60`, `maxFixedStepsPerFrame = 8`(초과분 시뮬레이션 시간은 **버려짐** — 죽음의 나선 방지), `maxFrameTime = 0.25`(더 긴 프레임은 클램프), `physics`(`WorldDesc`; `physics.fixedStep`은 `fixedStep`으로 덮어씀), `mixer`(`MixerDesc`), `openAudioDevice = false`(false면 믹서는 있지만 아무도 끌어가지 않음), `audioBackend = Default`.

`RuntimeStats`: `frames`, `fixedSteps`, `droppedFixedSteps`, `time`(스케일된 초).

```cpp
static Result<std::unique_ptr<Runtime>> create(const RuntimeDesc& = {});
scene::Scene& scene();  PhysicsSystem& physics();  audio::Mixer& mixer();
const RuntimeStats& stats() const;
void setTimeScale(float);  float timeScale() const;     // 0 = 일시정지(프레임은 dt=0으로 계속 tick)
void tick(float realDt);
std::uint64_t run(const std::function<bool()>& keepGoing, const std::function<void(float dt)>& onFrame = {});
```
- **tick 순서**: 정수 개의 고정 스텝(`Scene::fixedUpdate` → 물리 스텝 → 포즈 write-back → 접촉 콜백) 먼저, 그다음 프레임 시간으로 `Scene::update`, `Scene::lateUpdate` 한 번씩.
- 시간은 호출자가 공급하므로 테스트·리플레이가 결정적이다. `run`은 단조 시계로 `tick`을 구동하는 편의 함수(프레임 수 반환; `onFrame`은 매 tick 뒤, 예: 렌더).
- 생성자에서 `Scene::setService`로 `PhysicsSystem`·`audio::Mixer`를 등록한다. 멤버 선언 순서 때문에 **씬이 가장 먼저 파괴**된다(컴포넌트가 소멸 중 물리/오디오를 호출하므로).

## 물리 (`physics_components.hpp`)
모델: **Rigidbody가 물리 바디 하나를 소유**하고, **같은 GameObject**의 `Collider`들이 그 모양을 제공한다. Collider만 있고 Rigidbody가 없으면 아무 일도 하지 않고 로그로 알린다. 정적 콜라이더 = `BodyType::Static`인 Rigidbody. 콜라이더는 **Rigidbody가 enable될 때 읽힌다**; 나중 변경은 Rigidbody를 disable→enable 해야 반영된다. 월드 스케일은 그 시점에 모양에 구워진다(구/캡슐의 비균일 스케일은 가장 큰 관련 축 사용).

**Transform 동기화(매 고정 스텝)**: Transform을 직접 움직이면 바디가 **순간이동**(0.1 mm / 0.003° 미만 이동은 float 노이즈로 무시). Dynamic 바디는 시뮬레이션 포즈를 Transform에 기록. Kinematic은 Transform을 따른다.

### `ContactListener`
Component와 **함께 상속**하는 인터페이스: `onCollisionEnter(GameObject& other, const physics::ContactEvent&)`, `onCollisionExit(other)`, `onTriggerEnter(other)`, `onTriggerExit(other)` — 모두 기본 빈 구현. 콜백 예외는 로그되고 해당 컴포넌트가 비활성화된다.

### `Collider` (`"Collider"`)
```cpp
static Collider* addBox(GameObject&, Vec3 halfExtents, Vec3 center = {});
static Collider* addSphere(GameObject&, float radius, Vec3 center = {});
static Collider* addCapsule(GameObject&, float radius, float height, Vec3 center = {});
static Collider* addMesh(GameObject&, std::shared_ptr<const physics::MeshShapeData>);
physics::ShapeDesc shape;   // 오프셋/회전은 GameObject 기준
```

### `Rigidbody` (`"Rigidbody"`)
| 필드 | 기본 |
|---|---|
| `type` | `BodyType::Dynamic` |
| `mass`, `friction`, `restitution` | `1`, `0.5`, `0` |
| `linearDamping`, `angularDamping` | `0`, `0.05` |
| `isTrigger`, `allowSleep` | `false`, `true` |
| `layer`, `mask` | `1`, `0xFFFFFFFF` |
| `lockRotationX/Y/Z`, `lockPositionX/Y/Z` | 전부 `false` |
바디는 컴포넌트가 처음 start할 때(첫 고정 스텝 전) 생성되므로 `addComponent` 직후 설정하면 된다.
런타임 제어(전부 **바디가 없으면 `NotFound`로 실패** — 비활성이거나 콜라이더 없음): `setVelocity`, `setAngularVelocity`, `addForce`, `addImpulse`, `addTorqueImpulse`; `state()` → `optional<physics::BodyState>`; `hasBody()`, `bodyId()`.

### `PhysicsSystem`
```cpp
static Result<std::unique_ptr<PhysicsSystem>> create(const physics::WorldDesc& = {});
physics::World& world();
void fixedStep(Scene&);        // Transform 편집 반영 → 시뮬레이션 → 포즈 write-back → 접촉 콜백
std::optional<Hit> raycast(Scene&, Vec3 origin, Vec3 direction, float maxDistance, uint32_t mask = ~0u);
std::vector<GameObject*> overlapSphere(Scene&, Vec3 center, float radius, uint32_t mask = ~0u);
std::size_t bodyCount();  std::uint64_t callbackFailures();
struct Hit { GameObject* object; Vec3 point; Vec3 normal; float distance; };
```
`packObjectId` / `unpackObjectId`: `GameObjectId` ↔ 물리 월드의 `userData`(uint64).

## 오디오 (`audio_components.hpp`)
두 컴포넌트는 `Scene` 서비스 레지스트리에서 `audio::Mixer`를 찾는다(`Runtime`이 채움). 믹서가 없으면 재생이 실패한다.

### `AudioSource` (`"AudioSource"`)
필드: `clip`(`shared_ptr<const assets::AudioClip>`), `volume = 1`, `pitch = 1`, `pan = 0`(**2D 전용**), `loop = false`, `playOnEnable = true`, `priority = 0`, `spatial = false`(3D: `AudioListener` 기준 감쇠·팬; 이 객체를 따라감), `minDistance = 1`, `maxDistance = 50`, `rolloff = 1`.
메서드: `play()`(처음부터 재시작; **clip/믹서가 없거나 설정이 잘못되면 실패**), `stop()`, `setPaused(bool)`, `isPlaying()`(**마지막으로 렌더된 블록 기준**의 믹서 상태), `applySettings()`(`play()` 이후 바꾼 volume/pitch/pan을 실행 중 보이스에 반영).

### `AudioListener` (`"AudioListener"`)
GameObject의 위치·방향이 "듣는 곳". **씬당 하나**.

## 모델 (`model_instance.hpp`)
```cpp
struct GpuModel { meshes[ModelMesh][primitive], materials[ModelMaterial], defaultMaterial, skeletons[ModelSkin], clips[ModelSkin][...] };
Result<GpuModel> uploadModel(rhi::Device&, renderer::Renderer&, const assets::ModelData&);   // 업로드가 끝날 때까지 블록
struct ModelInstance { GameObject* root; std::vector<GameObject*> nodes; /* ModelNode와 같은 인덱스, null 없음 */ };
Result<ModelInstance> instantiateModel(Scene&, const assets::ModelData&, const GpuModel&, const std::string& name = "model", GameObject* parent = nullptr);
```
- `uploadModel`: 텍스처 업로드(베이스컬러·이미시브 **sRGB**, 나머지 선형, 전체 밉 체인), 머티리얼·메시 생성. `GpuModel`은 인스턴스 여러 개가 공유 가능.
- `instantiateModel`: glTF 노드마다 GameObject 하나. 메시 노드는 프리미티브마다 `MeshRenderer`; 스킨드 노드는 스켈레톤과 모든 클립을 가진 `Animator`도 받는다(**`Animator::play`를 부르기 전엔 아무것도 재생되지 않음**). glTF 규격대로 스킨드 메시 객체는 **인스턴스 루트 바로 아래에 항등 변환**으로 놓인다. `GpuModel::skeletons/clips`가 비어 있으면 인스턴스별 사본을 만든다.

## 사용 예
```cpp
runtime::RuntimeDesc rd;                 // 오디오 장치 없이(테스트/헤드리스)
auto rt = runtime::Runtime::create(rd).value();

auto* floor = rt->scene().createObject("Floor");
auto* frb = floor->addComponent<runtime::Rigidbody>(); frb->type = physics::BodyType::Static;
runtime::Collider::addBox(*floor, {10, 0.5F, 10});

auto* ball = rt->scene().createObject("Ball");
ball->transform().setLocalPosition({0, 5, 0});
ball->addComponent<runtime::Rigidbody>();
runtime::Collider::addSphere(*ball, 0.5F);

for (int i = 0; i < 120; ++i) rt->tick(1.0F / 60);        // 2초 시뮬레이션
auto hit = rt->physics().raycast(rt->scene(), {0, 10, 0}, {0, -1, 0}, 100);
```
*(스케치. `tests/runtime`가 같은 API를 컴파일·실행한다.)*

## 한계
물리 월드는 Bullet 기반(`physics.md`). 컴포넌트 프로퍼티 변경이 바디에 반영되려면 disable→enable 필요. `AudioSource::isPlaying()`은 블록 단위 지연. 3D 오디오는 단순 거리 감쇠+팬(HRTF 없음).
