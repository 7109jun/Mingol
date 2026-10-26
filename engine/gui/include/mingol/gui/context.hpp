// SPDX-License-Identifier: GPL-2.0-only
// Immediate-mode GUI context: input -> widgets -> DrawList. One Context per window.
//
// Frame protocol:  feed(events...)  beginFrame()  <widget calls>  endFrame()  drawList()
// Widgets are identified by label ("text##hidden-id") plus the ID stack. State that must outlive a
// frame (scroll, open trees, text-edit state, popup sizes) is kept inside the Context by ID.
// Layout is one-frame-lagged where it depends on content size (scrollbars, popup size).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "mingol/gui/draw.hpp"
#include "mingol/gui/text_edit.hpp"
#include "mingol/platform/event.hpp"
#include "mingol/platform/window.hpp"

namespace mingol::gui {

using ID = std::uint32_t;

struct Style {
  Color bg{30, 31, 34}, panelBg{37, 38, 42}, panelHeader{45, 47, 52}, text{222, 224, 228},
      textDim{140, 144, 152}, accent{70, 130, 230}, accentDim{48, 84, 150}, button{58, 61, 68},
      buttonHover{74, 78, 88}, buttonActive{50, 85, 150}, frame{26, 27, 30}, frameHover{34, 36, 41},
      border{14, 14, 16}, selection{60, 100, 170, 170}, popupBg{44, 46, 52}, modalDim{0, 0, 0, 150},
      scrollTrack{28, 29, 33}, scrollThumb{84, 88, 98}, scrollThumbHover{110, 114, 126},
      error{235, 90, 90}, warning{230, 190, 80}, success{90, 200, 120};
  int fontScale = 2;
  float spacing = 4, framePad = 3, indent = 14, scrollbar = 10, rounding = 3, regionPad = 4;
  float labelFraction = 0.4F;  // width of the label column of labelled frame widgets
};

struct ItemState {
  bool hovered = false;
  bool pressed = false;       // went down on the item this frame
  bool held = false;          // item is active and the button is down
  bool clicked = false;       // pressed and released on the item
  bool doubleClicked = false;
  bool rightClicked = false;  // right button pressed over the item
};

enum class TreeFlags : std::uint8_t { None = 0, Leaf = 1, DefaultOpen = 2, Selected = 4 };
inline constexpr TreeFlags operator|(TreeFlags a, TreeFlags b) {
  return static_cast<TreeFlags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
inline constexpr bool hasFlag(TreeFlags v, TreeFlags f) {
  return (static_cast<std::uint8_t>(v) & static_cast<std::uint8_t>(f)) != 0;
}

class Context {
 public:
  Context();
  ~Context();
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  Style style;
  Clipboard clipboard;  // optional; without it copy/paste inside the GUI uses an internal buffer
  std::function<void(platform::CursorShape)> setCursorShape;  // optional

  // ---- frame ----------------------------------------------------------------------------
  void feed(const platform::Event& e);
  void beginFrame(Vec2 displaySize, double timeSeconds);
  void endFrame();
  const DrawList& drawList() const { return final_; }
  // Problems found while building the frame (unbalanced begin/end, ...). Empty = clean.
  const std::vector<std::string>& errors() const { return errors_; }
  Vec2 displaySize() const { return display_; }
  double time() const { return time_; }
  float deltaTime() const { return dt_; }

  // ---- input queries (valid between beginFrame and endFrame) ---------------------------
  Vec2 mousePos() const { return mouse_; }
  Vec2 mouseDelta() const { return mouseDelta_; }
  bool mouseDown(int button = 0) const { return down_[button]; }
  bool mouseClicked(int button = 0) const { return pressed_[button]; }
  bool mouseReleased(int button = 0) const { return released_[button]; }
  float scrollY() const { return wheelY_; }
  bool keyPressed(platform::Key k, bool allowRepeat = false) const;
  platform::Modifiers modifiers() const { return mods_; }
  bool ctrl() const;
  bool shift() const;
  // True while a text field has keyboard focus: the application must not treat keys as shortcuts.
  bool wantTextInput() const { return focus_ != 0; }
  bool anyItemActive() const { return active_ != 0; }
  bool anyPopupOpen() const { return !popups_.empty() || modal_ != 0; }
  void clearFocus() { focus_ = 0; }

  // ---- ids / layout ---------------------------------------------------------------------
  void pushID(std::string_view s);
  void pushID(std::int64_t n);
  void popID();
  ID getID(std::string_view label) const;

  void sameLine(float spacing = -1.0F);
  void setNextWidth(float w) { nextWidth_ = w; }
  void spacing();
  void separator();
  void indent(float amount = -1.0F);
  void unindent(float amount = -1.0F);
  float availableWidth() const;
  float availableHeight() const;
  float itemHeight() const;  // standard widget height
  float lineHeight() const;
  float textWidth(std::string_view utf8) const;
  Rect lastItemRect() const { return lastRect_; }
  // True during the frame in which the last edit widget finished an edit (drag released after a
  // change, text committed with Enter or by losing focus). Use it to group undo steps.
  bool itemEditFinished() const { return editFinished_; }
  ItemState lastItem() const { return lastState_; }

  // ---- regions ---------------------------------------------------------------------------
  // A top-level panel at an absolute rect (docked panels, toolbars). Always call endPanel().
  bool beginPanel(std::string_view name, const Rect& bounds, bool scroll = true, bool background = true);
  void endPanel();
  // Nested scrolling area inside the current region. size <= 0 means "fill the remainder".
  bool beginChild(std::string_view id, Vec2 size, bool border = false);
  void endChild();
  // Scroll control for the current region (e.g. console "stick to bottom").
  float scrollOffset() const;
  float scrollMax() const;
  void setScrollOffset(float y);
  bool scrolledToBottom() const;

  // ---- basic widgets ---------------------------------------------------------------------
  void label(std::string_view text);
  void labelColored(std::string_view text, Color c);
  void labelDim(std::string_view text);
  bool button(std::string_view label, Vec2 size = {0, 0});
  bool smallButton(std::string_view label);
  bool checkbox(std::string_view label, bool& v);
  bool radio(std::string_view label, int& v, int value);
  bool selectable(std::string_view label, bool selected = false, Vec2 size = {0, 0});
  void progressBar(float fraction, std::string_view overlay = {}, Vec2 size = {0, 0});
  void image(std::uint32_t texture, Vec2 size, Vec2 uv0 = {0, 0}, Vec2 uv1 = {1, 1});
  // Interactive area (3D viewport): returns the item state; the rect is lastItemRect().
  ItemState interactiveArea(std::string_view id, Vec2 size);

  bool dragFloat(std::string_view label, float& v, float speed = 0.1F, float min = 0.0F, float max = 0.0F);
  bool dragInt(std::string_view label, int& v, float speed = 0.2F, int min = 0, int max = 0);
  bool dragFloat3(std::string_view label, float v[3], float speed = 0.1F);
  bool inputText(std::string_view label, std::string& s, bool* entered = nullptr);
  bool combo(std::string_view label, int& index, std::span<const std::string> items);
  bool colorEdit(std::string_view label, float rgba[4]);

  bool treeNode(std::string_view label, TreeFlags flags = TreeFlags::None);
  void treePop();
  bool collapsingHeader(std::string_view label, bool defaultOpen = true);

  // ---- tabs --------------------------------------------------------------------------------
  bool beginTabBar(std::string_view id);
  bool tabItem(std::string_view label);  // true when this tab is the selected one
  void endTabBar();

  // ---- popups / menus / dialogs ---------------------------------------------------------
  void openPopup(std::string_view id, Vec2 pos);  // absolute position (clamped into the display)
  void openPopupAtLastItem(std::string_view id);  // below the last item
  void closeCurrentPopup();
  bool isPopupOpen(std::string_view id) const;
  bool beginPopup(std::string_view id);
  void endPopup();
  // Opens `id` on right click over the last item and begins it.
  bool beginContextMenu(std::string_view id);

  bool beginMainMenuBar(const Rect& bounds);
  void endMainMenuBar();
  bool beginMenu(std::string_view label, bool enabled = true);
  void endMenu();
  bool menuItem(std::string_view label, std::string_view shortcut = {}, bool selected = false, bool enabled = true);
  void menuSeparator();

  void openModal(std::string_view id);
  bool beginModal(std::string_view id, std::string_view title, Vec2 size);
  void endModal();
  void closeModal();
  bool isModalOpen() const { return modal_ != 0; }

  void tooltip(std::string_view text);  // for the last item, after ~0.4 s of hover

  // ---- drag and drop (string payloads) ---------------------------------------------------
  bool dragSource(std::string_view type, std::string_view payload, std::string_view preview);
  bool dropTarget(std::string_view type, std::string* payload);  // true on the frame of the drop
  bool isDragging() const { return !dragType_.empty(); }
  std::string_view dragType() const { return dragType_; }
  std::string_view dragPayload() const { return dragPayload_; }
  DrawList& overlay() { return overlay_; }  // drawn after (above) everything else

  // Draw directly into the current region (custom widgets, e.g. the code editor).
  DrawList& draw();
  Rect clipRect();
  // Reserves a rectangle (default width = remaining width) and registers it as the last item.
  Rect allocate(Vec2 size);
  ItemState itemBehavior(const Rect& r, ID id);
  int fontScale() const { return style.fontScale; }

  // Keyboard/mouse handling for custom multi-line text widgets (the script editor). Registers `frame` as an
  // item, focuses it on press, selects with the mouse (`byteAtMouse` maps a position to a byte offset; double
  // click selects a word) and, while focused, applies typed characters and editing keys to `te`. `onKey` (optional)
  // sees every non-character key first and returns true to consume it (auto-indent, Tab handling, ...).
  struct TextAreaResult {
    ItemState item;
    bool focused = false;
    bool changed = false;
  };
  TextAreaResult textArea(const Rect& frame, std::string_view id, TextEdit& te, const std::function<std::size_t(Vec2)>& byteAtMouse,
                          const std::function<bool(platform::Key, platform::Modifiers)>& onKey = {}, int pageLines = 10);
  bool hasFocus(std::string_view id) const { return focus_ == getID(id); }
  void setFocus(std::string_view id) { focus_ = getID(id); }

  // Layout cursor helpers for custom widgets.
  void setHoverCursor(platform::CursorShape shape);

 private:
  struct KeyEv {
    platform::Key key = platform::Key::Unknown;
    platform::Modifiers mods = platform::Modifiers::None;
    bool repeat = false;
    bool isChar = false;
    char32_t ch = 0;
  };
  struct Region {
    ID id = 0;
    Rect bounds, content;
    DrawList* dl = nullptr;
    int layer = 0;
    float indent = 0, rowTop = 0, rowBottom = 0, prevRight = 0, startY = 0, maxY = 0, usedRight = 0;
    float scrollY = 0, viewH = 0, sameLineSpacing = -1.0F, pad = 0;
    bool firstItem = true, sameLine = false, scroll = false, menuBar = false, popup = false;
    ID popupId = 0;
    float minWidth = 0;
  };
  struct PopupInfo {
    ID id = 0, parent = 0;
    Vec2 pos;
    Rect rect;
    bool menu = false;
    float minWidth = 0;
  };
  struct FieldResult {
    bool changed = false, entered = false, cancelled = false;
  };

  Region& region() { return regions_.back(); }
  const Region& region() const { return regions_.back(); }
  void error(std::string msg);
  bool pushRegion(ID id, const Rect& bounds, DrawList* dl, int layer, bool scroll, bool bg, float pad);
  void popRegion();
  bool hoverOk(const Rect& r, int layer, bool ignoreBlock) const;
  bool hoverIn(const Rect& r, const Rect& clip, int layer, bool ignoreBlock) const;
  ItemState behavior(const Rect& r, ID id, bool ignoreBlock);
  void drawText(Vec2 pos, std::string_view s, Color c);
  void drawTextCentered(const Rect& r, std::string_view s, Color c);
  Vec2 textOrigin(const Rect& r) const;
  Color stateColor(const ItemState& st, ID id, Color normal, Color hover, Color active) const;
  FieldResult textField(const Rect& frame, ID id, TextEdit& te, float& scrollX);
  TextEdit& editFor(ID id, bool multiline = false);
  bool dragScalar(const Rect& frame, ID id, float& v, float speed, float lo, float hi, bool isInt);
  Rect labelledFrame(std::string_view shown, float& labelEnd);
  void openPopupInternal(ID id, Vec2 pos, bool menu, float minWidth, ID parent);
  void closePopupsAbove(ID parent);
  void closeAllPopups();
  bool beginPopupInternal(ID id, bool menu);
  PopupInfo* findPopup(ID id);
  const PopupInfo* findPopup(ID id) const;
  bool consumeKey(platform::Key k);
  Clipboard activeClipboard();

  // ---- state ----
  std::vector<std::string> errors_;
  DrawList main_, overlay_, final_;
  Vec2 display_{};
  double time_ = 0;
  bool haveTime_ = false;
  float dt_ = 0;
  Vec2 mouse_{}, prevMouse_{}, mouseDelta_{};
  bool mouseValid_ = false;
  bool down_[3] = {false, false, false};
  bool pressed_[3] = {false, false, false};
  bool released_[3] = {false, false, false};
  bool doubleClick_ = false;
  float wheelY_ = 0;
  platform::Modifiers mods_ = platform::Modifiers::None;
  std::vector<KeyEv> keys_;

  // accumulated between frames
  Vec2 accMouse_{};
  bool accDown_[3] = {false, false, false};
  bool accPressed_[3] = {false, false, false};
  bool accReleased_[3] = {false, false, false};
  float accWheel_ = 0;
  std::vector<KeyEv> accKeys_;

  std::vector<ID> idStack_;
  std::vector<Region> regions_;
  std::unordered_map<ID, float> scroll_, contentH_, editScroll_, labelDummy_;
  std::unordered_map<ID, bool> open_;
  std::unordered_map<ID, std::unique_ptr<TextEdit>> edits_;
  std::unordered_map<ID, Vec2> popupSize_;
  std::unordered_map<ID, ID> tabSel_;
  std::vector<PopupInfo> popups_;
  std::vector<ID> popupStack_;  // popups currently being built
  ID modal_ = 0;
  int blockLayer_ = 0;
  bool consumeClick_ = false;
  bool wheelConsumed_ = false;
  bool pressedAnyItem_ = false;
  bool modalDimDrawn_ = false;

  ID active_ = 0, focus_ = 0, lastId_ = 0, hoverId_ = 0, frameHover_ = 0, scalarEdit_ = 0;
  float grab_ = 0, dragMoved_ = 0;
  Vec2 pressPos_{};
  double hoverSince_ = 0, lastClickTime_ = -10;
  Vec2 lastClickPos_{};
  float nextWidth_ = 0;
  Rect lastRect_{};
  ItemState lastState_{};
  std::string internalClip_;

  // tab bar under construction
  ID tabBarId_ = 0;
  bool tabAny_ = false;

  // drag & drop
  std::string dragType_, dragPayload_, dragPreview_;
  ID dragSourceId_ = 0;
  ID editDirtyId_ = 0;
  bool editFinished_ = false;
  float scalarAccum_ = 0;
};

// Utility: formats a float with up to 3 decimals, trimming trailing zeros ("1", "0.25").
std::string formatFloat(float v);

}  // namespace mingol::gui
