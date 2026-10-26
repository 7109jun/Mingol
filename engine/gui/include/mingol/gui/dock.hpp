// SPDX-License-Identifier: GPL-2.0-only
// Docking: a binary split tree whose leaves hold tabbed panels. Tabs can be dragged onto another
// leaf (merge) or onto its edges (split); splitters are draggable; the layout round-trips through JSON.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/core/json.hpp"
#include "mingol/gui/context.hpp"

namespace mingol::gui {

enum class DockZone : std::uint8_t { Center, Left, Right, Top, Bottom };

class DockLayout {
 public:
  struct Node {
    bool leaf = true;
    bool sideBySide = true;  // split: children are left|right (true) or top/bottom (false)
    float ratio = 0.5F;      // split: share of the first child
    std::unique_ptr<Node> a, b;
    std::vector<std::string> tabs;  // leaf
    int active = 0;                 // leaf: index into tabs
  };

  DockLayout();
  DockLayout(DockLayout&&) noexcept = default;
  DockLayout& operator=(DockLayout&&) noexcept = default;

  // ---- building ----
  // Adds a panel as a tab of the root-most leaf (or the first leaf found).
  void addPanel(const std::string& name);
  // Splits `anchorPanel`'s leaf; the new panel takes `ratio` of the space on `zone`'s side.
  // Fails when the anchor does not exist or the panel already exists.
  Result<void> dockRelative(const std::string& panel, const std::string& anchorPanel, DockZone zone, float ratio = 0.3F);
  // Removes a panel from the layout (collapsing emptied leaves). Returns false if absent.
  bool removePanel(const std::string& name);
  bool contains(const std::string& name) const;
  std::vector<std::string> panels() const;
  bool activate(const std::string& name);
  // Moves `panel` onto `targetPanel`'s leaf. No-op (false) if it would not change anything.
  bool movePanel(const std::string& panel, const std::string& targetPanel, DockZone zone);

  json::Value toJson() const;
  static Result<DockLayout> fromJson(const json::Value& v);

  Node& root() { return *root_; }
  const Node& root() const { return *root_; }

  // Structural invariant check (used by tests and after loading): every split has two children,
  // every leaf has >= 1 tab (except an empty root), tab names unique, ratios in (0,1).
  bool valid() const;

 private:
  friend struct DockUi;
  std::unique_ptr<Node> root_;
};

struct DockSlot {
  std::string name;
  Rect content;  // area for the panel body (below its tab bar)
  bool active = true;
};

// Draws dock chrome (tab bars, splitters, drop zones) and lets the user rearrange `layout`.
// Returns the content rect of the active panel of every leaf. Call before submitting the panels.
std::vector<DockSlot> dockSpace(Context& ctx, DockLayout& layout, const Rect& area);

}  // namespace mingol::gui
