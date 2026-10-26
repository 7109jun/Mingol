// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/gui/gui_renderer.hpp"

#include <algorithm>
#include <cstring>

#include "mingol/gui/font.hpp"
#include "mingol/shader/shader_compiler.hpp"

namespace mingol::gui {

using namespace rhi;

namespace {

const char* const kVertexSource = R"(#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(push_constant) uniform PC { vec2 scale; vec2 translate; uint mode; uint srgbTarget; } pc;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;
void main() {
  gl_Position = vec4(inPos * pc.scale + pc.translate, 0.0, 1.0);
  uv = inUv;
  color = inColor;
}
)";

const char* const kFragmentSource = R"(#version 450
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform PC { vec2 scale; vec2 translate; uint mode; uint srgbTarget; } pc;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 outColor;
vec3 toLinear(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
void main() {
  vec4 c = color;
  if (pc.mode == 1u) c.a *= texture(tex, uv).r;
  else if (pc.mode == 2u) c *= texture(tex, uv);
  if (pc.srgbTarget != 0u) c.rgb = toLinear(c.rgb);
  outColor = c;
}
)";

struct PushConstants {
  float scale[2];
  float translate[2];
  std::uint32_t mode;
  std::uint32_t srgbTarget;
};

bool isSrgb(Format f) { return f == Format::RGBA8Srgb || f == Format::BGRA8Srgb; }

}  // namespace

struct GuiRenderer::Impl {
  Device* dev = nullptr;
  GuiRendererDesc desc;
  std::shared_ptr<BindGroupLayout> layout;
  std::shared_ptr<GraphicsPipeline> pipeline;
  std::shared_ptr<Sampler> nearest, linear;
  std::shared_ptr<Texture> fontTex, whiteTex;
  std::shared_ptr<BindGroup> fontGroup, whiteGroup;
  struct UserTex {
    std::shared_ptr<Texture> tex;
    std::shared_ptr<BindGroup> group;
  };
  std::unordered_map<std::uint32_t, UserTex> user;
  struct Retired {
    std::uint64_t serial;
    UserTex tex;
  };
  std::vector<Retired> retired;
  struct Slot {
    std::shared_ptr<Buffer> vb, ib;
    std::unique_ptr<CommandList> list;
    std::uint64_t serial = 0;
  };
  std::vector<Slot> slots;
  std::uint64_t frame = 0;
  std::uint64_t lastSerial = 0;
  std::uint64_t skipped = 0;

  Result<std::shared_ptr<BindGroup>> makeGroup(const std::shared_ptr<Texture>& t, const std::shared_ptr<Sampler>& s) {
    BindGroupDesc g;
    g.layout = layout;
    BindGroupEntry e;
    e.binding = 0;
    e.texture = t;
    e.sampler = s;
    g.entries.push_back(e);
    g.debugName = "gui-texture";
    return dev->createBindGroup(g);
  }

  void purgeRetired() {
    retired.erase(std::remove_if(retired.begin(), retired.end(), [&](const Retired& r) { return dev->isComplete(r.serial); }),
                  retired.end());
  }

  Result<void> ensureBuffer(std::shared_ptr<Buffer>& b, std::uint64_t bytes, BufferUsage usage, const char* name) {
    if (b && b->size() >= bytes) return {};
    std::uint64_t cap = std::max<std::uint64_t>(bytes, 64 * 1024);
    if (b) cap = std::max(cap, b->size() * 2);
    BufferDesc d;
    d.size = cap;
    d.usage = usage;
    d.memory = MemoryType::CpuToGpu;
    d.debugName = name;
    MINGOL_TRY_ASSIGN(b, dev->createBuffer(d));
    return {};
  }
};

GuiRenderer::GuiRenderer() = default;
GuiRenderer::~GuiRenderer() {
  if (impl_ && impl_->dev != nullptr) (void)impl_->dev->waitIdle();
}

