// SPDX-License-Identifier: GPL-2.0-only
// Viewport panel: offscreen rendering, editor camera, picking, gizmos and the 2D overlay (grid, icons).
#include <algorithm>
#include <cmath>

#include "mingol/editor/editor_app.hpp"
#include "mingol/runtime/physics_components.hpp"
#include "mingol/scene/components.hpp"

namespace mingol::editor {

using gui::Color;
using platform::Key;

namespace {

Vec3 worldAxis(int i) { return i == 0 ? Vec3{1, 0, 0} : (i == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1}); }
Color axisColor(int i, std::uint8_t a = 255) { return i == 0 ? Color{230, 70, 70, a} : (i == 1 ? Color{90, 210, 90, a} : Color{70, 120, 240, a}); }

const scene::CameraComponent* findGameCamera(scene::Scene& s) {
  const scene::CameraComponent* best = nullptr;
  for (const scene::CameraComponent* c : s.findComponents<scene::CameraComponent>()) {
    if (!c->activeAndEnabled()) continue;
    if (best == nullptr || c->priority > best->priority) best = c;
  }
  return best;
}

}  // namespace

renderer::Camera EditorApp::currentCamera(float aspect, bool* usingGame) {
  if (usingGame != nullptr) *usingGame = false;
  if (doc_->playing()) {
    if (const auto* c = findGameCamera(doc_->scene())) {
      const scene::Transform& t = c->gameObject().transform();
      renderer::Camera cam;
      Mat4 inv;
      if (inverse(Mat4::trs(t.worldPosition(), t.worldRotation(), {1, 1, 1}), inv)) {
        cam.view = inv;
        cam.projection = Mat4::perspective(c->fovYRadians, aspect, c->nearZ, c->farZ);
        cam.position = t.worldPosition();
        if (usingGame != nullptr) *usingGame = true;
        return cam;
      }
    }
  }
  return camera_.toCamera(aspect);
}

scene::GameObject* EditorApp::pickObject(Vec2 pixel, Vec2 size) {
  if (size.x < 1 || size.y < 1) return nullptr;
  const renderer::Camera cam = currentCamera(size.x / size.y);
  const Ray ray = screenRay(cam, pixel, size);
  scene::GameObject* best = nullptr;
  float bestT = 1e30F;
  for (scene::GameObject* go : doc_->allObjects()) {
    if (!go->activeInHierarchy()) continue;
    float t = 0;
    bool hit = false;
    if (const auto* mr = go->getComponent<scene::MeshRenderer>(); mr != nullptr && mr->mesh != nullptr) {
      hit = rayLocalAabb(ray, go->transform().worldMatrix(), mr->mesh->bounds(), t);
    } else {
      const float r = std::max(0.25F, length(go->transform().worldPosition() - cam.position) * 0.03F);
      hit = raySphere(ray, go->transform().worldPosition(), r, t);
    }
    if (hit && t < bestT) {
      bestT = t;
      best = go;
    }
  }
  return best;
}

float EditorApp::gizmoLength(const renderer::Camera& cam, Vec3 at) const { return std::max(0.05F, length(at - cam.position) * 0.18F); }

namespace {
Vec3 gizmoAxis(GizmoMode mode, scene::GameObject& go, int i) {
  if (mode == GizmoMode::Scale) return normalize(rotate(go.transform().worldRotation(), worldAxis(i)));
  return worldAxis(i);
}
}  // namespace

