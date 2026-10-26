// SPDX-License-Identifier: GPL-2.0-only
// RHI tests run on whatever Vulkan 1.3 device the machine offers (lavapipe on CI). The validation
// layer is enabled when installed, and every rendering test asserts it reported no errors.
#include <cstdlib>
#include <cstring>
#include <format>
#include <thread>

#include "mingol/rhi/device.hpp"
#include "mingol/shader/shader_compiler.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::rhi;

namespace {

struct GpuHolder {
  std::unique_ptr<Device> device;
  std::string skipReason;
};

GpuHolder& gpu() {
  static GpuHolder holder = [] {
    GpuHolder h;
    DeviceDesc desc;
    desc.enableValidation = true;
    auto r = createVulkanDevice(desc);
    if (r.ok()) {
      h.device = std::move(r).value();
    } else {
      h.skipReason = "no usable Vulkan device: " + r.error().describe();
    }
    return h;
  }();
  return holder;
}

#define GPU_OR_SKIP()                                  \
  if (!gpu().device) SKIP_TEST(gpu().skipReason);      \
  Device& dev = *gpu().device;                         \
  ValidationGuard validationGuard(dev)

// Fails the test if the validation layer reported errors while the test ran.
struct ValidationGuard {
  Device& device;
  std::uint64_t base;
  explicit ValidationGuard(Device& d) : device(d), base(d.validationErrorCount()) {}
  ~ValidationGuard() { CHECK_EQ(device.validationErrorCount(), base); }
};

std::span<const std::uint8_t> bytesOf(const void* p, std::size_t n) {
  return {static_cast<const std::uint8_t*>(p), n};
}
template <class T>
std::span<const std::uint8_t> bytesOf(const std::vector<T>& v) {
  return bytesOf(v.data(), v.size() * sizeof(T));
}

struct Px {
  int r, g, b, a;
};
Px pixel(const std::vector<std::uint8_t>& data, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
  const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
  return {data[i], data[i + 1], data[i + 2], data[i + 3]};
}
bool nearPx(Px p, int r, int g, int b, int a, int tol = 1) {
  return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol && std::abs(p.a - a) <= tol;
}
std::string str(Px p) { return std::format("({},{},{},{})", p.r, p.g, p.b, p.a); }

std::shared_ptr<ShaderModule> makeShader(Device& dev, const char* source, shader::Stage stage, const char* name) {
  shader::CompileOptions o;
  o.stage = stage;
  o.sourceName = name;
  auto spirv = shader::compile(source, o);
  if (!spirv.ok()) {
    test::fail(__FILE__, __LINE__, spirv.error().message);
    return nullptr;
  }
  ShaderModuleDesc d;
  d.spirv = spirv.value();
  d.stage = stage == shader::Stage::Vertex ? ShaderStage::Vertex : ShaderStage::Fragment;
  d.debugName = name;
  auto m = dev.createShaderModule(d);
  if (!m.ok()) {
    test::fail(__FILE__, __LINE__, m.error().describe());
    return nullptr;
  }
  return m.value();
}

std::shared_ptr<Texture> makeTarget(Device& dev, std::uint32_t w, std::uint32_t h, Format format = Format::RGBA8Unorm) {
  TextureDesc d;
  d.width = w;
  d.height = h;
  d.format = format;
  d.usage = TextureUsage::ColorAttachment | TextureUsage::TransferSrc | TextureUsage::Sampled;
  d.debugName = "target";
  auto t = dev.createTexture(d);
  if (!t.ok()) {
    test::fail(__FILE__, __LINE__, t.error().describe());
    return nullptr;
  }
  return t.value();
}

}  // namespace

// ---- Format helpers (no GPU) ---------------------------------------------------------------------

TEST(RhiTypes, FormatHelpers) {
  CHECK_EQ(bytesPerPixel(Format::RGBA8Unorm), 4u);
  CHECK_EQ(bytesPerPixel(Format::RGBA16Float), 8u);
  CHECK_EQ(bytesPerPixel(Format::RGB32Float), 12u);
  CHECK_EQ(bytesPerPixel(Format::Undefined), 0u);
  CHECK(isDepthFormat(Format::D32Float));
  CHECK(!isDepthFormat(Format::RGBA8Unorm));
  CHECK_EQ(fullMipCount(1, 1), 1u);
  CHECK_EQ(fullMipCount(4, 4), 3u);
  CHECK_EQ(fullMipCount(512, 128), 10u);
  CHECK_EQ(fullMipCount(5, 3), 3u);
}

TEST(RhiTypes, EnumFlags) {
  const BufferUsage u = BufferUsage::Vertex | BufferUsage::TransferDst;
  CHECK(hasFlag(u, BufferUsage::Vertex));
  CHECK(hasFlag(u, BufferUsage::TransferDst));
  CHECK(!hasFlag(u, BufferUsage::Index));
  CHECK(!hasFlag(u, BufferUsage::None));
  CHECK(!anyFlag(BufferUsage::None));
}

// ---- Device ----------------------------------------------------------------------------------------

TEST(Device, CreatesAndReportsInfo) {
  GPU_OR_SKIP();
  const DeviceInfo& info = dev.info();
  CHECK(!info.name.empty());
  CHECK(info.apiMajor > 1 || info.apiMinor >= 3);
  CHECK(info.maxTextureSize >= 4096);
  CHECK(info.maxPushConstantSize >= 128);
}

