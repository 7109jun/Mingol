// SPDX-License-Identifier: GPL-2.0-only
#include <random>

#include "gui_util.hpp"
#include "mingol/gui/dock.hpp"
#include "mingol_test.hpp"

using namespace guitest;
using mingol::gui::DockLayout;
using mingol::gui::DockZone;

namespace {
DockLayout sample() {
  DockLayout l;
  l.addPanel("Viewport");
  l.addPanel("Console");
  (void)l.dockRelative("Hierarchy", "Viewport", DockZone::Left, 0.2F);
  (void)l.dockRelative("Inspector", "Viewport", DockZone::Right, 0.25F);
  (void)l.dockRelative("Assets", "Viewport", DockZone::Bottom, 0.3F);
  l.activate("Viewport");
  return l;
}
}  // namespace

TEST(GuiDock, BuildAndQuery) {
  DockLayout l = sample();
  CHECK(l.valid());
  CHECK_EQ(l.panels().size(), 5U);
  CHECK(l.contains("Inspector"));
  CHECK(!l.contains("Nope"));
  CHECK(!l.dockRelative("Inspector", "Viewport", DockZone::Left).ok());  // duplicate
  CHECK(!l.dockRelative("X", "Nope", DockZone::Left).ok());              // missing anchor
  CHECK(l.dockRelative("Profiler", "Console", DockZone::Center).ok());   // tab merge
  CHECK(l.valid());
}

TEST(GuiDock, RemoveCollapsesEmptyLeaves) {
  DockLayout l = sample();
  CHECK(l.removePanel("Hierarchy"));
  CHECK(l.valid());
  CHECK(!l.removePanel("Hierarchy"));
  for (const char* n : {"Inspector", "Assets", "Console", "Viewport"}) CHECK(l.removePanel(n));
  CHECK(l.panels().empty());
  CHECK(l.valid());
  l.addPanel("A");
  CHECK(l.valid());
  CHECK(l.root().leaf);
}

TEST(GuiDock, MovePanelMergesAndSplits) {
  DockLayout l = sample();
  CHECK(l.movePanel("Assets", "Inspector", DockZone::Center));
  CHECK(l.valid());
  CHECK_EQ(l.panels().size(), 5U);
  CHECK(l.movePanel("Console", "Hierarchy", DockZone::Top));
  CHECK(l.valid());
  CHECK_EQ(l.panels().size(), 5U);
  CHECK(!l.movePanel("Console", "Console", DockZone::Center));
  CHECK(!l.movePanel("Ghost", "Console", DockZone::Left));
  CHECK(!l.movePanel("Inspector", "Ghost", DockZone::Left));
  // a leaf with one tab cannot be split relative to itself
  DockLayout one;
  one.addPanel("Only");
  CHECK(!one.movePanel("Only", "Only", DockZone::Left));
}

TEST(GuiDock, JsonRoundTrip) {
  DockLayout l = sample();
  (void)l.dockRelative("Profiler", "Console", DockZone::Center);
  const mingol::json::Value v = l.toJson();
  auto text = mingol::json::write(v);
  REQUIRE(text.ok());
  auto parsed = mingol::json::parse(text.value());
  REQUIRE(parsed.ok());
  auto back = DockLayout::fromJson(parsed.value());
  REQUIRE(back.ok());
  CHECK(back.value().valid());
  CHECK(back.value().toJson() == v);
  CHECK(l.panels() == back.value().panels());
}

