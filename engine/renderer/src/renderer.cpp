// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/renderer/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

#include "mingol/core/log.hpp"
#include "mingol/renderer/animation.hpp"
#include "mingol/shader/shader_compiler.hpp"
#include "shaders.hpp"

namespace mingol::renderer {

using namespace rhi;

namespace {

constexpr std::uint32_t kObjectDataSize = 128;

// std140 mirror of FrameUBO in shaders.cpp.
struct FrameData {
  float viewProj[16];
  float shadowViewProj[16];
  float cameraPos[4];
  float ambient[4];
  float dirDirection[4];
  float dirColor[4];
  float shadowParams[4];
  std::int32_t counts[4];
  float lightPosRange[kMaxPunctualLights][4];
  float lightColorType[kMaxPunctualLights][4];
  float lightDirCos[kMaxPunctualLights][4];
  float lightSpot[kMaxPunctualLights][4];
};
static_assert(sizeof(FrameData) == 736, "FrameData must match the std140 layout of FrameUBO");

// std430 mirror of ObjectData.
struct ObjectData {
  float model[16];
  float normalCols[12];
  std::uint32_t skin[4];
};
static_assert(sizeof(ObjectData) == kObjectDataSize);

// std140 mirror of MaterialUBO.
struct MaterialData {
  float baseColor[4];
  float emissive[4];
  float params[4];
};
static_assert(sizeof(MaterialData) == 48);

bool isSrgb(Format f) { return f == Format::RGBA8Srgb || f == Format::BGRA8Srgb; }
bool isSupportedTarget(Format f) {
  return f == Format::RGBA8Srgb || f == Format::BGRA8Srgb || f == Format::RGBA8Unorm || f == Format::BGRA8Unorm;
}

// Inverse transpose of the upper 3x3, stored as three vec4 columns.
void normalMatrix(const Mat4& model, float out[12]) {
  Mat4 inv;
  if (!inverse(model, inv)) inv = Mat4::identity();
  const Mat4 t = transpose(inv);
  for (int c = 0; c < 3; ++c) {
    out[c * 4 + 0] = t.m[c * 4 + 0];
    out[c * 4 + 1] = t.m[c * 4 + 1];
    out[c * 4 + 2] = t.m[c * 4 + 2];
    out[c * 4 + 3] = 0.0F;
  }
}

bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finiteMat(const Mat4& m) {
  for (float v : m.m) {
    if (!std::isfinite(v)) return false;
  }
  return true;
}

std::span<const std::uint8_t> asBytes(const void* p, std::size_t n) {
  return {static_cast<const std::uint8_t*>(p), n};
}

}  // namespace

struct Renderer::Impl {
  struct Slot {
    std::shared_ptr<Buffer> frame, objects, skin;
    std::shared_ptr<Texture> shadow;
    std::shared_ptr<BindGroup> mainGroup;    // set 0 for the colour pass (includes the shadow map)
    std::shared_ptr<BindGroup> shadowGroup;  // set 0 for the shadow pass (no texture)
    std::unique_ptr<CommandList> list;
    std::uint64_t serial = 0;
  };

  Impl(Device& d, const RendererDesc& desc) : dev(d), cfg(desc) {}

  Device& dev;
  RendererDesc cfg;
  std::shared_ptr<BindGroupLayout> frameLayout, shadowFrameLayout, materialLayout;
  std::shared_ptr<Sampler> shadowSampler, materialSampler;
  std::shared_ptr<Texture> white, flatNormal;
  std::vector<Slot> slots;
  std::uint64_t frameCounter = 0;
  std::shared_ptr<Texture> depth;
  std::map<int, std::shared_ptr<ShaderModule>> shaderModules;
  std::map<std::uint64_t, std::shared_ptr<GraphicsPipeline>> pipelines;
  std::uint32_t pipelinesCreated = 0;

