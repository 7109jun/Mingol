// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <memory>
#include <string>

#include "mingol/core/json.hpp"
#include "mingol/scene/scene.hpp"
#include "mingol/scripting/script_host.hpp"

namespace mingol::scripting {

// Runs one script object (an instance of a class deriving from `Script`) for its GameObject.
class ScriptComponent final : public scene::Component {
 public:
  // Prefer ScriptHost::addScript, which validates the class name first.
  ScriptComponent(ScriptHost& host, std::string className);
  ~ScriptComponent() override;

  const char* typeName() const override { return "Script"; }
  const std::string& className() const noexcept { return className_; }
  // False when the instance could not be created or was lost in a reload.
  bool hasInstance() const noexcept { return instance_ != nullptr; }
  script::Value instance() const;

  // Field access by name on the script object (primitives and strings).
  Result<void> setInt(const std::string& field, std::int64_t v);
  Result<void> setFloat(const std::string& field, double v);
  Result<void> setBool(const std::string& field, bool v);
  Result<void> setString(const std::string& field, const std::string& v);
  Result<std::int64_t> getInt(const std::string& field) const;
  Result<double> getFloat(const std::string& field) const;
  Result<bool> getBool(const std::string& field) const;
  Result<std::string> getString(const std::string& field) const;

  // Serialises the script object's data fields (int/float/bool/string/enum and arrays of them) to a JSON object
  // keyed by field name. Object-typed fields and the `gameObject` handle are not saved. Fails on NaN/Infinity.
  Result<json::Value> serializeFields() const;
  // Applies a JSON object produced by serializeFields(). Unknown fields, static fields and type mismatches
  // are skipped and described in `warnings`; the object itself is never left half-written for one field.
  Result<void> deserializeFields(const json::Value& fields, std::vector<std::string>& warnings);

  // Invokes a parameterless script method by name (for tests and tools).
  Result<void> call(const std::string& method);

 protected:
  void onAwake() override;
  void onEnable() override;
  void onStart() override;
  void onFixedUpdate(float dt) override;
  void onUpdate(float dt) override;
  void onLateUpdate(float dt) override;
  void onDisable() override;
  void onDestroy() override;

 private:
  friend class ScriptHost;
  enum Cb { Awake, Enable, Start, Fixed, Update, Late, Disable, Destroy, CbCount };
  // (Re)creates the script object in the host's current VM; used at construction and by hot reload.
  Result<void> createInstance();
  void bindCallbacks();
  void bindGameObjectHandle();
  void invoke(Cb cb, const float* dt);
  void releaseInstance();
  const script::FieldSym* findField(const std::string& name) const;

  ScriptHost* host_;
  std::string className_;
  std::unique_ptr<script::Rooted> instance_;
  bool handleSet_ = false;
  const script::ClassSym* cls_ = nullptr;
  std::array<const script::MethodSym*, CbCount> callbacks_{};
};

}  // namespace mingol::scripting
