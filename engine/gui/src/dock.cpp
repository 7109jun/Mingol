// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/gui/dock.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "mingol/gui/font.hpp"

namespace mingol::gui {

using Node = DockLayout::Node;

namespace {
constexpr float kGap = 4.0F;
constexpr float kMinSize = 80.0F;

std::unique_ptr<Node> makeLeaf() { return std::make_unique<Node>(); }

void collect(const Node& n, std::vector<std::string>& out) {
  if (n.leaf) {
    for (const auto& t : n.tabs) out.push_back(t);
    return;
  }
  collect(*n.a, out);
  collect(*n.b, out);
}

Node* findLeaf(Node& n, const std::string& name, int* idx = nullptr) {
  if (n.leaf) {
    for (std::size_t i = 0; i < n.tabs.size(); ++i)
      if (n.tabs[i] == name) {
        if (idx != nullptr) *idx = static_cast<int>(i);
        return &n;
      }
    return nullptr;
  }
  if (Node* r = findLeaf(*n.a, name, idx)) return r;
  return findLeaf(*n.b, name, idx);
}

Node* firstLeaf(Node& n) { return n.leaf ? &n : firstLeaf(*n.a); }

// Replaces every split that has an empty-leaf child with the other child.
void prune(std::unique_ptr<Node>& n) {
  if (n->leaf) return;
  prune(n->a);
  prune(n->b);
  const bool aEmpty = n->a->leaf && n->a->tabs.empty();
  const bool bEmpty = n->b->leaf && n->b->tabs.empty();
  if (aEmpty && bEmpty) { n->leaf = true; n->a.reset(); n->b.reset(); }
  else if (aEmpty) { std::unique_ptr<Node> keep = std::move(n->b); n = std::move(keep); }
  else if (bEmpty) { std::unique_ptr<Node> keep = std::move(n->a); n = std::move(keep); }
}

bool validNode(const Node& n, std::set<std::string>& seen, bool isRoot) {
  if (n.leaf) {
    if (n.tabs.empty() && !isRoot) return false;
    if (!n.tabs.empty() && (n.active < 0 || n.active >= static_cast<int>(n.tabs.size()))) return false;
    for (const auto& t : n.tabs)
      if (t.empty() || !seen.insert(t).second) return false;
    return n.a == nullptr && n.b == nullptr;
  }
  if (!n.a || !n.b || !(n.ratio > 0.0F && n.ratio < 1.0F) || !n.tabs.empty()) return false;
  return validNode(*n.a, seen, false) && validNode(*n.b, seen, false);
}

json::Value nodeToJson(const Node& n) {
  json::Object o;
  if (n.leaf) {
    o.set("type", "leaf");
    json::Array tabs;
    for (const auto& t : n.tabs) tabs.emplace_back(t);
    o.set("tabs", std::move(tabs));
    o.set("active", n.active);
  } else {
    o.set("type", "split");
    o.set("direction", n.sideBySide ? "horizontal" : "vertical");
    o.set("ratio", static_cast<double>(n.ratio));
    o.set("a", nodeToJson(*n.a));
    o.set("b", nodeToJson(*n.b));
  }
  return o;
}

Result<std::unique_ptr<Node>> nodeFromJson(const json::Value& v, int depth) {
  if (depth > 32 || !v.isObject()) return Error{ErrorCode::Corrupt, "dock layout: bad node"};
  const auto& o = v.asObject();
  const json::Value* type = o.find("type");
  if (type == nullptr || !type->isString()) return Error{ErrorCode::Corrupt, "dock layout: missing type"};
  auto n = std::make_unique<Node>();
  if (type->asString() == "leaf") {
    const json::Value* tabs = o.find("tabs");
    const json::Value* active = o.find("active");
    if (tabs == nullptr || !tabs->isArray()) return Error{ErrorCode::Corrupt, "dock layout: leaf without tabs"};
    for (const auto& t : tabs->asArray()) {
      if (!t.isString()) return Error{ErrorCode::Corrupt, "dock layout: tab is not a string"};
      n->tabs.push_back(t.asString());
    }
    n->active = (active != nullptr && active->isInt()) ? static_cast<int>(std::clamp<std::int64_t>(active->asInt(), 0, std::max<std::int64_t>(0, static_cast<std::int64_t>(n->tabs.size()) - 1))) : 0;
    return n;
  }
  if (type->asString() == "split") {
    const json::Value* dir = o.find("direction");
    const json::Value* ratio = o.find("ratio");
    const json::Value* a = o.find("a");
    const json::Value* b = o.find("b");
    if (dir == nullptr || !dir->isString() || ratio == nullptr || !ratio->isNumber() || a == nullptr || b == nullptr)
      return Error{ErrorCode::Corrupt, "dock layout: bad split"};
    n->leaf = false;
    n->sideBySide = dir->asString() != "vertical";
    const double r = ratio->toDouble();
    n->ratio = static_cast<float>(std::clamp(std::isfinite(r) ? r : 0.5, 0.05, 0.95));
    MINGOL_TRY_ASSIGN(auto na, nodeFromJson(*a, depth + 1));
    MINGOL_TRY_ASSIGN(auto nb, nodeFromJson(*b, depth + 1));
    n->a = std::move(na);
    n->b = std::move(nb);
    return n;
  }
  return Error{ErrorCode::Corrupt, "dock layout: unknown node type"};
}

}  // namespace

DockLayout::DockLayout() : root_(makeLeaf()) {}

void DockLayout::addPanel(const std::string& name) {
  if (contains(name) || name.empty()) return;
  Node* leaf = firstLeaf(*root_);
  leaf->tabs.push_back(name);
  leaf->active = static_cast<int>(leaf->tabs.size()) - 1;
}

bool DockLayout::contains(const std::string& name) const { return findLeaf(const_cast<Node&>(*root_), name) != nullptr; }
std::vector<std::string> DockLayout::panels() const {
  std::vector<std::string> v;
  collect(*root_, v);
  return v;
}

bool DockLayout::activate(const std::string& name) {
  int idx = 0;
  Node* leaf = findLeaf(*root_, name, &idx);
  if (leaf == nullptr) return false;
  leaf->active = idx;
  return true;
}

Result<void> DockLayout::dockRelative(const std::string& panel, const std::string& anchor, DockZone zone, float ratio) {
  if (panel.empty() || contains(panel)) return Error{ErrorCode::InvalidArgument, "panel already docked: " + panel};
  Node* leaf = findLeaf(*root_, anchor);
  if (leaf == nullptr) return Error{ErrorCode::NotFound, "anchor panel not found: " + anchor};
  if (zone == DockZone::Center) {
    leaf->tabs.push_back(panel);
    leaf->active = static_cast<int>(leaf->tabs.size()) - 1;
    return {};
  }
  ratio = std::clamp(ratio, 0.05F, 0.95F);
  auto fresh = makeLeaf();
  fresh->tabs.push_back(panel);
  auto old = makeLeaf();
  old->tabs = std::move(leaf->tabs);
  old->active = leaf->active;
  leaf->tabs.clear();
  leaf->active = 0;
  leaf->leaf = false;
  leaf->sideBySide = zone == DockZone::Left || zone == DockZone::Right;
  const bool newFirst = zone == DockZone::Left || zone == DockZone::Top;
  leaf->ratio = newFirst ? ratio : 1.0F - ratio;
  leaf->a = newFirst ? std::move(fresh) : std::move(old);
  leaf->b = newFirst ? std::move(old) : std::move(fresh);
  return {};
}

bool DockLayout::removePanel(const std::string& name) {
  int idx = 0;
  Node* leaf = findLeaf(*root_, name, &idx);
  if (leaf == nullptr) return false;
  leaf->tabs.erase(leaf->tabs.begin() + idx);
  if (leaf->active >= static_cast<int>(leaf->tabs.size())) leaf->active = std::max(0, static_cast<int>(leaf->tabs.size()) - 1);
  prune(root_);
  return true;
}

bool DockLayout::movePanel(const std::string& panel, const std::string& target, DockZone zone) {
  if (panel == target) return false;
  Node* src = findLeaf(*root_, panel);
  Node* dst = findLeaf(*root_, target);
  if (src == nullptr || dst == nullptr) return false;
  if (src == dst && (zone == DockZone::Center || src->tabs.size() == 1)) return false;
  removePanel(panel);
  // `target` is still present: removePanel only ever deletes emptied leaves
  return dockRelative(panel, target, zone).ok();
}

json::Value DockLayout::toJson() const {
  json::Object o;
  o.set("format", "mingol-dock");
  o.set("version", 1);
  o.set("root", nodeToJson(*root_));
  return o;
}

Result<DockLayout> DockLayout::fromJson(const json::Value& v) {
  if (!v.isObject()) return Error{ErrorCode::Corrupt, "dock layout: not an object"};
  const json::Value* root = v.asObject().find("root");
  const json::Value* fmt = v.asObject().find("format");
  if (fmt == nullptr || !fmt->isString() || fmt->asString() != "mingol-dock" || root == nullptr)
    return Error{ErrorCode::Corrupt, "dock layout: wrong format"};
  MINGOL_TRY_ASSIGN(auto node, nodeFromJson(*root, 0));
  DockLayout l;
  l.root_ = std::move(node);
  if (!l.valid()) return Error{ErrorCode::Corrupt, "dock layout: invalid structure"};
  return l;
}

bool DockLayout::valid() const {
  std::set<std::string> seen;
  return validNode(*root_, seen, true);
}

// ---------------------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------------------
struct DockUi {
  DockUi(Context& c, DockLayout& l) : ctx(c), layout(l) {}
  Context& ctx;
  DockLayout& layout;
  std::vector<DockSlot> slots;
  struct LeafInfo {
    Node* node;
    Rect rect;
    Rect tabBar;
  };
  std::vector<LeafInfo> leaves;
  int serial = 0;