EditorApp::GizmoHit EditorApp::hitGizmo(const renderer::Camera& cam, Vec2 size, Vec2 mouse) const {
  GizmoHit hit;
  auto* sel = doc_->selected();
  if (sel == nullptr) return hit;
  const Vec3 o = sel->transform().worldPosition();
  const float len = gizmoLength(cam, o);
  const auto o2 = projectToScreen(cam, o, size);
  if (!o2) return hit;
  float best = 8.0F;
  if (gizmoMode_ == GizmoMode::Scale) {
    const Vec2 d = mouse - *o2;
    if (std::sqrt(d.x * d.x + d.y * d.y) < 9.0F) {
      hit.axis = 3;
      return hit;
    }
  }
  for (int i = 0; i < 3; ++i) {
    const Vec3 dir = gizmoAxis(gizmoMode_, *sel, i);
    if (gizmoMode_ == GizmoMode::Rotate) {
      const Vec3 u = normalize(cross(dir, std::fabs(dir.y) < 0.9F ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
      const Vec3 v = cross(dir, u);
      std::optional<Vec2> prev;
      for (int k = 0; k <= 48; ++k) {
        const float a = 2.0F * kPi * static_cast<float>(k) / 48.0F;
        const auto p = projectToScreen(cam, o + (u * std::cos(a) + v * std::sin(a)) * len, size);
        if (p && prev) {
          const float d = distancePointSegment2D(mouse, *prev, *p);
          if (d < best) { best = d; hit.axis = i; }
        }
        prev = p;
      }
    } else {
      const auto t2 = projectToScreen(cam, o + dir * len, size);
      if (!t2) continue;
      const float d = distancePointSegment2D(mouse, *o2, *t2);
      if (d < best) { best = d; hit.axis = i; }
    }
  }
  return hit;
}

void EditorApp::beginGizmoDrag(const GizmoHit& hit, const renderer::Camera& cam, Vec2 size, Vec2 mouse) {
  auto* sel = doc_->selected();
  if (sel == nullptr || hit.axis < 0) return;
  drag_ = {};
  drag_.active = true;
  drag_.axis = hit.axis;
  drag_.mode = gizmoMode_;
  drag_.object = sel->id();
  drag_.origin = sel->transform().worldPosition();
  drag_.startPos = drag_.origin;
  drag_.startRot = sel->transform().worldRotation();
  drag_.startScale = sel->transform().localScale();
  drag_.length = gizmoLength(cam, drag_.origin);
  drag_.startMouse = mouse;
  drag_.axisDir = hit.axis < 3 ? gizmoAxis(gizmoMode_, *sel, hit.axis) : Vec3{1, 0, 0};
  if (hit.axis < 3 && gizmoMode_ != GizmoMode::Rotate)
    drag_.startParam = closestOnAxis(screenRay(cam, mouse, size), drag_.origin, drag_.axisDir);
}

void EditorApp::updateGizmoDrag(const renderer::Camera& cam, Vec2 size, Vec2 mouse) {
  auto* sel = doc_->scene().get(drag_.object);
  if (!drag_.active || sel == nullptr) return;
  switch (drag_.mode) {
    case GizmoMode::Translate: {
      const float s = closestOnAxis(screenRay(cam, mouse, size), drag_.origin, drag_.axisDir) - drag_.startParam;
      sel->transform().setWorldPosition(drag_.startPos + drag_.axisDir * s);
      break;
    }
    case GizmoMode::Rotate: {
      const float angle = (mouse.x - drag_.startMouse.x) * 0.01F;
      sel->transform().setWorldRotation(Quat::fromAxisAngle(drag_.axisDir, angle) * drag_.startRot);
      break;
    }
    case GizmoMode::Scale: {
      Vec3 ns = drag_.startScale;
      if (drag_.axis == 3) {
        const float f = std::max(0.01F, 1.0F + (drag_.startMouse.y - mouse.y) * 0.01F);
        ns = drag_.startScale * f;
      } else {
        const float s = closestOnAxis(screenRay(cam, mouse, size), drag_.origin, drag_.axisDir) - drag_.startParam;
        const float f = std::max(0.01F, 1.0F + s / drag_.length);
        if (drag_.axis == 0) ns.x *= f;
        else if (drag_.axis == 1) ns.y *= f;
        else ns.z *= f;
      }
      sel->transform().setLocalScale(ns);
      break;
    }
  }
  drag_.changed = true;
}

void EditorApp::endGizmoDrag(bool cancel) {
  if (!drag_.active) return;
  auto* sel = doc_->scene().get(drag_.object);
  if (sel != nullptr) {
    if (cancel) {
      sel->transform().setWorldPosition(drag_.startPos);
      sel->transform().setWorldRotation(drag_.startRot);
      sel->transform().setLocalScale(drag_.startScale);
    } else if (drag_.changed) {
      const char* what = drag_.mode == GizmoMode::Translate ? "Move " : (drag_.mode == GizmoMode::Rotate ? "Rotate " : "Scale ");
      doc_->commit(what + sel->name());
    }
  }
  drag_ = {};
}

void EditorApp::viewportInput(const gui::ItemState& st) {
  const Vec2 mouse = ctx_.mousePos() - vpRect_.min();
  const float aspect = vpSize_.x / std::max(vpSize_.y, 1.0F);
  const renderer::Camera cam = currentCamera(aspect);
  const bool hovered = st.hovered;
  const bool typing = ctx_.wantTextInput();

  if (hovered && ctx_.scrollY() != 0.0F && !ctx_.anyPopupOpen()) camera_.zoom(ctx_.scrollY());

  // camera drags (button held that started inside the viewport)
  if (vpDragButton_ < 0 && hovered && !ctx_.anyPopupOpen()) {
    if (ctx_.mouseClicked(1)) vpDragButton_ = 1;
    else if (ctx_.mouseClicked(2)) vpDragButton_ = 2;
    else if (ctx_.mouseClicked(0) && (keysDown_.count(Key::LeftAlt) != 0 || keysDown_.count(Key::RightAlt) != 0)) vpDragButton_ = 0;
  }
  if (vpDragButton_ >= 0) {
    if (!ctx_.mouseDown(vpDragButton_)) {
      vpDragButton_ = -1;
    } else {
      const Vec2 d = ctx_.mouseDelta();
      if (vpDragButton_ == 1) {
        camera_.look(d.x, d.y);
        Vec3 dir{0, 0, 0};
        if (keysDown_.count(Key::W) != 0) dir.z += 1;
        if (keysDown_.count(Key::S) != 0) dir.z -= 1;
        if (keysDown_.count(Key::D) != 0) dir.x += 1;
        if (keysDown_.count(Key::A) != 0) dir.x -= 1;
        if (keysDown_.count(Key::E) != 0) dir.y += 1;
        if (keysDown_.count(Key::Q) != 0) dir.y -= 1;
        const float speed = (keysDown_.count(Key::LeftShift) != 0 ? 3.0F : 1.0F) * std::max(2.0F, camera_.distance);
        camera_.fly(dir, dt_, speed);
      } else if (vpDragButton_ == 2) {
        camera_.pan(d.x, d.y, vpSize_.y);
      } else {
        camera_.orbit(d.x, d.y);
      }
    }
  }

  // left button: gizmo or selection
  if (st.pressed && vpDragButton_ != 0 && vpDragButton_ != 1 && vpDragButton_ != 2) {
    const GizmoHit hit = hitGizmo(cam, vpSize_, mouse);
    if (hit.axis >= 0) {
      beginGizmoDrag(hit, cam, vpSize_, mouse);
    } else {
      doc_->select(pickObject(mouse, vpSize_));
    }
  }
  if (drag_.active) {
    if (ctx_.keyPressed(Key::Escape)) endGizmoDrag(true);
    else if (!ctx_.mouseDown(0)) endGizmoDrag(false);
    else updateGizmoDrag(cam, vpSize_, mouse);
  }

  // shortcuts while the pointer is over the viewport (not while flying: WASD moves the camera)
  if (hovered && !typing && vpDragButton_ != 1 && !ctx_.ctrl()) {
    if (ctx_.keyPressed(Key::W)) gizmoMode_ = GizmoMode::Translate;
    if (ctx_.keyPressed(Key::E)) gizmoMode_ = GizmoMode::Rotate;
    if (ctx_.keyPressed(Key::R)) gizmoMode_ = GizmoMode::Scale;
    if (ctx_.keyPressed(Key::F)) {
      if (auto* sel = doc_->selected()) {
        float radius = 1.0F;
        if (const auto* mr = sel->getComponent<scene::MeshRenderer>(); mr != nullptr && mr->mesh != nullptr) {
          const Vec3 s = sel->transform().worldScale();
          radius = mr->mesh->bounds().radius() * std::max({std::fabs(s.x), std::fabs(s.y), std::fabs(s.z)});
        }
        camera_.frame(sel->transform().worldPosition(), radius);
      }
    }
  }
}

void EditorApp::drawViewportOverlay(gui::DrawList& dl, const Rect& r, const renderer::Camera& cam) {
  const Vec2 size = vpSize_;
  const Vec2 off = r.min();
  auto P = [&](Vec3 w) -> std::optional<Vec2> {
    auto p = projectToScreen(cam, w, size);
    if (!p) return std::nullopt;
    return *p + off;
  };
  auto line = [&](Vec3 a, Vec3 b, Color c, float th = 1.0F) {
    const auto pa = P(a), pb = P(b);
    if (pa && pb) dl.addLine(*pa, *pb, c, th);
  };
  // ground grid
  if (!doc_->playing()) {
    for (int i = -10; i <= 10; ++i) {
      const bool major = i % 5 == 0;
      const Color c = major ? Color{255, 255, 255, 46} : Color{255, 255, 255, 20};
      line({static_cast<float>(i), 0, -10}, {static_cast<float>(i), 0, 10}, i == 0 ? axisColor(2, 90) : c);
      line({-10, 0, static_cast<float>(i)}, {10, 0, static_cast<float>(i)}, i == 0 ? axisColor(0, 90) : c);
    }
  }
  // camera and light icons
  const Color iconC{255, 220, 120, 220};
  for (scene::GameObject* go : doc_->allObjects()) {
    if (!go->activeInHierarchy()) continue;
    const Mat4& w = go->transform().worldMatrix();
    if (const auto* c = go->getComponent<scene::CameraComponent>()) {
      const float h = std::tan(c->fovYRadians * 0.5F), ww = h * 1.6F, d = 1.0F;
      const Vec3 o = transformPoint(w, {0, 0, 0});
      const Vec3 corners[4] = {transformPoint(w, {-ww * d, -h * d, -d}), transformPoint(w, {ww * d, -h * d, -d}),
                               transformPoint(w, {ww * d, h * d, -d}), transformPoint(w, {-ww * d, h * d, -d})};
      for (int i = 0; i < 4; ++i) {
        line(o, corners[i], iconC);
        line(corners[i], corners[(i + 1) % 4], iconC);
      }
    }
    if (const auto* l = go->getComponent<scene::LightComponent>()) {
      const Vec3 p = go->transform().worldPosition();
      if (const auto sp = P(p)) {
        dl.addCircle(*sp, 6, iconC.withAlpha(60), 16);
        dl.addCircle(*sp, 3, iconC, 12);
      }
      if (l->type != renderer::LightType::Point) line(p, p + normalize(transformDirection(w, {0, 0, -1})) * 1.5F, iconC);
    }
  }
  // selection
  if (auto* sel = doc_->selected()) {
    const Color sc{255, 170, 40, 255};
    if (const auto* mr = sel->getComponent<scene::MeshRenderer>(); mr != nullptr && mr->mesh != nullptr) {
      const renderer::Aabb b = mr->mesh->bounds();
      const Mat4& w = sel->transform().worldMatrix();
      Vec3 c[8];
      for (int i = 0; i < 8; ++i)
        c[i] = transformPoint(w, {(i & 1) != 0 ? b.max.x : b.min.x, (i & 2) != 0 ? b.max.y : b.min.y, (i & 4) != 0 ? b.max.z : b.min.z});
      static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
      for (const auto& e : edges) line(c[e[0]], c[e[1]], sc);
    }
    // gizmo
    const Vec3 o = sel->transform().worldPosition();
    const float len = gizmoLength(cam, o);
    const auto o2 = P(o);
    if (o2) {
      for (int i = 0; i < 3; ++i) {
        const Vec3 dir = gizmoAxis(gizmoMode_, *sel, i);
        const bool active = drag_.active && drag_.axis == i;
        const Color c = active ? Color{255, 255, 80} : axisColor(i);
        if (gizmoMode_ == GizmoMode::Rotate) {
          const Vec3 u = normalize(cross(dir, std::fabs(dir.y) < 0.9F ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
          const Vec3 v = cross(dir, u);
          std::optional<Vec2> prev;
          for (int k = 0; k <= 48; ++k) {
            const float a = 2.0F * kPi * static_cast<float>(k) / 48.0F;
            const auto p = P(o + (u * std::cos(a) + v * std::sin(a)) * len);
            if (p && prev) dl.addLine(*prev, *p, c, 2.0F);
            prev = p;
          }
        } else {
          const auto t2 = P(o + dir * len);
          if (!t2) continue;
          dl.addLine(*o2, *t2, c, 2.0F);
          if (gizmoMode_ == GizmoMode::Translate) {
            const Vec2 d = *t2 - *o2;
            const float l = std::sqrt(d.x * d.x + d.y * d.y);
            if (l > 1.0F) {
              const Vec2 n = d * (1.0F / l), pr{-n.y, n.x};
              dl.addTriangle(*t2 + n * 8.0F, *t2 + pr * 4.0F, *t2 - pr * 4.0F, c);
            }
          } else {
            dl.addRect({t2->x - 4, t2->y - 4, t2->x + 4, t2->y + 4}, c);
          }
        }
      }
      if (gizmoMode_ == GizmoMode::Scale) dl.addRect({o2->x - 5, o2->y - 5, o2->x + 5, o2->y + 5}, {230, 230, 230});
    }
  }
}

void EditorApp::panelViewport() {
  vpVisible_ = true;
  const bool playing = doc_->playing();
  ctx_.label(playing ? (doc_->paused() ? "Game view (paused)" : "Game view") : "Scene view   RMB+WASD fly | MMB pan | Alt+LMB orbit | wheel zoom | W/E/R tools | F frame");
  const float w = std::max(16.0F, ctx_.availableWidth());
  const float h = std::max(16.0F, ctx_.availableHeight());
  const gui::ItemState st = ctx_.interactiveArea("##viewport", {w, h});
  vpRect_ = ctx_.lastItemRect();
  vpSize_ = {std::floor(vpRect_.width()), std::floor(vpRect_.height())};
  gui::DrawList& dl = ctx_.draw();
  const auto& env = doc_->scene().environment;
  dl.addRect(vpRect_, {static_cast<std::uint8_t>(std::clamp(env.clearColor[0], 0.0F, 1.0F) * 255), static_cast<std::uint8_t>(std::clamp(env.clearColor[1], 0.0F, 1.0F) * 255),
                       static_cast<std::uint8_t>(std::clamp(env.clearColor[2], 0.0F, 1.0F) * 255)});
  if (vpTexture_ && vpTexture_->desc().width == static_cast<std::uint32_t>(vpSize_.x) && vpTexture_->desc().height == static_cast<std::uint32_t>(vpSize_.y))
    dl.addImage(gui::kTextureUserBase, vpRect_);
  else if (desc_.renderer == nullptr)
    dl.addText({vpRect_.x0 + 8, vpRect_.y1 - 22}, "No GPU renderer: overlay only", ctx_.style.textDim, ctx_.style.fontScale);
  dl.pushClip(vpRect_);
  const renderer::Camera cam = currentCamera(vpSize_.x / std::max(vpSize_.y, 1.0F));
  drawViewportOverlay(dl, vpRect_, cam);
  if (!vpNotice_.empty()) dl.addText({vpRect_.x0 + 8, vpRect_.y0 + 8}, vpNotice_, ctx_.style.warning, ctx_.style.fontScale);
  dl.popClip();
  viewportInput(st);
}

Result<std::shared_ptr<rhi::Texture>> EditorApp::renderViewport() {
  if (desc_.renderer == nullptr || desc_.device == nullptr || !vpVisible_ || vpSize_.x < 8 || vpSize_.y < 8)
    return std::shared_ptr<rhi::Texture>();
  const std::uint32_t w = std::min(4096U, static_cast<std::uint32_t>(vpSize_.x)), h = std::min(4096U, static_cast<std::uint32_t>(vpSize_.y));
  if (!vpTexture_ || vpTexture_->desc().width != w || vpTexture_->desc().height != h) {
    rhi::TextureDesc td;
    td.width = w;
    td.height = h;
    td.format = rhi::Format::RGBA8Unorm;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc;
    td.debugName = "editor-viewport";
    MINGOL_TRY_ASSIGN(vpTexture_, desc_.device->createTexture(td));
    ++vpGeneration_;
  }
  Profiler::Scoped prof(profiler_, "render");
  bool game = false;
  const renderer::Camera cam = currentCamera(static_cast<float>(w) / static_cast<float>(h), &game);
  auto rs = scene::buildRenderScene(doc_->scene(), cam);
  if (!rs.ok()) {
    vpNotice_ = "Render: " + rs.error().message;
    return rs.error();
  }
  vpNotice_.clear();
  auto serial = desc_.renderer->render(rs.value(), *vpTexture_);
  if (!serial.ok()) {
    vpNotice_ = "Render failed: " + serial.error().message;
    return serial.error();
  }
  if (!vpTransition_) {
    MINGOL_TRY_ASSIGN(vpTransition_, desc_.device->createCommandList());
  }
  MINGOL_TRY(vpTransition_->begin());
  vpTransition_->transition(*vpTexture_, rhi::TextureState::ShaderRead);
  MINGOL_TRY(vpTransition_->end());
  MINGOL_TRY(desc_.device->submit(*vpTransition_));
  return vpTexture_;
}

}  // namespace mingol::editor
