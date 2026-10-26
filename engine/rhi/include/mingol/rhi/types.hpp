// SPDX-License-Identifier: GPL-2.0-only
// Backend-neutral GPU types. Nothing here depends on Vulkan.
#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "mingol/core/flags.hpp"

namespace mingol::rhi {

inline constexpr std::uint64_t kWholeSize = std::numeric_limits<std::uint64_t>::max();

enum class Format : std::uint8_t {
  Undefined,
  R8Unorm,
  RG8Unorm,
  RGBA8Unorm,
  RGBA8Srgb,
  BGRA8Unorm,
  BGRA8Srgb,
  R16Float,
  RG16Float,
  RGBA16Float,
  R32Float,
  RG32Float,
  RGB32Float,
  RGBA32Float,
  D16Unorm,
  D32Float,
};

std::uint32_t bytesPerPixel(Format format) noexcept;  // 0 for Undefined
bool isDepthFormat(Format format) noexcept;

// ---- Buffers -----------------------------------------------------------------------------------

enum class BufferUsage : std::uint32_t {
  None = 0,
  Vertex = 1u << 0,
  Index = 1u << 1,
  Uniform = 1u << 2,
  Storage = 1u << 3,
  TransferSrc = 1u << 4,
  TransferDst = 1u << 5,
};
MINGOL_ENUM_FLAGS(BufferUsage)

enum class MemoryType : std::uint8_t {
  GpuOnly,   // device-local; fill through a transfer
  CpuToGpu,  // persistently mapped, written by the CPU (uniform data, staging)
  GpuToCpu,  // persistently mapped, read back by the CPU
};

struct BufferDesc {
  std::uint64_t size = 0;
  BufferUsage usage = BufferUsage::None;
  MemoryType memory = MemoryType::GpuOnly;
  std::string debugName;
};

// ---- Textures ----------------------------------------------------------------------------------

enum class TextureUsage : std::uint32_t {
  None = 0,
  Sampled = 1u << 0,
  ColorAttachment = 1u << 1,
  DepthStencil = 1u << 2,
  TransferSrc = 1u << 3,
  TransferDst = 1u << 4,
  Storage = 1u << 5,
};
MINGOL_ENUM_FLAGS(TextureUsage)

// Logical state of a texture. The command list inserts the layout transitions and barriers.
enum class TextureState : std::uint8_t {
  Undefined,
  ShaderRead,
  ColorAttachment,
  DepthAttachment,
  TransferSrc,
  TransferDst,
  Storage,
  Present,
};

struct TextureDesc {
  std::uint32_t width = 1;
  std::uint32_t height = 1;
  std::uint32_t mipLevels = 1;
  std::uint32_t arrayLayers = 1;
  Format format = Format::RGBA8Unorm;
  TextureUsage usage = TextureUsage::Sampled;
  bool cube = false;  // arrayLayers must be 6
  std::string debugName;
};

// Number of mip levels in a full chain for the given size.
std::uint32_t fullMipCount(std::uint32_t width, std::uint32_t height) noexcept;

enum class FilterMode : std::uint8_t { Nearest, Linear };
enum class AddressMode : std::uint8_t { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder };
enum class CompareOp : std::uint8_t { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };

struct SamplerDesc {
  FilterMode minFilter = FilterMode::Linear;
  FilterMode magFilter = FilterMode::Linear;
  FilterMode mipFilter = FilterMode::Linear;
  AddressMode addressU = AddressMode::Repeat;
  AddressMode addressV = AddressMode::Repeat;
  AddressMode addressW = AddressMode::Repeat;
  float maxAnisotropy = 1.0f;  // 1 disables anisotropic filtering
  bool compareEnable = false;  // shadow samplers
  CompareOp compareOp = CompareOp::LessOrEqual;
  float maxLod = 1000.0f;
  bool borderWhite = false;  // ClampToBorder colour: opaque white instead of transparent black
};

// ---- Shaders / pipelines -----------------------------------------------------------------------

enum class ShaderStage : std::uint8_t {
  None = 0,
  Vertex = 1u << 0,
  Fragment = 1u << 1,
  Compute = 1u << 2,
};
MINGOL_ENUM_FLAGS(ShaderStage)

struct ShaderModuleDesc {
  std::span<const std::uint32_t> spirv;
  ShaderStage stage = ShaderStage::Vertex;
  std::string entryPoint = "main";
  std::string debugName;
};

enum class BindingType : std::uint8_t { UniformBuffer, StorageBuffer, SampledTexture };

struct BindingDesc {
  std::uint32_t binding = 0;
  BindingType type = BindingType::UniformBuffer;
  ShaderStage stages = ShaderStage::Vertex | ShaderStage::Fragment;
  std::uint32_t count = 1;  // array size
};

struct BindGroupLayoutDesc {
  std::vector<BindingDesc> bindings;
  std::string debugName;
};

enum class Topology : std::uint8_t { TriangleList, TriangleStrip, LineList, PointList };
enum class CullMode : std::uint8_t { None, Front, Back };
enum class FrontFace : std::uint8_t { CounterClockwise, Clockwise };
enum class BlendFactor : std::uint8_t {
  Zero,
  One,
  SrcAlpha,
  OneMinusSrcAlpha,
  DstAlpha,
  OneMinusDstAlpha
};
enum class BlendOp : std::uint8_t { Add, Subtract, ReverseSubtract, Min, Max };

struct BlendState {
  bool enable = false;
  BlendFactor srcColor = BlendFactor::SrcAlpha;
  BlendFactor dstColor = BlendFactor::OneMinusSrcAlpha;
  BlendOp colorOp = BlendOp::Add;
  BlendFactor srcAlpha = BlendFactor::One;
  BlendFactor dstAlpha = BlendFactor::OneMinusSrcAlpha;
  BlendOp alphaOp = BlendOp::Add;
};

struct VertexBinding {
  std::uint32_t binding = 0;
  std::uint32_t stride = 0;
  bool perInstance = false;
};

struct VertexAttribute {
  std::uint32_t location = 0;
  std::uint32_t binding = 0;
  Format format = Format::RGB32Float;
  std::uint32_t offset = 0;
};

class ShaderModule;
class BindGroupLayout;

struct GraphicsPipelineDesc {
  std::shared_ptr<ShaderModule> vertexShader;
  std::shared_ptr<ShaderModule> fragmentShader;  // may be null (depth-only passes)
  std::vector<VertexBinding> vertexBindings;
  std::vector<VertexAttribute> vertexAttributes;
  Topology topology = Topology::TriangleList;
  CullMode cullMode = CullMode::Back;
  FrontFace frontFace = FrontFace::CounterClockwise;
  bool depthTest = true;
  bool depthWrite = true;
  CompareOp depthCompare = CompareOp::Less;
  bool depthBias = false;  // constant/slope bias, used by shadow passes
  float depthBiasConstant = 0.0f;
  float depthBiasSlope = 0.0f;
  std::vector<Format> colorFormats;  // one per colour attachment; may be empty (depth-only)
  std::vector<BlendState> blend;     // empty = no blending; otherwise one entry per colour attachment
  Format depthFormat = Format::Undefined;
  std::vector<std::shared_ptr<BindGroupLayout>> setLayouts;  // set index = position
  std::uint32_t pushConstantSize = 0;                        // bytes, visible to vertex + fragment
  std::string debugName;
};

// ---- Rendering ---------------------------------------------------------------------------------

enum class LoadOp : std::uint8_t { Load, Clear, DontCare };
enum class StoreOp : std::uint8_t { Store, DontCare };
enum class IndexType : std::uint8_t { U16, U32 };

class Texture;

struct ColorAttachment {
  Texture* texture = nullptr;
  std::uint32_t mip = 0;
  std::uint32_t layer = 0;
  LoadOp load = LoadOp::Clear;
  StoreOp store = StoreOp::Store;
  float clear[4] = {0, 0, 0, 1};
};

struct DepthAttachment {
  Texture* texture = nullptr;
  std::uint32_t mip = 0;
  std::uint32_t layer = 0;
  LoadOp load = LoadOp::Clear;
  StoreOp store = StoreOp::Store;
  float clearDepth = 1.0f;
};

struct RenderPassDesc {
  std::vector<ColorAttachment> colors;
  DepthAttachment depth;  // depth.texture == nullptr means no depth attachment
  std::string debugName;
};

// ---- Device / presentation ---------------------------------------------------------------------

struct DeviceDesc {
  std::string appName = "Mingol";
  bool enableValidation = false;           // enables the validation layer if it is installed
  std::string preferredDeviceName;         // substring match; empty = best available
  bool allowSoftwareDevice = true;         // accept CPU devices such as lavapipe
};

enum class DeviceType : std::uint8_t { Other, Integrated, Discrete, Virtual, Cpu };

struct DeviceInfo {
  std::string name;
  std::string driver;
  DeviceType type = DeviceType::Other;
  std::uint32_t apiMajor = 0, apiMinor = 0, apiPatch = 0;
  std::uint32_t maxTextureSize = 0;
  float maxSamplerAnisotropy = 1.0f;
  std::uint32_t maxPushConstantSize = 0;
  bool validationEnabled = false;
};

// Native window handle used to create a presentation surface without tying the RHI to a window
// library. Headless uses a surface with no display, which is how swapchains are tested on CI.
struct NativeWindow {
  enum class Kind : std::uint8_t { Headless, Win32, Xlib, Xcb, Wayland };
  Kind kind = Kind::Headless;
  void* display = nullptr;      // Xlib Display*, xcb_connection_t*, wl_display*, or HINSTANCE
  std::uintptr_t window = 0;    // Window / xcb_window_t / wl_surface* / HWND
  std::uint32_t width = 0;      // initial size (headless surfaces and fallback)
  std::uint32_t height = 0;
};

struct SwapchainDesc {
  NativeWindow window;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool vsync = true;
};

}  // namespace mingol::rhi
