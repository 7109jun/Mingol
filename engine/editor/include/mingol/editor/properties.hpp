// SPDX-License-Identifier: GPL-2.0-only
// Property reflection for the inspector: turns a component (or a transform) into a flat list of
// typed, named properties with getters/setters. The UI and the tests use the same table.
//
// A Property holds raw pointers into the object it was built from; use it only within the frame
// in which describe*() was called (before any edit that may destroy the object).
#pragma once

#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/core/math.hpp"
#include "mingol/scene/scene.hpp"
#include "mingol/sceneio/scene_io.hpp"

namespace mingol::editor {

enum class PropKind : std::uint8_t {
  Bool,
  Int,
  Float,
  Vec3,
  Color3,  // Vec3 in 0..1, edited as color
  Color4,  // Vec4 rgba in 0..1
  Enum,    // Int index into enumNames
  String,
  Asset,   // String name from assetChoices ("" = none)
  Label,   // read-only text
};

using PropValue = std::variant<bool, std::int64_t, double, Vec3, Vec4, std::string>;

struct Property {
  std::string name;
  PropKind kind = PropKind::Label;
  std::function<PropValue()> get;
  std::function<Result<void>(const PropValue&)> set;  // empty for read-only properties
  double min = 0, max = 0, speed = 0.1;                // min == max: unbounded
  std::vector<std::string> enumNames;
  std::vector<std::string> assetChoices;
  std::string tooltip;
  bool readOnly() const { return !set; }
};

struct PropertyEnv {
  const sceneio::AssetTable* assets = nullptr;  // for asset pickers; null = pickers offer only "none"
};

// Transform: Position, Rotation (euler degrees, see euler.hpp), Scale — local space.
std::vector<Property> describeTransform(scene::GameObject& go);

// Built-in components by typeName(); Script components list their serialisable fields.
// Unknown component types yield a single read-only Label with the type name.
std::vector<Property> describeComponent(scene::Component& c, const PropertyEnv& env);

// Type names offered by "Add Component" for the built-ins (Script is added separately by class).
const std::vector<std::string>& addableComponentTypes();

// Value helpers
std::string toDisplayString(const PropValue& v);

}  // namespace mingol::editor
