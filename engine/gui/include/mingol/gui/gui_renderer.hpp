// SPDX-License-Identifier: GPL-2.0-only
// RHI backend for DrawList: uploads the frame's vertices/indices and draws them with scissor rects.
// Colors in a DrawList are final display values, so UNORM targets match the software rasterizer;
// sRGB targets are supported by converting in the shader.
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/gui/draw.hpp"
#include "mingol/rhi/device.hpp"

namespace mingol::gui {

struct GuiRendererDesc {
  rhi::Format targetFormat = rhi::Format::BGRA8Unorm;  // must match the textures passed to render()
  std::uint32_t framesInFlight = 2;
};

class GuiRenderer {
 public:
  static Result<std::unique_ptr<GuiRenderer>> create(rhi::Device& device, const GuiRendererDesc& desc = {});
  ~GuiRenderer();
  GuiRenderer(const GuiRenderer&) = delete;
  GuiRenderer& operator=(const GuiRenderer&) = delete;

  // Associates a draw-list texture id (>= kTextureUserBase) with a GPU texture. Passing a null
  // texture removes the id. `linear` selects bilinear filtering (viewport images) over nearest.
  Result<void> setTexture(std::uint32_t id, std::shared_ptr<rhi::Texture> texture, bool linear = true);

  // Draws `list` into `target` (clearing it to `clearColor` when non-null, otherwise loading it).
  // With `presenting` the target is transitioned for presentation and the swapchain is presented.
  // Returns the submission serial. Draw commands that use an unknown texture id are skipped.
  Result<std::uint64_t> render(const DrawList& list, rhi::Texture& target, const float* clearColor = nullptr,
                               rhi::Swapchain* presenting = nullptr);

  // Number of draw commands skipped because their texture id was not registered (diagnostics).
  std::uint64_t skippedCommands() const noexcept;

 private:
  GuiRenderer();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mingol::gui
