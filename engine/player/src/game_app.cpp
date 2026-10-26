// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/player/game_app.hpp"

#include <algorithm>

#include "mingol/assets/audio.hpp"
#include "mingol/assets/model.hpp"
#include "mingol/scene/components.hpp"

namespace mingol::player {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string extensionOf(const std::string& path) {
  const auto dot = path.find_last_of('.');
  const auto slash = path.find_last_of('/');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
  return lower(path.substr(dot));
}
std::string dirOf(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

}  // namespace

GameApp::~GameApp() {
  if (runtime_ && host_) {
    scripting::ScriptHost::Scope sc(*host_, &runtime_->scene());
    runtime_->scene().clear();
  }
}

Result<std::unique_ptr<GameApp>> GameApp::create(std::unique_ptr<ContentSource> source, const GameAppDesc& desc) {
  if (source == nullptr) return makeError(ErrorCode::InvalidArgument, "no content source");
  std::unique_ptr<GameApp> app(new GameApp());
  app->source_ = std::move(source);
  app->desc_ = desc;
  const ContentSource& src = *app->source_;

  MINGOL_TRY_ASSIGN(std::vector<std::uint8_t> projectBytes, src.read("project.mindev"));
  MINGOL_TRY_ASSIGN(app->project_, package::parseMindev(std::string_view(reinterpret_cast<const char*>(projectBytes.data()), projectBytes.size())));
  MINGOL_TRY_ASSIGN(app->runtime_, runtime::Runtime::create(desc.runtime));
  app->registry_ = sceneio::ComponentRegistry::withBuiltins();

  // scripts
  std::vector<script::SourceInput> sources;
  for (const std::string& dir : app->project_.scriptDirs) {
    for (const std::string& f : src.list(dir)) {
      if (extensionOf(f) != ".mins") continue;
      MINGOL_TRY_ASSIGN(std::vector<std::uint8_t> bytes, src.read(f));
      sources.push_back({f, std::string(bytes.begin(), bytes.end())});
    }
  }
  MINGOL_TRY_ASSIGN(app->host_, scripting::ScriptHost::create(sources));

  // assets
  if (desc.renderer != nullptr) {
    MINGOL_TRY_ASSIGN(app->builtins_, sceneio::BuiltinAssets::create(*desc.renderer));
    app->builtins_.addTo(app->assets_);
  }
  for (const std::string& dir : app->project_.assetDirs) {
    for (const std::string& f : src.list(dir)) {
      const std::string ext = extensionOf(f);
      if (ext == ".wav" || ext == ".ogg") {
        auto bytes = src.read(f);
        auto clip = bytes.ok() ? assets::decodeAudio(bytes.value()) : Result<assets::AudioClip>(bytes.error());
        if (!clip.ok()) { app->warnings_.push_back(f + ": " + clip.error().message); continue; }
        app->assets_.addAudioClip(f, std::make_shared<const assets::AudioClip>(std::move(clip).value()));
      } else if (ext == ".glb" || ext == ".gltf") {
        if (desc.renderer == nullptr || desc.device == nullptr) { app->warnings_.push_back(f + ": no renderer, model skipped"); continue; }
        auto bytes = src.read(f);
        if (!bytes.ok()) { app->warnings_.push_back(f + ": " + bytes.error().message); continue; }
        const std::string base = dirOf(f);
        auto resolver = [&src, base](const std::string& uri) -> Result<std::vector<std::uint8_t>> {
          const std::string name = base.empty() ? uri : base + "/" + uri;
          if (!package::isValidRelativePath(name)) return makeError(ErrorCode::InvalidArgument, "model resource '{}' escapes the project", uri);
          return src.read(name);
        };
        auto model = assets::loadGltf(bytes.value(), resolver);
        if (!model.ok()) { app->warnings_.push_back(f + ": " + model.error().message); continue; }
        auto gpu = runtime::uploadModel(*desc.device, *desc.renderer, model.value());
        if (!gpu.ok()) { app->warnings_.push_back(f + ": " + gpu.error().message); continue; }
        sceneio::addModelAssets(app->assets_, f, gpu.value());
        app->models_.push_back(std::move(gpu).value());
      }
    }
  }

  const std::string sceneName = desc.scene.empty() ? app->project_.entryScene : desc.scene;
  if (sceneName.empty()) return makeError(ErrorCode::NotFound, "the project has no entry scene");
  MINGOL_TRY(app->loadScene(sceneName));
  return app;
}

Result<void> GameApp::loadScene(const std::string& name) {
  MINGOL_TRY_ASSIGN(std::vector<std::uint8_t> bytes, source_->read(name));
  sceneio::SceneIoContext ctx;
  ctx.assets = &assets_;
  ctx.scripts = host_.get();
  sceneio::LoadOptions lo;
  lo.replaceExisting = true;
  scripting::ScriptHost::Scope sc(*host_, &runtime_->scene());
  MINGOL_TRY_ASSIGN(sceneio::LoadReport rep, sceneio::loadScene(runtime_->scene(), std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), ctx, registry_, lo));
  for (const std::string& w : rep.warnings) warnings_.push_back(name + ": " + w);
  sceneName_ = name;
  return {};
}

void GameApp::tick(float dt) {
  scripting::ScriptHost::Scope sc(*host_, &runtime_->scene());
  runtime_->tick(dt);
}

Result<void> GameApp::render(rhi::Texture& target, rhi::Swapchain* presenting) {
  if (desc_.renderer == nullptr) return makeError(ErrorCode::Unsupported, "no renderer");
  const float aspect = static_cast<float>(target.desc().width) / static_cast<float>(std::max(1U, target.desc().height));
  MINGOL_TRY_ASSIGN(renderer::RenderScene rs, scene::buildRenderScene(runtime_->scene(), aspect));
  MINGOL_TRY(desc_.renderer->render(rs, target, presenting));
  return {};
}

}  // namespace mingol::player
