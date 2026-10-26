// SPDX-License-Identifier: GPL-2.0-only
// Pure text-editing model (UTF-8, byte offsets that always sit on code point boundaries). Shared
// by the single-line input widget and the editor's multi-line script editor. No drawing here.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mingol/platform/input.hpp"

namespace mingol::gui {

struct Clipboard {
  std::function<std::string()> get;
  std::function<void(const std::string&)> set;
};

class TextEdit {
 public:
  explicit TextEdit(bool multiline = false) : multiline_(multiline) {}

  bool multiline() const noexcept { return multiline_; }
  const std::string& text() const noexcept { return text_; }
  // Replaces the text (invalid UTF-8 is sanitised to U+FFFD, CR removed); cursor goes to the end.
  // Clears the undo history.
  void setText(std::string_view utf8);

  std::size_t cursor() const noexcept { return cursor_; }
  std::size_t anchor() const noexcept { return anchor_; }
  bool hasSelection() const noexcept { return cursor_ != anchor_; }
  std::pair<std::size_t, std::size_t> selection() const noexcept {
    return cursor_ < anchor_ ? std::pair{cursor_, anchor_} : std::pair{anchor_, cursor_};
  }
  std::string selectedText() const;
  // Moves the cursor to the nearest boundary at or before `byte`; keeps the anchor when `extend`.
  void setCursor(std::size_t byte, bool extend = false);
  void selectAll();
  void selectWordAt(std::size_t byte);
  void selectLineAt(std::size_t byte);

  // Typed text: replaces the selection. In single-line mode, newlines are dropped.
  void insert(std::string_view utf8);

  // Handles an editing key (arrows, Home/End, PageUp/Down, Backspace/Delete, Enter, Tab, Ctrl+A/C/X/V/Z/Y).
  // Returns true when the key was consumed. Enter in single-line mode is NOT consumed (the widget
  // decides what Enter means). `pageLines` is the PageUp/PageDown distance.
  bool key(platform::Key key, platform::Modifiers mods, const Clipboard& clip, int pageLines = 10);

  bool undo();
  bool redo();
  std::size_t undoDepth() const noexcept { return undo_.size(); }

  // Lines (multi-line): a line ends at '\n'; there is always at least one line.
  std::size_t lineCount() const noexcept;
  std::size_t lineStart(std::size_t line) const noexcept;
  std::size_t lineEnd(std::size_t line) const noexcept;  // offset of '\n' or text end
  std::size_t lineOf(std::size_t byte) const noexcept;
  std::size_t columnOf(std::size_t byte) const noexcept;               // in code points
  std::size_t offsetAt(std::size_t line, std::size_t column) const noexcept;  // clamps both

  std::uint64_t revision() const noexcept { return revision_; }  // bumps on every text change

  static bool isBoundary(std::string_view s, std::size_t i) noexcept;

 private:
  struct Snapshot {
    std::string text;
    std::size_t cursor, anchor;
  };
  void pushUndo(bool coalesce);
  void replace(std::size_t b, std::size_t e, std::string_view with, bool coalesce);
  void move(std::size_t to, bool extend, bool keepColumn = false);
  std::size_t prevBoundary(std::size_t i) const noexcept;
  std::size_t nextBoundary(std::size_t i) const noexcept;
  std::size_t wordLeft(std::size_t i) const noexcept;
  std::size_t wordRight(std::size_t i) const noexcept;

  bool multiline_;
  std::string text_;
  std::size_t cursor_ = 0, anchor_ = 0;
  std::size_t desiredColumn_ = 0;
  bool haveDesired_ = false;
  std::vector<Snapshot> undo_, redo_;
  bool lastWasTyping_ = false;
  std::uint64_t revision_ = 0;
};

// Replaces invalid UTF-8 with U+FFFD (one per bad byte) and removes '\r'.
std::string sanitizeUtf8(std::string_view s);

}  // namespace mingol::gui