TEST(Device, ValidationLayerReportsRealErrors) {
  if (!gpu().device) SKIP_TEST(gpu().skipReason);
  Device& dev = *gpu().device;
  if (!dev.info().validationEnabled) SKIP_TEST("validation layer not installed");

  // Overlapping ranges inside one buffer are illegal (VUID-vkCmdCopyBuffer-pRegions-00117).
  BufferDesc d;
  d.size = 64;
  d.usage = BufferUsage::TransferSrc | BufferUsage::TransferDst;
  d.memory = MemoryType::GpuOnly;
  auto buf = dev.createBuffer(d);
  REQUIRE(buf.ok());
  auto list = dev.createCommandList();
  REQUIRE(list.ok());
  const std::uint64_t before = dev.validationErrorCount();
  REQUIRE(list.value()->begin().ok());
  list.value()->copyBuffer(*buf.value(), *buf.value(), 16, 0, 8);
  REQUIRE(list.value()->end().ok());
  // The layer reports this while recording, so the illegal command is never submitted to the GPU.
  CHECK(dev.validationErrorCount() > before);  // proves the other tests' zero-error checks mean something
}

// ---- Buffers ---------------------------------------------------------------------------------------

TEST(Buffer, GpuOnlyUploadAndReadback) {
  GPU_OR_SKIP();
  std::vector<std::uint32_t> data(1000);
  for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint32_t>(i * 2654435761u);

  BufferDesc d;
  d.usage = BufferUsage::Storage | BufferUsage::TransferSrc;
  d.memory = MemoryType::GpuOnly;
  d.debugName = "gpu buffer";
  auto gpuBuf = dev.createBufferWithData(d, bytesOf(data));
  REQUIRE(gpuBuf.ok());
  CHECK_EQ(gpuBuf.value()->size(), data.size() * sizeof(std::uint32_t));
  CHECK(gpuBuf.value()->mapped() == nullptr);

  BufferDesc rd;
  rd.size = gpuBuf.value()->size();
  rd.usage = BufferUsage::TransferDst;
  rd.memory = MemoryType::GpuToCpu;
  auto readback = dev.createBuffer(rd);
  REQUIRE(readback.ok());
  REQUIRE(readback.value()->mapped() != nullptr);

  auto list = dev.createCommandList();
  REQUIRE(list.ok());
  REQUIRE(list.value()->begin().ok());
  list.value()->copyBuffer(*gpuBuf.value(), *readback.value(), rd.size);
  REQUIRE(list.value()->end().ok());
  auto serial = dev.submit(*list.value());
  REQUIRE(serial.ok());
  REQUIRE(dev.waitForSerial(serial.value()).ok());
  CHECK(dev.isComplete(serial.value()));
  readback.value()->invalidate();
  CHECK(std::memcmp(readback.value()->mapped(), data.data(), rd.size) == 0);
}

TEST(Buffer, CpuToGpuIsMappedAndWritten) {
  GPU_OR_SKIP();
  const std::vector<float> values = {1.0f, 2.0f, 3.0f, 4.0f};
  BufferDesc d;
  d.usage = BufferUsage::Uniform;
  d.memory = MemoryType::CpuToGpu;
  auto buf = dev.createBufferWithData(d, bytesOf(values));
  REQUIRE(buf.ok());
  REQUIRE(buf.value()->mapped() != nullptr);
  CHECK(std::memcmp(buf.value()->mapped(), values.data(), sizeof(float) * 4) == 0);
}

TEST(Buffer, InvalidDescriptionsAreRejected) {
  GPU_OR_SKIP();
  BufferDesc zero;
  zero.usage = BufferUsage::Vertex;
  CHECK(!dev.createBuffer(zero).ok());  // size 0

  BufferDesc noUsage;
  noUsage.size = 16;
  CHECK(!dev.createBuffer(noUsage).ok());

  BufferDesc small;
  small.size = 4;
  small.usage = BufferUsage::Vertex;
  const std::vector<std::uint8_t> big(16, 1);
  auto r = dev.createBufferWithData(small, big);
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::InvalidArgument);
}

// ---- Textures --------------------------------------------------------------------------------------

TEST(Texture, UploadReadbackAndGeneratedMips) {
  GPU_OR_SKIP();
  // 4x4 made of four uniform 2x2 blocks: red, green / blue, white.
  std::vector<std::uint8_t> pixels(4 * 4 * 4);
  auto set = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const std::size_t i = (static_cast<std::size_t>(y) * 4 + x) * 4;
    pixels[i] = r; pixels[i + 1] = g; pixels[i + 2] = b; pixels[i + 3] = 255;
  };
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if (y < 2) { if (x < 2) set(x, y, 255, 0, 0); else set(x, y, 0, 255, 0); }
      else       { if (x < 2) set(x, y, 0, 0, 255); else set(x, y, 255, 255, 255); }
    }
  }

  TextureDesc td;
  td.width = 4;
  td.height = 4;
  td.format = Format::RGBA8Unorm;
  td.usage = TextureUsage::Sampled | TextureUsage::TransferSrc;
  td.debugName = "mip test";
  auto tex = dev.createTextureWithData(td, pixels, true);
  REQUIRE(tex.ok());
  CHECK_EQ(tex.value()->desc().mipLevels, 3u);

  auto mip0 = dev.readbackTexture(*tex.value(), 0);
  REQUIRE(mip0.ok());
  CHECK(mip0.value() == pixels);

  auto mip1 = dev.readbackTexture(*tex.value(), 1);
  REQUIRE(mip1.ok());
  REQUIRE(mip1.value().size() == 2 * 2 * 4);
  CHECK(nearPx(pixel(mip1.value(), 2, 0, 0), 255, 0, 0, 255));
  CHECK(nearPx(pixel(mip1.value(), 2, 1, 0), 0, 255, 0, 255));
  CHECK(nearPx(pixel(mip1.value(), 2, 0, 1), 0, 0, 255, 255));
  CHECK(nearPx(pixel(mip1.value(), 2, 1, 1), 255, 255, 255, 255));

  auto mip2 = dev.readbackTexture(*tex.value(), 2);
  REQUIRE(mip2.ok());
  REQUIRE(mip2.value().size() == 4);
  const Px avg = pixel(mip2.value(), 1, 0, 0);
  CHECK(nearPx(avg, 127, 127, 127, 255, 2) || nearPx(avg, 128, 128, 128, 255, 2));
}

