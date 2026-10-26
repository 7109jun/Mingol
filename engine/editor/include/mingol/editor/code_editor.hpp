// SPDX-License-Identifier: GPL-2.0-only
// Multi-line code editor widget for Mingol Script (.mins): line numbers, syntax highlighting,
// diagnostics, auto-indent, selection, scrolling. The text model is gui::TextEdit.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mingol/gui/context.hpp"

namespace mingol::editor {

enum class SyntaxKind : std::uint8_t { Plain, Keyword, Type, Number, String, Comment, Punct };

struct SyntaxSpan {
  std::uint32_t begin = 0, end = 0;  // byte offsets within the line
  SyntaxKind kind = SyntaxKind::Plain;
};

// Highlights `text` line by line (block comments carry over). Pure function, used by the widget and tests.
std::vector<std::vector<SyntaxSpan>> highlightScript(std::string_view text);

struct EditorDiagnostic {
  int line = 0;  // 1-based
  int col = 0;   // 1-based, bytes
  std::string message;
};

class CodeEditor {
 public:
  CodeEditor();

  void setText(std::string_view text);  // resets undo history and the modified flag
  const std::string& text() const { return te_.text(); }
  bool modified() const { return te_.revision() != savedRevision_; }
  void markSaved() { savedRevision_ = te_.revision(); }

  void setDiagnostics(std::vector<EditorDiagnostic> d) { diags_ = std::move(d); }
  void clearDiagnostics() { diags_.clear(); }
  const std::vector<EditorDiagnostic>& diagnostics() const { return diags_; }

  void gotoLine(int line1);  // 1-based; moves the cursor and scrolls on the next draw
  int cursorLine() const { return static_cast<int>(te_.lineOf(te_.cursor())) + 1; }
  int cursorColumn() const { return static_cast<int>(te_.columnOf(te_.cursor())) + 1; }
  gui::TextEdit& edit() { return te_; }
  bool focused() const { return focused_; }

  // Draws into the current region using the remaining space (size <= 0: fill). Returns true when the text changed.
  bool draw(gui::Context& ctx, std::string_view id, Vec2 size = {0, 0});

 private:
  gui::TextEdit te_{true};
  std::uint64_t savedRevision_ = 0;
  std::uint64_t highlightRevision_ = ~0ULL;
  std::vector<std::vector<SyntaxSpan>> spans_;
  std::vector<EditorDiagnostic> diags_;
  float scrollY_ = 0, scrollX_ = 0;
  std::size_t lastCursor_ = ~std::size_t{0};
  int pendingGoto_ = 0;
  bool focused_ = false;
};

}  // namespace mingol::editor
