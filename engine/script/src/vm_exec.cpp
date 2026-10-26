// SPDX-License-Identifier: GPL-2.0-only
// The bytecode interpreter, exception unwinding and call machinery.
#include <charconv>
#include <cmath>

#include "vm_impl.hpp"

namespace mingol::script {

namespace {
constexpr std::int64_t kMaxArrayLength = 1 << 27;
constexpr std::size_t kMaxStringBytes = 1u << 30;

std::string formatFloat(double d) {
  if (std::isnan(d)) return "nan";
  if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
  char buf[64];
  auto r = std::to_chars(buf, buf + sizeof buf, d);
  std::string s(buf, r.ptr);
  // Same text on every platform: exponents without leading zeros ("1.5e-7", not "1.5e-07").
  const auto e = s.find('e');
  if (e != std::string::npos) {
    std::size_t k = e + 1;
    std::string sign;
    if (k < s.size() && (s[k] == '+' || s[k] == '-')) sign = s.substr(k++, 1);
    while (k + 1 < s.size() && s[k] == '0') ++k;
    s = s.substr(0, e + 1) + sign + s.substr(k);
  } else if (s.find('.') == std::string::npos) {
    s += ".0";
  }
  return s;
}

std::int64_t f2i(double d) {
  if (std::isnan(d)) return 0;
  if (d >= 9223372036854775808.0) return INT64_MAX;
  if (d <= -9223372036854775808.0) return INT64_MIN;
  return static_cast<std::int64_t>(d);
}

inline std::int64_t wrapAdd(std::int64_t a, std::int64_t b) { return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b)); }
inline std::int64_t wrapSub(std::int64_t a, std::int64_t b) { return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b)); }
inline std::int64_t wrapMul(std::int64_t a, std::int64_t b) { return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b)); }

int yieldKindOf(const Value& v) {
  switch (v.tag) {
    case MS_TAG_INT: return 1;
    case MS_TAG_FLOAT: return 2;
    case MS_TAG_BOOL: return 3;
    case MS_TAG_OBJECT: return objOf(v)->kind == ObjKind::String ? 4 : 5;
    default: return 0;
  }
}
}  // namespace

// ---- helpers --------------------------------------------------------------------------------------------

std::string VMImpl::toStr(const Value& v, int mode, int classId) const {
  switch (mode) {
    case 0: return std::to_string(v.u.i);
    case 1: return formatFloat(v.u.f);
    case 2: return v.u.i != 0 ? "true" : "false";
    default: {
      const ClassSym* c = prog.classes[static_cast<std::size_t>(classId)].get();
      for (const auto& ec : c->enumConsts) {
        if (ec->value == v.u.i) return ec->name;
      }
      return std::to_string(v.u.i);
    }
  }
}

bool VMImpl::refEquals(const Value& a, const Value& b) const {
  if (a.tag != MS_TAG_OBJECT || b.tag != MS_TAG_OBJECT) return a.tag == b.tag;  // both null
  Obj* x = objOf(a);
  Obj* y = objOf(b);
  if (x == y) return true;
  if (x->kind == ObjKind::Ref && y->kind == ObjKind::Ref) {
    auto* r = static_cast<RefObj*>(x);
    auto* s = static_cast<RefObj*>(y);
    return r->rk == s->rk && r->target == s->target && r->index == s->index;
  }
  return false;
}

Value VMImpl::makeError(int classId, const std::string& message) {
  InstanceObj* o = newInstance(classes[static_cast<std::size_t>(classId)]);
  o->fields[0] = vObj(newStr(message));
  return vObj(o);
}

void VMImpl::raise(int classId, const std::string& message) {
  Value ex = makeError(classId, message);
  setPending(ex);
  fillTrace(ex);
}

void VMImpl::raiseNamed(const char* q, const std::string& message) {
  int id = classIdByName(q);
  if (id < 0) id = clsNative;
  raise(id, message);
}

