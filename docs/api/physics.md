# API: physics (`mingol/physics/physics.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/physics` · 타깃 `Mingol::Physics` · 네임스페이스 `mingol::physics`. 의존: `core`, Bullet(구현 전용 — **Bullet 타입은 헤더에 노출되지 않음**, 백엔드 교체 가능).
**스레딩**: `World`는 단일 스레드(스텝을 돌리는 스레드에서만 사용). **단위**: m, kg, s, rad. **Y가 위**. 회전은 단위 쿼터니언. 2D 물리 = 모든 바디를 XY 평면에 제약한 같은 월드(`planar2D`).
GameObject와 묶인 고수준 사용은 [runtime.md](runtime.md)의 `Rigidbody`/`Collider`.

## 열거형
- `BodyType{Static(움직이지 않음, 무한 질량), Dynamic(시뮬레이션, mass>0 필요), Kinematic(게임이 setTransform으로 움직임; 동적 바디를 밀지만 밀리지 않음)}`
- `ShapeType{Box, Sphere, Capsule, TriangleMesh}`
- `ContactEventType{ContactBegin, ContactEnd, TriggerEnter, TriggerExit}`

## 데이터 구조
| 타입 | 필드 |
|---|---|
| `MeshShapeData` | `vector<Vec3> vertices`, `vector<uint32_t> indices`(삼각형 리스트). 바디 생성 시 **복사됨** |
| `ShapeDesc` | `type = Box`, `halfExtents{0.5}`(Box), `radius = 0.5`(구/캡슐), `height = 1`(캡슐의 **원통 부분 길이**, Y축), `offset`, `rotation`(바디 기준), `shared_ptr<const MeshShapeData> mesh`(**정적 바디 전용**) |
| `BodyDesc` | `type = Dynamic`, `shapes`(1개 이상이면 compound), `mass = 1`(Dynamic만), `position`, `rotation`, `friction = 0.5`(≥0), `restitution = 0`, `linearDamping = 0`, `angularDamping = 0.05`(0..1), `isTrigger`, `allowSleep = true`, `layer = 1`, `mask = 0xFFFFFFFF`, `lockRotationX/Y/Z`, `lockPositionX/Y/Z`, `userData`(uint64, 이벤트·쿼리 결과로 반환) |
| `BodyId` | `{index(슬롯+1, 0=없음), generation}`, `valid()`, `==`, `<`. 파괴된 바디의 id는 `exists()==false` |
| `WorldDesc` | `gravity{0,-9.81,0}`, `fixedStep = 1/60`, `maxSubSteps = 8`(`step(dt)` 한 번당 상한; 남은 시간은 **버려짐**), `planar2D = false` |
| `ContactEvent` | `type`, `a`, `b`(`a < b`; **트리거 이벤트에서는 `a`가 트리거**), `userDataA/B`, `point`(Begin/Enter 시 월드 접촉점, End/Exit은 0), `normal`(a→b) |
| `RayHit` | `body`, `userData`, `point`, `normal`, `distance` |
| `BodyState` | `position`, `rotation`, `linearVelocity`, `angularVelocity`, `sleeping` |

**충돌 필터**: 쌍은 **양쪽의 mask가 서로의 layer를 받아들일 때만** 충돌한다.

## `class World`
```cpp
static Result<std::unique_ptr<World>> create(const WorldDesc& = {});
Result<BodyId> createBody(const BodyDesc&);
bool destroyBody(BodyId);          // 알 수 없는/오래된 id는 무해한 no-op (false)
bool exists(BodyId) const;  std::size_t bodyCount() const;
int  step(float dt);               // fixedStep 단위로 정수 스텝 진행, 실행한 스텝 수 반환
void stepOnce();                   // 정확히 한 고정 스텝
std::optional<BodyState> state(BodyId) const;
Result<void> setTransform(BodyId, Vec3 pos, Quat rot);   // 순간이동. 속도는 유지. Kinematic이 움직이는 방법
Result<void> setLinearVelocity / setAngularVelocity(BodyId, Vec3);
Result<void> applyForce(BodyId, Vec3);          // 다음 스텝이 끝날 때까지
Result<void> applyImpulse(BodyId, Vec3);        // 즉시, 질량 중심에
Result<void> applyTorqueImpulse(BodyId, Vec3);
Result<void> setGravity(Vec3);  Vec3 gravity() const;  float fixedStep() const;
std::optional<RayHit> raycast(Vec3 origin, Vec3 direction, float maxDistance, uint32_t mask = ~0u) const;
std::vector<BodyId> overlapSphere(Vec3 center, float radius, uint32_t mask = ~0u) const;
std::vector<ContactEvent> drainEvents();        // 마지막 호출 이후 발생분, 감지 순서대로
```
- **`createBody` 실패 조건**: 모양 목록이 비어 있음, 크기가 비유한/0 이하, 질량 없는 Dynamic, 정적이 아닌 TriangleMesh, 비었거나 인덱스 범위를 벗어난 메시.
- 알 수 없는 id를 받는 setter들은 `Result` 오류를 반환한다.
- `raycast`: 선분 `origin → origin + direction * maxDistance`에서 **가장 가까운 솔리드(트리거 제외)** 바디. `direction`은 정규화 불필요하나 0이면 안 됨. `mask`는 맞출 레이어.
- `overlapSphere`: 솔리드와 트리거 **모두** 반환.
- 이벤트는 `drainEvents()`를 부르지 않으면 쌓인다(런타임은 매 고정 스텝 호출).

## 사용 예 (스케치)
```cpp
auto w = physics::World::create().value();
physics::BodyDesc ground; ground.type = physics::BodyType::Static;
ground.shapes = {{.type = physics::ShapeType::Box, .halfExtents = {10, 0.5F, 10}}};
w->createBody(ground).value();

physics::BodyDesc ball; ball.shapes = {{.type = physics::ShapeType::Sphere, .radius = 0.5F}};
ball.position = {0, 5, 0}; ball.userData = 42;
auto id = w->createBody(ball).value();
for (int i = 0; i < 120; ++i) w->step(1.0F / 60);
auto s = w->state(id);                 // s->position.y ≈ 1.0 근처에 정지
for (auto& e : w->drainEvents()) { /* e.type, e.userDataA/B */ }
```

## 한계
CCD(연속 충돌), 조인트/제약, 연속 캐릭터 컨트롤러, 소프트바디는 없다. 삼각형 메시는 정적 전용. 결정성은 같은 플랫폼·같은 빌드에서만 기대할 수 있다(플랫폼 간 비트 일치 미검증). Bullet 의존성은 CMake `find_package(Bullet)`.
