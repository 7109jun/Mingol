// SPDX-License-Identifier: GPL-2.0-only
// Bridge between the scene and Mingol Script: owns the compiled program and VM, exposes the engine API to
// scripts (Log, Vec3, GameObject) and attaches script classes to GameObjects as ScriptComponents.
//
// Script side (see engineApiSource()): a script component is an instance of a class deriving from `Script`.
// Its overridden onAwake/onEnable/onStart/onFixedUpdate/onUpdate/onLateUpdate/onDisable/onDestroy methods are
// called by the scene lifecycle. An uncaught script exception disables that component (the scene logs it).
//
// Threading: single-threaded, like the Scene. The host must outlive every scene that holds its components
// (components call into the VM while they are destroyed).
//
// Hot reload: reload() recompiles, creates a fresh VM and migrates every live ScriptComponent by *field name
// and type*; see ReloadReport. Statics are reset, object-typed fields are not migrated.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/scene/scene.hpp"
#include "mingol/script/compile.hpp"
#include "mingol/script/vm.hpp"

namespace mingol::scripting {

class ScriptComponent;

struct HostDesc {
  script::VMOptions vm;
  std::vector<std::string> plugins;  // shared libraries loaded after the engine natives are bound (and again on reload)
};

struct ReloadReport {
  std::size_t componentsMigrated = 0;
  std::size_t componentsDisabled = 0;  // class removed/changed incompatibly, or its constructor threw
  std::size_t fieldsRestored = 0;
  std::size_t fieldsSkipped = 0;       // renamed, retyped or object-typed fields keep their new initial value
  std::vector<std::string> messages;
};

// The script source of the engine API (module-less classes Log, Vec3, GameObject, Script).
const char* engineApiSource() noexcept;

class ScriptHost {
 public:
  // Compiles `sources` together with the engine API. On failure the error message holds the diagnostics.
  static Result<std::unique_ptr<ScriptHost>> create(const std::vector<script::SourceInput>& sources, HostDesc desc = {});
  ~ScriptHost();
  ScriptHost(const ScriptHost&) = delete;
  ScriptHost& operator=(const ScriptHost&) = delete;

  script::VM& vm() { return *vm_; }
  const script::Program& program() const { return *program_; }

  // True if `className` is a concrete class deriving from Script with a parameterless constructor.
  bool isScriptClass(const std::string& className) const;
  std::vector<std::string> scriptClasses() const;

  // Attaches a ScriptComponent running `className` to `object`.
  Result<ScriptComponent*> addScript(scene::GameObject& object, const std::string& className);

  // Recompiles and migrates. On a compile error nothing changes and the diagnostics are returned.
  Result<ReloadReport> reload(const std::vector<script::SourceInput>& sources);

  // Engine-API natives find the active host/scene through these (set around every call into the VM).
  static ScriptHost* current() noexcept;
  scene::Scene* currentScene() const noexcept { return scene_; }

  // Sets host/scene as current for the lifetime of the object (nestable).
  class Scope {
   public:
    Scope(ScriptHost& host, scene::Scene* scene) noexcept;
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

   private:
    ScriptHost* prevHost_;
    scene::Scene* prevScene_;
    ScriptHost& host_;
  };

 private:
  friend class ScriptComponent;
  ScriptHost() = default;

  struct Built {
    std::unique_ptr<script::Program> program;
    std::unique_ptr<script::VM> vm;
  };
  Result<Built> build(const std::vector<script::SourceInput>& sources) const;

  void registerComponent(ScriptComponent* c);
  void unregisterComponent(ScriptComponent* c);

  HostDesc desc_;
  std::unique_ptr<script::Program> program_;
  std::unique_ptr<script::VM> vm_;
  std::vector<ScriptComponent*> components_;
  scene::Scene* scene_ = nullptr;
};

}  // namespace mingol::scripting
