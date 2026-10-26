// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/editor/code_editor.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::editor;
using platform::Key;
using platform::Modifiers;

namespace {
struct Rig {
  gui::Context ctx;
  CodeEditor ed;
  double t = 0;
  Vec2 size{600, 300};
  void frame() {
    ctx.beginFrame(size, t);
    ctx.beginPanel("p", {0, 0, size.x, size.y}, false);
    ed.draw(ctx, "code", {0, 0});
    ctx.endPanel();
    ctx.endFrame();
    t += 1.0 / 60;
  }
  void click(Vec2 p) {
    ctx.feed(platform::MouseMoveEvent{p.x, p.y});
    frame();
    ctx.feed(platform::MouseButtonEvent{platform::MouseButton::Left, platform::KeyAction::Press, Modifiers::None});
    frame();
    ctx.feed(platform::MouseButtonEvent{platform::MouseButton::Left, platform::KeyAction::Release, Modifiers::None});
    frame();
  }
  void key(Key k, Modifiers m = Modifiers::None) {
    ctx.feed(platform::KeyEvent{k, 0, platform::KeyAction::Press, m});
    ctx.feed(platform::KeyEvent{k, 0, platform::KeyAction::Release, m});
    frame();
  }
  void type(const std::string& s) {
    for (char c : s) ctx.feed(platform::CharEvent{static_cast<char32_t>(c)});
    frame();
  }
};
}  // namespace

TEST(CodeEditor, HighlightClassifiesTokens) {
  const auto h = highlightScript("class Foo extends Script { // note\n  int x = 12; string s = \"a\\\"b\"; /* c\n d */ y }\n");
  REQUIRE(h.size() == 4);
  auto has = [&](std::size_t line, SyntaxKind k) { for (const auto& s : h[line]) if (s.kind == k) return true; return false; };
  CHECK(has(0, SyntaxKind::Keyword));
  CHECK(has(0, SyntaxKind::Type));
  CHECK(has(0, SyntaxKind::Comment));
  CHECK(has(1, SyntaxKind::Number));
  CHECK(has(1, SyntaxKind::String));
  CHECK(has(1, SyntaxKind::Comment));
  CHECK(has(2, SyntaxKind::Comment));  // block comment continues on the next line
  // the string with an escaped quote is one span
  int strings = 0;
  for (const auto& s : h[1]) if (s.kind == SyntaxKind::String) ++strings;
  CHECK_EQ(strings, 1);
  // spans never overlap and stay inside the line
  const std::string text = "if (a) { return 1; } // x";
  const auto h2 = highlightScript(text);
  std::uint32_t prevEnd = 0;
  for (const auto& s : h2[0]) { CHECK(s.begin >= prevEnd); CHECK(s.end <= text.size()); prevEnd = s.end; }
}

TEST(CodeEditor, TypingAutoIndentAndBraces) {
  Rig r;
  r.ed.setText("");
  r.frame();
  r.click({200, 100});
  r.type("class A {");
  r.key(Key::Enter);
  r.type("int x;");
  r.key(Key::Enter);
  CHECK_EQ(r.ed.text(), std::string("class A {\n    int x;\n    "));
  CHECK(r.ed.modified());
  r.key(Key::Z, Modifiers::Control);
  CHECK(r.ed.text().size() < 24);
  // Tab inserts spaces
  r.ed.setText("");
  r.frame();
  r.click({200, 100});
  r.key(Key::Tab);
  CHECK_EQ(r.ed.text(), std::string("    "));
  // "{" + Enter before "}" opens a block
  r.ed.setText("{}");
  r.frame();
  r.click({200, 100});
  r.ed.edit().setCursor(1);
  r.key(Key::Enter);
  CHECK_EQ(r.ed.text(), std::string("{\n    \n}"));
  CHECK_EQ(r.ed.cursorLine(), 2);
}

TEST(CodeEditor, MouseSelectionAndGoto) {
  Rig r;
  std::string many;
  for (int i = 0; i < 100; ++i) many += "line " + std::to_string(i) + "\n";
  r.ed.setText(many);
  r.frame();
  r.ed.gotoLine(60);
  r.frame();
  CHECK_EQ(r.ed.cursorLine(), 60);
  // click selects a position, shift+click is not required for this check
  r.click({150, 40});
  CHECK(r.ed.cursorLine() >= 50);  // scrolled: the click lands deep in the file
  r.ed.edit().selectAll();
  CHECK(r.ed.edit().hasSelection());
  r.frame();
  r.ed.markSaved();
  CHECK(!r.ed.modified());
  r.ed.setDiagnostics({{3, 2, "boom"}});
  r.frame();
  CHECK_EQ(r.ed.diagnostics().size(), 1u);
  CHECK(r.ctx.errors().empty());
}
