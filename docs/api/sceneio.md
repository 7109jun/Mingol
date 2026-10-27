# API: sceneio (`mingol/sceneio/{scene_io,standard_assets}.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/sceneio` · 타깃 `Mingol::SceneIO` · 네임스페이스 `mingol::sceneio`. 의존: `core`, `scene`, `runtime`, `scripting`(+`renderer`, `physics`, `assets`). 파일 포맷은 [../MINSEEN.md](../MINSEEN.md).
**`.minseen`(JSON 텍스트 씬 파일) 저장/로드**와 에디터·플레이어가 공유하는 **에셋 이름 규약**.
**스레딩**: 씬과 같은 스레드(단일).

## 핵심 규칙
- 에셋은 **이름으로 참조**한다. `AssetTable`이 이름 ↔ 로드된 에셋을 매핑한다(저장 시에는 역방향).
- 스크립트 컴포넌트는 **클래스 이름 + 데이터 필드**로 저장되고 `ScriptHost`로 다시 만들어진다.
- **로드는 전부-아니면-전무**: 문서를 먼저 검증하고, 객체는 **비활성 상태로** 만들어(절반만 지어진 상태에서 스크립트/컴포넌트 콜백이 돌지 않게) 모두 끝난 뒤에 활성화하며, 중간에 실패하면 로드가 만든 모든 것을 파괴한다.
- `kMinseenFormatVersion = 1`.

## AssetTable
이름 있는 로드된 에셋을 보관한다(씬 저장과 로드가 공유).
| 종류 | 등록 | 조회 | 이름 목록(정렬) |
|---|---|---|---|
| 메시 | `addMesh(name, shared_ptr<const Mesh>)` | `mesh(name)` | `meshNames()` |
| 머티리얼 | `addMaterial` | `material` | `materialNames()` |
| 오디오 클립 | `addAudioClip` | `audioClip` | `audioClipNames()` |
| 스켈레톤 | `addSkeleton` | `skeleton` | `skeletonNames()` |
| 애니메이션 클립 | `addAnimationClip` | `animationClip` | `animationClipNames()` |
| 충돌 메시 | `addCollisionMesh(name, shared_ptr<const physics::MeshShapeData>)` | `collisionMesh` | `collisionMeshNames()` |
- 같은 이름으로 다시 등록하면 **덮어쓴다**. 조회 실패는 `nullptr`.
- 역조회 `nameOf(shared_ptr<...>)` — 등록되지 않은 포인터(또는 null)는 **빈 문자열**. 선형 탐색이다.
- `clear()`. 이름 목록은 에디터 에셋 피커용.

## 컨텍스트·리포트·옵션
```cpp
struct SceneIoContext { const AssetTable* assets = nullptr;          // 씬이 에셋을 참조하면 필요
                        scripting::ScriptHost* scripts = nullptr; }; // Script 컴포넌트가 있으면 필요
struct LoadReport { size_t objectsCreated, componentsCreated; std::vector<std::string> warnings; };   // 무시된 필드, 건너뛴 알 수 없는 컴포넌트 등
struct SaveOptions { std::string sceneName; bool pretty = true; };
struct LoadOptions { bool verifyIntegrity = true;
                     bool skipUnknownComponents = false;   // true: 알 수 없는 컴포넌트 타입이 오류 대신 경고
                     bool replaceExisting = false;         // true: 문서 검증이 끝난 뒤 Scene::clear()
                     size_t maxObjects = 1u << 20; };
```

## ComponentRegistry (컴포넌트 타입별 직렬화)
```cpp
using SaveFn = std::function<Result<json::Value>(const scene::Component&, const SceneIoContext&)>;
using LoadFn = std::function<Result<void>(scene::GameObject& go, const json::Value& data, const SceneIoContext&, LoadReport&)>;
static ComponentRegistry withBuiltins();      // Camera, Light, MeshRenderer, Animator, Rigidbody, Collider, AudioSource, AudioListener, Script
Result<void> registerType(std::string typeName, SaveFn, LoadFn);
bool has(const std::string& typeName) const;
Result<json::Value> save(const scene::Component&, const SceneIoContext&) const;
Result<void> load(const std::string& typeName, scene::GameObject&, const json::Value&, const SceneIoContext&, LoadReport&) const;
```
- **typeName은 `Component::typeName()`과 일치**해야 한다. 등록되지 않은 컴포넌트는 저장할 수 없다(오류). 알 수 없는 타입을 로드하면 기본값은 **오류**(`skipUnknownComponents`로 완화).
- `LoadFn`은 **실패 시 반쯤 초기화된 컴포넌트를 남기면 안 된다**.
- 이름 중복 등록은 `registerType`이 오류로 거부한다.