std::string VMImpl::buildTrace(std::size_t maxLines) const {
  std::string out;
  std::size_t n = 0;
  for (std::size_t i = frames.size(); i-- > 0;) {
    const Frame& f = frames[i];
    if (n++ >= maxLines) {
      out += "  ...\n";
      break;
    }
    std::uint32_t line = 0;
    const std::uint32_t pc = f.pc > 0 ? f.pc - 1 : 0;
    for (const LineEntry& le : f.fn->lines) {
      if (le.pc <= pc) line = le.line;
      else break;
    }
    const std::string file = f.fn->file < prog.files.size() ? prog.files[f.fn->file] : "?";
    out += "  at " + f.fn->name + " (" + file + ":" + std::to_string(line) + ")\n";
  }
  return out;
}

void VMImpl::fillTrace(Value ex) {
  if (!isKind(ex, ObjKind::Instance)) return;
  InstanceObj* o = instOf(ex);
  if (o->fields.size() < 2) return;
  const Value& cur = o->fields[1];
  if (isKind(cur, ObjKind::String) && !strOf(cur)->s.empty()) return;
  o->fields[1] = vObj(newStr(buildTrace()));
}

ScriptException VMImpl::flatten(Value ex) const {
  ScriptException e;
  if (!isKind(ex, ObjKind::Instance)) {
    e.type = "<unknown>";
    return e;
  }
  InstanceObj* o = instOf(ex);
  e.type = o->cls->sym->qualifiedName;
  if (o->fields.size() >= 2) {
    if (isKind(o->fields[0], ObjKind::String)) e.message = strOf(o->fields[0])->s;
    if (isKind(o->fields[1], ObjKind::String)) e.stackTrace = strOf(o->fields[1])->s;
  }
  return e;
}

int VMImpl::resolveVirtual(const Value& receiver, int slot) const {
  return instOf(receiver)->cls->vtable[static_cast<std::size_t>(slot)];
}

bool VMImpl::pushFrame(const FuncProto& fn, std::uint32_t base, std::uint32_t argc, std::uint32_t retSp, ClosureObj* closure) {
  if (frames.size() >= opt.maxFrames) return false;
  if (static_cast<std::size_t>(base) + static_cast<std::size_t>(fn.numLocals) + static_cast<std::size_t>(fn.maxStack) > stackCap) return false;
  for (std::uint32_t i = argc; i < static_cast<std::uint32_t>(fn.numLocals); ++i) stack[base + i] = makeNull();
  Frame f;
  f.fn = &fn;
  f.base = base;
  f.retSp = retSp;
  f.closure = closure;
  frames.push_back(f);
  return true;
}

Status VMImpl::callNative(const FuncProto& fn, int funcId, std::uint32_t argBase, std::uint32_t argc, Value& result) {
  MsNativeFn f = natives[static_cast<std::size_t>(funcId)];
  if (f == nullptr) {
    raise(clsNative, "native method '" + fn.nativeName + "' is not bound to an implementation");
    return Status::Threw;
  }
  sp = argBase + argc;
  Value ret = makeNull();
  f(&vm, &stack[argBase], static_cast<std::int32_t>(argc), &ret);
  if (hasPending) {
    fillTrace(pending);
    return Status::Threw;
  }
  result = ret;
  return Status::Returned;
}

Status VMImpl::invoke(int funcId, std::uint32_t argc, ClosureObj* closure) {
  const FuncProto& fn = prog.funcs[static_cast<std::size_t>(funcId)];
  const std::uint32_t base = static_cast<std::uint32_t>(sp);
  if (nativeDepth >= opt.maxNativeDepth) {
    raise(clsStackOverflow, "native call depth limit reached");
    return Status::Threw;
  }
  if (fn.isNative) {
    Value result = makeNull();
    ++nativeDepth;
    Status st = callNative(fn, funcId, base, argc, result);
    --nativeDepth;
    sp = base;
    retVal = result;
    return st;
  }
  const std::size_t stop = frames.size();
  if (!pushFrame(fn, base, argc, base, closure)) {
    raise(clsStackOverflow, "stack overflow");
    return Status::Threw;
  }
  ++nativeDepth;
  Status st = execute(stop);
  --nativeDepth;
  sp = base;
  return st;
}