  // ---- setup ----------------------------------------------------------------------------------
  Result<void> init() {
    const ShaderStage vf = ShaderStage::Vertex | ShaderStage::Fragment;
    {
      BindGroupLayoutDesc d;
      d.debugName = "frame";
      d.bindings = {{0, BindingType::UniformBuffer, vf, 1},
                    {1, BindingType::StorageBuffer, ShaderStage::Vertex, 1},
                    {2, BindingType::StorageBuffer, ShaderStage::Vertex, 1},
                    {3, BindingType::SampledTexture, ShaderStage::Fragment, 1}};
      MINGOL_TRY_ASSIGN(frameLayout, dev.createBindGroupLayout(d));
      d.debugName = "frame-shadow";
      d.bindings.pop_back();
      MINGOL_TRY_ASSIGN(shadowFrameLayout, dev.createBindGroupLayout(d));
    }
    {
      BindGroupLayoutDesc d;
      d.debugName = "material";
      d.bindings = {{0, BindingType::UniformBuffer, ShaderStage::Fragment, 1},
                    {1, BindingType::SampledTexture, ShaderStage::Fragment, 1},
                    {2, BindingType::SampledTexture, ShaderStage::Fragment, 1},
                    {3, BindingType::SampledTexture, ShaderStage::Fragment, 1},
                    {4, BindingType::SampledTexture, ShaderStage::Fragment, 1}};
      MINGOL_TRY_ASSIGN(materialLayout, dev.createBindGroupLayout(d));
    }
    {
      SamplerDesc s;
      s.compareEnable = true;
      s.compareOp = CompareOp::LessOrEqual;
      s.minFilter = s.magFilter = FilterMode::Linear;
      s.mipFilter = FilterMode::Nearest;
      s.addressU = s.addressV = s.addressW = AddressMode::ClampToEdge;
      s.maxLod = 0.0F;
      MINGOL_TRY_ASSIGN(shadowSampler, dev.createSampler(s));
      SamplerDesc m;
      MINGOL_TRY_ASSIGN(materialSampler, dev.createSampler(m));
    }
    {
      TextureDesc t;
      t.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
      t.debugName = "default-white";
      const std::uint8_t whitePx[4] = {255, 255, 255, 255};
      MINGOL_TRY_ASSIGN(white, dev.createTextureWithData(t, asBytes(whitePx, 4), false));
      t.debugName = "default-normal";
      const std::uint8_t flat[4] = {128, 128, 255, 255};
      MINGOL_TRY_ASSIGN(flatNormal, dev.createTextureWithData(t, asBytes(flat, 4), false));
    }
    const std::uint32_t frames = std::max(1U, cfg.framesInFlight);
    slots.resize(frames);
    for (std::uint32_t i = 0; i < frames; ++i) {
      Slot& s = slots[i];
      BufferDesc b;
      b.memory = MemoryType::CpuToGpu;
      b.usage = BufferUsage::Uniform;
      b.size = sizeof(FrameData);
      b.debugName = "frame-ubo";
      MINGOL_TRY_ASSIGN(s.frame, dev.createBuffer(b));
      b.usage = BufferUsage::Storage;
      b.size = static_cast<std::uint64_t>(cfg.maxObjects) * kObjectDataSize;
      b.debugName = "objects";
      MINGOL_TRY_ASSIGN(s.objects, dev.createBuffer(b));
      b.size = static_cast<std::uint64_t>(std::max(1U, cfg.maxSkinMatrices)) * sizeof(Mat4);
      b.debugName = "skin-matrices";
      MINGOL_TRY_ASSIGN(s.skin, dev.createBuffer(b));

      TextureDesc sm;
      sm.width = sm.height = cfg.shadowMapSize;
      sm.format = Format::D32Float;
      sm.usage = TextureUsage::DepthStencil | TextureUsage::Sampled;
      sm.debugName = "shadow-map";
      MINGOL_TRY_ASSIGN(s.shadow, dev.createTexture(sm));

      BindGroupDesc g;
      g.layout = frameLayout;
      g.debugName = "frame-main";
      BindGroupEntry e0;
      e0.binding = 0;
      e0.buffer = s.frame;
      BindGroupEntry e1;
      e1.binding = 1;
      e1.buffer = s.objects;
      BindGroupEntry e2;
      e2.binding = 2;
      e2.buffer = s.skin;
      g.entries = {e0, e1, e2};
      BindGroupEntry e3;
      e3.binding = 3;
      e3.texture = s.shadow;
      e3.sampler = shadowSampler;
      g.entries.push_back(e3);
      MINGOL_TRY_ASSIGN(s.mainGroup, dev.createBindGroup(g));
      g.layout = shadowFrameLayout;
      g.debugName = "frame-shadow";
      g.entries.pop_back();
      MINGOL_TRY_ASSIGN(s.shadowGroup, dev.createBindGroup(g));

      MINGOL_TRY_ASSIGN(s.list, dev.createCommandList());
    }
    return {};
  }

  // ---- shaders / pipelines --------------------------------------------------------------------
  enum Variant { kVs = 0, kVsSkinned = 1, kShadowVs = 2, kShadowVsSkinned = 3, kFs = 4 };