  void run(Node& n, const Rect& r) {
    if (n.leaf) {
      leafUi(n, r);
      return;
    }
    Rect ra, rb, gap;
    if (n.sideBySide) {
      const float w = r.width() - kGap;
      float a = std::floor(w * n.ratio);
      a = std::clamp(a, std::min(kMinSize, w * 0.5F), std::max(w - kMinSize, w * 0.5F));
      ra = {r.x0, r.y0, r.x0 + a, r.y1};
      gap = {ra.x1, r.y0, ra.x1 + kGap, r.y1};
      rb = {gap.x1, r.y0, r.x1, r.y1};
    } else {
      const float h = r.height() - kGap;
      float a = std::floor(h * n.ratio);
      a = std::clamp(a, std::min(kMinSize, h * 0.5F), std::max(h - kMinSize, h * 0.5F));
      ra = {r.x0, r.y0, r.x1, r.y0 + a};
      gap = {r.x0, ra.y1, r.x1, ra.y1 + kGap};
      rb = {r.x0, gap.y1, r.x1, r.y1};
    }
    // splitter
    ctx.pushID(serial++);
    const ID gid = ctx.getID("##split");
    const ItemState st = ctx.itemBehavior(gap, gid);
    if (st.hovered || st.held) {
      ctx.draw().addRect(gap, ctx.style.accent.withAlpha(st.held ? 255 : 160));
      ctx.setHoverCursor(n.sideBySide ? platform::CursorShape::ResizeH : platform::CursorShape::ResizeV);
    }
    if (st.held) {
      const float total = n.sideBySide ? r.width() - kGap : r.height() - kGap;
      const float pos = n.sideBySide ? ctx.mousePos().x - r.x0 : ctx.mousePos().y - r.y0;
      if (total > 1) n.ratio = std::clamp(pos / total, std::min(kMinSize, total * 0.5F) / total, 1.0F - std::min(kMinSize, total * 0.5F) / total);
    }
    ctx.popID();
    run(*n.a, ra);
    run(*n.b, rb);
  }

