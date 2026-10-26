// SPDX-License-Identifier: GPL-2.0-only
// Loading native plugins (.dll / .so). No sandbox: a plugin has the full privileges of the process.
#include "vm_impl.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mingol::script {

const MsHostApi* hostApi() noexcept;

namespace {

struct RegCtx {
  VM* vm;
  std::vector<std::string> failed;
};

int32_t registerNative(void* ctx, const char* name, MsNativeFn fn) {
  auto* c = static_cast<RegCtx*>(ctx);
  auto r = c->vm->bindNative(name != nullptr ? name : "", fn);
  if (!r.ok()) {
    c->failed.push_back(name != nullptr ? name : "(null)");
    return 1;
  }
  return 0;
}

}  // namespace

Result<void> VM::loadPlugin(const std::string& path) {
#if defined(_WIN32)
  void* h = reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
  if (h == nullptr) return makeError(ErrorCode::NotFound, "cannot load plugin '{}' (error {})", path, GetLastError());
  auto sym = [&](const char* n) { return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(h), n)); };
  auto closeLib = [&] { FreeLibrary(reinterpret_cast<HMODULE>(h)); };
#else
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (h == nullptr) return makeError(ErrorCode::NotFound, "cannot load plugin '{}': {}", path, dlerror());
  auto sym = [&](const char* n) { return dlsym(h, n); };
  auto closeLib = [&] { dlclose(h); };
#endif
  auto abiFn = reinterpret_cast<MsPluginAbiFn>(sym(MS_PLUGIN_ABI_SYMBOL));
  auto initFn = reinterpret_cast<MsPluginInitFn>(sym(MS_PLUGIN_INIT_SYMBOL));
  if (abiFn == nullptr || initFn == nullptr) {
    closeLib();
    return makeError(ErrorCode::Corrupt, "'{}' is not a Mingol plugin (missing {} / {})", path, MS_PLUGIN_ABI_SYMBOL, MS_PLUGIN_INIT_SYMBOL);
  }
  const std::uint32_t abi = abiFn();
  if (abi != MS_ABI_VERSION) {
    closeLib();
    return makeError(ErrorCode::VersionMismatch, "plugin '{}' uses ABI version {}, this engine supports {}", path, abi, MS_ABI_VERSION);
  }
  RegCtx ctx{this, {}};
  MsRegistrar reg{&ctx, &registerNative};
  const std::int32_t rc = initFn(hostApi(), &reg);
  if (rc != 0 || !ctx.failed.empty()) {
    closeLib();
    std::string names;
    for (const auto& n : ctx.failed) names += (names.empty() ? "" : ", ") + n;
    if (!ctx.failed.empty()) return makeError(ErrorCode::NotFound, "plugin '{}' registered unknown native methods: {}", path, names);
    return makeError(ErrorCode::Internal, "plugin '{}' failed to initialise (code {})", path, rc);
  }
  impl_->pluginHandles.push_back(h);
  return {};
}

void unloadPlugins(std::vector<void*>& handles) {
  for (void* h : handles) {
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(h));
#else
    dlclose(h);
#endif
  }
  handles.clear();
}

}  // namespace mingol::script