  Result<std::shared_ptr<ShaderModule>> shaderFor(Variant v) {
    if (const auto it = shaderModules.find(v); it != shaderModules.end()) return it->second;
    std::string src = "#version 450\n";
    shader::CompileOptions opt;
    opt.language = shader::Language::Glsl;
    opt.stage = v == kFs ? shader::Stage::Fragment : shader::Stage::Vertex;
    const bool skinned = v == kVsSkinned || v == kShadowVsSkinned;
    const bool shadowPass = v == kShadowVs || v == kShadowVsSkinned;
    if (skinned) src += "#define SKINNED 1\n";
    if (shadowPass) src += "#define SHADOW_PASS 1\n";
    src += shaders::kCommon;
    src += v == kFs ? shaders::kFragment : (shadowPass ? shaders::kShadowVertex : shaders::kVertex);
    static const char* const kNames[] = {"mesh.vert", "mesh_skinned.vert", "shadow.vert", "shadow_skinned.vert", "pbr.frag"};
    opt.sourceName = kNames[v];
    MINGOL_TRY_ASSIGN(shader::SpirV spirv, shader::compile(src, opt));
    ShaderModuleDesc d;
    d.spirv = spirv;
    d.stage = v == kFs ? ShaderStage::Fragment : ShaderStage::Vertex;
    d.debugName = opt.sourceName;
    MINGOL_TRY_ASSIGN(std::shared_ptr<ShaderModule> m, dev.createShaderModule(d));
    shaderModules[v] = m;
    return m;
  }

  static std::uint64_t pipelineKey(Format f, bool skinned, bool blend, bool doubleSided, bool shadow) {
    return static_cast<std::uint64_t>(f) | (skinned ? 1ULL << 8 : 0) | (blend ? 1ULL << 9 : 0) |
           (doubleSided ? 1ULL << 10 : 0) | (shadow ? 1ULL << 11 : 0);
  }

  Result<GraphicsPipeline*> pipelineFor(Format target, bool skinned, bool blend, bool doubleSided, bool shadow) {
    const std::uint64_t key = pipelineKey(shadow ? Format::Undefined : target, skinned, blend && !shadow,
                                          doubleSided && !shadow, shadow);
    if (const auto it = pipelines.find(key); it != pipelines.end()) return it->second.get();

    GraphicsPipelineDesc d;
    MINGOL_TRY_ASSIGN(d.vertexShader, shaderFor(shadow ? (skinned ? kShadowVsSkinned : kShadowVs)
                                                      : (skinned ? kVsSkinned : kVs)));
    if (!shadow) {
      MINGOL_TRY_ASSIGN(d.fragmentShader, shaderFor(kFs));
    }
    d.vertexBindings = {{0, sizeof(Vertex), false}};
    d.vertexAttributes = {{0, 0, Format::RGB32Float, 0},
                          {1, 0, Format::RGB32Float, 12},
                          {2, 0, Format::RG32Float, 24},
                          {3, 0, Format::RGBA32Float, 32}};
    if (skinned) {
      d.vertexBindings.push_back({1, sizeof(SkinVertex), false});
      d.vertexAttributes.push_back({4, 1, Format::RGBA32Float, 0});
      d.vertexAttributes.push_back({5, 1, Format::RGBA32Float, 16});
    }
    d.depthFormat = Format::D32Float;
    d.depthTest = true;
    if (shadow) {
      d.cullMode = CullMode::None;
      d.depthBias = true;
      d.depthBiasConstant = cfg.shadowDepthBiasConstant;
      d.depthBiasSlope = cfg.shadowDepthBiasSlope;
      d.setLayouts = {shadowFrameLayout};
      d.debugName = skinned ? "shadow-skinned" : "shadow";
    } else {
      d.cullMode = doubleSided ? CullMode::None : CullMode::Back;
      d.colorFormats = {target};
      if (blend) {
        BlendState b;
        b.enable = true;
        d.blend = {b};
        d.depthWrite = false;
      }
      d.setLayouts = {frameLayout, materialLayout};
      d.debugName = std::string("forward") + (skinned ? "-skinned" : "") + (blend ? "-blend" : "") +
                    (doubleSided ? "-2sided" : "");
    }
    MINGOL_TRY_ASSIGN(std::shared_ptr<GraphicsPipeline> p, dev.createGraphicsPipeline(d));
    ++pipelinesCreated;
    GraphicsPipeline* raw = p.get();
    pipelines[key] = std::move(p);
    return raw;
  }