TEST(GuiDock, FromJsonRejectsGarbage) {
  using mingol::json::Value;
  CHECK(!DockLayout::fromJson(Value(5)).ok());
  const char* bad[] = {
      R"({"format":"x","root":{"type":"leaf","tabs":[]}})",
      R"({"format":"mingol-dock","root":{"type":"leaf","tabs":[1]}})",
      R"({"format":"mingol-dock","root":{"type":"leaf","tabs":["a","a"]}})",
      R"({"format":"mingol-dock","root":{"type":"split","direction":"horizontal","ratio":0.5,"a":{"type":"leaf","tabs":[]},"b":{"type":"leaf","tabs":["x"]}}})",
      R"({"format":"mingol-dock","root":{"type":"split","direction":"horizontal","ratio":0.5,"a":{"type":"leaf","tabs":["x"]}}})",
      R"({"format":"mingol-dock","root":{"type":"weird"}})",
      R"({"format":"mingol-dock"})",
  };
  for (const char* t : bad) {
    auto p = mingol::json::parse(t);
    REQUIRE(p.ok());
    CHECK_MSG(!DockLayout::fromJson(p.value()).ok(), t);
  }
  // deeply nested split chain must not overflow the stack
  std::string deep;
  for (int i = 0; i < 200; ++i) deep += R"({"type":"split","direction":"horizontal","ratio":0.5,"a":)";
  deep += R"({"type":"leaf","tabs":["x"]})";
  for (int i = 0; i < 200; ++i) deep += R"(,"b":{"type":"leaf","tabs":["y)" + std::to_string(i) + R"("]}})";
  const std::string doc = std::string(R"({"format":"mingol-dock","root":)") + deep + "}";
  auto p = mingol::json::parse(doc);
  if (p.ok()) CHECK(!DockLayout::fromJson(p.value()).ok());
}

TEST(GuiDock, RatioClampedOnLoad) {
  auto p = mingol::json::parse(R"({"format":"mingol-dock","root":{"type":"split","direction":"vertical","ratio":7,"a":{"type":"leaf","tabs":["a"]},"b":{"type":"leaf","tabs":["b"]}}})");
  REQUIRE(p.ok());
  auto l = DockLayout::fromJson(p.value());
  REQUIRE(l.ok());
  CHECK(l.value().valid());
  CHECK(l.value().root().ratio <= 0.95F);
}

TEST(GuiDock, SlotsCoverAreaWithoutOverlap) {
  Harness h;
  h.size = {800, 600};
  DockLayout l = sample();
  std::vector<mingol::gui::DockSlot> slots;
  h.frame([&] { slots = mingol::gui::dockSpace(h.ctx, l, {0, 0, 800, 600}); });
  CHECK(h.ctx.errors().empty());
  CHECK_EQ(slots.size(), 4U);  // Console shares a leaf with Viewport? (no: Console was added first as a tab)
  for (const auto& s : slots) CHECK(s.content.width() > 50 && s.content.height() > 20);
  for (std::size_t i = 0; i < slots.size(); ++i)
    for (std::size_t j = i + 1; j < slots.size(); ++j) {
      const auto a = slots[i].content, b = slots[j].content;
      const bool overlap = a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
      CHECK_MSG(!overlap, slots[i].name + " overlaps " + slots[j].name);
    }
}

TEST(GuiDock, SplitterDragChangesRatio) {
  Harness h;
  h.size = {800, 600};
  DockLayout l;
  l.addPanel("A");
  (void)l.dockRelative("B", "A", DockZone::Right, 0.5F);
  auto ui = [&] { (void)mingol::gui::dockSpace(h.ctx, l, {0, 0, 800, 600}); };
  h.frame(ui);
  const float before = l.root().ratio;
  h.move(398, 300);  // the 4px gap sits at x=398..402
  h.frame(ui);
  h.down();
  h.frame(ui);
  h.move(598, 300);
  h.frame(ui);
  h.up();
  h.frame(ui);
  CHECK(l.root().ratio > before + 0.2F);
  h.move(598, 300);
  h.frame(ui);
  h.down();
  h.frame(ui);
  h.move(-500, 300);  // way past the left edge: clamped to minimum size
  h.frame(ui);
  h.frame(ui);
  CHECK(l.root().ratio > 0.05F);
  CHECK(l.root().ratio < 0.5F);
  CHECK(l.valid());
}

