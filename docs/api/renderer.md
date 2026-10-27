# API: renderer (`mingol/renderer/*.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/renderer` · 타깃 `Mingol::Renderer` · 네임스페이스 `mingol::renderer`. 의존: `core`, `rhi`, `shader`. 개요는 [../RENDERER.md](../RENDERER.md).
**Forward + PBR**, 방향광 그림자 맵, GPU 스키닝. 입력은 평평한 `RenderScene`, 출력은 `rhi::Texture`. 씬 그래프를 모른다(`scene` 모듈이 `RenderScene`을 만든다).
**스레딩**: `Renderer`는 스레드 안전하지 않다(한 스레드, 보통 렌더 스레드). `rhi::Device`가 `Renderer`보다 오래 살아야 한다.

## 목차
[mesh.hpp](#meshhpp) · [scene.hpp](#scenehpp) · [renderer.hpp](#rendererhpp) · [animation.hpp](#animationhpp) · [사용 예](#사용-예)

---

## mesh.hpp (CPU 메시 데이터)
| 타입 | 설명 |
|---|---|
| `Vertex` (48B) | `Vec3 position, Vec3 normal, Vec2 uv, Vec4 tangent`(xyz 방향, **w = 접선 부호 ±1**) |
| `SkinVertex` (32B) | `float joints[4]`(**float로 저장** — RHI에 정수 정점 포맷이 없음, 2²⁴ 미만에서 정확), `float weights[4]`(기본 `{1,0,0,0}`) |
| `Aabb` | `min`, `max`, `center()`, `radius()` |
| `MeshData` | `vertices`, `indices`(삼각형 리스트), `skin`(비어 있으면 정적, 아니면 정점당 1개) |
| `MeshData::validate()` | 비어 있지 않음, 인덱스 개수는 3의 배수이며 범위 안, 모든 float 유한, 법선이 영벡터 아님, `skin` 크기 일치, 조인트 인덱스 < `kMaxJoints`(128), 가중치가 음수 아니고 합이 ≈1 |
| `MeshData::bounds()` | `Aabb` |

프리미티브(모두 법선·UV·접선 포함, **바깥에서 볼 때 반시계 방향 감김**):
```cpp
MeshData makePlane(float width, float depth);            // XZ 평면, 법선 +Y, 원점 중심
MeshData makeBox(Vec3 halfExtents);                      // 24 정점(면별 법선)
MeshData makeUvSphere(float radius, int segments, int rings);
```

## scene.hpp (한 프레임의 서술)
- `Camera{Mat4 view, Mat4 projection, Vec3 position}` — `position`은 **월드 공간 눈 위치**(스페큘러에 필요). `Camera::lookAt(eye, target, up, fovY_rad, aspect, near, far)`.
- `LightType{Directional, Point, Spot}`.
- `Light`: `type`, `color{1,1,1}`, `intensity = 1`, `position`, `direction{0,-1,0}`(**빛이 진행하는 방향**), `range = 10`(점/스폿: 이 거리에서 기여가 0), `innerConeRadians = 0.4`, `outerConeRadians = 0.6`, `castShadow`(**방향광만**).
  - `intensity` 의미: 흰색 알베도 1인 표면이 광원을 정면으로 보면 톤매핑 전에 `intensity/π · cosθ`가 보인다(`intensity = π`이면 `albedo · cosθ`).
- `DrawItem{shared_ptr<const Mesh> mesh, shared_ptr<const Material> material, Mat4 transform(모델→월드), vector<Mat4> skinMatrices(스킨드 메시일 때만, computeSkinMatrices 결과), bool castShadow = true}`.
- `RenderScene{Camera camera, Vec3 ambient{0.03,…}(디퓨즈 색에 곱해짐), float clearColor[4], vector<Light> lights, vector<DrawItem> items, optional<ShadowFocus{center, radius}> shadowFocus}`.
  - `lights`: **방향광 최대 1개 + 점/스폿 최대 `kMaxPunctualLights`(= 8)**.
  - `shadowFocus`: 그림자 맵이 덮는 구. 없으면 그림자를 드리우는 모든 항목의 경계 구를 쓴다(스킨드 메시는 바인드 포즈 경계를 쓰므로 멀리 움직이면 지정할 것).

## renderer.hpp
### `Mesh` (GPU 상주, 생성 후 불변)
`indexCount()`, `vertexCount()`, `skinned()`, `bounds()`, `maxJointIndex()`(가중치가 0이 아닌 최고 조인트 인덱스; 조인트 행렬 검증용).

### `MaterialDesc` / `Material` (생성 후 불변 — 바꾸려면 새로 만든다)
| 필드 | 기본 | 설명 |
|---|---|---|
| `Vec4 baseColor` | `{1,1,1,1}` | 선형, 베이스컬러 텍스처에 곱함 |
| `float metallic` / `roughness` | `0` / `1` | |
| `Vec3 emissive` | `{0,0,0}` | 조명 후에 더해지는 선형 복사량 |
| `float normalScale` | `1` | |
| `AlphaMode alphaMode` | `Opaque` | `Opaque` / `Blend`(거리 정렬되어 뒤에서 앞으로 그려짐) |
| `bool doubleSided` | `false` | |
| `baseColorTexture`, `metallicRoughnessTexture`, `normalTexture`, `emissiveTexture` | `nullptr`(중립 기본값) | **ShaderRead 상태**여야 함. 베이스컬러·이미시브는 **sRGB**, 메탈릭-러프니스(G=roughness, B=metallic, glTF 규약)와 노멀맵은 **선형** 포맷 |
`Material::desc()`.

### `RendererDesc`
`maxObjects = 4096`(프레임당 드로 항목), `maxSkinMatrices = 8192`(프레임당 모든 스킨드 항목의 조인트 행렬 합), `shadowMapSize = 2048`(정사각, 방향광), `framesInFlight = 2`(CPU가 GPU보다 앞서 기록할 수 있는 프레임 수), `shadowNormalBias = 1.5`(그림자 맵 텍셀 단위, 법선 방향 오프셋), `shadowDepthBiasConstant = 1.25`, `shadowDepthBiasSlope = 1.75`.

### `class Renderer`
```cpp
static Result<std::unique_ptr<Renderer>> create(rhi::Device&, const RendererDesc& = {});
Result<std::shared_ptr<Mesh>>     createMesh(const MeshData&);       // 검증 후 GPU 업로드
Result<std::shared_ptr<Material>> createMaterial(const MaterialDesc&);
Result<std::uint64_t> render(const RenderScene&, rhi::Texture& target, rhi::Swapchain* presenting = nullptr);
const FrameStats& stats() const noexcept;
```
- `render`: `target`은 **ColorAttachment 사용처**가 있고 RGBA8/BGRA8 **sRGB 또는 UNORM** 이어야 한다. 렌더 후 제출한다.
  - `presenting` 지정: 이미지를 **Present** 상태로 남기고 제출이 스왑체인 이미지를 기다린다.
  - 미지정: 타깃은 **ColorAttachment 상태로 남는다**(샘플링하려면 `ShaderRead` 전환 필요, 읽으려면 복사).
  - 반환값 = 제출 시리얼(`Device::waitForSerial`).
  - 잘못된 입력(스킨 행렬 부족, 항목 수 초과, 유효하지 않은 메시 등)은 **오류로 반환**(크래시 없음).
- `FrameStats`: `drawCalls`, `shadowDrawCalls`, `objects`, `pipelinesCreated`(누적), `shadowsRendered`.
- 셰이더는 실행 시 glslang으로 컴파일하고 파이프라인은 캐시한다. 프레임 슬롯 링으로 2프레임 동시 진행.

## animation.hpp (순수 CPU, GPU 무관 — 결과가 정확히 테스트 가능)
- `kMaxJoints = 128`(스키닝 셰이더와 공유하는 한도).
- `Transform{translation, rotation, scale}` + `matrix()`.  *(주의: `scene::Transform`과 다른 타입)*
- `Joint{name, parent(-1이면 루트, 반드시 자신의 인덱스보다 작아야 함), bindLocal, optional<Mat4> inverseBind(없으면 bindLocal에서 계산)}`.
- `Skeleton::create(vector<Joint>)` → 실패 조건: 조인트 없음, 128개 초과, 부모 인덱스가 자신 이상(→ 부모가 항상 먼저 평가됨), 이름 중복, 바인드 포즈가 가역 아님. 멤버: `jointCount()`, `joint(i)`, `inverseBind(i)`, `findJoint(name)`, `bindPose()`.
- `Interpolation{Linear, Step}`, `Track<T>{times(엄격 증가, 초), values(같은 길이), interpolation}`, `Channel{joint, translation, rotation, scale}`(빈 트랙은 그 성분을 바인드 포즈로 둠).
- `AnimationClip::create(name, duration, channels)` → 트랙 길이 불일치·시간이 증가하지 않음/비유한·`duration <= 0`이면 실패. `name()`, `duration()`, `channels()`.
- 함수:
```cpp
Result<vector<Transform>> samplePose(const Skeleton&, const AnimationClip&, float time, bool loop);   // loop=true면 [0,duration)로 감김, false면 클램프. 스켈레톤에 없는 조인트를 가리키면 실패
vector<Transform> blendPoses(const vector<Transform>& a, const vector<Transform>& b, float t);        // 이동/스케일 lerp, 회전 slerp, t는 [0,1]로 클램프
Result<vector<Mat4>> computeSkinMatrices(const Skeleton&, const vector<Transform>& pose);             // global * inverseBind — 업로드용
Result<vector<Mat4>> computeGlobalPose(const Skeleton&, const vector<Transform>& pose);               // 본에 물체를 붙일 때
```
`pose.size()`는 조인트 수와 같아야 한다.

## 사용 예
```cpp
auto rnd  = renderer::Renderer::create(*device).value();
auto mesh = rnd->createMesh(renderer::makeBox({0.5F, 0.5F, 0.5F})).value();
renderer::MaterialDesc md; md.baseColor = {0.8F, 0.2F, 0.2F, 1}; md.roughness = 0.5F;
auto mat = rnd->createMaterial(md).value();

renderer::RenderScene rs;
rs.camera = renderer::Camera::lookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0}, radians(60), 16.0F / 9, 0.1F, 100);
renderer::Light sun; sun.type = renderer::LightType::Directional; sun.direction = normalize(Vec3{-1, -1, -1});
sun.intensity = 3.14159F; sun.castShadow = true;
rs.lights.push_back(sun);
rs.items.push_back({mesh, mat, Mat4::identity(), {}, true});

auto serial = rnd->render(rs, *colorTarget);       // 또는 render(rs, *img.texture, swapchain.get())
if (!serial.ok()) MINGOL_LOG_ERROR("render", "{}", serial.error().message);
```
스킨드 메시:
```cpp
auto pose = renderer::samplePose(skeleton, clip, time, /*loop=*/true).value();
item.skinMatrices = renderer::computeSkinMatrices(skeleton, pose).value();   // item.mesh->skinned() == true일 때만
```

## 한계
IBL/환경광, 점광·캐스케이드 그림자, 알파 컷아웃, HDR·후처리, 컬링·인스턴싱이 없다. 스키닝 법선은 비균일 스케일 조인트에서 근사다. 머티리얼은 불변. 실제 GPU 성능은 측정하지 못했다([../KNOWN-LIMITS.md](../KNOWN-LIMITS.md)).
