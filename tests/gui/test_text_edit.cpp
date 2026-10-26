// SPDX-License-Identifier: GPL-2.0-only
#include <random>

#include "mingol/gui/text_edit.hpp"
#include "mingol_test.hpp"

using namespace mingol::gui;
using mingol::platform::Key;
using mingol::platform::Modifiers;

namespace {
struct Clip {
  std::string data;
  Clipboard iface() { return {[this] { return data; }, [this](const std::string& s) { data = s; }}; }
};
const Modifiers kNone = Modifiers::None, kShift = Modifiers::Shift, kCtrl = Modifiers::Control;
}  // namespace

TEST(TextEdit, InsertAndCursorMovesByCodePoint) {
  TextEdit e;
  e.insert("a\xEC\x95\x88z");  // a 안 z
  CHECK_EQ(e.text().size(), 5U);
  Clip c;
  e.key(Key::Left, kNone, c.iface());
  CHECK_EQ(e.cursor(), 4U);
  e.key(Key::Left, kNone, c.iface());
  CHECK_EQ(e.cursor(), 1U);  // jumped over the 3-byte code point
  e.key(Key::Left, kNone, c.iface());
  e.key(Key::Left, kNone, c.iface());
  CHECK_EQ(e.cursor(), 0U);
  e.key(Key::Right, kNone, c.iface());
  e.key(Key::Right, kNone, c.iface());
  CHECK_EQ(e.cursor(), 4U);
}

TEST(TextEdit, BackspaceDeleteAndSelection) {
  TextEdit e;
  Clip c;
  e.insert("hello world");
  e.key(Key::Backspace, kNone, c.iface());
  CHECK_EQ(e.text(), std::string("hello worl"));
  e.key(Key::Left, kShift, c.iface());
  e.key(Key::Left, kShift, c.iface());
  CHECK_EQ(e.selectedText(), std::string("rl"));
  e.key(Key::Delete, kNone, c.iface());
  CHECK_EQ(e.text(), std::string("hello wo"));
  e.key(Key::Home, kNone, c.iface());
  e.key(Key::Delete, kNone, c.iface());
  CHECK_EQ(e.text(), std::string("ello wo"));
  e.key(Key::Backspace, kNone, c.iface());  // at 0: no-op
  CHECK_EQ(e.text(), std::string("ello wo"));
  e.key(Key::End, kNone, c.iface());
  e.key(Key::Backspace, kCtrl, c.iface());  // delete word
  CHECK_EQ(e.text(), std::string("ello "));
}

TEST(TextEdit, WordMotion) {
  TextEdit e;
  Clip c;
  e.setText("foo  bar_baz, qux");
  e.setCursor(0);
  e.key(Key::Right, kCtrl, c.iface());
  CHECK_EQ(e.cursor(), 3U);
  e.key(Key::Right, kCtrl, c.iface());
  CHECK_EQ(e.cursor(), 12U);
  e.key(Key::Left, kCtrl, c.iface());
  CHECK_EQ(e.cursor(), 5U);
  e.selectWordAt(7);
  CHECK_EQ(e.selectedText(), std::string("bar_baz"));
  e.selectWordAt(3);  // on a space
  CHECK_EQ(e.selectedText(), std::string("bar").substr(0, 0) + "foo");
}

TEST(TextEdit, ClipboardCopyCutPaste) {
  TextEdit e;
  Clip c;
  e.setText("abcdef");
  e.setCursor(1);
  e.setCursor(4, true);
  e.key(Key::C, kCtrl, c.iface());
  CHECK_EQ(c.data, std::string("bcd"));
  CHECK_EQ(e.text(), std::string("abcdef"));
  e.key(Key::X, kCtrl, c.iface());
  CHECK_EQ(e.text(), std::string("aef"));
  e.setCursor(3);
  e.key(Key::V, kCtrl, c.iface());
  CHECK_EQ(e.text(), std::string("aefbcd"));
  c.data = "x\r\ny";
  TextEdit single;
  single.key(Key::V, kCtrl, c.iface());
  CHECK_EQ(single.text(), std::string("xy"));  // newline and CR dropped in single-line mode
  TextEdit multi(true);
  multi.key(Key::V, kCtrl, c.iface());
  CHECK_EQ(multi.text(), std::string("x\ny"));
  Clipboard none;  // missing callbacks must not crash
  e.key(Key::V, kCtrl, none);
  e.key(Key::C, kCtrl, none);
}

TEST(TextEdit, UndoRedoCoalescesTyping) {
  TextEdit e;
  Clip c;
  for (char ch : std::string("hello")) e.insert(std::string(1, ch));
  CHECK_EQ(e.undoDepth(), 1U);  // one typing run
  e.key(Key::Backspace, kNone, c.iface());
  CHECK_EQ(e.text(), std::string("hell"));
  CHECK(e.undo());
  CHECK_EQ(e.text(), std::string("hello"));
  CHECK(e.undo());
  CHECK_EQ(e.text(), std::string(""));
  CHECK(!e.undo());
  CHECK(e.redo());
  CHECK_EQ(e.text(), std::string("hello"));
  e.key(Key::Z, kCtrl | kShift, c.iface());  // redo the backspace
  CHECK_EQ(e.text(), std::string("hell"));
  e.key(Key::Z, kCtrl, c.iface());
  CHECK_EQ(e.text(), std::string("hello"));
  e.key(Key::Y, kCtrl, c.iface());
  CHECK_EQ(e.text(), std::string("hell"));
  e.insert("!");
  CHECK(!e.redo());  // new edit clears redo
}

