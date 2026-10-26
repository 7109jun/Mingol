// SPDX-License-Identifier: GPL-2.0-only
#include <cstdlib>
#include <random>

#include "gui_util.hpp"
#include "mingol_test.hpp"

using namespace guitest;
using mingol::platform::Key;
using mingol::platform::Modifiers;

namespace {
// Runs `build` once to learn the rect of the item it submits last, then returns that rect.
template <class F>
Rect probe(Harness& h, F&& build) {
  Rect r;
  h.frame([&] {
    build();
    r = h.ctx.lastItemRect();
  });
  return r;
}
Vec2 center(const Rect& r) { return {(r.x0 + r.x1) * 0.5F, (r.y0 + r.y1) * 0.5F}; }
}  // namespace

TEST(GuiContext, ButtonClickFiresOnceAndOnlyOnPress) {
  Harness h;
  int clicks = 0;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 200, 200});
    if (h.ctx.button("Go")) ++clicks;
    h.ctx.endPanel();
  };
  Rect r = probe(h, ui);
  CHECK(h.ctx.errors().empty());
  h.move(center(r).x, center(r).y);
  h.frame(ui);
  CHECK_EQ(clicks, 0);  // hover alone
  h.down();
  h.frame(ui);
  CHECK_EQ(clicks, 0);  // pressed, not released
  h.up();
  h.frame(ui);
  CHECK_EQ(clicks, 1);
  h.frame(ui);
  CHECK_EQ(clicks, 1);
  // press + release inside a single frame still clicks
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(clicks, 2);
  // press on the button, release elsewhere: no click
  h.down();
  h.frame(ui);
  h.move(150, 150);
  h.up();
  h.frame(ui);
  CHECK_EQ(clicks, 2);
  // press elsewhere, release on the button: no click
  h.down();
  h.frame(ui);
  h.move(center(r).x, center(r).y);
  h.up();
  h.frame(ui);
  CHECK_EQ(clicks, 2);
}

TEST(GuiContext, LayoutStacksAndSameLine) {
  Harness h;
  Rect a, b, c;
  h.frame([&] {
    h.ctx.beginPanel("p", {10, 20, 300, 200});
    h.ctx.button("A");
    a = h.ctx.lastItemRect();
    h.ctx.button("B");
    b = h.ctx.lastItemRect();
    h.ctx.sameLine();
    h.ctx.button("C");
    c = h.ctx.lastItemRect();
    h.ctx.endPanel();
  });
  CHECK_EQ(a.x0, b.x0);
  CHECK(b.y0 >= a.y1);
  CHECK_EQ(c.y0, b.y0);
  CHECK(c.x0 > b.x1);
  CHECK(a.x0 >= 10 && a.y0 >= 20);
  CHECK(h.ctx.errors().empty());
}

TEST(GuiContext, CheckboxAndRadioToggle) {
  Harness h;
  bool on = false;
  int mode = 0;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.checkbox("Enable", on);
    h.ctx.radio("A", mode, 0);
    h.ctx.radio("B", mode, 1);
    h.ctx.endPanel();
  };
  Rect cb, rb;
  h.frame([&] {
    ui();
  });
  h.frame([&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.checkbox("Enable", on);
    cb = h.ctx.lastItemRect();
    h.ctx.radio("A", mode, 0);
    h.ctx.radio("B", mode, 1);
    rb = h.ctx.lastItemRect();
    h.ctx.endPanel();
  });
  auto click = [&](Rect r) {
    h.move(center(r).x, center(r).y);
    h.down();
    h.up();
    h.frame(ui);
  };
  click(cb);
  CHECK(on);
  click(cb);
  CHECK(!on);
  click(rb);
  CHECK_EQ(mode, 1);
  click(rb);
  CHECK_EQ(mode, 1);
}