bool VMImpl::unwind(std::size_t stopDepth) {
  while (true) {
    Frame& fr = frames.back();
    const std::uint32_t pcThrow = fr.pc > 0 ? fr.pc - 1 : 0;
    InstanceObj* ex = isKind(pending, ObjKind::Instance) ? instOf(pending) : nullptr;
    for (const Handler& h : fr.fn->handlers) {
      if (pcThrow < h.startPc || pcThrow >= h.endPc) continue;
      if (h.classId >= 0 && (ex == nullptr || !ex->cls->sym->isSubclassOf(classes[static_cast<std::size_t>(h.classId)].sym))) continue;
      stack[fr.base + h.slot] = pending;
      hasPending = false;
      sp = fr.base + static_cast<std::size_t>(fr.fn->numLocals);
      fr.pc = h.handlerPc;
      return true;
    }
    if (fr.co != nullptr) {
      fr.co->done = true;
      fr.co->running = false;
    }
    frames.pop_back();
    if (frames.size() <= stopDepth) return false;
  }
}

Value VMImpl::typeObject(const Type* t) {
  if (t->isClassLike()) {
    ClassRt& rt = classes[static_cast<std::size_t>(t->cls->id)];
    if (rt.typeObj == nullptr) {
      InstanceObj* o = newInstance(classes[static_cast<std::size_t>(clsType)]);
      auto d = std::make_unique<TypeData>();
      d->type = t;
      o->native = std::move(d);
      rt.typeObj = o;
    }
    return vObj(rt.typeObj);
  }
  auto it = typeObjs.find(t);
  if (it != typeObjs.end()) return vObj(it->second);
  InstanceObj* o = newInstance(classes[static_cast<std::size_t>(clsType)]);
  auto d = std::make_unique<TypeData>();
  d->type = t;
  o->native = std::move(d);
  typeObjs[t] = o;
  return vObj(o);
}

// ---- the interpreter ------------------------------------------------------------------------------------

