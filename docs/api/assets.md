# API: assets (`mingol/assets/{image,audio,model}.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/assets` · 타깃 `Mingol::Assets` · 네임스페이스 `mingol::assets`. 의존: `core`, `renderer`(모델의 CPU 데이터 타입), 내장 디코더(stb 계열, cgltf). 개요는 [../ASSETS.md](../ASSETS.md).
**순수 CPU 디코딩** — GPU를 건드리지 않는다. 업로드와 씬 인스턴스화는 [runtime.md](runtime.md)의 `uploadModel`/`instantiateModel`.
**공통 보장**: 입력은 신뢰하지 않는다. 모든 크기·인덱스·범위를 **할당 전에** 한도(`*Limits`)와 대조하고, 잘못된 파일은 오류를 반환하며 크래시하지 않는다. 입력은 바이트(`std::span<const std::uint8_t>`)이므로 파일 확장자를 믿지 않는다. 스레드 안전(전역 상태 없음).

## image.hpp
```cpp
struct Image { uint32_t width, height; std::vector<uint8_t> rgba; size_t byteSize() const; };
// rgba: width*height*4, 행 우선, 맨 위 행부터, straight(비곱셈) 알파
struct ImageLimits { uint32_t maxDimension = 16384; uint64_t maxDecodedBytes = 512 MiB; };
enum class ImageFormat { Unknown, Png, Jpeg };
ImageFormat sniffImageFormat(std::span<const uint8_t>) noexcept;      // 앞부분 바이트만 본다
Result<Image> decodeImage(std::span<const uint8_t>, const ImageLimits& = {});
```
- PNG(모든 색 타입, 1–16비트, 인터레이스)와 JPEG(baseline/progressive). **16비트 PNG는 8비트로 축소**된다.
- 크기와 디코드 후 바이트 수를 **픽셀 메모리 할당 전에** 한도와 비교한다(압축 폭탄 방어). 알 수 없는 형식, 잘림, 손상 시 실패.

## audio.hpp
```cpp
struct AudioClip { uint32_t sampleRate, channels; std::vector<float> samples; uint64_t frames() const; double durationSeconds() const; };
// samples: 인터리브, 명목 범위 [-1, 1]
struct AudioLimits { uint32_t maxChannels = 8; uint32_t maxSampleRate = 768000; uint64_t maxFrames = 1<<28; /* 48 kHz 약 1.5시간 */ };
enum class AudioFormat { Unknown, Wav, Ogg };
AudioFormat sniffAudioFormat(std::span<const uint8_t>) noexcept;
Result<AudioClip> decodeWav(span, const AudioLimits& = {});
Result<AudioClip> decodeOgg(span, const AudioLimits& = {});
Result<AudioClip> decodeAudio(span, const AudioLimits& = {});   // 내용으로 디코더 선택
```
- **WAV**: PCM 8/16/24/32비트 정수, IEEE float 32/64비트, 그리고 이들을 감싼 `WAVE_FORMAT_EXTENSIBLE`. 모르는 청크는 건너뜀. **data 청크가 잘려 있으면 거부**; data 크기 0 또는 `0xFFFFFFFF`(스트리밍 인코더)는 "파일 끝까지"로 해석.
- **Ogg Vorbis**(stb_vorbis): 디코딩 전에 스트림 길이를 한도와 대조.
- 결과는 믹서([audio.md](audio.md))에 `shared_ptr<const AudioClip>`로 넘긴다.

## model.hpp (glTF 2.0 / GLB)
### 지원 / 미지원
- **지원**: 삼각형 리스트/스트립/팬, `POSITION/NORMAL/TEXCOORD_0/TANGENT/JOINTS_0/WEIGHTS_0`, metallic-roughness 머티리얼(+`KHR_materials_emissive_strength`), PNG/JPEG 텍스처, 노드 계층, 스킨, 스켈레탈 TRS 애니메이션(LINEAR/STEP).
- **미지원(조용히 넘기지 않고 `ModelData::warnings`에 기록)**: alpha MASK(불투명으로 로드), CUBICSPLINE(탄젠트 폐기, 키프레임 값만 사용), 모프 타깃, 카메라/라이트, cgltf가 해석하는 범위를 넘는 sparse accessor, 텍스처 변환, specular-glossiness, 두 번째 UV 세트, 조인트가 아닌 노드의 애니메이션, 스켈레톤의 비조인트 조상이 애니메이션되는 경우.