  void leafUi(Node& n, const Rect& r) {
    const float barH = ctx.itemHeight();
    const Rect bar{r.x0, r.y0, r.x1, r.y0 + barH};
    leaves.push_back({&n, r, bar});
    ctx.draw().addRect(bar, ctx.style.panelHeader);
    ctx.draw().addRect({r.x0, r.y0 + barH, r.x1, r.y1}, ctx.style.panelBg);
    float x = r.x0;
    ctx.pushID(serial++);
    for (std::size_t i = 0; i < n.tabs.size(); ++i) {
      const std::string& name = n.tabs[i];
      const float w = ctx.textWidth(name) + 2 * (ctx.style.framePad + 6);
      const Rect tr{x, r.y0, std::min(x + w, r.x1), r.y0 + barH};
      x += w + 1;
      if (tr.width() < 10) break;
      const ID tid = ctx.getID(name);
      const ItemState st = ctx.itemBehavior(tr, tid);
      if (st.pressed || st.clicked) n.active = static_cast<int>(i);
      ctx.dragSource("DOCKTAB", name, name);
      const bool active = static_cast<int>(i) == n.active;
      ctx.draw().addRect(tr, active ? ctx.style.panelBg : (st.hovered ? ctx.style.buttonHover : ctx.style.panelHeader));
      if (active) ctx.draw().addRect({tr.x0, tr.y0, tr.x1, tr.y0 + 2}, ctx.style.accent);
      ctx.draw().pushClip(tr);
      ctx.draw().addText({tr.x0 + ctx.style.framePad + 6,
                          tr.y0 + std::floor((barH - static_cast<float>(font::kGlyphH * ctx.style.fontScale)) * 0.5F)},
                         name, active ? ctx.style.text : ctx.style.textDim, ctx.style.fontScale);
      ctx.draw().popClip();
    }
    ctx.popID();
    if (!n.tabs.empty()) {
      if (n.active < 0 || n.active >= static_cast<int>(n.tabs.size())) n.active = 0;
      slots.push_back({n.tabs[static_cast<std::size_t>(n.active)], {r.x0, r.y0 + barH, r.x1, r.y1}, true});
    }
  }