커스텀 컴포넌트를 저장 가능하게 만들기(스케치):
```cpp
auto reg = sceneio::ComponentRegistry::withBuiltins();
reg.registerType("Spinner",
  [](const scene::Component& c, const sceneio::SceneIoContext&) -> Result<json::Value> {
    json::Object o; o.set("degPerSec", static_cast<const Spinner&>(c).degPerSec); return json::Value(std::move(o)); },
  [](scene::GameObject& go, const json::Value& d, const sceneio::SceneIoContext&, sceneio::LoadReport&) -> Result<void> {
    auto* s = go.addComponent<Spinner>(); /* d에서 degPerSec 읽기 — json.md 참조 */ return {}; });
```
*(json API 이름은 [core.md](core.md)에서 확인해 맞출 것.)*

## 저장 / 로드
```cpp
Result<std::string> saveScene(const scene::Scene&, const SceneIoContext&, const ComponentRegistry&, const SaveOptions& = {});
Result<LoadReport>  loadScene(scene::Scene&, std::string_view text, const SceneIoContext&, const ComponentRegistry&, const LoadOptions& = {});
Result<void>        saveSceneFile(const std::string& path, const scene::Scene&, ctx, registry, const SaveOptions& = {});
Result<LoadReport>  loadSceneFile(const std::string& path, scene::Scene&, ctx, registry, const LoadOptions& = {});
```
- `saveScene`: **살아 있는(파괴 예약이 아닌) 모든 객체**를 **부모가 자식보다 먼저** 직렬화한다. 환경(`SceneEnvironment`), 트랜스폼, 활성 상태, 컴포넌트를 포함.
- `loadScene`: 위 전부-아니면-전무 규칙. 무결성 필드가 있으면 `verifyIntegrity`에 따라 검사. 반환 `LoadReport`로 경고 확인.
- `replaceExisting=false`면 기존 씬 객체 **뒤에 추가**로 로드한다.
- 파일 변형은 같은 일을 파일로 수행(저장은 크래시 안전 쓰기를 쓴다면 `core fs` 규약에 따름 — 정확한 보장은 소스 `scene_io.cpp` 참조).

## standard_assets.hpp — 모든 Mingol 도구가 합의한 에셋 이름
에디터에서 저장한 씬이 플레이어에서 그대로 로드되도록 이름 규약을 공유한다.
```
builtin/box, builtin/sphere, builtin/plane, builtin/default        프리미티브와 그 머티리얼
<path>#mesh<m>.<p>   <path>#material<k>   <path>#default          프로젝트 경로 <path>의 glTF 모델 (m=메시, p=프리미티브, k=머티리얼)
<path>#skin<k>   <path>#skin<k>/<clip>                           스켈레톤과 애니메이션 클립
<path>                                                           오디오 파일
```
```cpp
struct BuiltinAssets { shared_ptr<Mesh> box, sphere, plane; shared_ptr<Material> material;
  static Result<BuiltinAssets> create(renderer::Renderer&);     // 단위 박스, 지름 1인 구, 10×10 평면, 회색 머티리얼
  void addTo(AssetTable&) const; };
void addModelAssets(AssetTable&, const std::string& path, const runtime::GpuModel&);   // 업로드된 모델의 메시/머티리얼/스켈레톤/클립을 path 아래 이름으로 등록
```

## 사용 예 (스케치)
```cpp
sceneio::AssetTable table;
auto builtins = sceneio::BuiltinAssets::create(*renderer).value();  builtins.addTo(table);
sceneio::addModelAssets(table, "assets/robot.glb", gpuModel);

sceneio::SceneIoContext ctx{&table, host.get()};
auto reg = sceneio::ComponentRegistry::withBuiltins();

auto text = sceneio::saveScene(scene, ctx, reg, {.sceneName = "main"}).value();
auto rep  = sceneio::loadScene(other, text, ctx, reg, {.replaceExisting = true});
if (!rep.ok()) MINGOL_LOG_ERROR("sceneio", "{}", rep.error().message);
else for (auto& w : rep.value().warnings) MINGOL_LOG_WARN("sceneio", "{}", w);
```
## 한계
참조는 이름 기반이라 에셋 이름이 바뀌면 끊어진다(자동 마이그레이션 없음). 스크립트 컴포넌트의 객체형 필드와 `gameObject`는 저장되지 않는다([scripting.md](scripting.md)). 프리팹/씬 중첩 없음.