### 데이터 구조
| 타입 | 필드 |
|---|---|
| `ModelMaterial` | `name`, `baseColor{1,1,1,1}`, `metallic = 1`, `roughness = 1`(glTF 기본값), `emissive`, `normalScale = 1`, `alphaMode`, `doubleSided`, `baseColorImage / metallicRoughnessImage / normalImage / emissiveImage`(`images` 인덱스, 없으면 −1). 베이스컬러·이미시브 이미지는 **sRGB 데이터**, 나머지는 **선형** |
| `ModelPrimitive` | `renderer::MeshData data`, `material`(−1 = 기본 머티리얼) |
| `ModelMesh` | `name`, `primitives`, `skin`(−1 = 정적). **(glTF 메시, 스킨) 쌍마다 하나**. 스킨드 프리미티브의 조인트 인덱스는 `skin`의 **재정렬된 스켈레톤** 기준 |
| `ModelNode` | `name`, `parent`(−1), `translation/rotation/scale`, `mesh`, `skin`, `children`. 스킨드 메시는 항등 노드 변환으로 그려진다 |
| `ModelSkin` | `name`, `skeleton`(부모가 자식보다 앞), `jointNodes`(조인트마다 노드 인덱스), `clips`(이 스켈레톤을 구동하는 애니메이션) |
| `ModelData` | `images`, `materials`, `meshes`, `nodes`, `skins`, `rootNodes`(기본 씬의 루트), `warnings` |
| `ModelLimits` | `maxVertices = 1<<24`, `maxNodes = 1<<20`, `image`(`ImageLimits`) |

### 함수
```cpp
using UriResolver = std::function<Result<std::vector<uint8_t>>(const std::string& uri)>;   // 퍼센트 디코드된, .gltf 기준 상대 URI → 바이트
Result<ModelData> loadGltf(std::span<const uint8_t> data, const UriResolver& = {}, const ModelLimits& = {});
Result<ModelData> loadGltfFile(const std::filesystem::path&, const ModelLimits& = {});
```
- `loadGltf`: `data`는 `.glb` 또는 `.gltf`(JSON). 외부 `.bin`/이미지에만 `resolver`가 필요(`data:` URI와 GLB 바이너리 청크는 내부 처리). 모든 인덱스, accessor 범위, 조인트 인덱스, 크기를 검증한다.
- `loadGltfFile`: 파일을 읽고 외부 URI를 **파일의 디렉터리 기준**으로 해석. **디렉터리를 벗어나는 URI(절대 경로, `..`)는 거부**한다.

## 사용 예 (스케치)
```cpp
auto md = assets::loadGltfFile("models/robot.glb");
if (!md.ok()) { MINGOL_LOG_ERROR("assets", "{}", md.error().message); return; }
for (auto& w : md.value().warnings) MINGOL_LOG_WARN("assets", "{}", w);

auto gpu  = runtime::uploadModel(*device, *renderer, md.value()).value();
auto inst = runtime::instantiateModel(scene, md.value(), gpu, "robot").value();

auto bytes = fs::readBinaryFile("sfx/hit.ogg");                       // core.md
auto clip  = std::make_shared<const assets::AudioClip>(assets::decodeAudio(bytes.value()).value());
```

## 한계
KTX2/DDS/HDR 이미지, 압축 텍스처, 스트리밍 오디오, FBX/OBJ 임포터는 없다. Draco/meshopt 확장 미지원. 텍스처 밉맵은 업로드 시 생성된다. 큰 모델은 전부 메모리에 디코드된다.
