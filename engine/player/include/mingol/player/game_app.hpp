// SPDX-License-Identifier: GPL-2.0-only
// Runs a game from a ContentSource: project file, scripts, audio, models, entry scene. Window independent:
// the `mingol-player` executable feeds it frame times and a render target.
#pragma once

#include <memory>
#include <string>

#include "mingol/package/mindev.hpp"
#include "mingol/player/content_source.hpp"
#include "mingol/renderer/renderer.hpp"
#include "mingol/rhi/device.hpp"
#include "mingol/runtime/runtime.hpp"
#include "mingol/sceneio/scene_io.hpp"
#include "mingol/sceneio/standard_assets.hpp"
#include "mingol/scripting/script_host.hpp"

namespace mingol::player {

struct GameAppDesc {
  rhi::Device* device = nullptr;          // optional: without a GPU the game runs but cannot render
  renderer::Renderer* renderer = nullptr;
  runtime::RuntimeDesc runtime;
  std::string scene;                      // empty: the project's entryScene
};

class GameApp {
 public:
  static Result<std::unique_ptr<GameApp>> create(std::unique_ptr<ContentSource> source, const GameAppDesc& desc);
  ~GameApp();
  GameApp(const GameApp&) = delete;
  GameApp& operator=(const GameApp&) = delete;

  const package::ProjectFile& project() const { return project_; }
  runtime::Runtime& runtime() { return *runtime_; }
  scene::Scene& scene() { return runtime_->scene(); }
  scripting::ScriptHost& scripts() { return *host_; }
  const sceneio::AssetTable& assets() const { return assets_; }
  const std::string& currentScene() const { return sceneName_; }
  const std::vector<std::string>& warnings() const { return warnings_; }

  // Replaces the current scene (all-or-nothing: on failure the old scene stays).
  Result<void> loadScene(const std::string& name);
  void tick(float dt);
  // Renders the scene from its active Camera into `target` (aspect from the target size). Fails when
  // there is no renderer or no enabled camera. With `presenting` the swapchain image is presented-ready.
  Result<void> render(rhi::Texture& target, rhi::Swapchain* presenting = nullptr);

 private:
  GameApp() = default;
  std::unique_ptr<ContentSource> source_;
  GameAppDesc desc_;
  package::ProjectFile project_;
  std::unique_ptr<runtime::Runtime> runtime_;
  std::unique_ptr<scripting::ScriptHost> host_;
  sceneio::AssetTable assets_;
  sceneio::ComponentRegistry registry_;
  sceneio::BuiltinAssets builtins_;
  std::vector<runtime::GpuModel> models_;  // keep GPU resources alive (the table holds shared pointers too)
  std::string sceneName_;
  std::vector<std::string> warnings_;
};

}  // namespace mingol::player
