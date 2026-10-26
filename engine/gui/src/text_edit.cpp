// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/gui/text_edit.hpp"

#include <algorithm>
#include <cctype>

#include "mingol/gui/font.hpp"

namespace mingol::gui {

using platform::Key;
using platform::Modifiers;

namespace {
constexpr std::size_t kMaxUndo = 256;
bool isWordChar(unsigned char c) { return std::isalnum(c) != 0 || c == '_' || c >= 0x80; }
}  // namespace

std::string sanitizeUtf8(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char32_t cp : font::decode(s)) {
    if (cp == U'\r') continue;
    platform::appendUtf8(out, cp);
  }
  return out;
}

bool TextEdit::isBoundary(std::string_view s, std::size_t i) noexcept {
  return i == 0 || i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80;
}

std::size_t TextEdit::prevBoundary(std::size_t i) const noexcept {
  if (i == 0) return 0;
  --i;
  while (i > 0 && !isBoundary(text_, i)) --i;
  return i;
}
std::size_t TextEdit::nextBoundary(std::size_t i) const noexcept {
  if (i >= text_.size()) return text_.size();
  ++i;
  while (i < text_.size() && !isBoundary(text_, i)) ++i;
  return i;
}
std::size_t TextEdit::wordLeft(std::size_t i) const noexcept {
  while (i > 0 && !isWordChar(static_cast<unsigned char>(text_[prevBoundary(i)]))) i = prevBoundary(i);
  while (i > 0 && isWordChar(static_cast<unsigned char>(text_[prevBoundary(i)]))) i = prevBoundary(i);
  return i;
}
std::size_t TextEdit::wordRight(std::size_t i) const noexcept {
  while (i < text_.size() && !isWordChar(static_cast<unsigned char>(text_[i]))) i = nextBoundary(i);
  while (i < text_.size() && isWordChar(static_cast<unsigned char>(text_[i]))) i = nextBoundary(i);
  return i;
}

void TextEdit::setText(std::string_view utf8) {
  text_ = sanitizeUtf8(utf8);
  if (!multiline_) text_.erase(std::remove(text_.begin(), text_.end(), '\n'), text_.end());
  cursor_ = anchor_ = text_.size();
  undo_.clear();
  redo_.clear();
  lastWasTyping_ = false;
  haveDesired_ = false;
  ++revision_;
}

std::string TextEdit::selectedText() const {
  const auto [b, e] = selection();
  return text_.substr(b, e - b);
}

void TextEdit::setCursor(std::size_t byte, bool extend) {
  byte = std::min(byte, text_.size());
  while (byte > 0 && !isBoundary(text_, byte)) --byte;
  cursor_ = byte;
  if (!extend) anchor_ = byte;
  haveDesired_ = false;
  lastWasTyping_ = false;
}

void TextEdit::selectAll() {
  anchor_ = 0;
  cursor_ = text_.size();
  lastWasTyping_ = false;
}

void TextEdit::selectWordAt(std::size_t byte) {
  byte = std::min(byte, text_.size());
  std::size_t b = byte, e = byte;
  while (b > 0 && isWordChar(static_cast<unsigned char>(text_[prevBoundary(b)]))) b = prevBoundary(b);
  while (e < text_.size() && isWordChar(static_cast<unsigned char>(text_[e]))) e = nextBoundary(e);
  if (b == e && e < text_.size()) e = nextBoundary(e);  // punctuation / space: select that one char
  anchor_ = b;
  cursor_ = e;
  lastWasTyping_ = false;
}

void TextEdit::selectLineAt(std::size_t byte) {
  const std::size_t l = lineOf(std::min(byte, text_.size()));
  anchor_ = lineStart(l);
  cursor_ = lineEnd(l);
  lastWasTyping_ = false;
}

void TextEdit::pushUndo(bool coalesce) {
  if (coalesce && lastWasTyping_ && !undo_.empty()) return;  // extend the current typing run
  undo_.push_back({text_, cursor_, anchor_});
  if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
  redo_.clear();
}

void TextEdit::replace(std::size_t b, std::size_t e, std::string_view with, bool coalesce) {
  if (b == e && with.empty()) return;
  pushUndo(coalesce);
  text_.replace(b, e - b, with);
  cursor_ = anchor_ = b + with.size();
  lastWasTyping_ = coalesce;
  haveDesired_ = false;
  ++revision_;
}

void TextEdit::insert(std::string_view utf8) {
  std::string clean = sanitizeUtf8(utf8);
  if (!multiline_) clean.erase(std::remove(clean.begin(), clean.end(), '\n'), clean.end());
  if (clean.empty()) return;
  const auto [b, e] = selection();
  // A newline or a replaced selection ends a typing run; plain characters coalesce.
  const bool coalesce = !hasSelection() && clean.find('\n') == std::string::npos;
  replace(b, e, clean, coalesce);
}

void TextEdit::move(std::size_t to, bool extend, bool keepColumn) {
  const std::size_t col = haveDesired_ ? desiredColumn_ : columnOf(cursor_);
  cursor_ = to;
  if (!extend) anchor_ = to;
  lastWasTyping_ = false;
  haveDesired_ = keepColumn;
  if (keepColumn) desiredColumn_ = col;
}

bool TextEdit::undo() {
  if (undo_.empty()) return false;
  redo_.push_back({text_, cursor_, anchor_});
  const Snapshot s = std::move(undo_.back());
  undo_.pop_back();
  text_ = s.text; cursor_ = s.cursor; anchor_ = s.anchor;
  lastWasTyping_ = false;
  ++revision_;
  return true;
}

