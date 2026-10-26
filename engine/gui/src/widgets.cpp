// SPDX-License-Identifier: GPL-2.0-only
// Widgets built on the Context core.
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "mingol/gui/context.hpp"
#include "mingol/gui/font.hpp"

namespace mingol::gui {

using platform::Key;

namespace {
std::string_view shown(std::string_view l) {
  const auto pos = l.find("##");
  return pos == std::string_view::npos ? l : l.substr(0, pos);
}
bool parseFloat(const std::string& s, float& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  const float v = std::strtof(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) return false;
  out = v;
  return true;
}
}  // namespace

Color Context::stateColor(const ItemState& st, ID id, Color normal, Color hover, Color active) const {
  if (active_ == id && st.held) return active;
  return st.hovered ? hover : normal;
}

// ---------------------------------------------------------------------------------------
// text, buttons, toggles
// ---------------------------------------------------------------------------------------
void Context::labelColored(std::string_view text, Color c) {
  const Rect r = allocate({textWidth(text) + 2 * style.framePad, 0});
  drawText({r.x0 + style.framePad, textOrigin(r).y}, text, c);
}
void Context::label(std::string_view text) { labelColored(text, style.text); }
void Context::labelDim(std::string_view text) { labelColored(text, style.textDim); }

bool Context::button(std::string_view labelText, Vec2 size) {
  const ID id = getID(labelText);
  const std::string_view s = shown(labelText);
  const float nat = textWidth(s) + 2 * (style.framePad + 5);
  const Rect r = allocate({size.x > 0 ? size.x : (nextWidth_ > 0 ? 0 : nat), size.y});
  const ItemState st = behavior(r, id, false);
  draw().addRectRounded(r, stateColor(st, id, style.button, style.buttonHover, style.buttonActive), style.rounding);
  drawTextCentered(r, s, style.text);
  return st.clicked;
}
bool Context::smallButton(std::string_view labelText) {
  const ID id = getID(labelText);
  const std::string_view s = shown(labelText);
  const Rect r = allocate({textWidth(s) + 2 * style.framePad, itemHeight() - 4});
  const ItemState st = behavior(r, id, false);
  draw().addRectRounded(r, stateColor(st, id, style.button, style.buttonHover, style.buttonActive), style.rounding);
  drawTextCentered(r, s, style.text);
  return st.clicked;
}

bool Context::checkbox(std::string_view labelText, bool& v) {
  const ID id = getID(labelText);
  const std::string_view s = shown(labelText);
  const float box = lineHeight() - 2;
  const Rect r = allocate({box + style.spacing + textWidth(s) + 2 * style.framePad, 0});
  const ItemState st = behavior(r, id, false);
  if (st.clicked) v = !v;
  const Rect b{r.x0 + style.framePad, r.y0 + (r.height() - box) * 0.5F, r.x0 + style.framePad + box, r.y0 + (r.height() + box) * 0.5F};
  draw().addRectRounded(b, st.hovered ? style.frameHover.mix({255, 255, 255}, 0.1F) : style.frame, 2);
  draw().addRectOutline(b, style.border.mix({255, 255, 255}, 0.3F));
  if (v) draw().addRect(b.expanded(-4), style.accent);
  drawText({b.x1 + style.spacing, textOrigin(r).y}, s, style.text);
  return st.clicked;
}

bool Context::radio(std::string_view labelText, int& v, int value) {
  const ID id = getID(labelText);
  const std::string_view s = shown(labelText);
  const float box = lineHeight() - 2;
  const Rect r = allocate({box + style.spacing + textWidth(s) + 2 * style.framePad, 0});
  const ItemState st = behavior(r, id, false);
  if (st.clicked) v = value;
  const Vec2 c{r.x0 + style.framePad + box * 0.5F, (r.y0 + r.y1) * 0.5F};
  draw().addCircle(c, box * 0.5F, st.hovered ? style.frameHover.mix({255, 255, 255}, 0.1F) : style.frame, 16);
  if (v == value) draw().addCircle(c, box * 0.25F, style.accent, 12);
  drawText({c.x + box * 0.5F + style.spacing, textOrigin(r).y}, s, style.text);
  return st.clicked;
}

bool Context::selectable(std::string_view labelText, bool selected, Vec2 size) {
  const ID id = getID(labelText);
  const std::string_view s = shown(labelText);
  const Rect r = allocate({size.x > 0 ? size.x : (nextWidth_ > 0 ? 0 : 0), size.y});
  const ItemState st = behavior(r, id, false);
  if (!r.intersect(clipRect()).empty()) {
    if (selected) draw().addRect(r, style.selection);
    else if (st.hovered) draw().addRect(r, style.frameHover.mix({255, 255, 255}, 0.05F));
    drawText(textOrigin(r), s, style.text);
  }
  return st.clicked;
}

void Context::progressBar(float fraction, std::string_view overlay, Vec2 size) {
  const Rect r = allocate(size);
  draw().addRectRounded(r, style.frame, style.rounding);
  fraction = clamp(fraction, 0.0F, 1.0F);
  if (fraction > 0) draw().addRectRounded({r.x0, r.y0, r.x0 + std::max(style.rounding * 2, r.width() * fraction), r.y1}, style.accent, style.rounding);
  if (!overlay.empty()) drawTextCentered(r, overlay, style.text);
}

void Context::image(std::uint32_t texture, Vec2 size, Vec2 uv0, Vec2 uv1) {
  const Rect r = allocate(size);
  draw().addImage(texture, r, uv0, uv1);
}

ItemState Context::interactiveArea(std::string_view id, Vec2 size) {
  const ID iid = getID(id);
  if (size.x <= 0) size.x = availableWidth();
  if (size.y <= 0) size.y = availableHeight();
  const Rect r = allocate(size);
  return behavior(r, iid, false);
}

// ---------------------------------------------------------------------------------------
// text field
// ---------------------------------------------------------------------------------------
TextEdit& Context::editFor(ID id, bool multiline) {
  auto& slot = edits_[id];
  if (!slot) slot = std::make_unique<TextEdit>(multiline);
  return *slot;
}

Context::FieldResult Context::textField(const Rect& frame, ID id, TextEdit& te, float& scrollX) {
  FieldResult fr;
  const float adv = font::advance(style.fontScale);
  const float padX = style.framePad + 1;
  const ItemState st = behavior(frame, id, false);
  const std::uint64_t rev0 = te.revision();
  auto byteAt = [&](float mx) {
    const float col = (mx - (frame.x0 + padX) + scrollX) / adv;
    return te.offsetAt(0, static_cast<std::size_t>(std::max(0.0F, std::round(col))));
  };
  if (st.pressed) {
    focus_ = id;
    const std::size_t b = byteAt(mouse_.x);
    if (st.doubleClicked) te.selectWordAt(b);
    else te.setCursor(b, shift());
  } else if (st.held && focus_ == id) {
    te.setCursor(byteAt(mouse_.x), true);
  }
  if (focus_ == id) {
    for (const KeyEv& ev : keys_) {
      if (ev.isChar) {
        if (ev.ch >= 32 && ev.ch != 127) {
          std::string s;
          platform::appendUtf8(s, ev.ch);
          te.insert(s);
        }
      } else if (ev.key == Key::Enter || ev.key == Key::KpEnter) {
        fr.entered = true;
      } else if (ev.key == Key::Escape) {
        fr.cancelled = true;
      } else {
        te.key(ev.key, ev.mods, activeClipboard());
      }
    }
    keys_.clear();  // the focused field owns the keyboard
  }
  fr.changed = te.revision() != rev0;

  // caret visibility
  const float visibleW = frame.width() - 2 * padX;
  const float caretX = static_cast<float>(te.columnOf(te.cursor())) * adv;
  if (caretX - scrollX > visibleW - 2) scrollX = caretX - visibleW + 2;
  if (caretX < scrollX) scrollX = caretX;
  if (static_cast<float>(te.text().size()) * 0 + static_cast<float>(font::decode(te.text()).size()) * adv <= visibleW) scrollX = 0;

  const bool focused = focus_ == id;
  draw().addRectRounded(frame, focused ? style.frame.mix({255, 255, 255}, 0.04F) : (st.hovered ? style.frameHover : style.frame), style.rounding);
  draw().addRectOutline(frame, focused ? style.accent : style.border.mix({255, 255, 255}, 0.18F));
  draw().pushClip(frame.expanded(-1));
  const float ty = textOrigin(frame).y;
  if (focused && te.hasSelection()) {
    const auto [b, e] = te.selection();
    const float x0 = frame.x0 + padX - scrollX + static_cast<float>(te.columnOf(b)) * adv;
    const float x1 = frame.x0 + padX - scrollX + static_cast<float>(te.columnOf(e)) * adv;
    draw().addRect({x0, frame.y0 + 2, x1, frame.y1 - 2}, style.selection);
  }
  drawText({frame.x0 + padX - scrollX, ty}, te.text(), style.text);
  if (focused && !te.hasSelection() && (static_cast<long>(time_ * 2.0) % 2 == 0)) {
    const float cx = std::floor(frame.x0 + padX - scrollX + caretX);
    draw().addRect({cx, frame.y0 + 3, cx + 1, frame.y1 - 3}, style.text);
  }
  draw().popClip();
  return fr;
}

Context::TextAreaResult Context::textArea(const Rect& frame, std::string_view idText, TextEdit& te, const std::function<std::size_t(Vec2)>& byteAtMouse,
                                         const std::function<bool(platform::Key, platform::Modifiers)>& onKey, int pageLines) {
  TextAreaResult r;
  const ID id = getID(idText);
  r.item = behavior(frame, id, false);
  const std::uint64_t rev0 = te.revision();
  if (r.item.pressed) {
    focus_ = id;
    const std::size_t b = byteAtMouse ? byteAtMouse(mouse_) : te.cursor();
    if (r.item.doubleClicked) te.selectWordAt(b);
    else te.setCursor(b, shift());
  } else if (r.item.held && focus_ == id && byteAtMouse) {
    te.setCursor(byteAtMouse(mouse_), true);
  }
  if (focus_ == id) {
    for (const KeyEv& ev : keys_) {
      if (ev.isChar) {
        if (ev.ch >= 32 && ev.ch != 127) {
          std::string s;
          platform::appendUtf8(s, ev.ch);
          te.insert(s);
        }
      } else if (ev.key == Key::Escape) {
        focus_ = 0;
        break;
      } else if (onKey && onKey(ev.key, ev.mods)) {
        // consumed by the caller
      } else {
        te.key(ev.key, ev.mods, activeClipboard(), pageLines);
      }
    }
    keys_.clear();
  }
  r.focused = focus_ == id;
  r.changed = te.revision() != rev0;
  return r;
}

Rect Context::labelledFrame(std::string_view s, float& labelEnd) {
  const Rect row = allocate({0, 0});
  labelEnd = row.x0;
  if (s.empty()) return row;
  const float lw = std::floor(row.width() * style.labelFraction);
  drawText(textOrigin({row.x0, row.y0, row.x0 + lw, row.y1}), s, style.text);
  labelEnd = row.x0 + lw;
  return {row.x0 + lw, row.y0, row.x1, row.y1};
}

bool Context::inputText(std::string_view labelText, std::string& s, bool* entered) {
  const ID id = getID(labelText);
  float le;
  const Rect frame = labelledFrame(shown(labelText), le);
  TextEdit& te = editFor(id);
  if (focus_ != id && te.text() != s) te.setText(s);
  const FieldResult fr = textField(frame, id, te, editScroll_[id]);
  bool changed = false;
  if (focus_ == id && te.text() != s) {
    s = te.text();
    changed = true;
    editDirtyId_ = id;
  }
  lastRect_ = frame;
  editFinished_ = false;
  if (entered != nullptr) *entered = fr.entered;
  if (fr.entered) focus_ = 0;
  if (fr.cancelled) focus_ = 0;
  if (editDirtyId_ == id && focus_ != id) {
    editFinished_ = true;
    editDirtyId_ = 0;
  }
  return changed;
}

// ---------------------------------------------------------------------------------------
// drag values
// ---------------------------------------------------------------------------------------
bool Context::dragScalar(const Rect& frame, ID id, float& v, float speed, float lo, float hi, bool isInt) {
  bool changed = false;
  if (scalarEdit_ == id) {
    TextEdit& te = editFor(id);
    const FieldResult fr = textField(frame, id, te, editScroll_[id]);
    if (fr.entered || fr.cancelled || focus_ != id) {
      float nv;
      if (!fr.cancelled && parseFloat(te.text(), nv)) {
        if (isInt) nv = std::round(nv);
        if (hi > lo) nv = clamp(nv, lo, hi);
        if (nv != v) { v = nv; changed = true; editFinished_ = true; }
      }
      scalarEdit_ = 0;
      if (focus_ == id) focus_ = 0;
    }
    return changed;
  }
  const ItemState st = behavior(frame, id, false);
  if (st.pressed) scalarAccum_ = 0;
  if (st.held && mouseDelta_.x != 0) {
    float d = mouseDelta_.x * speed * (shift() ? 0.1F : 1.0F) * (ctrl() ? 10.0F : 1.0F);
    float nv = v;
    if (isInt) {
      scalarAccum_ += d;
      const float step = std::trunc(scalarAccum_);
      scalarAccum_ -= step;
      nv += step;
    } else {
      nv += d;
    }
    if (hi > lo) nv = clamp(nv, lo, hi);
    if (nv != v) {
      v = nv;
      changed = true;
      editDirtyId_ = id;
    }
  }
  if (st.clicked && dragMoved_ < 3.0F) {
    scalarEdit_ = id;
    focus_ = id;
    TextEdit& te = editFor(id);
    te.setText(isInt ? std::to_string(static_cast<long long>(std::lround(v))) : formatFloat(v));
    te.selectAll();
  } else if (editDirtyId_ == id && active_ != id) {
    editFinished_ = true;
    editDirtyId_ = 0;
  }
  // visuals
  draw().addRectRounded(frame, st.hovered || active_ == id ? style.frameHover.mix({255, 255, 255}, 0.05F) : style.frame, style.rounding);
  if (hi > lo) {
    const float f = clamp((v - lo) / (hi - lo), 0.0F, 1.0F);
    if (f > 0) draw().addRect({frame.x0 + 1, frame.y1 - 4, frame.x0 + 1 + (frame.width() - 2) * f, frame.y1 - 1}, style.accent);
  }
  draw().addRectOutline(frame, style.border.mix({255, 255, 255}, 0.18F));
  const std::string txt = isInt ? std::to_string(static_cast<long long>(std::lround(v))) : formatFloat(v);
  draw().pushClip(frame.expanded(-1));
  drawTextCentered(frame, txt, style.text);
  draw().popClip();
  if (st.hovered && setCursorShape) setCursorShape(platform::CursorShape::ResizeH);
  return changed;
}

bool Context::dragFloat(std::string_view labelText, float& v, float speed, float lo, float hi) {
  const ID id = getID(labelText);
  float le;
  editFinished_ = false;
  const Rect frame = labelledFrame(shown(labelText), le);
  const bool changed = dragScalar(frame, id, v, speed, lo, hi, false);
  lastRect_ = frame;
  return changed;
}

bool Context::dragInt(std::string_view labelText, int& v, float speed, int lo, int hi) {
  const ID id = getID(labelText);
  float le;
  editFinished_ = false;
  const Rect frame = labelledFrame(shown(labelText), le);
  float f = static_cast<float>(v);
  const bool changed = dragScalar(frame, id, f, speed, static_cast<float>(lo), static_cast<float>(hi), true);
  if (changed) v = static_cast<int>(std::lround(f));
  lastRect_ = frame;
  return changed;
}

bool Context::dragFloat3(std::string_view labelText, float v[3], float speed) {
  const ID base = getID(labelText);
  float le;
  editFinished_ = false;
  const Rect frame = labelledFrame(shown(labelText), le);
  const float gap = 2;
  const float w = (frame.width() - 2 * gap) / 3.0F;
  bool changed = false, finished = false;
  for (int i = 0; i < 3; ++i) {
    const Rect f{frame.x0 + (w + gap) * static_cast<float>(i), frame.y0, frame.x0 + (w + gap) * static_cast<float>(i) + w, frame.y1};
    editFinished_ = false;
    changed = dragScalar(f, base + static_cast<ID>(i) * 7919U + 1U, v[i], speed, 0, 0, false) || changed;
    finished = finished || editFinished_;
  }
  editFinished_ = finished;
  lastRect_ = frame;
  return changed;
}

bool Context::colorEdit(std::string_view labelText, float rgba[4]) {
  const ID base = getID(labelText);
  float le;
  editFinished_ = false;
  const Rect frame = labelledFrame(shown(labelText), le);
  const float sw = frame.height();
  const Rect swatch{frame.x0, frame.y0, frame.x0 + sw, frame.y1};
  auto to8 = [](float f) { return static_cast<std::uint8_t>(std::lround(clamp(f, 0.0F, 1.0F) * 255.0F)); };
  draw().addRectRounded(swatch.expanded(-1), Color(to8(rgba[0]), to8(rgba[1]), to8(rgba[2]), 255), 2);
  const float gap = 2;
  const float w = (frame.width() - sw - 4 * gap) / 4.0F;
  bool changed = false, finished = false;
  for (int i = 0; i < 4; ++i) {
    const float x0 = frame.x0 + sw + gap + (w + gap) * static_cast<float>(i);
    editFinished_ = false;
    changed = dragScalar({x0, frame.y0, x0 + w, frame.y1}, base + static_cast<ID>(i) * 104729U + 1U, rgba[i], 0.005F, 0.0F, 1.0F, false) || changed;
    finished = finished || editFinished_;
  }
  editFinished_ = finished;
  lastRect_ = frame;
  return changed;
}

// ---------------------------------------------------------------------------------------
// combo
// ---------------------------------------------------------------------------------------
bool Context::combo(std::string_view labelText, int& index, std::span<const std::string> items) {
  const ID id = getID(labelText);
  float le;
  const Rect frame = labelledFrame(shown(labelText), le);
  const ItemState st = behavior(frame, id, false);
  const ID pid = id ^ 0x5555AAAAU;
  const bool open = findPopup(pid) != nullptr;
  if (st.clicked && !open) openPopupInternal(pid, {frame.x0, frame.y1}, false, frame.width(), popupStack_.empty() ? 0 : popupStack_.back());
  draw().addRectRounded(frame, st.hovered ? style.buttonHover : style.button, style.rounding);
  const std::string cur = (index >= 0 && static_cast<std::size_t>(index) < items.size()) ? items[static_cast<std::size_t>(index)] : std::string();
  draw().pushClip(frame.expanded(-1));
  drawText(textOrigin(frame), cur, style.text);
  draw().popClip();
  const float cx = frame.x1 - 10, cy = (frame.y0 + frame.y1) * 0.5F;
  draw().addTriangle({cx - 4, cy - 2}, {cx + 4, cy - 2}, {cx, cy + 3}, style.text);
  lastRect_ = frame;
  bool changed = false;
  if (beginPopupInternal(pid, false)) {
    for (std::size_t i = 0; i < items.size(); ++i) {
      setNextWidth(std::max(textWidth(items[i]) + 2 * style.framePad + 4, availableWidth()));
      if (selectable(items[i] + "##" + std::to_string(i), static_cast<int>(i) == index)) {
        index = static_cast<int>(i);
        changed = true;
        popups_.clear();
      }
    }
    endPopup();
  }
  lastRect_ = frame;  // the popup rows must not become "the last item" of the combo
  lastState_ = st;
  lastId_ = id;
  return changed;
}

// ---------------------------------------------------------------------------------------
// trees / headers / tabs
// ---------------------------------------------------------------------------------------
bool Context::treeNode(std::string_view labelText, TreeFlags flags) {
  const ID id = getID(labelText);
  const std::string_view s = shown(labelText);
  const Rect r = allocate({0, 0});
  const ItemState st = behavior(r, id, false);
  const bool leaf = hasFlag(flags, TreeFlags::Leaf);
  if (!open_.count(id)) open_[id] = hasFlag(flags, TreeFlags::DefaultOpen);
  bool& open = open_[id];
  const float arrowW = lineHeight();
  if (!leaf) {
    const bool onArrow = mouse_.x < r.x0 + arrowW;
    if ((st.clicked && onArrow) || st.doubleClicked) open = !open;
  }
  if (!r.intersect(clipRect()).empty()) {
    if (hasFlag(flags, TreeFlags::Selected)) draw().addRect(r, style.selection);
    else if (st.hovered) draw().addRect(r, style.frameHover.mix({255, 255, 255}, 0.05F));
    if (!leaf) {
      const float cx = r.x0 + arrowW * 0.5F, cy = (r.y0 + r.y1) * 0.5F;
      if (open) draw().addTriangle({cx - 4, cy - 2}, {cx + 4, cy - 2}, {cx, cy + 3}, style.textDim);
      else draw().addTriangle({cx - 2, cy - 4}, {cx - 2, cy + 4}, {cx + 3, cy}, style.textDim);
    }
    drawText({r.x0 + arrowW + 2, textOrigin(r).y}, s, style.text);
  }
  if (!leaf && open) {
    indent();
    idStack_.push_back(id);
    return true;
  }
  return false;
}
void Context::treePop() {
  if (idStack_.size() < 2) {
    error("treePop without open treeNode");
    return;
  }
  unindent();
  idStack_.pop_back();
}

bool Context::collapsingHeader(std::string_view labelText, bool defaultOpen) {
  const ID id = getID(labelText);
  const Rect r = allocate({0, 0});
  const ItemState st = behavior(r, id, false);
  if (!open_.count(id)) open_[id] = defaultOpen;
  bool& open = open_[id];
  if (st.clicked) open = !open;
  draw().addRect(r, st.hovered ? style.panelHeader.mix({255, 255, 255}, 0.08F) : style.panelHeader);
  const float cx = r.x0 + lineHeight() * 0.5F, cy = (r.y0 + r.y1) * 0.5F;
  if (open) draw().addTriangle({cx - 4, cy - 2}, {cx + 4, cy - 2}, {cx, cy + 3}, style.text);
  else draw().addTriangle({cx - 2, cy - 4}, {cx - 2, cy + 4}, {cx + 3, cy}, style.text);
  drawText({r.x0 + lineHeight() + 2, textOrigin(r).y}, shown(labelText), style.text);
  return open;
}

bool Context::beginTabBar(std::string_view id) {
  tabBarId_ = getID(id);
  tabAny_ = false;
  idStack_.push_back(tabBarId_);
  return true;
}
bool Context::tabItem(std::string_view labelText) {
  if (tabBarId_ == 0) {
    error("tabItem outside beginTabBar");
    return false;
  }
  const ID id = getID(labelText);
  const std::string_view s = shown(labelText);
  if (tabAny_) sameLine(2);
  const Rect r = allocate({textWidth(s) + 2 * (style.framePad + 6), 0});
  const ItemState st = behavior(r, id, false);
  ID& sel = tabSel_[tabBarId_];
  if (st.clicked || sel == 0) sel = id;
  const bool selected = sel == id;
  draw().addRect(r, selected ? style.panelBg.mix({255, 255, 255}, 0.07F) : (st.hovered ? style.buttonHover : style.panelHeader));
  if (selected) draw().addRect({r.x0, r.y1 - 2, r.x1, r.y1}, style.accent);
  drawTextCentered(r, s, selected ? style.text : style.textDim);
  tabAny_ = true;
  return selected;
}
void Context::endTabBar() {
  if (tabBarId_ == 0 || idStack_.size() < 2) {
    error("endTabBar without beginTabBar");
    return;
  }
  idStack_.pop_back();
  tabBarId_ = 0;
}

}  // namespace mingol::gui