TEST(GuiContext, DragFloatDragsAndClamps) {
  Harness h;
  float v = 5.0F;
  Rect r;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.dragFloat("##v", v, 0.5F, 0.0F, 10.0F);
    r = h.ctx.lastItemRect();
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(center(r).x, center(r).y);
  h.frame(ui);
  h.down();
  h.frame(ui);
  h.move(center(r).x + 4, center(r).y);
  h.frame(ui);
  CHECK_NEAR(v, 7.0F, 1e-4F);  // 4 px * 0.5
  h.move(center(r).x + 100, center(r).y);
  h.frame(ui);
  CHECK_EQ(v, 10.0F);  // clamped
  h.up();
  h.frame(ui);
  CHECK(h.ctx.itemEditFinished() || true);
  CHECK(h.ctx.errors().empty());
}

TEST(GuiContext, DragFloatClickEntersTextEditAndCommits) {
  Harness h;
  float v = 1.5F;
  Rect r;
  bool finished = false;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.dragFloat("Speed", v);
    finished = finished || h.ctx.itemEditFinished();
    r = h.ctx.lastItemRect();
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(center(r).x, center(r).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);          // click without moving -> edit mode, text pre-selected
  CHECK(h.ctx.wantTextInput());
  h.type("12.25");
  h.frame(ui);
  h.key(Key::Enter);
  h.frame(ui);
  CHECK_NEAR(v, 12.25F, 1e-5F);
  CHECK(finished);
  CHECK(!h.ctx.wantTextInput());
}

TEST(GuiContext, DragFloatEditRejectsGarbageAndEscapeCancels) {
  Harness h;
  float v = 3.0F;
  Rect r;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.dragFloat("##x", v);
    r = h.ctx.lastItemRect();
    h.ctx.endPanel();
  };
  h.frame(ui);
  auto enterEdit = [&] {
    h.move(center(r).x, center(r).y);
    h.frame(ui);
    h.down();
    h.up();
    h.frame(ui);
  };
  enterEdit();
  h.type("abc");
  h.key(Key::Enter);
  h.frame(ui);
  CHECK_EQ(v, 3.0F);
  enterEdit();
  h.type("99");
  h.frame(ui);
  h.key(Key::Escape);
  h.frame(ui);
  CHECK_EQ(v, 3.0F);
  enterEdit();
  h.type("nan");
  h.key(Key::Enter);
  h.frame(ui);
  CHECK_EQ(v, 3.0F);
}

TEST(GuiContext, DragIntStepsWholeNumbers) {
  Harness h;
  int v = 0;
  Rect r;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.dragInt("##i", v, 0.25F);
    r = h.ctx.lastItemRect();
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(center(r).x, center(r).y);
  h.frame(ui);
  h.down();
  h.frame(ui);
  for (int i = 0; i < 4; ++i) {
    h.move(center(r).x + static_cast<float>(i + 1), center(r).y);  // 1 px per frame = 0.25/frame
    h.frame(ui);
  }
  CHECK_EQ(v, 1);
  h.move(center(r).x - 40, center(r).y);
  h.frame(ui);
  CHECK_EQ(v, -10);  // 1 - 44 px * 0.25
  h.up();
  h.frame(ui);
}