TEST(Texture, InvalidDescriptionsAreRejected) {
  GPU_OR_SKIP();
  TextureDesc d;
  d.usage = TextureUsage::Sampled;

  TextureDesc zero = d;
  zero.width = 0;
  CHECK(!dev.createTexture(zero).ok());

  TextureDesc huge = d;
  huge.width = dev.info().maxTextureSize + 1;
  CHECK(!dev.createTexture(huge).ok());

  TextureDesc badMips = d;
  badMips.width = badMips.height = 4;
  badMips.mipLevels = 9;
  CHECK(!dev.createTexture(badMips).ok());

  TextureDesc cube = d;
  cube.width = cube.height = 8;
  cube.cube = true;
  cube.arrayLayers = 5;
  CHECK(!dev.createTexture(cube).ok());

  TextureDesc depthAsColor = d;
  depthAsColor.format = Format::D32Float;
  depthAsColor.usage = TextureUsage::ColorAttachment;
  CHECK(!dev.createTexture(depthAsColor).ok());

  TextureDesc colorAsDepth = d;
  colorAsDepth.usage = TextureUsage::DepthStencil;
  CHECK(!dev.createTexture(colorAsDepth).ok());

  CHECK(!dev.createTexture([&] { TextureDesc t; t.usage = TextureUsage::None; return t; }()).ok());

  // Wrong data size for createTextureWithData.
  TextureDesc four = d;
  four.width = four.height = 4;
  const std::vector<std::uint8_t> tooSmall(10, 0);
  CHECK(!dev.createTextureWithData(four, tooSmall, false).ok());
}

TEST(Texture, CubeMapLayersAreUsableAsAttachments) {
  GPU_OR_SKIP();
  TextureDesc d;
  d.width = d.height = 16;
  d.arrayLayers = 6;
  d.cube = true;
  d.format = Format::RGBA8Unorm;
  d.usage = TextureUsage::Sampled | TextureUsage::ColorAttachment | TextureUsage::TransferSrc;
  auto cube = dev.createTexture(d);
  REQUIRE(cube.ok());

  // Clear each face to a different colour, then read two of them back.
  auto list = dev.createCommandList();
  REQUIRE(list.ok());
  REQUIRE(list.value()->begin().ok());
  for (std::uint32_t face = 0; face < 6; ++face) {
    RenderPassDesc pass;
    ColorAttachment a;
    a.texture = cube.value().get();
    a.layer = face;
    a.clear[0] = face / 5.0f;
    a.clear[1] = 0;
    a.clear[2] = 1.0f - face / 5.0f;
    a.clear[3] = 1;
    pass.colors.push_back(a);
    list.value()->beginRendering(pass);
    list.value()->endRendering();
  }
  REQUIRE(list.value()->end().ok());
  auto serial = dev.submit(*list.value());
  REQUIRE(serial.ok());
  REQUIRE(dev.waitForSerial(serial.value()).ok());

  auto face0 = dev.readbackTexture(*cube.value(), 0, 0);
  auto face5 = dev.readbackTexture(*cube.value(), 0, 5);
  REQUIRE(face0.ok());
  REQUIRE(face5.ok());
  CHECK(nearPx(pixel(face0.value(), 16, 3, 3), 0, 0, 255, 255));
  CHECK(nearPx(pixel(face5.value(), 16, 3, 3), 255, 0, 0, 255));
}

// ---- Rendering -------------------------------------------------------------------------------------