TEST(TextEdit, MultilineNavigationKeepsDesiredColumn) {
  TextEdit e(true);
  Clip c;
  e.setText("abcdef\nab\nabcdefgh");
  e.setCursor(5);  // line 0 col 5
  e.key(Key::Down, kNone, c.iface());
  CHECK_EQ(e.lineOf(e.cursor()), 1U);
  CHECK_EQ(e.columnOf(e.cursor()), 2U);  // clamped to the short line
  e.key(Key::Down, kNone, c.iface());
  CHECK_EQ(e.lineOf(e.cursor()), 2U);
  CHECK_EQ(e.columnOf(e.cursor()), 5U);  // restored
  e.key(Key::Down, kNone, c.iface());
  CHECK_EQ(e.cursor(), e.text().size());
  e.key(Key::Up, kShift, c.iface());
  CHECK(e.hasSelection());
  e.setCursor(0);
  e.key(Key::Up, kNone, c.iface());
  CHECK_EQ(e.cursor(), 0U);
  e.key(Key::PageDown, kNone, c.iface());
  CHECK_EQ(e.cursor(), e.text().size());
  CHECK_EQ(e.lineCount(), 3U);
  CHECK_EQ(e.lineStart(1), 7U);
  CHECK_EQ(e.lineEnd(1), 9U);
  CHECK_EQ(e.lineStart(99), e.lineStart(2));
  CHECK_EQ(e.offsetAt(1, 99), 9U);
}

TEST(TextEdit, EnterTabAndSingleLineBehaviour) {
  TextEdit m(true);
  Clip c;
  CHECK(m.key(Key::Enter, kNone, c.iface()));
  CHECK(m.key(Key::Tab, kNone, c.iface()));
  CHECK_EQ(m.text(), std::string("\n  "));
  TextEdit s;
  CHECK(!s.key(Key::Enter, kNone, c.iface()));
  CHECK(!s.key(Key::Up, kNone, c.iface()));
  CHECK(!s.key(Key::Tab, kNone, c.iface()));
  CHECK(!s.key(Key::Q, kNone, c.iface()));
  CHECK(!s.key(Key::A, kNone, c.iface()));  // 'A' without ctrl is text, not a command
}

TEST(TextEdit, SetTextSanitisesAndSelectAllReplaces) {
  TextEdit e;
  e.setText("a\r\nb\xFF");
  CHECK_EQ(e.text(), std::string("ab\xEF\xBF\xBD"));
  e.selectAll();
  e.insert("Z");
  CHECK_EQ(e.text(), std::string("Z"));
  CHECK_EQ(e.cursor(), 1U);
}

TEST(TextEdit, SetCursorSnapsToBoundary) {
  TextEdit e;
  e.setText("\xEC\x95\x88");
  e.setCursor(2);
  CHECK_EQ(e.cursor(), 0U);
  e.setCursor(100);
  CHECK_EQ(e.cursor(), 3U);
}

TEST(TextEdit, RandomOpsKeepInvariants) {
  std::mt19937 rng(7);
  const Key keys[] = {Key::Left, Key::Right, Key::Up, Key::Down, Key::Home, Key::End, Key::Backspace, Key::Delete,
                      Key::Enter, Key::Tab, Key::A, Key::C, Key::X, Key::V, Key::Z, Key::Y, Key::PageUp, Key::PageDown};
  const char* snippets[] = {"a", "\xEC\x95\x88", "\n", "word ", "\xF0\x9F\x98\x80", "\xFF", "x\ty"};
  for (int run = 0; run < 50; ++run) {
    TextEdit e(run % 2 == 0);
    Clip c;
    for (int i = 0; i < 400; ++i) {
      if (rng() % 3 == 0) e.insert(snippets[rng() % 7]);
      else {
        auto mods = Modifiers::None;
        if (rng() & 1) mods = mods | Modifiers::Shift;
        if (rng() & 1) mods = mods | Modifiers::Control;
        e.key(keys[rng() % (sizeof keys / sizeof keys[0])], mods, c.iface());
      }
      REQUIRE(e.cursor() <= e.text().size());
      REQUIRE(e.anchor() <= e.text().size());
      REQUIRE(TextEdit::isBoundary(e.text(), e.cursor()));
      REQUIRE(TextEdit::isBoundary(e.text(), e.anchor()));
      REQUIRE(sanitizeUtf8(e.text()) == e.text());  // always valid UTF-8, no CR
      if (!e.multiline()) REQUIRE(e.text().find('\n') == std::string::npos);
    }
  }
}
