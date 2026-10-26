// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/editor/code_editor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

#include "mingol/gui/font.hpp"

namespace mingol::editor {

using gui::Color;
using gui::Rect;
using platform::Key;

namespace {

const std::unordered_set<std::string_view>& keywords() {
  static const std::unordered_set<std::string_view> k = {
      "module", "import", "class", "interface", "enum", "extends", "implements", "static", "public", "private", "protected",
      "final", "abstract", "override", "native", "new", "null", "this", "super", "true", "false", "if", "else", "while", "do",
      "for", "switch", "case", "default", "break", "continue", "return", "throw", "try", "catch", "finally", "yield",
      "coroutine", "var", "is", "as", "typeof", "operator", "fn", "ref", "int", "float", "bool", "string", "void"};
  return k;
}

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool identPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

Color colorFor(const gui::Style& s, SyntaxKind k) {
  switch (k) {
    case SyntaxKind::Keyword: return {197, 134, 192};
    case SyntaxKind::Type: return {78, 201, 176};
    case SyntaxKind::Number: return {181, 206, 168};
    case SyntaxKind::String: return {206, 145, 120};
    case SyntaxKind::Comment: return {106, 153, 85};
    case SyntaxKind::Punct: return s.textDim.mix(s.text, 0.5F);
    case SyntaxKind::Plain: break;
  }
  return s.text;
}

}  // namespace

std::vector<std::vector<SyntaxSpan>> highlightScript(std::string_view text) {
  std::vector<std::vector<SyntaxSpan>> lines(1);
  bool inBlock = false;
  std::size_t lineStart = 0;
  auto flushLine = [&] { lines.emplace_back(); };
  std::size_t i = 0;
  auto add = [&](std::size_t b, std::size_t e, SyntaxKind k) {
    if (e > b) lines.back().push_back({static_cast<std::uint32_t>(b - lineStart), static_cast<std::uint32_t>(e - lineStart), k});
  };
  while (i <= text.size()) {
    if (i == text.size()) break;
    const char c = text[i];
    if (c == '\n') { flushLine(); ++i; lineStart = i; continue; }
    if (inBlock) {
      std::size_t j = i;
      while (j < text.size() && text[j] != '\n' && !(text[j] == '*' && j + 1 < text.size() && text[j + 1] == '/')) ++j;
      if (j < text.size() && text[j] == '*') { j += 2; inBlock = false; }
      add(i, j, SyntaxKind::Comment);
      i = j;
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      std::size_t j = i;
      while (j < text.size() && text[j] != '\n') ++j;
      add(i, j, SyntaxKind::Comment);
      i = j;
    } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      inBlock = true;
      std::size_t j = i + 2;
      while (j < text.size() && text[j] != '\n' && !(text[j] == '*' && j + 1 < text.size() && text[j + 1] == '/')) ++j;
      if (j < text.size() && text[j] == '*') { j += 2; inBlock = false; }
      add(i, j, SyntaxKind::Comment);
      i = j;
    } else if (c == '"') {
      std::size_t j = i + 1;
      while (j < text.size() && text[j] != '\n' && text[j] != '"') { if (text[j] == '\\' && j + 1 < text.size() && text[j + 1] != '\n') ++j; ++j; }
      if (j < text.size() && text[j] == '"') ++j;
      add(i, j, SyntaxKind::String);
      i = j;
    } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
      std::size_t j = i;
      while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) != 0 || text[j] == '.' || text[j] == '_')) ++j;
      add(i, j, SyntaxKind::Number);
      i = j;
    } else if (identStart(c)) {
      std::size_t j = i;
      while (j < text.size() && identPart(text[j])) ++j;
      const std::string_view w = text.substr(i, j - i);
      if (keywords().count(w) != 0) add(i, j, SyntaxKind::Keyword);
      else if (std::isupper(static_cast<unsigned char>(w[0])) != 0) add(i, j, SyntaxKind::Type);
      i = j;
    } else if (std::ispunct(static_cast<unsigned char>(c)) != 0) {
      add(i, i + 1, SyntaxKind::Punct);
      ++i;
    } else {
      ++i;
    }
  }
  return lines;
}

CodeEditor::CodeEditor() = default;

void CodeEditor::setText(std::string_view text) {
  te_.setText(text);
  te_.setCursor(0);
  savedRevision_ = te_.revision();
  highlightRevision_ = ~0ULL;
  diags_.clear();
  scrollX_ = scrollY_ = 0;
  lastCursor_ = ~std::size_t{0};
}

void CodeEditor::gotoLine(int line1) { pendingGoto_ = std::max(1, line1); }

namespace {
// column (in code points) of byte offset `b` within the line starting at `ls`
float columnsBetween(const std::string& t, std::size_t ls, std::size_t b) {
  std::size_t n = 0;
  for (std::size_t i = ls; i < b && i < t.size(); ++i)
    if ((static_cast<unsigned char>(t[i]) & 0xC0) != 0x80) ++n;
  return static_cast<float>(n);
}
}  // namespace