  // drop target while a tab is dragged
  void dropUi() {
    if (ctx.dragType() != "DOCKTAB") return;
    const Vec2 m = ctx.mousePos();
    const std::string dragged(ctx.dragPayload());
    for (const LeafInfo& li : leaves) {
      if (!li.rect.contains(m)) continue;
      DockZone zone = DockZone::Center;
      Rect hl = li.rect;
      if (!li.tabBar.contains(m)) {
        const float fx = (m.x - li.rect.x0) / li.rect.width();
        const float fy = (m.y - li.rect.y0) / li.rect.height();
        const float dl = fx, dr = 1 - fx, dt = fy, db = 1 - fy;
        const float mn = std::min({dl, dr, dt, db});
        if (mn < 0.25F) {
          if (mn == dl) { zone = DockZone::Left; hl.x1 = li.rect.x0 + li.rect.width() * 0.5F; }
          else if (mn == dr) { zone = DockZone::Right; hl.x0 = li.rect.x0 + li.rect.width() * 0.5F; }
          else if (mn == dt) { zone = DockZone::Top; hl.y1 = li.rect.y0 + li.rect.height() * 0.5F; }
          else { zone = DockZone::Bottom; hl.y0 = li.rect.y0 + li.rect.height() * 0.5F; }
        }
      }
      ctx.overlay().addRect(hl, ctx.style.accent.withAlpha(70));
      ctx.overlay().addRectOutline(hl, ctx.style.accent, 2);
      if (ctx.mouseReleased(0) && !li.node->tabs.empty()) {
        pendingMove = true;
        moveName = dragged;
        moveTarget.clear();
        const std::size_t act = static_cast<std::size_t>(std::clamp(li.node->active, 0, static_cast<int>(li.node->tabs.size()) - 1));
        if (li.node->tabs[act] != dragged) moveTarget = li.node->tabs[act];
        else
          for (const auto& t : li.node->tabs)
            if (t != dragged) { moveTarget = t; break; }
        moveZone = zone;
        pendingMove = !moveTarget.empty();
      }
      return;
    }
  }

  bool pendingMove = false;
  std::string moveName, moveTarget;
  DockZone moveZone = DockZone::Center;
};

std::vector<DockSlot> dockSpace(Context& ctx, DockLayout& layout, const Rect& area) {
  DockUi ui(ctx, layout);
  ctx.beginPanel("##dockspace", area, false, false);
  ui.run(layout.root(), area);
  ui.dropUi();
  ctx.endPanel();
  if (ui.pendingMove) layout.movePanel(ui.moveName, ui.moveTarget, ui.moveZone);
  return std::move(ui.slots);
}

}  // namespace mingol::gui