Result<std::unique_ptr<GuiRenderer>> GuiRenderer::create(Device& device, const GuiRendererDesc& desc) {
  static_assert(sizeof(Vertex) == 20, "GUI vertex layout is uploaded as-is");
  std::unique_ptr<GuiRenderer> r(new GuiRenderer());
  r->impl_ = std::make_unique<Impl>();
  Impl& I = *r->impl_;
  I.dev = &device;
  I.desc = desc;
  if (desc.framesInFlight == 0 || desc.framesInFlight > 4)
    return Error{ErrorCode::InvalidArgument, "GuiRenderer: framesInFlight must be 1..4"};
  if (desc.targetFormat == Format::Undefined || isDepthFormat(desc.targetFormat))
    return Error{ErrorCode::InvalidArgument, "GuiRenderer: invalid target format"};

  BindGroupLayoutDesc ld;
  ld.bindings = {{0, BindingType::SampledTexture, ShaderStage::Fragment, 1}};
  ld.debugName = "gui-layout";
  MINGOL_TRY_ASSIGN(I.layout, device.createBindGroupLayout(ld));

  auto compile = [&](const char* src, shader::Stage st, ShaderStage rs, const char* name) -> Result<std::shared_ptr<ShaderModule>> {
    shader::CompileOptions o;
    o.stage = st;
    o.sourceName = name;
    MINGOL_TRY_ASSIGN(shader::SpirV spv, shader::compile(src, o));
    ShaderModuleDesc d;
    d.spirv = spv;
    d.stage = rs;
    d.debugName = name;
    return device.createShaderModule(d);
  };
  MINGOL_TRY_ASSIGN(auto vs, compile(kVertexSource, shader::Stage::Vertex, ShaderStage::Vertex, "gui.vert"));
  MINGOL_TRY_ASSIGN(auto fs, compile(kFragmentSource, shader::Stage::Fragment, ShaderStage::Fragment, "gui.frag"));

  GraphicsPipelineDesc pd;
  pd.vertexShader = vs;
  pd.fragmentShader = fs;
  pd.vertexBindings = {{0, sizeof(Vertex), false}};
  pd.vertexAttributes = {{0, 0, Format::RG32Float, 0}, {1, 0, Format::RG32Float, 8}, {2, 0, Format::RGBA8Unorm, 16}};
  pd.cullMode = CullMode::None;
  pd.depthTest = false;
  pd.depthWrite = false;
  pd.colorFormats = {desc.targetFormat};
  BlendState b;
  b.enable = true;
  pd.blend = {b};
  pd.setLayouts = {I.layout};
  pd.pushConstantSize = sizeof(PushConstants);
  pd.debugName = "gui";
  MINGOL_TRY_ASSIGN(I.pipeline, device.createGraphicsPipeline(pd));

  SamplerDesc sd;
  sd.minFilter = sd.magFilter = FilterMode::Nearest;
  sd.mipFilter = FilterMode::Nearest;
  sd.addressU = sd.addressV = AddressMode::ClampToEdge;
  MINGOL_TRY_ASSIGN(I.nearest, device.createSampler(sd));
  sd.minFilter = sd.magFilter = FilterMode::Linear;
  MINGOL_TRY_ASSIGN(I.linear, device.createSampler(sd));

  TextureDesc td;
  td.width = font::kAtlasW;
  td.height = font::kAtlasH;
  td.format = Format::R8Unorm;
  td.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
  td.debugName = "gui-font";
  MINGOL_TRY_ASSIGN(I.fontTex, device.createTextureWithData(td, font::atlas(), false));
  td.width = td.height = 1;
  td.format = Format::RGBA8Unorm;
  td.debugName = "gui-white";
  const std::uint8_t white[4] = {255, 255, 255, 255};
  MINGOL_TRY_ASSIGN(I.whiteTex, device.createTextureWithData(td, white, false));
  MINGOL_TRY_ASSIGN(I.fontGroup, I.makeGroup(I.fontTex, I.nearest));
  MINGOL_TRY_ASSIGN(I.whiteGroup, I.makeGroup(I.whiteTex, I.nearest));

  I.slots.resize(desc.framesInFlight);
  for (auto& s : I.slots) {
    MINGOL_TRY_ASSIGN(s.list, device.createCommandList());
  }
  return r;
}

Result<void> GuiRenderer::setTexture(std::uint32_t id, std::shared_ptr<Texture> texture, bool linear) {
  Impl& I = *impl_;
  if (id < kTextureUserBase) return Error{ErrorCode::InvalidArgument, "GuiRenderer: texture ids below 16 are reserved"};
  I.purgeRetired();
  auto it = I.user.find(id);
  if (it != I.user.end()) {
    I.retired.push_back({I.lastSerial, std::move(it->second)});
    I.user.erase(it);
  }
  if (!texture) return {};
  Impl::UserTex u;
  u.tex = texture;
  MINGOL_TRY_ASSIGN(u.group, I.makeGroup(texture, linear ? I.linear : I.nearest));
  I.user[id] = std::move(u);
  return {};
}

std::uint64_t GuiRenderer::skippedCommands() const noexcept { return impl_->skipped; }