bool CodeEditor::draw(gui::Context& ctx, std::string_view id, Vec2 size) {
  const gui::Style& st = ctx.style;
  const float adv = gui::font::advance(st.fontScale);
  const float lh = gui::font::lineHeight(st.fontScale);
  if (size.x <= 0) size.x = ctx.availableWidth();
  if (size.y <= 0) size.y = std::max(lh * 3, ctx.availableHeight());
  const Rect frame = ctx.allocate(size);
  const std::size_t lines = te_.lineCount();
  if (highlightRevision_ != te_.revision()) {
    spans_ = highlightScript(te_.text());
    highlightRevision_ = te_.revision();
  }
  const int digits = static_cast<int>(std::to_string(lines).size());
  const float gutter = static_cast<float>(std::max(digits, 3)) * adv + 14;
  const float textLeft = frame.x0 + gutter;
  const float viewH = frame.height();

  if (pendingGoto_ > 0) {
    const std::size_t l = std::min<std::size_t>(static_cast<std::size_t>(pendingGoto_ - 1), lines - 1);
    te_.setCursor(te_.lineStart(l));
    scrollY_ = std::max(0.0F, static_cast<float>(l) * lh - viewH * 0.4F);
    lastCursor_ = te_.cursor();
    pendingGoto_ = 0;
  }

  auto byteAtMouse = [&](Vec2 m) {
    const float ly = std::floor((m.y - frame.y0 + scrollY_) / lh);
    const std::size_t line = ly < 0 ? 0 : std::min<std::size_t>(static_cast<std::size_t>(ly), lines - 1);
    const float col = std::round((m.x - textLeft + scrollX_) / adv);
    return te_.offsetAt(line, static_cast<std::size_t>(std::max(0.0F, col)));
  };
  auto onKey = [&](Key k, platform::Modifiers mods) {
    const bool plain = !ctx.ctrl();
    if (k == Key::Enter || k == Key::KpEnter) {
      const std::size_t ls = te_.lineStart(te_.lineOf(te_.cursor()));
      const std::string& t = te_.text();
      std::string ind;
      for (std::size_t i = ls; i < t.size() && (t[i] == ' ' || t[i] == '\t'); ++i) ind += t[i];
      // last non-space character before the cursor decides whether to indent further
      std::size_t p = te_.cursor();
      while (p > ls && (t[p - 1] == ' ' || t[p - 1] == '\t')) --p;
      const bool open = p > ls && t[p - 1] == '{';
      const bool closeNext = te_.cursor() < t.size() && t[te_.cursor()] == '}';
      if (open && closeNext) {
        te_.insert("\n" + ind + "    \n" + ind);
        te_.setCursor(te_.cursor() - ind.size() - 1);
      } else {
        te_.insert("\n" + ind + (open ? "    " : ""));
      }
      return true;
    }
    if (k == Key::Tab && plain && (mods & platform::Modifiers::Shift) == platform::Modifiers::None) {
      te_.insert("    ");
      return true;
    }
    return false;
  };
  const auto res = ctx.textArea(frame, id, te_, byteAtMouse, onKey, std::max(1, static_cast<int>(viewH / lh) - 1));

  focused_ = res.focused;
  // wheel scrolling
  if (frame.contains(ctx.mousePos()) && !ctx.anyPopupOpen() && ctx.scrollY() != 0.0F) scrollY_ -= ctx.scrollY() * lh * 3;
  const float contentH = static_cast<float>(lines) * lh;
  const float maxScroll = std::max(0.0F, contentH - viewH + lh);
  // keep the caret visible when it moved
  if (te_.cursor() != lastCursor_) {
    const float cy = static_cast<float>(te_.lineOf(te_.cursor())) * lh;
    if (cy < scrollY_) scrollY_ = cy;
    if (cy + lh > scrollY_ + viewH) scrollY_ = cy + lh - viewH;
    const float cx = columnsBetween(te_.text(), te_.lineStart(te_.lineOf(te_.cursor())), te_.cursor()) * adv;
    const float visW = frame.width() - gutter - 8;
    if (cx - scrollX_ > visW) scrollX_ = cx - visW + adv * 4;
    if (cx < scrollX_) scrollX_ = std::max(0.0F, cx - adv * 4);
    lastCursor_ = te_.cursor();
  }
  scrollY_ = std::clamp(scrollY_, 0.0F, maxScroll);
  if (scrollX_ < 0) scrollX_ = 0;

  gui::DrawList& dl = ctx.draw();
  dl.addRect(frame, st.frame);
  dl.addRectOutline(frame, res.focused ? st.accent : st.border.mix({255, 255, 255}, 0.18F));
  dl.pushClip(frame.expanded(-1));
  const std::size_t first = static_cast<std::size_t>(std::max(0.0F, std::floor(scrollY_ / lh)));
  const std::size_t last = std::min(lines, first + static_cast<std::size_t>(viewH / lh) + 2);
  const std::size_t curLine = te_.lineOf(te_.cursor());
  const auto [selB, selE] = te_.selection();
  const std::string& t = te_.text();
  dl.addRect({frame.x0, frame.y0, textLeft - 4, frame.y1}, st.panelBg);
  for (std::size_t l = first; l < last; ++l) {
    const float y = frame.y0 + static_cast<float>(l) * lh - scrollY_;
    const std::size_t ls = te_.lineStart(l), le = te_.lineEnd(l);
    if (l == curLine && res.focused) dl.addRect({textLeft - 4, y, frame.x1, y + lh}, st.frame.mix({255, 255, 255}, 0.05F));
    const bool hasDiag = std::any_of(diags_.begin(), diags_.end(), [&](const EditorDiagnostic& d) { return d.line == static_cast<int>(l) + 1; });
    const std::string num = std::to_string(l + 1);
    dl.addText({frame.x0 + 6 + static_cast<float>(digits - static_cast<int>(num.size())) * adv, y + 2}, num,
               hasDiag ? st.error : (l == curLine ? st.text : st.textDim), st.fontScale);
    if (hasDiag) dl.addRect({frame.x0 + 1, y + 2, frame.x0 + 4, y + lh - 2}, st.error);
    // selection
    if (selB != selE && selE > ls && selB <= le) {
      const std::size_t b = std::max(selB, ls), e = std::min(selE, le);
      const float x0 = textLeft - scrollX_ + columnsBetween(t, ls, b) * adv;
      float x1 = textLeft - scrollX_ + columnsBetween(t, ls, e) * adv;
      if (selE > le) x1 += adv * 0.5F;  // newline is selected too
      dl.addRect({x0, y, std::max(x1, x0 + 2), y + lh}, st.selection);
    }
    // text
    dl.pushClip({textLeft - 2, frame.y0, frame.x1, frame.y1});
    const auto& sp = l < spans_.size() ? spans_[l] : std::vector<SyntaxSpan>{};
    std::size_t pos = 0;  // byte within line
    const std::size_t len = le - ls;
    auto emit = [&](std::size_t b, std::size_t e, SyntaxKind k) {
      if (e <= b) return;
      const float x = textLeft - scrollX_ + columnsBetween(t, ls, ls + b) * adv;
      if (x > frame.x1) return;
      dl.addText({x, y + 2}, std::string_view(t).substr(ls + b, e - b), colorFor(st, k), st.fontScale);
    };
    for (const SyntaxSpan& s : sp) {
      if (s.begin > pos) emit(pos, s.begin, SyntaxKind::Plain);
      emit(s.begin, std::min<std::size_t>(s.end, len), s.kind);
      pos = std::max<std::size_t>(pos, s.end);
    }
    if (pos < len) emit(pos, len, SyntaxKind::Plain);
    // diagnostics: underline + message after the line
    for (const EditorDiagnostic& d : diags_) {
      if (d.line != static_cast<int>(l) + 1) continue;
      const std::size_t cb = std::min<std::size_t>(static_cast<std::size_t>(std::max(0, d.col - 1)), len);
      std::size_t ce = cb;
      while (ce < len && identPart(t[ls + ce])) ++ce;
      if (ce == cb) ce = std::min(len, cb + 1);
      const float ux0 = textLeft - scrollX_ + columnsBetween(t, ls, ls + cb) * adv;
      const float ux1 = textLeft - scrollX_ + std::max(columnsBetween(t, ls, ls + ce), columnsBetween(t, ls, ls + cb) + 1) * adv;
      dl.addRect({ux0, y + lh - 3, ux1, y + lh - 1}, st.error);
      const float mx = textLeft - scrollX_ + (columnsBetween(t, ls, le) + 3) * adv;
      dl.addText({mx, y + 2}, d.message, st.error.withAlpha(200), st.fontScale);
      break;  // one message per line
    }
    dl.popClip();
  }
  // caret
  if (res.focused && !te_.hasSelection() && (static_cast<long>(ctx.time() * 2.0) % 2 == 0)) {
    const float cx = std::floor(textLeft - scrollX_ + columnsBetween(t, te_.lineStart(curLine), te_.cursor()) * adv);
    const float cy = frame.y0 + static_cast<float>(curLine) * lh - scrollY_;
    dl.addRect({cx, cy + 1, cx + 1, cy + lh - 1}, st.text);
  }
  // scroll thumb
  if (maxScroll > 0) {
    const float th = std::max(20.0F, viewH * viewH / (contentH + lh));
    const float ty = frame.y0 + (viewH - th) * (scrollY_ / maxScroll);
    dl.addRectRounded({frame.x1 - 7, ty, frame.x1 - 2, ty + th}, st.scrollThumb, 2);
  }
  dl.popClip();
  return res.changed;
}

}  // namespace mingol::editor