TEST(Render, ClearAndTriangleWithoutVertexBuffers) {
  GPU_OR_SKIP();
  auto vs = makeShader(dev, R"(#version 450
void main() {
  vec2 p[3] = vec2[](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.0, 0.5));
  gl_Position = vec4(p[gl_VertexIndex], 0.0, 1.0);
})", shader::Stage::Vertex, "tri.vert");
  auto fs = makeShader(dev, R"(#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(0.0, 1.0, 0.0, 1.0); })", shader::Stage::Fragment, "tri.frag");
  REQUIRE(vs != nullptr);
  REQUIRE(fs != nullptr);

  GraphicsPipelineDesc pd;
  pd.vertexShader = vs;
  pd.fragmentShader = fs;
  pd.cullMode = CullMode::None;
  pd.depthTest = false;
  pd.colorFormats = {Format::RGBA8Unorm};
  pd.debugName = "triangle";
  auto pipeline = dev.createGraphicsPipeline(pd);
  if (!pipeline.ok()) test::fail(__FILE__, __LINE__, pipeline.error().describe());
  REQUIRE(pipeline.ok());

  auto target = makeTarget(dev, 64, 64);
  REQUIRE(target != nullptr);

  auto list = dev.createCommandList();
  REQUIRE(list.ok());
  REQUIRE(list.value()->begin().ok());
  RenderPassDesc pass;
  ColorAttachment ca;
  ca.texture = target.get();
  ca.clear[0] = 0; ca.clear[1] = 0; ca.clear[2] = 0; ca.clear[3] = 1;
  pass.colors.push_back(ca);
  list.value()->beginRendering(pass);
  list.value()->bindPipeline(*pipeline.value());
  list.value()->draw(3);
  list.value()->endRendering();
  REQUIRE(list.value()->end().ok());
  auto serial = dev.submit(*list.value());
  REQUIRE(serial.ok());
  REQUIRE(dev.waitForSerial(serial.value()).ok());

  auto img = dev.readbackTexture(*target);
  REQUIRE(img.ok());
  const Px center = pixel(img.value(), 64, 32, 32);
  const Px corner = pixel(img.value(), 64, 2, 2);
  const Px top = pixel(img.value(), 64, 32, 4);
  CHECK_MSG(nearPx(center, 0, 255, 0, 255), "center " + str(center));
  CHECK_MSG(nearPx(corner, 0, 0, 0, 255), "corner " + str(corner));
  CHECK_MSG(nearPx(top, 0, 0, 0, 255), "top " + str(top));
}

namespace {

constexpr const char* kQuadVert = R"(#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 0) out vec2 uv;
layout(push_constant) uniform Push { float depthOffset; } pc;
void main() {
  uv = inUv;
  gl_Position = vec4(inPos.xy, inPos.z + pc.depthOffset, 1.0);
})";

constexpr const char* kQuadFrag = R"(#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform Tint { vec4 tint; } t;
layout(set = 0, binding = 1) uniform sampler2D tex;
void main() { outColor = texture(tex, uv) * t.tint; })";

}  // namespace