  Result<void> ensureDepth(std::uint32_t w, std::uint32_t h) {
    if (depth && depth->desc().width == w && depth->desc().height == h) return {};
    TextureDesc t;
    t.width = w;
    t.height = h;
    t.format = Format::D32Float;
    t.usage = TextureUsage::DepthStencil;
    t.debugName = "scene-depth";
    MINGOL_TRY_ASSIGN(depth, dev.createTexture(t));  // the old one is released; destruction is deferred
    return {};
  }
};

Renderer::Renderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Renderer::~Renderer() {
  if (impl_) {
    // Frames still running on the GPU reference our buffers; wait so teardown is orderly.
    (void)impl_->dev.waitIdle();
  }
}

Result<std::unique_ptr<Renderer>> Renderer::create(Device& device, const RendererDesc& desc) {
  if (desc.maxObjects == 0 || desc.shadowMapSize == 0) {
    return makeError(ErrorCode::InvalidArgument, "RendererDesc: maxObjects and shadowMapSize must be non-zero");
  }
  auto impl = std::make_unique<Impl>(device, desc);
  MINGOL_TRY(impl->init());
  return std::unique_ptr<Renderer>(new Renderer(std::move(impl)));
}

Result<std::shared_ptr<Mesh>> Renderer::createMesh(const MeshData& data) {
  MINGOL_TRY(data.validate());
  auto mesh = std::make_shared<Mesh>();
  BufferDesc b;
  b.memory = MemoryType::GpuOnly;
  b.usage = BufferUsage::Vertex | BufferUsage::TransferDst;
  b.size = data.vertices.size() * sizeof(Vertex);
  b.debugName = "mesh-vertices";
  MINGOL_TRY_ASSIGN(mesh->vertices_, impl_->dev.createBufferWithData(b, asBytes(data.vertices.data(), b.size)));
  if (!data.skin.empty()) {
    b.size = data.skin.size() * sizeof(SkinVertex);
    b.debugName = "mesh-skin";
    MINGOL_TRY_ASSIGN(mesh->skin_, impl_->dev.createBufferWithData(b, asBytes(data.skin.data(), b.size)));
    for (const SkinVertex& s : data.skin) {
      for (int k = 0; k < 4; ++k) {
        if (s.weights[k] > 0.0F) mesh->maxJoint_ = std::max(mesh->maxJoint_, static_cast<std::uint32_t>(s.joints[k]));
      }
    }
  }
  b.usage = BufferUsage::Index | BufferUsage::TransferDst;
  b.size = data.indices.size() * sizeof(std::uint32_t);
  b.debugName = "mesh-indices";
  MINGOL_TRY_ASSIGN(mesh->indices_, impl_->dev.createBufferWithData(b, asBytes(data.indices.data(), b.size)));
  mesh->indexCount_ = static_cast<std::uint32_t>(data.indices.size());
  mesh->vertexCount_ = static_cast<std::uint32_t>(data.vertices.size());
  mesh->bounds_ = data.bounds();
  return mesh;
}

Result<std::shared_ptr<Material>> Renderer::createMaterial(const MaterialDesc& desc) {
  const auto bad = [](float v) { return !std::isfinite(v); };
  if (bad(desc.baseColor.x) || bad(desc.baseColor.y) || bad(desc.baseColor.z) || bad(desc.baseColor.w) ||
      bad(desc.metallic) || bad(desc.roughness) || bad(desc.normalScale) || !finite(desc.emissive)) {
    return makeError(ErrorCode::InvalidArgument, "material has a non-finite parameter");
  }
  auto m = std::make_shared<Material>();
  m->desc_ = desc;

  MaterialData data{};
  data.baseColor[0] = desc.baseColor.x;
  data.baseColor[1] = desc.baseColor.y;
  data.baseColor[2] = desc.baseColor.z;
  data.baseColor[3] = desc.baseColor.w;
  data.emissive[0] = desc.emissive.x;
  data.emissive[1] = desc.emissive.y;
  data.emissive[2] = desc.emissive.z;
  data.emissive[3] = desc.normalScale;
  data.params[0] = clamp(desc.metallic, 0.0F, 1.0F);
  data.params[1] = clamp(desc.roughness, 0.0F, 1.0F);
  data.params[2] = desc.alphaMode == AlphaMode::Blend ? 1.0F : 0.0F;

  BufferDesc b;
  b.size = sizeof(MaterialData);
  b.usage = BufferUsage::Uniform;
  b.memory = MemoryType::CpuToGpu;
  b.debugName = "material-ubo";
  MINGOL_TRY_ASSIGN(m->uniforms_, impl_->dev.createBufferWithData(b, asBytes(&data, sizeof(data))));

  BindGroupDesc g;
  g.layout = impl_->materialLayout;
  g.debugName = "material";
  BindGroupEntry u;
  u.binding = 0;
  u.buffer = m->uniforms_;
  g.entries.push_back(u);
  const std::shared_ptr<Texture> textures[4] = {
      desc.baseColorTexture ? desc.baseColorTexture : impl_->white,
      desc.metallicRoughnessTexture ? desc.metallicRoughnessTexture : impl_->white,
      desc.normalTexture ? desc.normalTexture : impl_->flatNormal,
      desc.emissiveTexture ? desc.emissiveTexture : impl_->white};
  for (std::uint32_t i = 0; i < 4; ++i) {
    BindGroupEntry e;
    e.binding = i + 1;
    e.texture = textures[i];
    e.sampler = impl_->materialSampler;
    g.entries.push_back(e);
  }
  MINGOL_TRY_ASSIGN(m->bindGroup_, impl_->dev.createBindGroup(g));
  return m;
}

Result<std::uint64_t> Renderer::render(const RenderScene& scene, Texture& target, Swapchain* presenting) {
  Impl& I = *impl_;
  const RendererDesc& cfg = I.cfg;
  const TextureDesc& td = target.desc();
  if (!hasFlag(td.usage, TextureUsage::ColorAttachment)) {
    return makeError(ErrorCode::InvalidArgument, "render target lacks ColorAttachment usage");
  }
  if (!isSupportedTarget(td.format)) {
    return makeError(ErrorCode::Unsupported, "render target format must be RGBA8/BGRA8 (sRGB or UNORM)");
  }

  // ---- validate the scene ------------------------------------------------------------------------
  if (scene.items.size() > cfg.maxObjects) {
    return makeError(ErrorCode::OutOfRange, "scene has {} items, the renderer was created for {}", scene.items.size(),
                     cfg.maxObjects);
  }
  if (!finiteMat(scene.camera.view) || !finiteMat(scene.camera.projection) || !finite(scene.camera.position)) {
    return makeError(ErrorCode::InvalidArgument, "camera has non-finite values");
  }
  const Light* dirLight = nullptr;
  std::vector<const Light*> punctual;
  for (const Light& l : scene.lights) {
    if (!finite(l.color) || !std::isfinite(l.intensity) || l.intensity < 0.0F || !finite(l.direction) ||
        !finite(l.position) || !std::isfinite(l.range)) {
      return makeError(ErrorCode::InvalidArgument, "light has invalid values");
    }
    if (l.type != LightType::Point && lengthSq(l.direction) < 1e-10F) {
      return makeError(ErrorCode::InvalidArgument, "directional/spot light needs a non-zero direction");
    }
    if (l.type == LightType::Directional) {
      if (dirLight != nullptr) return makeError(ErrorCode::InvalidArgument, "only one directional light is supported");
      dirLight = &l;
    } else {
      if (l.range <= 0.0F) return makeError(ErrorCode::InvalidArgument, "point/spot light needs a positive range");
      if (l.type == LightType::Spot &&
          !(l.innerConeRadians >= 0.0F && l.innerConeRadians < l.outerConeRadians && l.outerConeRadians < kPi)) {
        return makeError(ErrorCode::InvalidArgument, "spot light cones must satisfy 0 <= inner < outer < pi");
      }
      punctual.push_back(&l);
    }
  }
  if (punctual.size() > static_cast<std::size_t>(kMaxPunctualLights)) {
    return makeError(ErrorCode::OutOfRange, "{} point/spot lights, the maximum is {}", punctual.size(), kMaxPunctualLights);
  }
  std::uint32_t skinTotal = 0;
  for (std::size_t i = 0; i < scene.items.size(); ++i) {
    const DrawItem& it = scene.items[i];
    if (!it.mesh || !it.material) return makeError(ErrorCode::InvalidArgument, "item {} has no mesh or material", i);
    if (!finiteMat(it.transform)) return makeError(ErrorCode::InvalidArgument, "item {} has a non-finite transform", i);
    if (it.mesh->skinned()) {
      if (it.skinMatrices.empty() || it.skinMatrices.size() > kMaxJoints ||
          it.mesh->maxJointIndex() >= it.skinMatrices.size()) {
        return makeError(ErrorCode::InvalidArgument,
                         "item {}: skinned mesh uses joint {} but {} joint matrices were supplied", i,
                         it.mesh->maxJointIndex(), it.skinMatrices.size());
      }
      skinTotal += static_cast<std::uint32_t>(it.skinMatrices.size());
      if (skinTotal > cfg.maxSkinMatrices) {
        return makeError(ErrorCode::OutOfRange, "scene needs more than {} joint matrices", cfg.maxSkinMatrices);
      }
    } else if (!it.skinMatrices.empty()) {
      return makeError(ErrorCode::InvalidArgument, "item {}: joint matrices supplied for a static mesh", i);
    }
  }
  if (td.width == 0 || td.height == 0) return makeError(ErrorCode::InvalidArgument, "empty render target");

  // ---- pick a frame slot and make sure the GPU is done with it -----------------------------------
  Impl::Slot& slot = I.slots[I.frameCounter % I.slots.size()];
  if (slot.serial != 0) MINGOL_TRY(I.dev.waitForSerial(slot.serial));
  MINGOL_TRY(I.ensureDepth(td.width, td.height));

  // ---- shadow setup ------------------------------------------------------------------------------
  const bool shadowsEnabled = dirLight != nullptr && dirLight->castShadow;
  Mat4 shadowViewProj;
  float shadowWorldTexel = 0.0F;
  if (shadowsEnabled) {
    Vec3 center{0, 0, 0};
    float radius = 1.0F;
    if (scene.shadowFocus) {
      center = scene.shadowFocus->center;
      radius = std::max(scene.shadowFocus->radius, 1e-3F);
    } else {
      bool any = false;
      Vec3 lo{}, hi{};
      for (const DrawItem& it : scene.items) {
        if (!it.castShadow) continue;
        const Aabb& b = it.mesh->bounds();
        for (int c = 0; c < 8; ++c) {
          const Vec3 corner{(c & 1) ? b.max.x : b.min.x, (c & 2) ? b.max.y : b.min.y, (c & 4) ? b.max.z : b.min.z};
          const Vec3 w = transformPoint(it.transform, corner);
          if (!any) {
            lo = hi = w;
            any = true;
          } else {
            lo = {std::fmin(lo.x, w.x), std::fmin(lo.y, w.y), std::fmin(lo.z, w.z)};
            hi = {std::fmax(hi.x, w.x), std::fmax(hi.y, w.y), std::fmax(hi.z, w.z)};
          }
        }
      }
      if (any) {
        center = (lo + hi) * 0.5F;
        radius = std::max(length(hi - lo) * 0.5F, 1e-3F);
      }
    }
    const Vec3 dir = normalize(dirLight->direction);
    const Vec3 up = std::fabs(dir.y) > 0.99F ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    // The light sits 2r behind the focus sphere so casters outside it (between sphere and light)
    // are still captured; depth range covers 4r.
    const Mat4 view = Mat4::lookAt(center - dir * (2.0F * radius), center, up);
    const Mat4 proj = Mat4::orthographic(-radius, radius, -radius, radius, 0.0F, 4.0F * radius);
    shadowViewProj = proj * view;
    shadowWorldTexel = 2.0F * radius / static_cast<float>(cfg.shadowMapSize);
  }

  // ---- upload per-frame data ---------------------------------------------------------------------
  FrameData fd{};
  const Mat4 viewProj = scene.camera.projection * scene.camera.view;
  std::memcpy(fd.viewProj, viewProj.m, sizeof(fd.viewProj));
  std::memcpy(fd.shadowViewProj, shadowViewProj.m, sizeof(fd.shadowViewProj));
  fd.cameraPos[0] = scene.camera.position.x;
  fd.cameraPos[1] = scene.camera.position.y;
  fd.cameraPos[2] = scene.camera.position.z;
  fd.ambient[0] = scene.ambient.x;
  fd.ambient[1] = scene.ambient.y;
  fd.ambient[2] = scene.ambient.z;
  fd.ambient[3] = isSrgb(td.format) ? 1.0F : 0.0F;
  if (dirLight != nullptr) {
    const Vec3 d = normalize(dirLight->direction);
    fd.dirDirection[0] = d.x;
    fd.dirDirection[1] = d.y;
    fd.dirDirection[2] = d.z;
    fd.dirDirection[3] = 1.0F;
    fd.dirColor[0] = dirLight->color.x * dirLight->intensity;
    fd.dirColor[1] = dirLight->color.y * dirLight->intensity;
    fd.dirColor[2] = dirLight->color.z * dirLight->intensity;
    fd.dirColor[3] = shadowsEnabled ? 1.0F : 0.0F;
  }
  fd.shadowParams[0] = cfg.shadowNormalBias * shadowWorldTexel;
  fd.shadowParams[1] = 1.0F / static_cast<float>(cfg.shadowMapSize);
  fd.shadowParams[2] = 1.0F;  // PCF radius in texels: a 3x3 kernel
  fd.counts[0] = static_cast<std::int32_t>(punctual.size());
  for (std::size_t i = 0; i < punctual.size(); ++i) {
    const Light& l = *punctual[i];
    fd.lightPosRange[i][0] = l.position.x;
    fd.lightPosRange[i][1] = l.position.y;
    fd.lightPosRange[i][2] = l.position.z;
    fd.lightPosRange[i][3] = l.range;
    fd.lightColorType[i][0] = l.color.x * l.intensity;
    fd.lightColorType[i][1] = l.color.y * l.intensity;
    fd.lightColorType[i][2] = l.color.z * l.intensity;
    fd.lightColorType[i][3] = l.type == LightType::Spot ? 2.0F : 1.0F;
    const Vec3 d = normalize(l.direction);
    fd.lightDirCos[i][0] = d.x;
    fd.lightDirCos[i][1] = d.y;
    fd.lightDirCos[i][2] = d.z;
    if (l.type == LightType::Spot) {
      const float cosOuter = std::cos(l.outerConeRadians);
      const float cosInner = std::cos(l.innerConeRadians);
      fd.lightDirCos[i][3] = cosOuter;
      fd.lightSpot[i][0] = 1.0F / std::max(cosInner - cosOuter, 1e-4F);
    }
  }
  std::memcpy(slot.frame->mapped(), &fd, sizeof(fd));
  slot.frame->flush();

  auto* objects = static_cast<ObjectData*>(slot.objects->mapped());
  auto* skin = static_cast<Mat4*>(slot.skin->mapped());
  std::uint32_t skinCursor = 0;
  for (std::size_t i = 0; i < scene.items.size(); ++i) {
    const DrawItem& it = scene.items[i];
    ObjectData& o = objects[i];
    std::memcpy(o.model, it.transform.m, sizeof(o.model));
    normalMatrix(it.transform, o.normalCols);
    o.skin[0] = skinCursor;
    o.skin[1] = o.skin[2] = o.skin[3] = 0;
    if (it.mesh->skinned()) {
      std::memcpy(skin + skinCursor, it.skinMatrices.data(), it.skinMatrices.size() * sizeof(Mat4));
      skinCursor += static_cast<std::uint32_t>(it.skinMatrices.size());
    }
  }
  if (!scene.items.empty()) slot.objects->flush(0, scene.items.size() * kObjectDataSize);
  if (skinCursor > 0) slot.skin->flush(0, static_cast<std::uint64_t>(skinCursor) * sizeof(Mat4));

  // ---- record --------------------------------------------------------------------------------------
  FrameStats st;
  st.objects = static_cast<std::uint32_t>(scene.items.size());
  st.shadowsRendered = shadowsEnabled;
  CommandList& list = *slot.list;
  MINGOL_TRY(list.begin());

  // Shadow pass. Always executed (cleared to far depth when there is no shadow) so the map is a
  // valid, initialised depth texture in ShaderRead state for the colour pass.
  {
    RenderPassDesc sp;
    sp.debugName = "shadow";
    sp.depth.texture = slot.shadow.get();
    sp.depth.load = LoadOp::Clear;
    sp.depth.store = StoreOp::Store;
    sp.depth.clearDepth = 1.0F;
    list.beginRendering(sp);
    if (shadowsEnabled) {
      list.setViewport(0, 0, static_cast<float>(cfg.shadowMapSize), static_cast<float>(cfg.shadowMapSize));
      list.setScissor(0, 0, cfg.shadowMapSize, cfg.shadowMapSize);
      GraphicsPipeline* bound = nullptr;
      for (int pass = 0; pass < 2; ++pass) {  // static casters first, then skinned (fewer pipeline switches)
        const bool skinnedPass = pass == 1;
        for (std::size_t i = 0; i < scene.items.size(); ++i) {
          const DrawItem& it = scene.items[i];
          if (!it.castShadow || it.mesh->skinned() != skinnedPass) continue;
          MINGOL_TRY_ASSIGN(GraphicsPipeline * pipe, I.pipelineFor(td.format, skinnedPass, false, false, true));
          if (pipe != bound) {
            list.bindPipeline(*pipe);
            list.bindGroup(0, *slot.shadowGroup);
            bound = pipe;
          }
          list.bindVertexBuffer(0, *it.mesh->vertices_);
          if (skinnedPass) list.bindVertexBuffer(1, *it.mesh->skin_);
          list.bindIndexBuffer(*it.mesh->indices_, IndexType::U32);
          list.drawIndexed(it.mesh->indexCount(), 1, 0, 0, static_cast<std::uint32_t>(i));
          ++st.shadowDrawCalls;
        }
      }
    }
    list.endRendering();
    list.transition(*slot.shadow, TextureState::ShaderRead);
  }

  // Colour pass.
  {
    RenderPassDesc mp;
    mp.debugName = "forward";
    ColorAttachment ca;
    ca.texture = &target;
    ca.load = LoadOp::Clear;
    ca.store = StoreOp::Store;
    for (int k = 0; k < 4; ++k) ca.clear[k] = scene.clearColor[k];
    mp.colors.push_back(ca);
    mp.depth.texture = I.depth.get();
    mp.depth.load = LoadOp::Clear;
    mp.depth.store = StoreOp::DontCare;
    mp.depth.clearDepth = 1.0F;
    list.beginRendering(mp);
    list.setViewport(0, 0, static_cast<float>(td.width), static_cast<float>(td.height));
    list.setScissor(0, 0, td.width, td.height);

    struct Draw {
      std::size_t item;
      std::uint64_t key;
      float viewZ;
    };
    std::vector<Draw> opaque, blended;
    for (std::size_t i = 0; i < scene.items.size(); ++i) {
      const DrawItem& it = scene.items[i];
      const MaterialDesc& md = it.material->desc();
      const bool blend = md.alphaMode == AlphaMode::Blend;
      Draw d{i, Impl::pipelineKey(td.format, it.mesh->skinned(), blend, md.doubleSided, false), 0.0F};
      if (blend) {
        const Vec3 c = transformPoint(it.transform, it.mesh->bounds().center());
        d.viewZ = transformPoint(scene.camera.view, c).z;  // more negative = farther
        blended.push_back(d);
      } else {
        opaque.push_back(d);
      }
    }
    std::stable_sort(opaque.begin(), opaque.end(), [&](const Draw& a, const Draw& b) {
      if (a.key != b.key) return a.key < b.key;
      return scene.items[a.item].material.get() < scene.items[b.item].material.get();
    });
    std::stable_sort(blended.begin(), blended.end(), [](const Draw& a, const Draw& b) { return a.viewZ < b.viewZ; });

    GraphicsPipeline* boundPipe = nullptr;
    const Material* boundMat = nullptr;
    auto emit = [&](const Draw& d) -> Result<void> {
      const DrawItem& it = scene.items[d.item];
      const MaterialDesc& md = it.material->desc();
      MINGOL_TRY_ASSIGN(GraphicsPipeline * pipe,
                        I.pipelineFor(td.format, it.mesh->skinned(), md.alphaMode == AlphaMode::Blend, md.doubleSided, false));
      if (pipe != boundPipe) {
        list.bindPipeline(*pipe);
        list.bindGroup(0, *slot.mainGroup);
        boundPipe = pipe;
        boundMat = nullptr;
      }
      if (it.material.get() != boundMat) {
        list.bindGroup(1, *it.material->bindGroup_);
        boundMat = it.material.get();
      }
      list.bindVertexBuffer(0, *it.mesh->vertices_);
      if (it.mesh->skinned()) list.bindVertexBuffer(1, *it.mesh->skin_);
      list.bindIndexBuffer(*it.mesh->indices_, IndexType::U32);
      list.drawIndexed(it.mesh->indexCount(), 1, 0, 0, static_cast<std::uint32_t>(d.item));
      ++st.drawCalls;
      return {};
    };
    for (const Draw& d : opaque) MINGOL_TRY(emit(d));
    for (const Draw& d : blended) MINGOL_TRY(emit(d));
    list.endRendering();
  }
  if (presenting != nullptr) list.transition(target, TextureState::Present);
  MINGOL_TRY(list.end());
  MINGOL_TRY_ASSIGN(std::uint64_t serial, I.dev.submit(list, presenting));
  slot.serial = serial;
  ++I.frameCounter;
  st.pipelinesCreated = I.pipelinesCreated;
  stats_ = st;
  return serial;
}

}  // namespace mingol::renderer