Status VMImpl::execute(std::size_t stopDepth) {
  Value* const stk = stack.get();
  Frame* f = nullptr;
  const Instr* code = nullptr;
  const Instr* ip = nullptr;
  Value* L = nullptr;
  Value* S = nullptr;
  std::string tmp;

#define LOAD_FRAME()                    \
  do {                                  \
    f = &frames.back();                 \
    code = f->fn->code.data();          \
    ip = code + f->pc;                  \
    L = stk + f->base;                  \
  } while (false)
#define SAVE()                          \
  do {                                  \
    f->pc = static_cast<std::uint32_t>(ip - code); \
    sp = static_cast<std::size_t>(S - stk);        \
  } while (false)
#define RAISE(cls, msg)                 \
  do {                                  \
    SAVE();                             \
    raise((cls), (msg));                \
    goto do_throw;                      \
  } while (false)
#define NULLCHK(v)                      \
  do {                                  \
    if ((v).tag != MS_TAG_OBJECT) RAISE(clsNullRef, "null reference"); \
  } while (false)
#define GC_POINT()                      \
  do {                                  \
    if (shouldCollect()) { SAVE(); collect(); } \
  } while (false)
#define BIN_I(expr) { const std::int64_t b_ = S[-1].u.i; const std::int64_t a_ = S[-2].u.i; (void)a_; (void)b_; --S; S[-1] = (expr); break; }

  LOAD_FRAME();
  S = stk + (sp);  // set by the caller: operand stack starts after the locals
  S = stk + f->base + f->fn->numLocals;

  for (;;) {
    const Instr in = *ip++;
    switch (in.op) {
      case Op::Nop: break;
      case Op::PushNull: *S++ = makeNull(); break;
      case Op::PushTrue: *S++ = makeBool(true); break;
      case Op::PushFalse: *S++ = makeBool(false); break;
      case Op::PushInt: *S++ = makeInt(in.b); break;
      case Op::PushConst: *S++ = consts[static_cast<std::size_t>(in.b)]; break;
      case Op::Pop: --S; break;
      case Op::Dup: S[0] = S[-1]; ++S; break;
      case Op::Dup2: S[0] = S[-2]; S[1] = S[-1]; S += 2; break;

      case Op::LoadLocal: *S++ = L[in.a]; break;
      case Op::StoreLocal: L[in.a] = *--S; break;
      case Op::LoadBoxed: *S++ = boxOf(L[in.a])->v; break;
      case Op::StoreBoxed: boxOf(L[in.a])->v = *--S; break;
      case Op::BoxLocal: L[in.a] = vObj(newBox(L[in.a])); GC_POINT(); break;
      case Op::LoadCapture: *S++ = f->closure->captures[in.a]; break;
      case Op::LoadCaptureBoxed: *S++ = boxOf(f->closure->captures[in.a])->v; break;
      case Op::StoreCaptureBoxed: boxOf(f->closure->captures[in.a])->v = *--S; break;

      case Op::LoadField: NULLCHK(S[-1]); S[-1] = instOf(S[-1])->fields[static_cast<std::size_t>(in.a)]; break;
      case Op::StoreField: NULLCHK(S[-2]); instOf(S[-2])->fields[static_cast<std::size_t>(in.a)] = S[-1]; S -= 2; break;
      case Op::LoadStatic: *S++ = statics[static_cast<std::size_t>(in.b)]; break;
      case Op::StoreStatic: statics[static_cast<std::size_t>(in.b)] = *--S; break;

      case Op::NewObject: {
        if (!heapRoom(1024)) RAISE(clsOutOfMemory, "out of memory");
        *S++ = vObj(newInstance(classes[static_cast<std::size_t>(in.b)]));
        GC_POINT();
        break;
      }
      case Op::NewArray: {
        const std::int64_t n = S[-1].u.i;
        if (n < 0) RAISE(clsArgument, "negative array length " + std::to_string(n));
        if (n > kMaxArrayLength || !heapRoom(static_cast<std::size_t>(n) * sizeof(Value))) RAISE(clsOutOfMemory, "cannot allocate an array of " + std::to_string(n) + " elements");
        S[-1] = vObj(newArr(static_cast<ElemKind>(in.a), static_cast<std::size_t>(n)));
        GC_POINT();
        break;
      }
      case Op::ArrayLit: {
        ArrayObj* a = newArr(static_cast<ElemKind>(in.a), static_cast<std::size_t>(in.b));
        for (int i = 0; i < in.b; ++i) a->items[static_cast<std::size_t>(i)] = S[-in.b + i];
        S -= in.b;
        *S++ = vObj(a);
        GC_POINT();
        break;
      }
      case Op::LoadIndex: {
        NULLCHK(S[-2]);
        ArrayObj* a = arrOf(S[-2]);
        const std::int64_t i = S[-1].u.i;
        if (i < 0 || i >= static_cast<std::int64_t>(a->items.size())) RAISE(clsIndex, "index " + std::to_string(i) + " is out of range for length " + std::to_string(a->items.size()));
        --S;
        S[-1] = a->items[static_cast<std::size_t>(i)];
        break;
      }
      case Op::StoreIndex: {
        NULLCHK(S[-3]);
        ArrayObj* a = arrOf(S[-3]);
        const std::int64_t i = S[-2].u.i;
        if (i < 0 || i >= static_cast<std::int64_t>(a->items.size())) RAISE(clsIndex, "index " + std::to_string(i) + " is out of range for length " + std::to_string(a->items.size()));
        a->items[static_cast<std::size_t>(i)] = S[-1];
        S -= 3;
        break;
      }
      case Op::ArrayLen: NULLCHK(S[-1]); S[-1] = makeInt(static_cast<std::int64_t>(arrOf(S[-1])->items.size())); break;
      case Op::StrLen: S[-1] = makeInt(static_cast<std::int64_t>(strOf(S[-1])->s.size())); break;
      case Op::StrIndex: {
        StringObj* s = strOf(S[-2]);
        const std::int64_t i = S[-1].u.i;
        if (i < 0 || i >= static_cast<std::int64_t>(s->s.size())) RAISE(clsIndex, "string index " + std::to_string(i) + " is out of range for length " + std::to_string(s->s.size()));
        --S;
        S[-1] = makeInt(static_cast<unsigned char>(s->s[static_cast<std::size_t>(i)]));
        break;
      }
      case Op::StrConcat: {
        StringObj* a = strOf(S[-2]);
        StringObj* b = strOf(S[-1]);
        const std::size_t n = a->s.size() + b->s.size();
        if (n > kMaxStringBytes || !heapRoom(n)) RAISE(clsOutOfMemory, "string too large");
        std::string r;
        r.reserve(n);
        r += a->s;
        r += b->s;
        --S;
        S[-1] = vObj(newStr(std::move(r)));
        GC_POINT();
        break;
      }

      case Op::AddI: BIN_I(makeInt(wrapAdd(a_, b_)))
      case Op::SubI: BIN_I(makeInt(wrapSub(a_, b_)))
      case Op::MulI: BIN_I(makeInt(wrapMul(a_, b_)))
      case Op::DivI: {
        const std::int64_t b = S[-1].u.i;
        const std::int64_t a = S[-2].u.i;
        if (b == 0) RAISE(clsDivZero, "integer division by zero");
        --S;
        S[-1] = makeInt(b == -1 ? wrapSub(0, a) : a / b);
        break;
      }
      case Op::ModI: {
        const std::int64_t b = S[-1].u.i;
        const std::int64_t a = S[-2].u.i;
        if (b == 0) RAISE(clsDivZero, "integer remainder by zero");
        --S;
        S[-1] = makeInt(b == -1 ? 0 : a % b);
        break;
      }
      case Op::NegI: S[-1] = makeInt(wrapSub(0, S[-1].u.i)); break;
      case Op::AddF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeFloat(a + b); break; }
      case Op::SubF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeFloat(a - b); break; }
      case Op::MulF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeFloat(a * b); break; }
      case Op::DivF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeFloat(a / b); break; }
      case Op::ModF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeFloat(std::fmod(a, b)); break; }
      case Op::NegF: S[-1] = makeFloat(-S[-1].u.f); break;
      case Op::BitAnd: { const bool isB = S[-1].tag == MS_TAG_BOOL || S[-2].tag == MS_TAG_BOOL; const std::int64_t r = S[-2].u.i & S[-1].u.i; --S; S[-1] = isB ? makeBool(r != 0) : makeInt(r); break; }
      case Op::BitOr: { const bool isB = S[-1].tag == MS_TAG_BOOL || S[-2].tag == MS_TAG_BOOL; const std::int64_t r = S[-2].u.i | S[-1].u.i; --S; S[-1] = isB ? makeBool(r != 0) : makeInt(r); break; }
      case Op::BitXor: { const bool isB = S[-1].tag == MS_TAG_BOOL || S[-2].tag == MS_TAG_BOOL; const std::int64_t r = S[-2].u.i ^ S[-1].u.i; --S; S[-1] = isB ? makeBool(r != 0) : makeInt(r); break; }
      case Op::BitNot: S[-1] = makeInt(~S[-1].u.i); break;
      case Op::Shl: BIN_I(makeInt(static_cast<std::int64_t>(static_cast<std::uint64_t>(a_) << (b_ & 63))))
      case Op::Shr: BIN_I(makeInt(a_ >> (b_ & 63)))
      case Op::Not: S[-1] = makeBool(S[-1].u.i == 0); break;

      case Op::EqI: BIN_I(makeBool(a_ == b_))
      case Op::NeI: BIN_I(makeBool(a_ != b_))
      case Op::LtI: BIN_I(makeBool(a_ < b_))
      case Op::LeI: BIN_I(makeBool(a_ <= b_))
      case Op::GtI: BIN_I(makeBool(a_ > b_))
      case Op::GeI: BIN_I(makeBool(a_ >= b_))
      case Op::EqF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeBool(a == b); break; }
      case Op::NeF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeBool(a != b); break; }
      case Op::LtF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeBool(a < b); break; }
      case Op::LeF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeBool(a <= b); break; }
      case Op::GtF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeBool(a > b); break; }
      case Op::GeF: { const double b = S[-1].u.f, a = S[-2].u.f; --S; S[-1] = makeBool(a >= b); break; }
      case Op::EqRef: { const bool r = refEquals(S[-2], S[-1]); --S; S[-1] = makeBool(r); break; }
      case Op::NeRef: { const bool r = refEquals(S[-2], S[-1]); --S; S[-1] = makeBool(!r); break; }
      case Op::EqStr: { const bool r = strOf(S[-2])->s == strOf(S[-1])->s; --S; S[-1] = makeBool(r); break; }
      case Op::NeStr: { const bool r = strOf(S[-2])->s != strOf(S[-1])->s; --S; S[-1] = makeBool(r); break; }
      case Op::LtStr: { const int c = strOf(S[-2])->s.compare(strOf(S[-1])->s); --S; S[-1] = makeBool(c < 0); break; }
      case Op::LeStr: { const int c = strOf(S[-2])->s.compare(strOf(S[-1])->s); --S; S[-1] = makeBool(c <= 0); break; }
      case Op::GtStr: { const int c = strOf(S[-2])->s.compare(strOf(S[-1])->s); --S; S[-1] = makeBool(c > 0); break; }
      case Op::GeStr: { const int c = strOf(S[-2])->s.compare(strOf(S[-1])->s); --S; S[-1] = makeBool(c >= 0); break; }

      case Op::I2F: S[-1] = makeFloat(static_cast<double>(S[-1].u.i)); break;
      case Op::F2I: S[-1] = makeInt(f2i(S[-1].u.f)); break;
      case Op::ToStr:
        S[-1] = vObj(newStr(toStr(S[-1], in.a, in.b)));
        GC_POINT();
        break;
      case Op::EnumFromInt: {
        const ClassSym* c = classes[static_cast<std::size_t>(in.b)].sym;
        bool ok = false;
        for (const auto& ec : c->enumConsts) ok = ok || ec->value == S[-1].u.i;
        if (!ok) RAISE(clsCast, std::to_string(S[-1].u.i) + " is not a value of enum " + c->name);
        S[-1] = makeInt(S[-1].u.i);
        break;
      }

      case Op::Jmp:
        if (static_cast<std::size_t>(in.b) < static_cast<std::size_t>(ip - code)) GC_POINT();
        ip = code + in.b;
        break;
      case Op::JmpIfFalse: {
        const bool c = (--S)->u.i != 0;
        if (!c) {
          if (static_cast<std::size_t>(in.b) < static_cast<std::size_t>(ip - code)) GC_POINT();
          ip = code + in.b;
        }
        break;
      }
      case Op::JmpIfTrue: {
        const bool c = (--S)->u.i != 0;
        if (c) {
          if (static_cast<std::size_t>(in.b) < static_cast<std::size_t>(ip - code)) GC_POINT();
          ip = code + in.b;
        }
        break;
      }

      case Op::Call:
      case Op::CallVirtual:
      case Op::CallInterface:
      case Op::CallClosure: {
        int funcId = -1;
        ClosureObj* closure = nullptr;
        std::uint32_t argc = in.a;
        Value* argp = nullptr;
        std::uint32_t retSp = 0;
        if (in.op == Op::CallClosure) {
          Value& cv = S[-static_cast<int>(in.a) - 1];
          NULLCHK(cv);
          closure = cloOf(cv);
          funcId = closure->funcId;
          retSp = static_cast<std::uint32_t>(&cv - stk);
          if (closure->bound) {
            cv = closure->self;  // `this` takes the closure's slot; the closure itself stays reachable via the frame
            argp = &cv;
            ++argc;
          } else {
            argp = &cv + 1;
          }
        } else {
          argp = S - in.a;
          retSp = static_cast<std::uint32_t>(argp - stk);
          if (in.op == Op::Call) {
            funcId = in.b;
          } else {
            NULLCHK(*argp);
            const InstanceObj* recv = instOf(*argp);
            if (in.op == Op::CallVirtual) {
              funcId = recv->cls->vtable[static_cast<std::size_t>(in.b)];
            } else {
              auto it = recv->cls->itable.find(in.b);
              if (it == recv->cls->itable.end()) RAISE(clsCast, recv->cls->sym->qualifiedName + " does not implement the called interface method");
              funcId = it->second;
            }
            if (funcId < 0) RAISE(clsNative, "abstract method called");
          }
        }
        const FuncProto& fn = prog.funcs[static_cast<std::size_t>(funcId)];
        SAVE();
        if (fn.isNative) {
          Value result = makeNull();
          ++nativeDepth;
          Status st = callNative(fn, funcId, static_cast<std::uint32_t>(argp - stk), argc, result);
          --nativeDepth;
          if (st == Status::Threw) goto do_throw;
          S = stk + retSp;
          *S++ = result;
          GC_POINT();
          break;
        }
        if (!pushFrame(fn, static_cast<std::uint32_t>(argp - stk), argc, retSp, closure)) {
          raise(clsStackOverflow, "stack overflow");
          goto do_throw;
        }
        LOAD_FRAME();
        S = stk + f->base + f->fn->numLocals;
        if (shouldCollect()) { SAVE(); collect(); }
        break;
      }
      case Op::Return: {
        const Value r = S[-1];
        const Frame fr = frames.back();
        frames.pop_back();
        if (frames.size() <= stopDepth) {
          retVal = r;
          sp = fr.retSp;
          return Status::Returned;
        }
        LOAD_FRAME();
        S = stk + fr.retSp;
        *S++ = r;
        break;
      }
      case Op::MakeClosure: {
        ClosureObj* c = newClosure(in.b, in.a);
        for (int i = 0; i < in.a; ++i) c->captures[static_cast<std::size_t>(i)] = S[-in.a + i];
        S -= in.a;
        *S++ = vObj(c);
        GC_POINT();
        break;
      }
      case Op::MakeBound:
      case Op::MakeBoundVirtual: {
        NULLCHK(S[-1]);
        const int funcId = in.op == Op::MakeBound ? in.b : resolveVirtual(S[-1], in.b);
        ClosureObj* c = newClosure(funcId, 0);
        c->bound = true;
        c->self = S[-1];
        S[-1] = vObj(c);
        GC_POINT();
        break;
      }

      case Op::IsType: {
        const bool r = isKind(S[-1], ObjKind::Instance) && instOf(S[-1])->cls->sym->isAssignableTo(classes[static_cast<std::size_t>(in.b)].sym);
        S[-1] = makeBool(r);
        break;
      }
      case Op::CastType: {
        if (S[-1].tag == MS_TAG_OBJECT) {
          const ClassSym* from = instOf(S[-1])->cls->sym;
          const ClassSym* to = classes[static_cast<std::size_t>(in.b)].sym;
          if (!from->isAssignableTo(to)) RAISE(clsCast, "cannot cast " + from->qualifiedName + " to " + to->qualifiedName);
        }
        break;
      }
      case Op::Throw: {
        const Value ex = *--S;
        if (ex.tag != MS_TAG_OBJECT) RAISE(clsNullRef, "cannot throw null");
        SAVE();
        setPending(ex);
        fillTrace(ex);
        goto do_throw;
      }

      case Op::MakeRefBox: S[-1] = vObj(newRef(RefObj::RK::Box, objOf(S[-1]), 0)); GC_POINT(); break;
      case Op::MakeRefField: NULLCHK(S[-1]); S[-1] = vObj(newRef(RefObj::RK::Field, objOf(S[-1]), in.a)); GC_POINT(); break;
      case Op::MakeRefElem: {
        NULLCHK(S[-2]);
        ArrayObj* a = arrOf(S[-2]);
        const std::int64_t i = S[-1].u.i;
        if (i < 0 || i >= static_cast<std::int64_t>(a->items.size())) RAISE(clsIndex, "index " + std::to_string(i) + " is out of range for length " + std::to_string(a->items.size()));
        --S;
        S[-1] = vObj(newRef(RefObj::RK::Elem, a, static_cast<std::int32_t>(i)));
        GC_POINT();
        break;
      }
      case Op::MakeRefStatic: *S++ = vObj(newRef(RefObj::RK::Static, nullptr, in.b)); GC_POINT(); break;
      case Op::LoadRef: {
        NULLCHK(S[-1]);
        RefObj* r = refOf(S[-1]);
        switch (r->rk) {
          case RefObj::RK::Box: S[-1] = static_cast<BoxObj*>(r->target)->v; break;
          case RefObj::RK::Field: S[-1] = static_cast<InstanceObj*>(r->target)->fields[static_cast<std::size_t>(r->index)]; break;
          case RefObj::RK::Elem: S[-1] = static_cast<ArrayObj*>(r->target)->items[static_cast<std::size_t>(r->index)]; break;
          case RefObj::RK::Static: S[-1] = statics[static_cast<std::size_t>(r->index)]; break;
        }
        break;
      }
      case Op::StoreRef: {
        NULLCHK(S[-2]);
        RefObj* r = refOf(S[-2]);
        const Value v = S[-1];
        switch (r->rk) {
          case RefObj::RK::Box: static_cast<BoxObj*>(r->target)->v = v; break;
          case RefObj::RK::Field: static_cast<InstanceObj*>(r->target)->fields[static_cast<std::size_t>(r->index)] = v; break;
          case RefObj::RK::Elem: static_cast<ArrayObj*>(r->target)->items[static_cast<std::size_t>(r->index)] = v; break;
          case RefObj::RK::Static: statics[static_cast<std::size_t>(r->index)] = v; break;
        }
        S -= 2;
        break;
      }

      case Op::Yield: {
        const Value y = *--S;
        CoroutineData* cd = f->co;
        MINGOL_VERIFY(cd != nullptr);
        const std::uint32_t retSp = f->retSp;
        cd->locals.assign(L, L + f->fn->numLocals);
        cd->pc = static_cast<std::uint32_t>(ip - code);
        cd->yielded = y;
        cd->yieldKind = yieldKindOf(y);
        frames.pop_back();
        sp = retSp;
        return Status::Yielded;
      }
      case Op::StartCoroutine:
      case Op::StartCoroutineVirtual: {
        int funcId = in.b;
        if (in.op == Op::StartCoroutineVirtual) {
          Value& recv = S[-static_cast<int>(in.a)];
          NULLCHK(recv);
          funcId = resolveVirtual(recv, in.b);
        } else if (in.a > 0 && prog.funcs[static_cast<std::size_t>(funcId)].hasThis) {
          NULLCHK(S[-static_cast<int>(in.a)]);
        }
        const FuncProto& fn = prog.funcs[static_cast<std::size_t>(funcId)];
        InstanceObj* co = newInstance(classes[static_cast<std::size_t>(clsCoroutine)]);
        auto cd = std::make_unique<CoroutineData>();
        cd->funcId = funcId;
        cd->locals.assign(static_cast<std::size_t>(fn.numLocals), makeNull());
        for (int i = 0; i < in.a; ++i) cd->locals[static_cast<std::size_t>(i)] = S[-in.a + i];
        co->native = std::move(cd);
        S -= in.a;
        *S++ = vObj(co);
        GC_POINT();
        break;
      }
      case Op::TypeOfStatic: *S++ = typeObject(prog.typeDescs[static_cast<std::size_t>(in.b)]); GC_POINT(); break;
      case Op::TypeOfValue:
        NULLCHK(S[-1]);
        S[-1] = typeObject(instOf(S[-1])->cls->sym->thisType);
        GC_POINT();
        break;
    }
    continue;

  do_throw:
    // `pending` is set and f->pc is saved.
    if (unwind(stopDepth)) {
      LOAD_FRAME();
      S = stk + sp;
      continue;
    }
    return Status::Threw;
  }