TEST(Render, TexturedQuadWithUniformsDepthAndPushConstants) {
  GPU_OR_SKIP();
  auto vs = makeShader(dev, kQuadVert, shader::Stage::Vertex, "quad.vert");
  auto fs = makeShader(dev, kQuadFrag, shader::Stage::Fragment, "quad.frag");
  REQUIRE(vs != nullptr);
  REQUIRE(fs != nullptr);

  BindGroupLayoutDesc bgl;
  bgl.bindings = {{0, BindingType::UniformBuffer, ShaderStage::Fragment, 1},
                  {1, BindingType::SampledTexture, ShaderStage::Fragment, 1}};
  auto layout = dev.createBindGroupLayout(bgl);
  REQUIRE(layout.ok());

  GraphicsPipelineDesc pd;
  pd.vertexShader = vs;
  pd.fragmentShader = fs;
  pd.vertexBindings = {{0, 20, false}};
  pd.vertexAttributes = {{0, 0, Format::RGB32Float, 0}, {1, 0, Format::RG32Float, 12}};
  pd.cullMode = CullMode::None;
  pd.depthTest = true;
  pd.depthWrite = true;
  pd.depthCompare = CompareOp::Less;
  pd.colorFormats = {Format::RGBA8Unorm};
  pd.depthFormat = Format::D32Float;
  pd.setLayouts = {layout.value()};
  pd.pushConstantSize = 4;
  auto pipeline = dev.createGraphicsPipeline(pd);
  if (!pipeline.ok()) test::fail(__FILE__, __LINE__, pipeline.error().describe());
  REQUIRE(pipeline.ok());

  // Geometry: one full-screen quad at z = 0.5; uv (0,0) is the top-left of the image.
  const std::vector<float> vertices = {-1, -1, 0.5f, 0, 0,  1, -1, 0.5f, 1, 0,  1, 1, 0.5f, 1, 1,  -1, 1, 0.5f, 0, 1};
  const std::vector<std::uint16_t> indices = {0, 1, 2, 2, 3, 0};
  BufferDesc vbd;
  vbd.usage = BufferUsage::Vertex;
  auto vb = dev.createBufferWithData(vbd, bytesOf(vertices));
  BufferDesc ibd;
  ibd.usage = BufferUsage::Index;
  auto ib = dev.createBufferWithData(ibd, bytesOf(indices));
  REQUIRE(vb.ok());
  REQUIRE(ib.ok());

  // 2x2 nearest-sampled texture: red, green / blue, white.
  const std::vector<std::uint8_t> texels = {255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 255,  255, 255, 255, 255};
  TextureDesc td;
  td.width = td.height = 2;
  auto tex = dev.createTextureWithData(td, texels, false);
  REQUIRE(tex.ok());
  SamplerDesc sd;
  sd.minFilter = sd.magFilter = FilterMode::Nearest;
  sd.mipFilter = FilterMode::Nearest;
  sd.addressU = sd.addressV = AddressMode::ClampToEdge;
  auto sampler = dev.createSampler(sd);
  REQUIRE(sampler.ok());

  auto makeGroup = [&](float r, float g, float b) -> std::shared_ptr<BindGroup> {
    const float tint[4] = {r, g, b, 1.0f};
    BufferDesc ubd;
    ubd.usage = BufferUsage::Uniform;
    ubd.memory = MemoryType::CpuToGpu;
    auto ub = dev.createBufferWithData(ubd, bytesOf(tint, sizeof(tint)));
    if (!ub.ok()) { test::fail(__FILE__, __LINE__, ub.error().describe()); return nullptr; }
    BindGroupDesc gd;
    gd.layout = layout.value();
    BindGroupEntry e0;
    e0.binding = 0;
    e0.buffer = ub.value();
    BindGroupEntry e1;
    e1.binding = 1;
    e1.texture = tex.value();
    e1.sampler = sampler.value();
    gd.entries = {e0, e1};
    auto g2 = dev.createBindGroup(gd);
    if (!g2.ok()) { test::fail(__FILE__, __LINE__, g2.error().describe()); return nullptr; }
    return g2.value();
  };
  auto white = makeGroup(1, 1, 1);
  auto black = makeGroup(0, 0, 0);
  auto blue = makeGroup(0, 0, 1);
  REQUIRE(white != nullptr);
  REQUIRE(black != nullptr);
  REQUIRE(blue != nullptr);

  auto target = makeTarget(dev, 64, 64);
  TextureDesc dd;
  dd.width = dd.height = 64;
  dd.format = Format::D32Float;
  dd.usage = TextureUsage::DepthStencil;
  auto depth = dev.createTexture(dd);
  REQUIRE(target != nullptr);
  REQUIRE(depth.ok());

  auto list = dev.createCommandList();
  REQUIRE(list.ok());

  auto drawQuad = [&](BindGroup& group, float depthOffset) {
    list.value()->bindPipeline(*pipeline.value());
    list.value()->bindGroup(0, group);
    list.value()->bindVertexBuffer(0, *vb.value());
    list.value()->bindIndexBuffer(*ib.value(), IndexType::U16);
    list.value()->pushConstants(&depthOffset, sizeof(depthOffset));
    list.value()->drawIndexed(6);
  };

  // Submission 1: textured quad, then a farther black quad that the depth test must reject.
  {
    REQUIRE(list.value()->begin().ok());
    RenderPassDesc pass;
    ColorAttachment ca;
    ca.texture = target.get();
    ca.clear[0] = 0.25f; ca.clear[1] = 0.25f; ca.clear[2] = 0.25f; ca.clear[3] = 1;
    pass.colors.push_back(ca);
    pass.depth.texture = depth.value().get();
    list.value()->beginRendering(pass);
    drawQuad(*white, 0.0f);
    drawQuad(*black, +0.3f);
    list.value()->endRendering();
    REQUIRE(list.value()->end().ok());
    auto serial = dev.submit(*list.value());
    REQUIRE(serial.ok());
    REQUIRE(dev.waitForSerial(serial.value()).ok());

    auto img = dev.readbackTexture(*target);
    REQUIRE(img.ok());
    const Px tl = pixel(img.value(), 64, 16, 16), tr = pixel(img.value(), 64, 48, 16);
    const Px bl = pixel(img.value(), 64, 16, 48), br = pixel(img.value(), 64, 48, 48);
    CHECK_MSG(nearPx(tl, 255, 0, 0, 255), "top-left " + str(tl));
    CHECK_MSG(nearPx(tr, 0, 255, 0, 255), "top-right " + str(tr));
    CHECK_MSG(nearPx(bl, 0, 0, 255, 255), "bottom-left " + str(bl));
    CHECK_MSG(nearPx(br, 255, 255, 255, 255), "bottom-right " + str(br));
  }

  // Submission 2 reuses the same list: Load both attachments, draw a nearer blue-tinted quad.
  {
    REQUIRE(list.value()->begin().ok());
    RenderPassDesc pass;
    ColorAttachment ca;
    ca.texture = target.get();
    ca.load = LoadOp::Load;
    pass.colors.push_back(ca);
    pass.depth.texture = depth.value().get();
    pass.depth.load = LoadOp::Load;
    list.value()->beginRendering(pass);
    drawQuad(*blue, -0.3f);
    list.value()->endRendering();
    REQUIRE(list.value()->end().ok());
    auto serial = dev.submit(*list.value());
    REQUIRE(serial.ok());
    REQUIRE(dev.waitForSerial(serial.value()).ok());

    auto img = dev.readbackTexture(*target);
    REQUIRE(img.ok());
    const Px tl = pixel(img.value(), 64, 16, 16), bl = pixel(img.value(), 64, 16, 48), br = pixel(img.value(), 64, 48, 48);
    CHECK_MSG(nearPx(tl, 0, 0, 0, 255), "top-left " + str(tl));      // red * blue tint
    CHECK_MSG(nearPx(bl, 0, 0, 255, 255), "bottom-left " + str(bl));  // blue * blue tint
    CHECK_MSG(nearPx(br, 0, 0, 255, 255), "bottom-right " + str(br)); // white * blue tint
  }
}