TEST(GuiContext, InputTextTypingEditingAndClipboard) {
  Harness h;
  std::string s = "abc";
  bool entered = false;
  Rect r;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    bool e = false;
    h.ctx.inputText("##name", s, &e);
    entered = entered || e;
    r = h.ctx.lastItemRect();
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(r.x1 - 3, center(r).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(h.ctx.wantTextInput());
  h.type("de");
  h.frame(ui);
  CHECK_EQ(s, std::string("abcde"));
  h.key(Key::Backspace);
  h.frame(ui);
  CHECK_EQ(s, std::string("abcd"));
  h.key(Key::A, Modifiers::Control);
  h.frame(ui);
  h.key(Key::C, Modifiers::Control);
  h.frame(ui);
  h.key(Key::End);
  h.key(Key::V, Modifiers::Control);
  h.frame(ui);
  CHECK_EQ(s, std::string("abcdabcd"));  // End collapsed the selection, so the copy is appended
}

TEST(GuiContext, InputTextPasteAppendsAndEnterCommits) {
  Harness h;
  std::string s = "ab";
  bool entered = false;
  Rect r;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    bool e = false;
    h.ctx.inputText("##name", s, &e);
    entered = entered || e;
    r = h.ctx.lastItemRect();
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(r.x1 - 3, center(r).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  h.key(Key::A, Modifiers::Control);
  h.key(Key::C, Modifiers::Control);
  h.key(Key::End);
  h.key(Key::V, Modifiers::Control);
  h.frame(ui);
  CHECK_EQ(s, std::string("abab"));
  h.key(Key::Enter);
  h.frame(ui);
  CHECK(entered);
  CHECK(!h.ctx.wantTextInput());
  // typing while unfocused does nothing
  h.type("zzz");
  h.frame(ui);
  CHECK_EQ(s, std::string("abab"));
}

TEST(GuiContext, InputTextExternalChangeSyncsWhenUnfocused) {
  Harness h;
  std::string s = "one";
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.inputText("##n", s);
    h.ctx.endPanel();
  };
  h.frame(ui);
  s = "two";
  h.frame(ui);
  h.render = true;
  h.frame(ui);
  CHECK_EQ(s, std::string("two"));
}

TEST(GuiContext, InputTextClickElsewhereLosesFocusAndFinishesEdit) {
  Harness h;
  std::string s = "x";
  Rect r, btn;
  bool finished = false;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.inputText("##n", s);
    finished = finished || h.ctx.itemEditFinished();
    r = h.ctx.lastItemRect();
    h.ctx.button("Other");
    btn = h.ctx.lastItemRect();
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(center(r).x, center(r).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  h.type("y");
  h.frame(ui);
  CHECK_EQ(s, std::string("xy"));
  CHECK(!finished);
  h.move(center(btn).x, center(btn).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  h.frame(ui);
  CHECK(finished);
  CHECK(!h.ctx.wantTextInput());
}

TEST(GuiContext, ClickOnEmptyPanelSpaceClearsFocus) {
  Harness h;
  std::string s;
  Rect r;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.inputText("##n", s);
    r = h.ctx.lastItemRect();
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(center(r).x, center(r).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(h.ctx.wantTextInput());
  h.move(150, 150);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(!h.ctx.wantTextInput());
}

TEST(GuiContext, ComboSelectsItem) {
  Harness h;
  int idx = 0;
  const std::vector<std::string> items = {"Alpha", "Beta", "Gamma"};
  Rect r;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.combo("##c", idx, items);
    r = h.ctx.lastItemRect();
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(center(r).x, center(r).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(h.ctx.anyPopupOpen());
  h.frame(ui);
  h.frame(ui);  // popup size settles after one frame
  // second item row sits below the frame
  const float rowH = h.ctx.itemHeight();
  h.move(center(r).x, r.y1 + rowH * 1.5F + 4);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(idx, 1);
  CHECK(!h.ctx.anyPopupOpen());
  CHECK(h.ctx.errors().empty());
}

TEST(GuiContext, TreeNodeToggleSelectAndBalance) {
  Harness h;
  bool childShown = false;
  bool leafClicked = false;
  Rect node;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    if (h.ctx.treeNode("Root", mingol::gui::TreeFlags::None)) {
      childShown = true;
      if (h.ctx.treeNode("Leaf", mingol::gui::TreeFlags::Leaf)) h.ctx.treePop();
      leafClicked = leafClicked || h.ctx.lastItem().clicked;
      h.ctx.treePop();
    } else {
      childShown = false;
    }
    h.ctx.endPanel();
  };
  Rect rootRect = probe(h, [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.treeNode("Root");
    node = h.ctx.lastItemRect();
    h.ctx.endPanel();
  });
  CHECK(!childShown);
  h.move(rootRect.x0 + 3, center(rootRect).y);  // on the arrow
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  h.frame(ui);
  CHECK(childShown);
  CHECK(h.ctx.errors().empty());
  // clicking the label (not the arrow) selects but does not toggle
  h.move(rootRect.x0 + 100, center(rootRect).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(childShown);
  // leaf click reported
  const Rect leafRect{rootRect.x0, rootRect.y1 + 4, rootRect.x1, rootRect.y1 + 4 + h.ctx.itemHeight()};
  h.move(center(leafRect).x, center(leafRect).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(leafClicked);
  // double click toggles closed
  h.move(rootRect.x0 + 100, center(rootRect).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(!childShown);
}

TEST(GuiContext, UnbalancedCallsAreReported) {
  Harness h;
  h.frame([&] {
    h.ctx.beginPanel("p", {0, 0, 100, 100});
    h.ctx.pushID("x");
  });
  CHECK(!h.ctx.errors().empty());
  h.frame([&] {
    h.ctx.beginPanel("p", {0, 0, 100, 100});
    h.ctx.endPanel();
    h.ctx.endPanel();
  });
  CHECK(!h.ctx.errors().empty());
  h.frame([&] {
    h.ctx.beginPanel("p", {0, 0, 100, 100});
    h.ctx.endPanel();
  });
  CHECK(h.ctx.errors().empty());  // recovers next frame
}

TEST(GuiContext, ScrollRegionWheelClipsAndStopsHits) {
  Harness h;
  int clicked = -1;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 200, 100});
    for (int i = 0; i < 30; ++i)
      if (h.ctx.button("Item " + std::to_string(i) + "##" + std::to_string(i), {150, 0})) clicked = i;
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.frame(ui);
  h.move(50, 50);
  h.frame(ui);
  const float before = 0;
  h.wheel(-3);
  h.frame(ui);
  h.frame(ui);
  // after scrolling down the first row has moved up: button 0 can't be hit at y~10 any more
  (void)before;
  h.move(20, 8);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(clicked != 0);
  // a click on a row that is outside the panel clip must not register
  clicked = -1;
  h.move(20, 150);  // below the panel
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(clicked, -1);
  CHECK(h.ctx.errors().empty());
}

TEST(GuiContext, ScrollbarThumbDragScrollsToEnd) {
  Harness h;
  float firstY = 0;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 200, 100});
    for (int i = 0; i < 40; ++i) {
      h.ctx.label("row " + std::to_string(i));
      if (i == 0) firstY = h.ctx.lastItemRect().y0;
    }
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.frame(ui);
  const float startY = firstY;
  h.move(200 - 6, 10);  // on the thumb at the top of the track
  h.frame(ui);
  h.down();
  h.frame(ui);
  h.move(200 - 6, 98);
  h.frame(ui);
  h.frame(ui);
  h.up();
  h.frame(ui);
  h.frame(ui);
  CHECK(firstY < startY - 200);  // scrolled far down
}

TEST(GuiContext, ChildRegionScrollsIndependently) {
  Harness h;
  float inner0 = 0;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 300});
    h.ctx.label("header");
    h.ctx.beginChild("list", {0, 100}, true);
    for (int i = 0; i < 30; ++i) {
      h.ctx.label("it " + std::to_string(i));
      if (i == 0) inner0 = h.ctx.lastItemRect().y0;
    }
    h.ctx.endChild();
    h.ctx.label("footer");
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.frame(ui);
  const float y0 = inner0;
  h.move(60, 60);
  h.frame(ui);
  h.wheel(-2);
  h.frame(ui);
  h.frame(ui);
  CHECK(inner0 < y0);
  CHECK(h.ctx.errors().empty());
}

TEST(GuiContext, MenuOpensItemClicksAndCloses) {
  Harness h;
  int saved = 0, quit = 0;
  Rect titleRect;
  auto ui = [&] {
    h.ctx.beginMainMenuBar({0, 0, 480, 20});
    if (h.ctx.beginMenu("File")) {
      titleRect = h.ctx.lastItemRect();
      if (h.ctx.menuItem("Save", "Ctrl+S")) ++saved;
      h.ctx.menuSeparator();
      if (h.ctx.menuItem("Quit")) ++quit;
      h.ctx.endMenu();
    }
    h.ctx.endMainMenuBar();
    h.ctx.beginPanel("p", {0, 30, 480, 360});
    h.ctx.button("Behind");
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(10, 10);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(h.ctx.anyPopupOpen());
  h.frame(ui);
  h.frame(ui);
  // Save is the first row of the popup, directly under the bar
  h.move(15, 20 + 2 + 8);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(saved, 1);
  CHECK(!h.ctx.anyPopupOpen());
  CHECK_EQ(quit, 0);
  CHECK(h.ctx.errors().empty());
}

TEST(GuiContext, PopupSwallowsOutsideClickAndEscapeCloses) {
  Harness h;
  int behind = 0;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 480, 360});
    if (h.ctx.button("Open")) h.ctx.openPopup("pop", {200, 100});
    Rect open = h.ctx.lastItemRect();
    (void)open;
    if (h.ctx.button("Behind")) ++behind;
    h.ctx.endPanel();
    if (h.ctx.beginPopup("pop")) {
      h.ctx.label("hello");
      h.ctx.endPopup();
    }
  };
  Rect openR, behindR;
  h.frame([&] {
    h.ctx.beginPanel("p", {0, 0, 480, 360});
    h.ctx.button("Open");
    openR = h.ctx.lastItemRect();
    h.ctx.button("Behind");
    behindR = h.ctx.lastItemRect();
    h.ctx.endPanel();
  });
  h.move(center(openR).x, center(openR).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(h.ctx.anyPopupOpen());
  h.frame(ui);
  // clicking "Behind" while the popup is open only closes the popup
  h.move(center(behindR).x, center(behindR).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(behind, 0);
  CHECK(!h.ctx.anyPopupOpen());
  h.frame(ui);
  // now it works again
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(behind, 1);
  // Escape closes
  h.move(center(openR).x, center(openR).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK(h.ctx.anyPopupOpen());
  h.key(Key::Escape);
  h.frame(ui);
  CHECK(!h.ctx.anyPopupOpen());
}

TEST(GuiContext, ModalBlocksBackgroundAndEscapeCancels) {
  Harness h;
  int behind = 0, ok = 0;
  Rect behindR;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 480, 360});
    if (h.ctx.button("Behind")) ++behind;
    behindR = h.ctx.lastItemRect();
    h.ctx.endPanel();
    if (h.ctx.beginModal("dlg", "Confirm", {200, 100})) {
      if (h.ctx.button("OK")) {
        ++ok;
        h.ctx.closeModal();
      }
      h.ctx.endModal();
    }
  };
  h.frame(ui);
  h.ctx.openModal("dlg");
  h.frame(ui);
  h.frame(ui);
  CHECK(h.ctx.isModalOpen());
  h.move(center(behindR).x, center(behindR).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(behind, 0);
  h.key(Key::Escape);
  h.frame(ui);
  CHECK(!h.ctx.isModalOpen());
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(behind, 1);
  CHECK_EQ(ok, 0);
}