TEST(GuiDock, TabClickSelectsAndDragMergesIntoOtherLeaf) {
  Harness h;
  h.size = {800, 600};
  DockLayout l;
  l.addPanel("A");
  l.addPanel("B");
  (void)l.dockRelative("C", "A", DockZone::Right, 0.5F);
  l.activate("A");
  auto ui = [&] { (void)mingol::gui::dockSpace(h.ctx, l, {0, 0, 800, 600}); };
  h.frame(ui);
  // click the "B" tab in the left leaf
  const float tabH = h.ctx.itemHeight();
  const float aw = h.ctx.textWidth("A") + 2 * (h.ctx.style.framePad + 6);
  h.move(aw + 12, tabH * 0.5F);
  h.frame(ui);
  h.down();
  h.up();
  h.frame(ui);
  CHECK_EQ(l.root().a->active, 1);
  // drag tab B to the centre of the right leaf
  h.down();
  h.frame(ui);
  h.move(aw + 40, tabH * 0.5F + 20);
  h.frame(ui);
  h.move(600, 300);
  h.frame(ui);
  h.up();
  h.frame(ui);
  h.frame(ui);
  CHECK(l.valid());
  CHECK_EQ(l.root().a->tabs.size(), 1U);
  CHECK_EQ(l.root().b->tabs.size(), 2U);
  CHECK(l.root().b->tabs[1] == "B");
}

TEST(GuiDock, TabDragToEdgeSplits) {
  Harness h;
  h.size = {800, 600};
  DockLayout l;
  l.addPanel("A");
  l.addPanel("B");
  auto ui = [&] { (void)mingol::gui::dockSpace(h.ctx, l, {0, 0, 800, 600}); };
  h.frame(ui);
  const float tabH = h.ctx.itemHeight();
  const float aw = h.ctx.textWidth("A") + 2 * (h.ctx.style.framePad + 6);
  h.move(aw + 12, tabH * 0.5F);  // tab B
  h.frame(ui);
  h.down();
  h.frame(ui);
  h.move(aw + 12, tabH * 0.5F + 30);
  h.frame(ui);
  h.move(790, 300);  // right edge zone
  h.frame(ui);
  h.up();
  h.frame(ui);
  h.frame(ui);
  CHECK(l.valid());
  CHECK(!l.root().leaf);
  CHECK(l.root().sideBySide);
  CHECK(l.root().b->tabs.size() == 1 && l.root().b->tabs[0] == "B");
}

TEST(GuiDock, RandomMovesKeepLayoutValid) {
  std::mt19937 rng(5);
  const std::vector<std::string> names = {"A", "B", "C", "D", "E", "F"};
  const DockZone zones[] = {DockZone::Center, DockZone::Left, DockZone::Right, DockZone::Top, DockZone::Bottom};
  for (int run = 0; run < 30; ++run) {
    DockLayout l;
    for (const auto& n : names) l.addPanel(n);
    for (int i = 0; i < 100; ++i) {
      const auto& a = names[rng() % names.size()];
      const auto& b = names[rng() % names.size()];
      switch (rng() % 3) {
        case 0: l.movePanel(a, b, zones[rng() % 5]); break;
        case 1: if (l.removePanel(a)) { if (l.panels().empty()) l.addPanel(a); else (void)l.dockRelative(a, l.panels().front(), zones[rng() % 5]); } break;
        default: l.activate(a); break;
      }
      REQUIRE(l.valid());
      REQUIRE(l.panels().size() == names.size());
    }
    // slots must still be producible
    Harness h;
    h.size = {800, 600};
    std::vector<mingol::gui::DockSlot> slots;
    h.frame([&] { slots = mingol::gui::dockSpace(h.ctx, l, {0, 0, 800, 600}); });
    REQUIRE(h.ctx.errors().empty());
    REQUIRE(!slots.empty());
    auto v = DockLayout::fromJson(l.toJson());
    REQUIRE(v.ok());
    REQUIRE(v.value().toJson() == l.toJson());
  }
}