#undef LOAD_FRAME
#undef SAVE
#undef RAISE
#undef NULLCHK
#undef GC_POINT
#undef BIN_I
}

Status VMImpl::resumeCo(InstanceObj* coObj) {
  auto* cd = dynamic_cast<CoroutineData*>(coObj->native.get());
  if (cd == nullptr) {
    raise(clsArgument, "not a started coroutine");
    return Status::Threw;
  }
  if (cd->done) return Status::Returned;
  if (cd->running) {
    raise(clsArgument, "the coroutine is already running");
    return Status::Threw;
  }
  if (nativeDepth >= opt.maxNativeDepth) {
    raise(clsStackOverflow, "native call depth limit reached");
    return Status::Threw;
  }
  const FuncProto& fn = prog.funcs[static_cast<std::size_t>(cd->funcId)];
  const std::uint32_t base = static_cast<std::uint32_t>(sp);
  if (frames.size() >= opt.maxFrames || static_cast<std::size_t>(base) + static_cast<std::size_t>(fn.numLocals) + static_cast<std::size_t>(fn.maxStack) > stackCap) {
    raise(clsStackOverflow, "stack overflow");
    return Status::Threw;
  }
  for (std::size_t i = 0; i < cd->locals.size(); ++i) stack[base + i] = cd->locals[i];
  const std::size_t stop = frames.size();
  Frame fr;
  fr.fn = &fn;
  fr.pc = cd->pc;
  fr.base = base;
  fr.retSp = base;
  fr.co = cd;
  frames.push_back(fr);
  cd->running = true;
  ++nativeDepth;
  Status st = execute(stop);
  --nativeDepth;
  cd->running = false;
  sp = base;
  if (st == Status::Returned) {
    cd->done = true;
    cd->yieldKind = 0;
    cd->yielded = makeNull();
  } else if (st == Status::Threw) {
    cd->done = true;
  }
  return st;
}

}  // namespace mingol::script