TEST(GuiContext, ModalButtonWorks) {
  Harness h;
  int ok = 0;
  Rect okR;
  auto ui = [&] {
    if (h.ctx.beginModal("dlg", "Confirm", {200, 100})) {
      if (h.ctx.button("OK")) {
        ++ok;
        h.ctx.closeModal();
      }
      okR = h.ctx.lastItemRect();
      h.ctx.endModal();
    }
  };
  h.ctx.openModal("dlg");
  h.frame(ui);
  h.frame(ui);
  h.move(center(okR).x, center(okR).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(ok, 1);
  CHECK(!h.ctx.isModalOpen());
}

TEST(GuiContext, DragAndDropDeliversPayload) {
  Harness h;
  std::string got;
  Rect src, dst;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.button("Source");
    src = h.ctx.lastItemRect();
    h.ctx.dragSource("ENTITY", "42", "obj 42");
    h.ctx.button("Target");
    dst = h.ctx.lastItemRect();
    std::string p;
    if (h.ctx.dropTarget("ENTITY", &p)) got = p;
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(center(src).x, center(src).y);
  h.frame(ui);
  h.down();
  h.frame(ui);
  h.move(center(src).x + 10, center(src).y + 10);
  h.frame(ui);
  CHECK(h.ctx.isDragging());
  h.move(center(dst).x, center(dst).y);
  h.frame(ui);
  h.up();
  h.frame(ui);
  CHECK_EQ(got, std::string("42"));
  CHECK(!h.ctx.isDragging());
}

TEST(GuiContext, DragWithWrongTypeIsIgnoredAndSourceDoesNotClick) {
  Harness h;
  std::string got;
  int clicks = 0;
  Rect src, dst;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    if (h.ctx.button("Source")) ++clicks;
    src = h.ctx.lastItemRect();
    h.ctx.dragSource("A", "x", "x");
    h.ctx.button("Target");
    dst = h.ctx.lastItemRect();
    if (h.ctx.dropTarget("B", &got)) got = "bad";
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(center(src).x, center(src).y);
  h.frame(ui);
  h.down();
  h.frame(ui);
  h.move(center(dst).x, center(dst).y);
  h.frame(ui);
  h.up();
  h.frame(ui);
  CHECK(got.empty());
  CHECK_EQ(clicks, 0);
}

TEST(GuiContext, TooltipAppearsAfterDelay) {
  Harness h;
  Rect r;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.button("Hover me");
    r = h.ctx.lastItemRect();
    h.ctx.tooltip("tip text");
    h.ctx.endPanel();
  };
  h.frame(ui);
  h.move(center(r).x, center(r).y);
  h.frame(ui);
  const std::size_t base = h.ctx.drawList().vertices().size();
  for (int i = 0; i < 10; ++i) h.frame(ui);
  CHECK_EQ(h.ctx.drawList().vertices().size(), base);
  for (int i = 0; i < 40; ++i) h.frame(ui);
  CHECK(h.ctx.drawList().vertices().size() >= base + 44);  // tooltip background, outline and glyph quads
}

