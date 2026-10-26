/* SPDX-License-Identifier: GPL-2.0-only */
/* Test plugin for the Mingol Script native ABI (plain C, to prove the ABI is C-compatible). */
#include <mingol/script/plugin_abi.h>
#include <string.h>

static const MsHostApi* g_api;

static void addInts(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {
  (void)vm; (void)n;
  r->tag = MS_TAG_INT; r->reserved = 0; r->u.i = a[0].u.i + a[1].u.i;
}
static void halve(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {
  (void)vm; (void)n;
  r->tag = MS_TAG_FLOAT; r->reserved = 0; r->u.f = a[0].u.f / 2.0;
}
static void shout(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {
  char buf[128]; size_t len = 0; (void)n;
  const char* s = g_api->stringData(vm, a[0], &len);
  if (len > 100) { g_api->throwError(vm, "mingol.ArgumentError", "too long"); return; }
  memcpy(buf, s, len); buf[len] = '!'; 
  *r = g_api->newString(vm, buf, len + 1);
}
static void fail(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {
  (void)a; (void)n; (void)r;
  g_api->throwError(vm, "mingol.ArgumentError", "plugin says no");
}
static void iota(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {
  int64_t k = a[0].u.i, i; (void)n;
  MsValue arr = g_api->newArray(vm, MS_ELEM_INT, k);
  for (i = 0; i < k; ++i) { MsValue v; v.tag = MS_TAG_INT; v.reserved = 0; v.u.i = i * i; g_api->arraySet(vm, arr, i, v); }
  *r = arr;
}
static void sumArray(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {
  int64_t len = g_api->arrayLength(vm, a[0]), i, s = 0; (void)n;
  for (i = 0; i < len; ++i) s += g_api->arrayGet(vm, a[0], i).u.i;
  r->tag = MS_TAG_INT; r->reserved = 0; r->u.i = s;
}
static int g_finalized = 0;
static void fin(void* p) { (void)p; ++g_finalized; }
static int g_counter = 0;
static void attach(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {   /* Handle.attach(): store native data */
  (void)n; (void)r; g_counter += 10;
  g_api->setNativeData(vm, a[0], &g_counter, fin);
}
static void readAttached(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {
  int* p = (int*)g_api->getNativeData(vm, a[0]); (void)n;
  r->tag = MS_TAG_INT; r->reserved = 0; r->u.i = p ? *p : -1;
}
static void finalizedCount(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {
  (void)vm; (void)a; (void)n; r->tag = MS_TAG_INT; r->reserved = 0; r->u.i = g_finalized;
}
static void applyTwice(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {   /* calls back into script */
  MsValue arg = a[1], tmp; (void)n;
  if (g_api->callClosure(vm, a[0], &arg, 1, &tmp)) return;
  if (g_api->callClosure(vm, a[0], &tmp, 1, r)) return;
}
static void makePoint(MsVM* vm, MsValue* a, int32_t n, MsValue* r) {
  MsValue o = g_api->newObject(vm, "Point"); (void)n;
  if (o.tag != MS_TAG_OBJECT) return;
  g_api->setField(vm, o, "x", a[0]);
  g_api->setField(vm, o, "y", a[1]);
  *r = o;
}

MS_PLUGIN_EXPORT uint32_t mingol_plugin_abi(void) { return MS_ABI_VERSION; }

#define REG(name, fn) do { if (reg->registerNative(reg->ctx, name, fn) != 0) return 1; } while (0)
MS_PLUGIN_EXPORT int32_t mingol_plugin_init(const MsHostApi* api, const MsRegistrar* reg) {
  g_api = api;
  REG("Native.addInts(int, int)", addInts);
  REG("Native.halve(float)", halve);
  REG("Native.shout(string)", shout);
  REG("Native.fail()", fail);
  REG("Native.iota(int)", iota);
  REG("Native.sumArray(int[])", sumArray);
  REG("Native.finalizedCount()", finalizedCount);
  REG("Native.applyTwice(fn(int): int, int)", applyTwice);
  REG("Native.makePoint(int, int)", makePoint);
  REG("Handle.attach()", attach);
  REG("Handle.read()", readAttached);
  return 0;
}