TEST(Render, AlphaBlending) {
  GPU_OR_SKIP();
  auto vs = makeShader(dev, R"(#version 450
void main() {
  vec2 p[6] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(1,1), vec2(-1,1), vec2(-1,-1));
  gl_Position = vec4(p[gl_VertexIndex], 0.0, 1.0);
})", shader::Stage::Vertex, "blend.vert");
  auto fs = makeShader(dev, R"(#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0, 0.0, 0.0, 0.5); })", shader::Stage::Fragment, "blend.frag");
  REQUIRE(vs != nullptr);
  REQUIRE(fs != nullptr);

  GraphicsPipelineDesc pd;
  pd.vertexShader = vs;
  pd.fragmentShader = fs;
  pd.cullMode = CullMode::None;
  pd.depthTest = false;
  pd.colorFormats = {Format::RGBA8Unorm};
  BlendState bs;
  bs.enable = true;
  pd.blend = {bs};
  auto pipeline = dev.createGraphicsPipeline(pd);
  REQUIRE(pipeline.ok());

  auto target = makeTarget(dev, 8, 8);
  REQUIRE(target != nullptr);
  auto list = dev.createCommandList();
  REQUIRE(list.ok());
  REQUIRE(list.value()->begin().ok());
  RenderPassDesc pass;
  ColorAttachment ca;
  ca.texture = target.get();
  ca.clear[0] = 0; ca.clear[1] = 0; ca.clear[2] = 1; ca.clear[3] = 1;  // blue background
  pass.colors.push_back(ca);
  list.value()->beginRendering(pass);
  list.value()->bindPipeline(*pipeline.value());
  list.value()->draw(6);
  list.value()->endRendering();
  REQUIRE(list.value()->end().ok());
  auto serial = dev.submit(*list.value());
  REQUIRE(serial.ok());
  REQUIRE(dev.waitForSerial(serial.value()).ok());

  auto img = dev.readbackTexture(*target);
  REQUIRE(img.ok());
  const Px p = pixel(img.value(), 8, 4, 4);  // 50% red over blue
  CHECK_MSG(nearPx(p, 128, 0, 128, 255, 2), "blended " + str(p));
}

// ---- Bind group / pipeline validation --------------------------------------------------------------

TEST(BindGroup, RejectsIncompleteOrWrongEntries) {
  GPU_OR_SKIP();
  BindGroupLayoutDesc bgl;
  bgl.bindings = {{0, BindingType::UniformBuffer, ShaderStage::Fragment, 1},
                  {1, BindingType::SampledTexture, ShaderStage::Fragment, 1}};
  auto layout = dev.createBindGroupLayout(bgl);
  REQUIRE(layout.ok());

  BufferDesc ubd;
  ubd.size = 16;
  ubd.usage = BufferUsage::Uniform;
  ubd.memory = MemoryType::CpuToGpu;
  auto ub = dev.createBuffer(ubd);
  BufferDesc vbd;
  vbd.size = 16;
  vbd.usage = BufferUsage::Vertex;
  auto notUniform = dev.createBuffer(vbd);
  TextureDesc td;
  auto tex = dev.createTexture(td);
  auto sampler = dev.createSampler({});
  REQUIRE(ub.ok());
  REQUIRE(notUniform.ok());
  REQUIRE(tex.ok());
  REQUIRE(sampler.ok());

  BindGroupEntry e0;
  e0.binding = 0;
  e0.buffer = ub.value();
  BindGroupEntry e1;
  e1.binding = 1;
  e1.texture = tex.value();
  e1.sampler = sampler.value();

  BindGroupDesc ok;
  ok.layout = layout.value();
  ok.entries = {e0, e1};
  CHECK(dev.createBindGroup(ok).ok());

  BindGroupDesc missing = ok;
  missing.entries = {e0};
  CHECK(!dev.createBindGroup(missing).ok());

  BindGroupDesc wrongBuffer = ok;
  BindGroupEntry bad = e0;
  bad.buffer = notUniform.value();
  wrongBuffer.entries = {bad, e1};
  CHECK(!dev.createBindGroup(wrongBuffer).ok());

  BindGroupDesc noSampler = ok;
  BindGroupEntry ns = e1;
  ns.sampler = nullptr;
  noSampler.entries = {e0, ns};
  CHECK(!dev.createBindGroup(noSampler).ok());

  BindGroupDesc dup = ok;
  dup.entries = {e0, e0, e1};
  CHECK(!dev.createBindGroup(dup).ok());

  BindGroupDesc unknown = ok;
  BindGroupEntry u = e0;
  u.binding = 7;
  unknown.entries = {e0, e1, u};
  CHECK(!dev.createBindGroup(unknown).ok());

  BindGroupDesc noLayout;
  CHECK(!dev.createBindGroup(noLayout).ok());

  BindGroupLayoutDesc empty;
  CHECK(!dev.createBindGroupLayout(empty).ok());
  BindGroupLayoutDesc dupBinding;
  dupBinding.bindings = {{0, BindingType::UniformBuffer, ShaderStage::Vertex, 1}, {0, BindingType::StorageBuffer, ShaderStage::Vertex, 1}};
  CHECK(!dev.createBindGroupLayout(dupBinding).ok());
}