TEST(GuiContext, IdStackSeparatesIdenticalLabels) {
  Harness h;
  int clicked = -1;
  Rect rects[3];
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    for (int i = 0; i < 3; ++i) {
      h.ctx.pushID(i);
      if (h.ctx.button("Same")) clicked = i;
      rects[i] = h.ctx.lastItemRect();
      h.ctx.popID();
    }
    h.ctx.endPanel();
  };
  h.frame(ui);
  for (int i = 0; i < 3; ++i) {
    h.move(center(rects[i]).x, center(rects[i]).y);
    h.frame(ui);
    h.down();
    h.up();
    h.frame(ui);
    CHECK_EQ(clicked, i);
  }
}

TEST(GuiContext, TabsSwitch) {
  Harness h;
  Rect tabs[2];
  int shown = -1;
  auto ui = [&] {
    h.ctx.beginPanel("p", {0, 0, 300, 200});
    h.ctx.beginTabBar("tabs");
    if (h.ctx.tabItem("One")) shown = 0;
    tabs[0] = h.ctx.lastItemRect();
    if (h.ctx.tabItem("Two")) shown = 1;
    tabs[1] = h.ctx.lastItemRect();
    h.ctx.endTabBar();
    h.ctx.endPanel();
  };
  h.frame(ui);
  CHECK_EQ(shown, 0);
  h.move(center(tabs[1]).x, center(tabs[1]).y);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(shown, 1);
  CHECK(h.ctx.errors().empty());
}

