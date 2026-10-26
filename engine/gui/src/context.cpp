// SPDX-License-Identifier: GPL-2.0-only
// Context core: input, frame lifecycle, IDs, layout, regions, popups, menus, modals, drag & drop.
#include <algorithm>
#include <cmath>

#include "mingol/core/hash.hpp"
#include "mingol/gui/context.hpp"
#include "mingol/gui/font.hpp"

namespace mingol::gui {

using platform::Key;
using platform::Modifiers;

namespace {
constexpr ID kIdSeed = 2166136261U;
constexpr int kModalLayer = 100;

std::string_view shownLabel(std::string_view l) {
  const auto pos = l.find("##");
  return pos == std::string_view::npos ? l : l.substr(0, pos);
}
}  // namespace

std::string formatFloat(float v) {
  if (!std::isfinite(v)) return std::isnan(v) ? "nan" : (v < 0 ? "-inf" : "inf");
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.3f", static_cast<double>(v));
  std::string s = buf;
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  if (s == "-0") s = "0";
  return s;
}

Context::Context() { idStack_.push_back(kIdSeed); }
Context::~Context() = default;

void Context::error(std::string msg) { errors_.push_back(std::move(msg)); }

// ---------------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------------
void Context::feed(const platform::Event& e) {
  using namespace platform;
  if (const auto* m = std::get_if<MouseMoveEvent>(&e)) {
    accMouse_ = {m->x, m->y};
    mouseValid_ = true;
  } else if (const auto* b = std::get_if<MouseButtonEvent>(&e)) {
    const int i = static_cast<int>(b->button);
    mods_ = b->mods;
    if (i >= 0 && i < 3) {
      if (b->action == KeyAction::Press) {
        accPressed_[i] = true;
        accDown_[i] = true;
      } else if (b->action == KeyAction::Release) {
        accReleased_[i] = true;
        accDown_[i] = false;
      }
    }
  } else if (const auto* s = std::get_if<ScrollEvent>(&e)) {
    accWheel_ += s->dy;
  } else if (const auto* k = std::get_if<KeyEvent>(&e)) {
    mods_ = k->mods;
    if (k->action != KeyAction::Release) {
      KeyEv ev;
      ev.key = k->key;
      ev.mods = k->mods;
      ev.repeat = k->action == KeyAction::Repeat;
      accKeys_.push_back(ev);
    }
  } else if (const auto* c = std::get_if<CharEvent>(&e)) {
    KeyEv ev;
    ev.isChar = true;
    ev.ch = c->codepoint;
    accKeys_.push_back(ev);
  } else if (const auto* f = std::get_if<FocusEvent>(&e)) {
    if (!f->focused) {
      for (int i = 0; i < 3; ++i) {
        if (accDown_[i]) accReleased_[i] = true;
        accDown_[i] = false;
      }
    }
  }
}

bool Context::keyPressed(Key k, bool allowRepeat) const {
  for (const KeyEv& ev : keys_)
    if (!ev.isChar && ev.key == k && (allowRepeat || !ev.repeat)) return true;
  return false;
}
bool Context::consumeKey(Key k) {
  for (auto it = keys_.begin(); it != keys_.end(); ++it)
    if (!it->isChar && it->key == k) {
      keys_.erase(it);
      return true;
    }
  return false;
}
bool Context::ctrl() const { return (mods_ & (Modifiers::Control | Modifiers::Super)) != Modifiers::None; }
bool Context::shift() const { return (mods_ & Modifiers::Shift) != Modifiers::None; }

Clipboard Context::activeClipboard() {
  Clipboard c;
  c.get = clipboard.get ? clipboard.get : std::function<std::string()>([this] { return internalClip_; });
  c.set = clipboard.set ? clipboard.set : std::function<void(const std::string&)>([this](const std::string& s) { internalClip_ = s; });
  return c;
}

void Context::setHoverCursor(platform::CursorShape shape) {
  if (setCursorShape) setCursorShape(shape);
}

// ---------------------------------------------------------------------------------------
// frame
// ---------------------------------------------------------------------------------------
void Context::beginFrame(Vec2 displaySize, double timeSeconds) {
  display_ = displaySize;
  dt_ = haveTime_ ? clamp(static_cast<float>(timeSeconds - time_), 0.0F, 0.25F) : 0.0F;
  haveTime_ = true;
  time_ = timeSeconds;
  errors_.clear();

  prevMouse_ = mouse_;
  mouse_ = accMouse_;
  mouseDelta_ = mouseValid_ ? mouse_ - prevMouse_ : Vec2{};
  for (int i = 0; i < 3; ++i) {
    pressed_[i] = accPressed_[i];
    released_[i] = accReleased_[i];
    down_[i] = accDown_[i];
    accPressed_[i] = accReleased_[i] = false;
  }
  wheelY_ = accWheel_;
  accWheel_ = 0;
  keys_ = std::move(accKeys_);
  accKeys_.clear();

  doubleClick_ = false;
  if (pressed_[0]) {
    const Vec2 d = mouse_ - lastClickPos_;
    if (time_ - lastClickTime_ < 0.35 && d.x * d.x + d.y * d.y <= 16.0F) {
      doubleClick_ = true;
      lastClickTime_ = -10;
    } else {
      lastClickTime_ = time_;
    }
    lastClickPos_ = mouse_;
    pressPos_ = mouse_;
  }

  idStack_.assign(1, kIdSeed);
  regions_.clear();
  popupStack_.clear();
  main_.clear();
  overlay_.clear();
  final_.clear();
  wheelConsumed_ = false;
  pressedAnyItem_ = false;
  modalDimDrawn_ = false;
  frameHover_ = 0;
  nextWidth_ = 0;
  lastRect_ = {};
  lastState_ = {};
  lastId_ = 0;
  tabBarId_ = 0;

  // Popups: a press outside every open popup closes them and is swallowed; Escape closes the top one.
  consumeClick_ = false;
  if (!popups_.empty()) {
    if (pressed_[0] || pressed_[1]) {
      bool inside = false;
      for (const PopupInfo& pi : popups_) inside = inside || pi.rect.contains(mouse_);
      if (!inside) {
        popups_.clear();
        consumeClick_ = true;
      }
    }
    if (!popups_.empty() && consumeKey(Key::Escape)) popups_.pop_back();
  }
  if (consumeClick_) {
    pressed_[0] = pressed_[1] = false;
    doubleClick_ = false;
  }
  blockLayer_ = modal_ != 0 ? kModalLayer : (!popups_.empty() ? 1 : 0);
}

void Context::endFrame() {
  if (!regions_.empty()) {
    error("unbalanced begin/end: " + std::to_string(regions_.size()) + " region(s) left open");
    while (!regions_.empty()) popRegion();
  }
  if (idStack_.size() != 1) error("unbalanced pushID/popID or treeNode/treePop");
  idStack_.assign(1, kIdSeed);
  if (pressed_[0] && !pressedAnyItem_ && !consumeClick_) focus_ = 0;
  if (!down_[0] && !pressed_[0]) active_ = 0;
  if (released_[0] && !down_[0]) active_ = 0;
  if (frameHover_ != hoverId_) {
    hoverId_ = frameHover_;
    hoverSince_ = time_;
  }
  if (!dragType_.empty()) {
    if (released_[0] || !down_[0]) {
      dragType_.clear();
      dragPayload_.clear();
      dragPreview_.clear();
      dragSourceId_ = 0;
    } else {
      const std::string text = dragPreview_;
      const float w = textWidth(text) + 2 * style.framePad;
      const Rect r{mouse_.x + 12, mouse_.y + 12, mouse_.x + 12 + w, mouse_.y + 12 + itemHeight()};
      overlay_.addRect(r, style.accentDim.withAlpha(220));
      overlay_.addText({r.x0 + style.framePad, r.y0 + (itemHeight() - font::kGlyphH * style.fontScale) * 0.5F}, text,
                       style.text, style.fontScale);
    }
  }
  final_ = main_;
  final_.append(overlay_);
}

// ---------------------------------------------------------------------------------------
// ids
// ---------------------------------------------------------------------------------------
ID Context::getID(std::string_view label) const {
  std::uint64_t h = fnv1a64(label) ^ (static_cast<std::uint64_t>(idStack_.back()) * 0x9E3779B97F4A7C15ULL);
  h ^= h >> 29;
  h *= 0xBF58476D1CE4E5B9ULL;
  h ^= h >> 32;
  const auto id = static_cast<ID>(h);
  return id == 0 ? 1U : id;
}
void Context::pushID(std::string_view s) { idStack_.push_back(getID(s)); }
void Context::pushID(std::int64_t n) {
  const std::string s = "#" + std::to_string(n);
  idStack_.push_back(getID(s));
}
void Context::popID() {
  if (idStack_.size() <= 1) {
    error("popID without pushID");
    return;
  }
  idStack_.pop_back();
}

// ---------------------------------------------------------------------------------------
// metrics / layout
// ---------------------------------------------------------------------------------------
float Context::lineHeight() const { return static_cast<float>((font::kGlyphH + 3) * style.fontScale); }
float Context::itemHeight() const { return lineHeight() + 2 * style.framePad; }
float Context::textWidth(std::string_view s) const {
  float w = 0, best = 0;
  for (char32_t cp : font::decode(s)) {
    if (cp == U'\n') { best = std::max(best, w); w = 0; continue; }
    w += font::advance(style.fontScale);
  }
  return std::max(best, w);
}

DrawList& Context::draw() { return *region().dl; }
Rect Context::clipRect() { return region().dl->currentClip(); }

void Context::drawText(Vec2 pos, std::string_view s, Color c) { region().dl->addText(pos, s, c, style.fontScale); }
Vec2 Context::textOrigin(const Rect& r) const {
  return {r.x0 + style.framePad + 1, r.y0 + std::floor((r.height() - static_cast<float>(font::kGlyphH * style.fontScale)) * 0.5F)};
}
void Context::drawTextCentered(const Rect& r, std::string_view s, Color c) {
  const float w = textWidth(s);
  drawText({std::floor(r.x0 + (r.width() - w) * 0.5F),
            r.y0 + std::floor((r.height() - static_cast<float>(font::kGlyphH * style.fontScale)) * 0.5F)},
           s, c);
}

float Context::availableWidth() const {
  const Region& r = region();
  const float x = (!r.firstItem && r.sameLine) ? r.prevRight + (r.sameLineSpacing >= 0 ? r.sameLineSpacing : style.spacing)
                                                : r.content.x0 + r.indent;
  return std::max(1.0F, r.content.x1 - x);
}
float Context::availableHeight() const {
  const Region& r = region();
  const float y = r.firstItem ? r.startY : (r.sameLine ? r.rowTop : r.rowBottom + style.spacing);
  return std::max(1.0F, r.content.y1 - y);
}

void Context::sameLine(float spacingPx) {
  Region& r = region();
  r.sameLine = true;
  r.sameLineSpacing = spacingPx;
}
void Context::spacing() { allocate({1, style.spacing * 2}); }
void Context::indent(float amount) { region().indent += amount < 0 ? style.indent : amount; }
void Context::unindent(float amount) {
  Region& r = region();
  r.indent = std::max(0.0F, r.indent - (amount < 0 ? style.indent : amount));
}
void Context::separator() {
  const Rect r = allocate({0, style.spacing * 2});
  draw().addRect({r.x0, std::floor((r.y0 + r.y1) * 0.5F), r.x1, std::floor((r.y0 + r.y1) * 0.5F) + 1}, style.border.mix({255, 255, 255}, 0.12F));
}

Rect Context::allocate(Vec2 size) {
  Region& r = region();
  float x, y;
  if (!r.firstItem && r.sameLine) {
    x = r.prevRight + (r.sameLineSpacing >= 0 ? r.sameLineSpacing : style.spacing);
    y = r.rowTop;
  } else {
    x = r.content.x0 + r.indent;
    y = r.firstItem ? r.startY : r.rowBottom + style.spacing;
    r.rowTop = y;
    r.rowBottom = y;
  }
  float w = nextWidth_ > 0 ? nextWidth_ : size.x;
  if (w <= 0) w = std::max(1.0F, r.content.x1 - x);
  const float h = size.y > 0 ? size.y : itemHeight();
  r.rowBottom = std::max(r.rowBottom, y + h);
  r.prevRight = x + w;
  r.maxY = std::max(r.maxY, y + h);
  r.usedRight = std::max(r.usedRight, x + w);
  r.firstItem = false;
  r.sameLine = false;
  r.sameLineSpacing = -1.0F;
  nextWidth_ = 0;
  lastRect_ = {x, y, x + w, y + h};
  lastState_ = {};
  lastId_ = 0;
  return lastRect_;
}

// ---------------------------------------------------------------------------------------
// item behaviour
// ---------------------------------------------------------------------------------------
bool Context::hoverIn(const Rect& r, const Rect& clip, int layer, bool ignoreBlock) const {
  if (!mouseValid_) return false;
  if (!r.intersect(clip).contains(mouse_)) return false;
  if (!ignoreBlock && layer < blockLayer_) return false;
  return true;
}
bool Context::hoverOk(const Rect& r, int layer, bool ignoreBlock) const {
  return hoverIn(r, regions_.back().dl->currentClip(), layer, ignoreBlock);
}

ItemState Context::behavior(const Rect& r, ID id, bool ignoreBlock) {
  ItemState st;
  lastId_ = id;
  const int layer = region().layer;
  const bool over = hoverOk(r, layer, ignoreBlock) && (active_ == 0 || active_ == id);
  st.hovered = over;
  if (over) frameHover_ = id;
  if (over && pressed_[0]) {
    active_ = id;
    pressedAnyItem_ = true;
    st.pressed = true;
    st.doubleClicked = doubleClick_;
    dragMoved_ = 0;
    if (focus_ != id) focus_ = 0;
  }
  if (over && pressed_[1]) st.rightClicked = true;
  if (active_ == id) {
    st.held = down_[0];
    dragMoved_ += std::fabs(mouseDelta_.x) + std::fabs(mouseDelta_.y);
    if (released_[0] || !down_[0]) {
      st.clicked = hoverOk(r, layer, ignoreBlock) && dragType_.empty();
      active_ = 0;
      st.held = false;
    }
  }
  lastState_ = st;
  return st;
}
ItemState Context::itemBehavior(const Rect& r, ID id) { return behavior(r, id, false); }

// ---------------------------------------------------------------------------------------
// regions
// ---------------------------------------------------------------------------------------
bool Context::pushRegion(ID id, const Rect& bounds, DrawList* dl, int layer, bool scroll, bool bg, float pad) {
  Region r;
  r.id = id;
  r.bounds = bounds;
  r.dl = dl;
  r.layer = layer;
  r.scroll = scroll;
  r.pad = pad;
  const float viewH = std::max(0.0F, bounds.height() - 2 * pad);
  const float prevH = contentH_.count(id) ? contentH_[id] : 0.0F;
  const bool needBar = scroll && prevH > viewH + 0.5F;
  r.content = {bounds.x0 + pad, bounds.y0 + pad, bounds.x1 - pad - (needBar ? style.scrollbar : 0.0F), bounds.y1 - pad};
  r.viewH = r.content.height();
  const float maxScroll = std::max(0.0F, prevH - viewH);
  r.scrollY = scroll && scroll_.count(id) ? clamp(scroll_[id], 0.0F, maxScroll) : 0.0F;
  r.startY = r.content.y0 - r.scrollY;
  r.rowTop = r.rowBottom = r.maxY = r.startY;
  r.usedRight = r.content.x0;
  if (bg) {
    dl->addRect(bounds, style.panelBg);
    dl->addRectOutline(bounds, style.border);
  }
  Rect clip = bounds;
  if (needBar) clip.x1 = r.content.x1 + pad;
  dl->pushClip(clip);
  regions_.push_back(r);
  nextWidth_ = 0;
  return true;
}

void Context::popRegion() {
  Region r = regions_.back();
  regions_.pop_back();
  const float contentH = r.firstItem ? 0.0F : r.maxY - r.startY;
  if (r.scroll) {
    const float viewH = std::max(0.0F, r.bounds.height() - 2 * r.pad);
    const float maxScroll = std::max(0.0F, contentH - viewH);
    float sy = r.scrollY;
    // wheel: innermost region under the mouse that can scroll gets it (inner regions end first)
    if (!wheelConsumed_ && wheelY_ != 0 && maxScroll > 0 && r.bounds.contains(mouse_) && r.layer >= blockLayer_ &&
        (active_ == 0)) {
      sy -= wheelY_ * lineHeight() * 3;
      wheelConsumed_ = true;
    }
    sy = clamp(sy, 0.0F, maxScroll);
    r.dl->popClip();
    if (maxScroll > 0) {
      const Rect track{r.bounds.x1 - style.scrollbar - 1, r.bounds.y0 + 1, r.bounds.x1 - 1, r.bounds.y1 - 1};
      r.dl->addRect(track, style.scrollTrack);
      const float th = std::max(20.0F, track.height() * viewH / contentH);
      const float room = std::max(1.0F, track.height() - th);
      float ty = track.y0 + room * (sy / maxScroll);
      Rect thumb{track.x0 + 1, ty, track.x1 - 1, ty + th};
      const ID tid = r.id * 31U + 7U;
      const bool over = hoverIn(thumb, r.bounds, r.layer, false) && (active_ == 0 || active_ == tid);
      if (over && pressed_[0]) {
        active_ = tid;
        grab_ = mouse_.y - thumb.y0;
        pressedAnyItem_ = true;
      } else if (hoverIn(track, r.bounds, r.layer, false) && pressed_[0] && active_ == 0 && !thumb.contains(mouse_)) {
        sy = clamp(sy + (mouse_.y < thumb.y0 ? -viewH : viewH), 0.0F, maxScroll);
        pressedAnyItem_ = true;
      }
      if (active_ == tid && down_[0]) {
        sy = clamp((mouse_.y - grab_ - track.y0) / room * maxScroll, 0.0F, maxScroll);
        ty = track.y0 + room * (sy / maxScroll);
        thumb = {track.x0 + 1, ty, track.x1 - 1, ty + th};
      }
      r.dl->addRectRounded(thumb, (over || active_ == tid) ? style.scrollThumbHover : style.scrollThumb, 3);
    }
    scroll_[r.id] = sy;
    contentH_[r.id] = contentH;
  } else {
    r.dl->popClip();
    contentH_[r.id] = contentH;
  }
  if (r.popup) {
    float w = r.usedRight - r.content.x0 + 2 * r.pad;
    w = std::max(w, r.minWidth);
    const float h = contentH + 2 * r.pad;
    Vec2& stored = popupSize_[r.popupId];
    stored = {w, std::min(h, std::max(40.0F, display_.y - 8))};
  }
  nextWidth_ = 0;
}

bool Context::beginPanel(std::string_view name, const Rect& bounds, bool scroll, bool background) {
  const ID id = getID(name);
  idStack_.push_back(id);
  pushRegion(id, bounds, &main_, 0, scroll, background, style.regionPad);
  return true;
}
void Context::endPanel() {
  if (regions_.empty() || regions_.back().popup || idStack_.size() < 2) {
    error("endPanel without beginPanel");
    return;
  }
  popRegion();
  idStack_.pop_back();
}

bool Context::beginChild(std::string_view id, Vec2 size, bool border) {
  const ID cid = getID(id);
  const Region& parent = region();
  const float x = (!parent.firstItem && parent.sameLine) ? parent.prevRight + style.spacing : parent.content.x0 + parent.indent;
  const float w = size.x > 0 ? size.x : std::max(1.0F, parent.content.x1 - x);
  const float h = size.y > 0 ? size.y : std::max(1.0F, availableHeight());
  const Rect r = allocate({w, h});
  idStack_.push_back(cid);
  pushRegion(cid, r, parent.dl, parent.layer, true, border, border ? style.regionPad : 2.0F);
  return true;
}
void Context::endChild() {
  if (regions_.size() < 2 || idStack_.size() < 2) {
    error("endChild without beginChild");
    return;
  }
  popRegion();
  idStack_.pop_back();
}

float Context::scrollOffset() const { return region().scrollY; }
float Context::scrollMax() const {
  const auto it = contentH_.find(region().id);
  const float h = it == contentH_.end() ? 0.0F : it->second;
  return std::max(0.0F, h - region().viewH);
}
void Context::setScrollOffset(float y) {
  scroll_[region().id] = std::max(0.0F, y);
  region().scrollY = std::max(0.0F, y);
}
bool Context::scrolledToBottom() const { return region().scrollY >= scrollMax() - 1.0F; }

// ---------------------------------------------------------------------------------------
// popups
// ---------------------------------------------------------------------------------------
Context::PopupInfo* Context::findPopup(ID id) {
  for (PopupInfo& p : popups_)
    if (p.id == id) return &p;
  return nullptr;
}
const Context::PopupInfo* Context::findPopup(ID id) const {
  for (const PopupInfo& p : popups_)
    if (p.id == id) return &p;
  return nullptr;
}

void Context::closePopupsAbove(ID parent) {
  // erase every popup opened after `parent` (parent == 0: all)
  if (parent == 0) { popups_.clear(); return; }
  for (std::size_t i = 0; i < popups_.size(); ++i)
    if (popups_[i].id == parent) { popups_.resize(i + 1); return; }
}
void Context::closeAllPopups() { popups_.clear(); }

void Context::openPopupInternal(ID id, Vec2 pos, bool menu, float minWidth, ID parent) {
  if (PopupInfo* p = findPopup(id)) {
    p->pos = pos;
    return;
  }
  closePopupsAbove(parent);
  PopupInfo pi;
  pi.id = id;
  pi.parent = parent;
  pi.pos = pos;
  pi.menu = menu;
  pi.minWidth = minWidth;
  popups_.push_back(pi);
}

void Context::openPopup(std::string_view id, Vec2 pos) {
  openPopupInternal(getID(id), pos, false, 0, popupStack_.empty() ? 0 : popupStack_.back());
}
void Context::openPopupAtLastItem(std::string_view id) {
  openPopupInternal(getID(id), {lastRect_.x0, lastRect_.y1}, false, lastRect_.width(), popupStack_.empty() ? 0 : popupStack_.back());
}
bool Context::isPopupOpen(std::string_view id) const { return findPopup(getID(id)) != nullptr; }
void Context::closeCurrentPopup() {
  if (popupStack_.empty()) return;
  closePopupsAbove(0);
}

bool Context::beginPopupInternal(ID id, bool menu) {
  PopupInfo* pi = findPopup(id);
  if (pi == nullptr) return false;
  Vec2 size = popupSize_.count(id) ? popupSize_[id] : Vec2{};
  size.x = std::max(size.x, pi->minWidth);
  Vec2 pos = pi->pos;
  pos.x = std::max(0.0F, std::min(pos.x, display_.x - size.x));
  pos.y = std::max(0.0F, std::min(pos.y, display_.y - size.y));
  const Rect bounds{pos.x, pos.y, pos.x + size.x, pos.y + size.y};
  pi->rect = bounds;
  pi->menu = menu;
  pushRegion(id, bounds, &overlay_, modal_ != 0 ? kModalLayer + 1 : 1, size.y >= display_.y - 8, true, 2.0F);
  Region& r = region();
  r.popup = true;
  r.popupId = id;
  r.minWidth = pi->minWidth;
  popupStack_.push_back(id);
  idStack_.push_back(id);
  // draw a slightly raised background
  overlay_.addRect(bounds, style.popupBg);
  overlay_.addRectOutline(bounds, style.border.mix({255, 255, 255}, 0.2F));
  return true;
}
bool Context::beginPopup(std::string_view id) { return beginPopupInternal(getID(id), false); }

void Context::endPopup() {
  if (regions_.empty() || !regions_.back().popup || popupStack_.empty()) {
    error("endPopup without beginPopup");
    return;
  }
  popRegion();
  popupStack_.pop_back();
  idStack_.pop_back();
}

bool Context::beginContextMenu(std::string_view id) {
  if (lastState_.rightClicked) openPopupInternal(getID(id), mouse_, false, 0, popupStack_.empty() ? 0 : popupStack_.back());
  return beginPopup(id);
}

// ---------------------------------------------------------------------------------------
// menus
// ---------------------------------------------------------------------------------------
bool Context::beginMainMenuBar(const Rect& bounds) {
  const ID id = getID("##mainmenubar");
  idStack_.push_back(id);
  pushRegion(id, bounds, &main_, 0, false, false, 0.0F);
  main_.addRect(bounds, style.panelHeader);
  main_.addRect({bounds.x0, bounds.y1 - 1, bounds.x1, bounds.y1}, style.border);
  region().menuBar = true;
  region().sameLineSpacing = 0;
  return true;
}
void Context::endMainMenuBar() {
  if (regions_.empty() || !regions_.back().menuBar) {
    error("endMainMenuBar without beginMainMenuBar");
    return;
  }
  popRegion();
  idStack_.pop_back();
}

bool Context::beginMenu(std::string_view labelText, bool enabled) {
  const ID id = getID(labelText);
  const std::string_view shown = shownLabel(labelText);
  const bool inBar = region().menuBar;
  const bool inPopup = region().popup;
  const ID parentPopup = popupStack_.empty() ? 0 : popupStack_.back();
  Rect r;
  ItemState st;
  if (inBar) {
    if (!region().firstItem) sameLine(0);
    setNextWidth(textWidth(shown) + 16);
    r = allocate({0, region().bounds.height()});
    st = behavior(r, id, true);
    const bool open = findPopup(id) != nullptr;
    const bool anyMenuOpen = !popups_.empty() && popups_.front().menu;
    if (enabled && st.hovered && anyMenuOpen && !open) {
      closeAllPopups();
      openPopupInternal(id, {r.x0, r.y1}, true, 0, 0);
    } else if (enabled && st.clicked) {
      if (open) closeAllPopups();
      else { closeAllPopups(); openPopupInternal(id, {r.x0, r.y1}, true, 0, 0); }
    }
    const bool nowOpen = findPopup(id) != nullptr;
    if (nowOpen || st.hovered) draw().addRect(r, nowOpen ? style.buttonActive : style.buttonHover);
    drawTextCentered(r, shown, enabled ? style.text : style.textDim);
  } else {
    const float natural = textWidth(shown) + 2 * style.framePad + 2 * style.fontScale * font::kCellW + 8;
    r = allocate({std::max(natural, availableWidth()), 0});
    if (r.width() < natural) r.x1 = r.x0 + natural;
    st = behavior(r, id, false);
    const bool open = findPopup(id) != nullptr;
    if (enabled && st.hovered && !open) {
      closePopupsAbove(parentPopup);
      openPopupInternal(id, {r.x1 - 2, r.y0 - 2}, true, 0, parentPopup);
    } else if (st.hovered && !enabled) {
      closePopupsAbove(parentPopup);
    }
    const bool nowOpen = findPopup(id) != nullptr;
    if (nowOpen || st.hovered) draw().addRect(r, style.buttonHover);
    drawText(textOrigin(r), shown, enabled ? style.text : style.textDim);
    const float cx = r.x1 - 10, cy = (r.y0 + r.y1) * 0.5F;
    draw().addTriangle({cx - 3, cy - 4}, {cx - 3, cy + 4}, {cx + 2, cy}, enabled ? style.text : style.textDim);
  }
  (void)inPopup;
  if (!enabled) return false;
  return beginPopupInternal(id, true);
}
void Context::endMenu() { endPopup(); }

bool Context::menuItem(std::string_view labelText, std::string_view shortcut, bool selected, bool enabled) {
  const ID id = getID(labelText);
  const std::string_view shown = shownLabel(labelText);
  const float check = static_cast<float>(font::kCellW * style.fontScale) + 6;
  const float natural = check + textWidth(shown) + (shortcut.empty() ? 0.0F : 24.0F + textWidth(shortcut)) + 2 * style.framePad + 8;
  Rect r = allocate({std::max(natural, availableWidth()), 0});
  if (r.width() < natural) r.x1 = r.x0 + natural;
  ItemState st = behavior(r, id, false);
  if (!enabled) st.clicked = false;
  if (st.hovered) {
    const ID cur = popupStack_.empty() ? 0 : popupStack_.back();
    // hovering a plain item closes any open submenu of this popup
    if (!popups_.empty() && popups_.back().id != cur) closePopupsAbove(cur);
  }
  if (st.hovered && enabled) draw().addRect(r, style.accentDim);
  const Color tc = enabled ? style.text : style.textDim;
  if (selected) drawText({r.x0 + style.framePad + 1, textOrigin(r).y}, "*", tc);
  drawText({r.x0 + check + style.framePad, textOrigin(r).y}, shown, tc);
  if (!shortcut.empty()) drawText({r.x1 - style.framePad - textWidth(shortcut) - 4, textOrigin(r).y}, shortcut, style.textDim);
  if (st.clicked) closeAllPopups();
  return st.clicked;
}

void Context::menuSeparator() {
  const Rect r = allocate({0, style.spacing * 2});
  const float y = std::floor((r.y0 + r.y1) * 0.5F);
  draw().addRect({r.x0 + 2, y, r.x1 - 2, y + 1}, style.border.mix({255, 255, 255}, 0.2F));
}

// ---------------------------------------------------------------------------------------
// modal
// ---------------------------------------------------------------------------------------
void Context::openModal(std::string_view id) { modal_ = getID(id); }
void Context::closeModal() { modal_ = 0; }

bool Context::beginModal(std::string_view id, std::string_view title, Vec2 size) {
  const ID mid = getID(id);
  if (modal_ != mid) return false;
  if (popups_.empty() && consumeKey(Key::Escape)) {
    modal_ = 0;
    return false;
  }
  if (!modalDimDrawn_) {
    overlay_.addRect({0, 0, display_.x, display_.y}, style.modalDim);
    modalDimDrawn_ = true;
  }
  size.x = std::min(size.x, display_.x - 16);
  size.y = std::min(size.y, display_.y - 16);
  const Rect b{std::floor((display_.x - size.x) * 0.5F), std::floor((display_.y - size.y) * 0.5F),
               std::floor((display_.x + size.x) * 0.5F), std::floor((display_.y + size.y) * 0.5F)};
  idStack_.push_back(mid);
  pushRegion(mid, b, &overlay_, kModalLayer, true, true, style.regionPad + 2);
  overlay_.addRect({b.x0, b.y0, b.x1, b.y0 + itemHeight()}, style.panelHeader);
  overlay_.addText({b.x0 + style.framePad + 2, b.y0 + (itemHeight() - font::kGlyphH * style.fontScale) * 0.5F}, shownLabel(title),
                   style.text, style.fontScale);
  // keep body below the title bar
  Region& r = region();
  r.content.y0 += itemHeight();
  r.startY = r.content.y0 - r.scrollY;
  r.rowTop = r.rowBottom = r.maxY = r.startY;
  return true;
}
void Context::endModal() {
  if (regions_.empty() || regions_.back().layer != kModalLayer || idStack_.size() < 2) {
    error("endModal without beginModal");
    return;
  }
  popRegion();
  idStack_.pop_back();
}

// ---------------------------------------------------------------------------------------
// tooltip / drag & drop
// ---------------------------------------------------------------------------------------
void Context::tooltip(std::string_view text) {
  if (!lastState_.hovered || lastId_ == 0 || lastId_ != hoverId_ || time_ - hoverSince_ < 0.4 || down_[0] || text.empty()) return;
  const float w = textWidth(text) + 2 * style.framePad + 4;
  const float h = itemHeight();
  Rect r{mouse_.x + 14, mouse_.y + 18, 0, 0};
  r.x1 = r.x0 + w;
  r.y1 = r.y0 + h;
  if (r.x1 > display_.x) { r.x0 -= r.x1 - display_.x; r.x1 = display_.x; }
  if (r.y1 > display_.y) { r.y0 = mouse_.y - h - 6; r.y1 = r.y0 + h; }
  overlay_.addRect(r, style.popupBg);
  overlay_.addRectOutline(r, style.border.mix({255, 255, 255}, 0.25F));
  overlay_.addText({r.x0 + style.framePad + 2, r.y0 + (h - font::kGlyphH * style.fontScale) * 0.5F}, text, style.text, style.fontScale);
}

bool Context::dragSource(std::string_view type, std::string_view payload, std::string_view preview) {
  if (lastId_ == 0 || active_ != lastId_ || !down_[0]) return false;
  if (!dragType_.empty()) return dragSourceId_ == lastId_;
  const Vec2 d = mouse_ - pressPos_;
  if (d.x * d.x + d.y * d.y < 25.0F) return false;
  dragType_ = std::string(type);
  dragPayload_ = std::string(payload);
  dragPreview_ = std::string(preview);
  dragSourceId_ = lastId_;
  return true;
}

bool Context::dropTarget(std::string_view type, std::string* payload) {
  if (dragType_.empty() || dragType_ != type) return false;
  const Rect r = lastRect_;
  if (!hoverOk(r, region().layer, false)) return false;
  region().dl->addRectOutline(r, style.accent, 1.0F);
  if (!released_[0]) return false;
  if (payload != nullptr) *payload = dragPayload_;
  return true;
}

}  // namespace mingol::gui