TEST(Pipeline, RejectsInvalidDescriptions) {
  GPU_OR_SKIP();
  auto vs = makeShader(dev, "#version 450\nvoid main() { gl_Position = vec4(0.0); }", shader::Stage::Vertex, "p.vert");
  auto fs = makeShader(dev, "#version 450\nlayout(location=0) out vec4 c;\nvoid main() { c = vec4(1.0); }", shader::Stage::Fragment, "p.frag");
  REQUIRE(vs != nullptr);
  REQUIRE(fs != nullptr);

  GraphicsPipelineDesc good;
  good.vertexShader = vs;
  good.fragmentShader = fs;
  good.colorFormats = {Format::RGBA8Unorm};
  CHECK(dev.createGraphicsPipeline(good).ok());

  GraphicsPipelineDesc noVs = good;
  noVs.vertexShader = nullptr;
  CHECK(!dev.createGraphicsPipeline(noVs).ok());

  GraphicsPipelineDesc swapped = good;
  swapped.vertexShader = fs;  // fragment module in the vertex slot
  CHECK(!dev.createGraphicsPipeline(swapped).ok());

  GraphicsPipelineDesc noAttachments = good;
  noAttachments.colorFormats.clear();
  CHECK(!dev.createGraphicsPipeline(noAttachments).ok());

  GraphicsPipelineDesc badDepth = good;
  badDepth.depthFormat = Format::RGBA8Unorm;
  CHECK(!dev.createGraphicsPipeline(badDepth).ok());

  GraphicsPipelineDesc badBlend = good;
  badBlend.blend = {BlendState{}, BlendState{}};
  CHECK(!dev.createGraphicsPipeline(badBlend).ok());

  GraphicsPipelineDesc badPush = good;
  badPush.pushConstantSize = 6;
  CHECK(!dev.createGraphicsPipeline(badPush).ok());

  ShaderModuleDesc garbage;
  const std::uint32_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  garbage.spirv = junk;
  auto r = dev.createShaderModule(garbage);
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::Corrupt);
}

TEST(CommandList, MisuseIsReportedNotUndefined) {
  GPU_OR_SKIP();
  auto list = dev.createCommandList();
  REQUIRE(list.ok());
  CHECK(!list.value()->end().ok());  // end without begin
  REQUIRE(list.value()->begin().ok());
  CHECK(!list.value()->begin().ok());  // double begin
  REQUIRE(list.value()->end().ok());

  // Submitting a list that is not in the executable state must fail.
  auto fresh = dev.createCommandList();
  REQUIRE(fresh.ok());
  CHECK(!dev.submit(*fresh.value()).ok());
  CHECK(!dev.waitForSerial(999999).ok());  // never submitted
}

// ---- Lifetime / threading --------------------------------------------------------------------------

TEST(Lifetime, ResourcesMayBeDroppedWhileInFlight) {
  GPU_OR_SKIP();
  const std::uint64_t serial0 = [&] {
    auto list = dev.createCommandList();
    CHECK(list.value()->begin().ok());
    CHECK(list.value()->end().ok());
    return dev.submit(*list.value()).value();
  }();
  CHECK(serial0 > 0);

  std::uint64_t last = 0;
  for (int i = 0; i < 20; ++i) {
    auto target = makeTarget(dev, 128, 128);
    REQUIRE(target != nullptr);
    BufferDesc bd;
    bd.size = 128 * 128 * 4;
    bd.usage = BufferUsage::TransferDst;
    bd.memory = MemoryType::GpuToCpu;
    auto readback = dev.createBuffer(bd);
    REQUIRE(readback.ok());
    auto list = dev.createCommandList();
    REQUIRE(list.ok());
    REQUIRE(list.value()->begin().ok());
    RenderPassDesc pass;
    ColorAttachment ca;
    ca.texture = target.get();
    pass.colors.push_back(ca);
    list.value()->beginRendering(pass);
    list.value()->endRendering();
    list.value()->copyTextureToBuffer(*target, *readback.value());
    REQUIRE(list.value()->end().ok());
    auto s = dev.submit(*list.value());
    REQUIRE(s.ok());
    last = s.value();
    // target, readback and the list go out of scope immediately - the GPU may still be using them.
  }
  REQUIRE(dev.waitIdle().ok());
  CHECK(dev.isComplete(last));
}

TEST(Lifetime, ResourcesMayOutliveTheirDevice) {
  if (!gpu().device) SKIP_TEST(gpu().skipReason);
  std::shared_ptr<Buffer> buffer;
  std::shared_ptr<Texture> texture;
  {
    auto local = createVulkanDevice();
    REQUIRE(local.ok());
    BufferDesc bd;
    bd.size = 256;
    bd.usage = BufferUsage::Uniform;
    bd.memory = MemoryType::CpuToGpu;
    auto b = local.value()->createBuffer(bd);
    REQUIRE(b.ok());
    buffer = b.value();
    TextureDesc td;
    auto t = local.value()->createTexture(td);
    REQUIRE(t.ok());
    texture = t.value();
  }  // device object destroyed; the Vulkan device lives on until the last resource is released
  CHECK(buffer->mapped() != nullptr);
  std::memset(buffer->mapped(), 0xAB, 256);
  buffer.reset();
  texture.reset();  // must tear everything down cleanly
}

TEST(Lifetime, ConcurrentResourceCreation) {
  GPU_OR_SKIP();
  std::vector<std::thread> threads;
  std::vector<int> okCounts(4, 0);
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 25; ++i) {
        BufferDesc bd;
        bd.size = 1024;
        bd.usage = BufferUsage::Storage;
        TextureDesc td;
        td.width = td.height = 32;
        if (dev.createBuffer(bd).ok() && dev.createTexture(td).ok()) ++okCounts[t];
      }
    });
  }
  for (auto& th : threads) th.join();
  for (int c : okCounts) CHECK_EQ(c, 25);
}

// ---- Swapchain -------------------------------------------------------------------------------------