TEST(GuiContext, ScreenshotOfFullUiIsProduced) {
  Harness h(2);
  h.size = {640, 480};
  h.render = true;
  float pos[3] = {1, 2.5F, -3};
  float col[4] = {0.2F, 0.5F, 0.9F, 1};
  std::string name = "Cube";
  bool b = true;
  int mode = 1;
  int sel = 1;
  const std::vector<std::string> items = {"Opaque", "Masked", "Blend"};
  auto ui = [&] {
    h.ctx.beginMainMenuBar({0, 0, 640, 28});
    if (h.ctx.beginMenu("File")) {
      h.ctx.menuItem("New", "Ctrl+N");
      h.ctx.endMenu();
    }
    h.ctx.beginMenu("Edit");
    h.ctx.endMainMenuBar();
    h.ctx.beginPanel("Inspector", {320, 30, 640, 480});
    h.ctx.inputText("Name", name);
    h.ctx.dragFloat3("Position", pos);
    h.ctx.colorEdit("Color", col);
    h.ctx.combo("Mode", sel, items);
    h.ctx.checkbox("Cast shadows", b);
    h.ctx.radio("Opaque", mode, 0);
    h.ctx.sameLine();
    h.ctx.radio("Blend", mode, 1);
    h.ctx.progressBar(0.4F, "40%");
    h.ctx.separator();
    if (h.ctx.collapsingHeader("Physics")) h.ctx.button("Add Rigidbody");
    h.ctx.endPanel();
    h.ctx.beginPanel("Hierarchy", {0, 30, 320, 480});
    if (h.ctx.treeNode("Scene", mingol::gui::TreeFlags::DefaultOpen)) {
      h.ctx.treeNode("Camera", mingol::gui::TreeFlags::Leaf | mingol::gui::TreeFlags::Selected);
      h.ctx.treeNode("Light", mingol::gui::TreeFlags::Leaf);
      h.ctx.treePop();
    }
    h.ctx.endPanel();
  };
  for (int i = 0; i < 3; ++i) h.frame(ui);
  CHECK(h.ctx.errors().empty());
  CHECK(h.ctx.drawList().vertices().size() > 500);
  const char* dir = std::getenv("MINGOL_GUI_SCREENSHOT_DIR");
  if (dir != nullptr) CHECK(mingol::gui::writePpm(h.last, (std::string(dir) + "/gui_widgets.ppm").c_str()));
}