Result<std::uint64_t> GuiRenderer::render(const DrawList& list, Texture& target, const float* clearColor, Swapchain* presenting) {
  Impl& I = *impl_;
  const TextureDesc& td = target.desc();
  if (td.format != I.desc.targetFormat)
    return Error{ErrorCode::InvalidArgument, "GuiRenderer: target format differs from the format the renderer was created for"};
  if (td.width == 0 || td.height == 0) return Error{ErrorCode::InvalidArgument, "GuiRenderer: empty target"};

  Impl::Slot& slot = I.slots[I.frame % I.slots.size()];
  if (slot.serial != 0) MINGOL_TRY(I.dev->waitForSerial(slot.serial));
  I.purgeRetired();

  const auto& vs = list.vertices();
  const auto& is = list.indices();
  if (!vs.empty()) {
    MINGOL_TRY(I.ensureBuffer(slot.vb, vs.size() * sizeof(Vertex), BufferUsage::Vertex, "gui-vb"));
    MINGOL_TRY(I.ensureBuffer(slot.ib, is.size() * sizeof(std::uint32_t), BufferUsage::Index, "gui-ib"));
    std::memcpy(slot.vb->mapped(), vs.data(), vs.size() * sizeof(Vertex));
    std::memcpy(slot.ib->mapped(), is.data(), is.size() * sizeof(std::uint32_t));
    slot.vb->flush(0, vs.size() * sizeof(Vertex));
    slot.ib->flush(0, is.size() * sizeof(std::uint32_t));
  }

  CommandList& cl = *slot.list;
  MINGOL_TRY(cl.begin());
  RenderPassDesc rp;
  ColorAttachment ca;
  ca.texture = &target;
  ca.load = clearColor != nullptr ? LoadOp::Clear : LoadOp::Load;
  ca.store = StoreOp::Store;
  if (clearColor != nullptr)
    for (int i = 0; i < 4; ++i) ca.clear[i] = clearColor[i];
  rp.colors.push_back(ca);
  rp.debugName = "gui";
  cl.beginRendering(rp);
  const auto w = static_cast<float>(td.width), h = static_cast<float>(td.height);
  cl.setViewport(0, 0, w, h);
  cl.setScissor(0, 0, td.width, td.height);
  if (!vs.empty()) {
    cl.bindPipeline(*I.pipeline);
    cl.bindVertexBuffer(0, *slot.vb);
    cl.bindIndexBuffer(*slot.ib, IndexType::U32);
    PushConstants pc{{2.0F / w, 2.0F / h}, {-1.0F, -1.0F}, 0, isSrgb(td.format) ? 1U : 0U};
    for (const DrawCmd& c : list.commands()) {
      if (c.indexCount == 0) continue;
      const float x0 = std::clamp(c.clip.x0, 0.0F, w), y0 = std::clamp(c.clip.y0, 0.0F, h);
      const float x1 = std::clamp(c.clip.x1, 0.0F, w), y1 = std::clamp(c.clip.y1, 0.0F, h);
      // a pixel is drawn when its center lies inside the clip rect (same rule as the rasterizer)
      const auto sx0 = static_cast<std::int32_t>(std::ceil(x0 - 0.5F));
      const auto sy0 = static_cast<std::int32_t>(std::ceil(y0 - 0.5F));
      const auto sx1 = static_cast<std::int32_t>(std::ceil(x1 - 0.5F));
      const auto sy1 = static_cast<std::int32_t>(std::ceil(y1 - 0.5F));
      if (sx1 <= sx0 || sy1 <= sy0) continue;
      BindGroup* group = nullptr;
      if (c.texture == kTextureNone) {
        pc.mode = 0;
        group = I.whiteGroup.get();
      } else if (c.texture == kTextureFont) {
        pc.mode = 1;
        group = I.fontGroup.get();
      } else {
        const auto it = I.user.find(c.texture);
        if (it == I.user.end()) {
          ++I.skipped;
          continue;
        }
        pc.mode = 2;
        group = it->second.group.get();
      }
      cl.setScissor(sx0, sy0, static_cast<std::uint32_t>(sx1 - sx0), static_cast<std::uint32_t>(sy1 - sy0));
      cl.bindGroup(0, *group);
      cl.pushConstants(&pc, sizeof pc);
      cl.drawIndexed(c.indexCount, 1, c.firstIndex, 0, 0);
    }
  }
  cl.endRendering();
  if (presenting != nullptr) cl.transition(target, TextureState::Present);
  MINGOL_TRY(cl.end());
  MINGOL_TRY_ASSIGN(std::uint64_t serial, I.dev->submit(cl, presenting));
  slot.serial = serial;
  I.lastSerial = serial;
  ++I.frame;
  return serial;
}

}  // namespace mingol::gui