TEST(Swapchain, HeadlessPresentLoopAndResize) {
  GPU_OR_SKIP();
  SwapchainDesc sd;
  sd.window.kind = NativeWindow::Kind::Headless;
  sd.window.width = 128;
  sd.window.height = 96;
  sd.width = 128;
  sd.height = 96;
  auto created = dev.createSwapchain(sd);
  if (!created.ok()) SKIP_TEST("headless swapchain unavailable: " + created.error().describe());
  Swapchain& sc = *created.value();
  CHECK_EQ(sc.width(), 128u);
  CHECK_EQ(sc.height(), 96u);
  CHECK(sc.imageCount() >= 2);
  CHECK(sc.format() != Format::Undefined);

  auto list = dev.createCommandList();
  REQUIRE(list.ok());

  auto renderFrame = [&](float r, float g, float b) -> bool {
    auto image = sc.acquire();
    if (!image.ok()) { test::fail(__FILE__, __LINE__, "acquire: " + image.error().describe()); return false; }
    if (!list.value()->begin().ok()) return false;
    RenderPassDesc pass;
    ColorAttachment ca;
    ca.texture = image.value().texture;
    ca.clear[0] = r; ca.clear[1] = g; ca.clear[2] = b; ca.clear[3] = 1;
    pass.colors.push_back(ca);
    list.value()->beginRendering(pass);
    list.value()->endRendering();
    list.value()->transition(*image.value().texture, TextureState::Present);
    if (!list.value()->end().ok()) return false;
    auto serial = dev.submit(*list.value(), &sc);
    if (!serial.ok()) { test::fail(__FILE__, __LINE__, "submit: " + serial.error().describe()); return false; }
    auto pr = sc.present();
    if (!pr.ok()) { test::fail(__FILE__, __LINE__, "present: " + pr.error().describe()); return false; }
    return true;
  };

  for (int frame = 0; frame < 6; ++frame) CHECK(renderFrame(frame / 5.0f, 0.0f, 1.0f - frame / 5.0f));

  // Acquiring twice without presenting is a usage error, not a hang.
  auto first = sc.acquire();
  REQUIRE(first.ok());
  CHECK(!sc.acquire().ok());
  // Presenting an image that was never submitted is rejected too.
  CHECK(!sc.present().ok());
  // Finish the frame properly so the swapchain stays consistent.
  REQUIRE(list.value()->begin().ok());
  RenderPassDesc pass;
  ColorAttachment ca;
  ca.texture = first.value().texture;
  pass.colors.push_back(ca);
  list.value()->beginRendering(pass);
  list.value()->endRendering();
  list.value()->transition(*first.value().texture, TextureState::Present);
  REQUIRE(list.value()->end().ok());
  REQUIRE(dev.submit(*list.value(), &sc).ok());
  REQUIRE(sc.present().ok());

  REQUIRE(sc.resize(64, 48).ok());
  CHECK_EQ(sc.width(), 64u);
  CHECK_EQ(sc.height(), 48u);
  CHECK(renderFrame(0.0f, 1.0f, 0.0f));
  CHECK(!sc.resize(0, 10).ok());
  REQUIRE(dev.waitIdle().ok());
}

TEST(Swapchain, RenderedPixelsAreCorrectBeforePresent) {
  GPU_OR_SKIP();
  SwapchainDesc sd;
  sd.window.kind = NativeWindow::Kind::Headless;
  sd.window.width = sd.width = 32;
  sd.window.height = sd.height = 32;
  auto created = dev.createSwapchain(sd);
  if (!created.ok()) SKIP_TEST("headless swapchain unavailable: " + created.error().describe());
  Swapchain& sc = *created.value();

  auto image = sc.acquire();
  REQUIRE(image.ok());
  if (!hasFlag(image.value().texture->desc().usage, TextureUsage::TransferSrc)) SKIP_TEST("swapchain images lack TransferSrc");

  BufferDesc bd;
  bd.size = 32 * 32 * 4;
  bd.usage = BufferUsage::TransferDst;
  bd.memory = MemoryType::GpuToCpu;
  auto readback = dev.createBuffer(bd);
  REQUIRE(readback.ok());

  auto list = dev.createCommandList();
  REQUIRE(list.ok());
  REQUIRE(list.value()->begin().ok());
  RenderPassDesc pass;
  ColorAttachment ca;
  ca.texture = image.value().texture;
  ca.clear[0] = 1; ca.clear[1] = 0; ca.clear[2] = 0; ca.clear[3] = 1;  // pure red
  pass.colors.push_back(ca);
  list.value()->beginRendering(pass);
  list.value()->endRendering();
  list.value()->copyTextureToBuffer(*image.value().texture, *readback.value());
  list.value()->transition(*image.value().texture, TextureState::Present);
  REQUIRE(list.value()->end().ok());
  auto serial = dev.submit(*list.value(), &sc);
  REQUIRE(serial.ok());
  REQUIRE(sc.present().ok());
  REQUIRE(dev.waitForSerial(serial.value()).ok());
  readback.value()->invalidate();

  const auto* px = static_cast<const std::uint8_t*>(readback.value()->mapped());
  const bool bgra = sc.format() == Format::BGRA8Srgb || sc.format() == Format::BGRA8Unorm;
  const int red = bgra ? px[2] : px[0];
  const int blue = bgra ? px[0] : px[2];
  CHECK_EQ(red, 255);
  CHECK_EQ(blue, 0);
  CHECK_EQ(static_cast<int>(px[1]), 0);
  REQUIRE(dev.waitIdle().ok());
}