bool TextEdit::redo() {
  if (redo_.empty()) return false;
  undo_.push_back({text_, cursor_, anchor_});
  const Snapshot s = std::move(redo_.back());
  redo_.pop_back();
  text_ = s.text; cursor_ = s.cursor; anchor_ = s.anchor;
  lastWasTyping_ = false;
  ++revision_;
  return true;
}

std::size_t TextEdit::lineCount() const noexcept {
  return static_cast<std::size_t>(std::count(text_.begin(), text_.end(), '\n')) + 1;
}
std::size_t TextEdit::lineStart(std::size_t line) const noexcept {
  std::size_t pos = 0;
  for (std::size_t l = 0; l < line; ++l) {
    const std::size_t nl = text_.find('\n', pos);
    if (nl == std::string::npos) return pos;  // clamps to the last line
    pos = nl + 1;
  }
  return pos;
}
std::size_t TextEdit::lineEnd(std::size_t line) const noexcept {
  const std::size_t nl = text_.find('\n', lineStart(line));
  return nl == std::string::npos ? text_.size() : nl;
}
std::size_t TextEdit::lineOf(std::size_t byte) const noexcept {
  byte = std::min(byte, text_.size());
  return static_cast<std::size_t>(std::count(text_.begin(), text_.begin() + static_cast<std::ptrdiff_t>(byte), '\n'));
}
std::size_t TextEdit::columnOf(std::size_t byte) const noexcept {
  byte = std::min(byte, text_.size());
  std::size_t start = byte;
  while (start > 0 && text_[start - 1] != '\n') --start;
  std::size_t col = 0;
  for (std::size_t i = start; i < byte; ++i)
    if ((static_cast<unsigned char>(text_[i]) & 0xC0) != 0x80) ++col;
  return col;
}
std::size_t TextEdit::offsetAt(std::size_t line, std::size_t column) const noexcept {
  std::size_t i = lineStart(line);
  const std::size_t end = lineEnd(line);
  for (std::size_t c = 0; c < column && i < end; ++c) i = nextBoundary(i);
  return std::min(i, end);
}

bool TextEdit::key(Key k, Modifiers mods, const Clipboard& clip, int pageLines) {
  const bool shift = (mods & Modifiers::Shift) != Modifiers::None;
  const bool ctrl = (mods & (Modifiers::Control | Modifiers::Super)) != Modifiers::None;
  const auto [sb, se] = selection();
  switch (k) {
    case Key::Left:
      if (hasSelection() && !shift && !ctrl) move(sb, false);
      else move(ctrl ? wordLeft(cursor_) : prevBoundary(cursor_), shift);
      return true;
    case Key::Right:
      if (hasSelection() && !shift && !ctrl) move(se, false);
      else move(ctrl ? wordRight(cursor_) : nextBoundary(cursor_), shift);
      return true;
    case Key::Home:
      move(ctrl ? 0 : lineStart(lineOf(cursor_)), shift);
      return true;
    case Key::End:
      move(ctrl ? text_.size() : lineEnd(lineOf(cursor_)), shift);
      return true;
    case Key::Up:
    case Key::Down:
    case Key::PageUp:
    case Key::PageDown: {
      if (!multiline_) return false;
      const long delta = (k == Key::Up ? -1 : k == Key::Down ? 1 : k == Key::PageUp ? -pageLines : pageLines);
      const long line = static_cast<long>(lineOf(cursor_));
      const long target = std::clamp(line + delta, 0L, static_cast<long>(lineCount()) - 1);
      const std::size_t col = haveDesired_ ? desiredColumn_ : columnOf(cursor_);
      std::size_t to;
      if (line + delta < 0) to = 0;                                              // past the top: start
      else if (line + delta >= static_cast<long>(lineCount())) to = text_.size();  // past the end
      else to = offsetAt(static_cast<std::size_t>(target), col);
      move(to, shift, true);
      desiredColumn_ = col;
      return true;
    }
    case Key::Backspace:
      if (hasSelection()) replace(sb, se, "", false);
      else if (cursor_ > 0) replace(ctrl ? wordLeft(cursor_) : prevBoundary(cursor_), cursor_, "", false);
      return true;
    case Key::Delete:
      if (hasSelection()) replace(sb, se, "", false);
      else if (cursor_ < text_.size()) replace(cursor_, ctrl ? wordRight(cursor_) : nextBoundary(cursor_), "", false);
      return true;
    case Key::Enter:
    case Key::KpEnter:
      if (!multiline_) return false;
      insert("\n");
      return true;
    case Key::Tab:
      if (!multiline_) return false;
      insert("  ");
      return true;
    case Key::A:
      if (!ctrl) return false;
      selectAll();
      return true;
    case Key::C:
    case Key::X:
      if (!ctrl) return false;
      if (hasSelection() && clip.set) clip.set(selectedText());
      if (k == Key::X && hasSelection()) replace(sb, se, "", false);
      return true;
    case Key::V:
      if (!ctrl) return false;
      if (clip.get) insert(clip.get());
      return true;
    case Key::Z:
      if (!ctrl) return false;
      if (shift) redo(); else undo();
      return true;
    case Key::Y:
      if (!ctrl) return false;
      redo();
      return true;
    default:
      return false;
  }
}

}  // namespace mingol::gui