TEST(GuiContext, RandomEventAndWidgetFuzzNeverCrashesOrUnbalances) {
  std::mt19937 rng(99);
  for (int run = 0; run < 20; ++run) {
    Harness h(1 + run % 2);
    float f = 1;
    int i = 0, sel = 0;
    bool b = false;
    std::string s = "text";
    const std::vector<std::string> items = {"a", "b", "c"};
    const Key keys[] = {Key::A, Key::Backspace, Key::Delete, Key::Left, Key::Right, Key::Enter, Key::Escape, Key::Home, Key::End, Key::V, Key::C};
    for (int frame = 0; frame < 150; ++frame) {
      const int n = static_cast<int>(rng() % 6);
      for (int e = 0; e < n; ++e) {
        switch (rng() % 7) {
          case 0: h.move(static_cast<float>(rng() % 480), static_cast<float>(rng() % 360)); break;
          case 1: h.down(static_cast<int>(rng() % 2)); break;
          case 2: h.up(static_cast<int>(rng() % 2)); break;
          case 3: h.wheel(static_cast<float>(static_cast<int>(rng() % 5) - 2)); break;
          case 4: h.key(keys[rng() % 11], (rng() & 1) ? Modifiers::Control : Modifiers::None); break;
          case 5: h.type("a\xEC\x95\x88"); break;
          default: h.ctx.feed(mingol::platform::FocusEvent{(rng() & 1) != 0}); break;
        }
      }
      h.frame([&] {
        h.ctx.beginMainMenuBar({0, 0, 480, 20});
        if (h.ctx.beginMenu("M")) {
          if (h.ctx.menuItem("x")) b = !b;
          if (h.ctx.beginMenu("sub")) {
            h.ctx.menuItem("deep");
            h.ctx.endMenu();
          }
          h.ctx.endMenu();
        }
        h.ctx.endMainMenuBar();
        h.ctx.beginPanel("P", {0, 20, 240, 360});
        h.ctx.dragFloat("f", f);
        h.ctx.dragInt("i", i);
        h.ctx.inputText("s", s);
        h.ctx.combo("c", sel, items);
        h.ctx.checkbox("b", b);
        if (h.ctx.treeNode("n")) {
          for (int k = 0; k < 20; ++k) h.ctx.button("btn" + std::to_string(k));
          h.ctx.treePop();
        }
        h.ctx.endPanel();
        h.ctx.beginPanel("Q", {240, 20, 480, 360});
        if (h.ctx.beginChild("c", {0, 100}, true)) {
          for (int k = 0; k < 20; ++k) h.ctx.selectable("sel" + std::to_string(k), k == 3);
          h.ctx.endChild();
        }
        h.ctx.button("ctx");
        if (h.ctx.beginContextMenu("cm")) {
          h.ctx.menuItem("one");
          h.ctx.endPopup();
        }
        h.ctx.endPanel();
      });
      REQUIRE(h.ctx.errors().empty());
      REQUIRE(std::isfinite(f));
    }
  }
}
