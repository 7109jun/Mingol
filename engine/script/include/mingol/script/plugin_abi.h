/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Mingol Script native-plugin ABI (version 1).
 *
 * A plugin is a shared library (.dll / .so) that implements `native` methods declared in Mingol Script
 * `native class`es. It exports exactly two C symbols:
 *
 *     uint32_t mingol_plugin_abi(void);                                       // must return MS_ABI_VERSION
 *     int32_t  mingol_plugin_init(const MsHostApi* api, const MsRegistrar* reg);  // 0 = success
 *
 * and registers its functions from mingol_plugin_init by *native name*: "<qualified class>.<method>(<param types>)",
 * e.g. "game.Audio.play(string, float)".
 *
 * Rules for native functions:
 *  - `args` points at the call's arguments (`this` first for instance methods); write the result to `*ret`
 *    (leave it untouched for void). `ref` parameters arrive as pointer values and are not readable by plugins.
 *  - The garbage collector never runs while a native function executes, so values obtained from `api` are
 *    valid until the function returns. To keep a script object across calls, pin it (api->pin).
 *  - Report errors with api->throwError and return; the VM raises the exception when the function returns.
 *    Never let a C++ exception or longjmp cross this boundary.
 *  - No sandbox: plugins run with the full privileges of the process.
 *
 * The struct layouts below are part of the ABI; changing them requires a new MS_ABI_VERSION.
 */
#ifndef MINGOL_SCRIPT_PLUGIN_ABI_H
#define MINGOL_SCRIPT_PLUGIN_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MS_ABI_VERSION 1u

/* Put in front of the two exported functions so they are visible from the shared library
 * (required on Windows, where nothing is exported by default). */
#if defined(_WIN32)
#define MS_PLUGIN_EXPORT __declspec(dllexport)
#else
#define MS_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

enum MsTag { MS_TAG_NULL = 0, MS_TAG_INT = 1, MS_TAG_FLOAT = 2, MS_TAG_BOOL = 3, MS_TAG_OBJECT = 4 };

typedef struct MsValue {
  int32_t tag;
  int32_t reserved;
  union {
    int64_t i;  /* MS_TAG_INT, MS_TAG_BOOL (0/1) */
    double f;   /* MS_TAG_FLOAT */
    void* p;    /* MS_TAG_OBJECT: opaque, only valid with the api functions */
  } u;
} MsValue;

typedef struct MsVM MsVM; /* opaque to plugins; the C++ VM class derives from it */
typedef void (*MsNativeFn)(MsVM* vm, MsValue* args, int32_t argc, MsValue* ret);

/* Array element kinds for newArray. */
enum MsElemKind { MS_ELEM_INT = 0, MS_ELEM_FLOAT = 1, MS_ELEM_BOOL = 2, MS_ELEM_STRING = 3, MS_ELEM_OBJECT = 4 };

enum MsLogLevel { MS_LOG_DEBUG = 0, MS_LOG_INFO = 1, MS_LOG_WARN = 2, MS_LOG_ERROR = 3 };

typedef struct MsHostApi {
  uint32_t abiVersion;  /* MS_ABI_VERSION */
  uint32_t structSize;  /* sizeof(MsHostApi) of the host; only use members inside this size */

  MsValue (*newString)(MsVM*, const char* utf8, size_t len);
  const char* (*stringData)(MsVM*, MsValue str, size_t* len); /* not NUL-terminated necessarily; valid until return */
  void (*throwError)(MsVM*, const char* qualifiedClass, const char* message); /* e.g. "mingol.ArgumentError" */

  void (*setNativeData)(MsVM*, MsValue obj, void* data, void (*finalizer)(void*));
  void* (*getNativeData)(MsVM*, MsValue obj);

  MsValue (*newObject)(MsVM*, const char* qualifiedClass); /* no constructor is run; null + pending error on failure */
  MsValue (*getField)(MsVM*, MsValue obj, const char* name);
  void (*setField)(MsVM*, MsValue obj, const char* name, MsValue v);

  MsValue (*newArray)(MsVM*, int32_t elemKind, int64_t length);
  int64_t (*arrayLength)(MsVM*, MsValue arr);
  MsValue (*arrayGet)(MsVM*, MsValue arr, int64_t index);
  void (*arraySet)(MsVM*, MsValue arr, int64_t index, MsValue v);

  uint64_t (*pin)(MsVM*, MsValue v); /* keeps v alive until unpin; returns a non-zero id */
  void (*unpin)(MsVM*, uint64_t id);
  MsValue (*pinned)(MsVM*, uint64_t id);

  /* Calls a script function value (lambda / method value). Returns 0 on success; non-zero means a script
     exception is pending and the native must return immediately. */
  int32_t (*callClosure)(MsVM*, MsValue closure, const MsValue* args, int32_t argc, MsValue* result);

  void (*log)(MsVM*, int32_t level, const char* message);
} MsHostApi;

typedef struct MsRegistrar {
  void* ctx;
  /* Returns 0 on success; non-zero if `name` is not a native method of the loaded program. */
  int32_t (*registerNative)(void* ctx, const char* name, MsNativeFn fn);
} MsRegistrar;

#define MS_PLUGIN_ABI_SYMBOL "mingol_plugin_abi"
#define MS_PLUGIN_INIT_SYMBOL "mingol_plugin_init"
typedef uint32_t (*MsPluginAbiFn)(void);
typedef int32_t (*MsPluginInitFn)(const MsHostApi* api, const MsRegistrar* reg);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MINGOL_SCRIPT_PLUGIN_ABI_H */
