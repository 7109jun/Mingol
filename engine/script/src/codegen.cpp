// SPDX-License-Identifier: GPL-2.0-only
// Code generation: checked syntax tree -> stack bytecode.
#include "codegen.hpp"

#include <cstring>

namespace mingol::script {
namespace {
constexpr int R(Res r) { return static_cast<int>(r); }

int stackEffect(Op op, int a, int b) {
  switch (op) {
    case Op::Nop: case Op::Jmp: case Op::BoxLocal: case Op::LoadField: case Op::NewArray: case Op::ArrayLen: case Op::StrLen:
    case Op::NegI: case Op::NegF: case Op::BitNot: case Op::Not: case Op::I2F: case Op::F2I: case Op::ToStr: case Op::EnumFromInt:
    case Op::IsType: case Op::CastType: case Op::MakeBound: case Op::MakeBoundVirtual: case Op::MakeRefBox: case Op::MakeRefField:
    case Op::LoadRef: case Op::TypeOfValue:
      return 0;
    case Op::PushNull: case Op::PushTrue: case Op::PushFalse: case Op::PushInt: case Op::PushConst: case Op::Dup: case Op::LoadLocal:
    case Op::LoadBoxed: case Op::LoadCapture: case Op::LoadCaptureBoxed: case Op::LoadStatic: case Op::NewObject: case Op::MakeRefStatic:
    case Op::TypeOfStatic:
      return 1;
    case Op::Dup2: return 2;
    case Op::Pop: case Op::StoreLocal: case Op::StoreBoxed: case Op::StoreCaptureBoxed: case Op::StoreStatic: case Op::LoadIndex:
    case Op::StrIndex: case Op::StrConcat: case Op::JmpIfFalse: case Op::JmpIfTrue: case Op::Return: case Op::Throw: case Op::Yield:
    case Op::MakeRefElem:
      return -1;
    case Op::StoreField: case Op::StoreRef: return -2;
    case Op::StoreIndex: return -3;
    case Op::ArrayLit: return 1 - b;
    case Op::Call: case Op::CallVirtual: case Op::CallInterface: case Op::StartCoroutine: case Op::StartCoroutineVirtual: return 1 - a;
    case Op::CallClosure: return -a;
    case Op::MakeClosure: return 1 - a;
    default: return -1;  // binary arithmetic / comparison
  }
}
}  // namespace

// ---- emission -------------------------------------------------------------------------------------------

std::size_t CodeGen::emit(Op op, int a, int b) {
  Instr i;
  i.op = op;
  i.a = static_cast<std::uint16_t>(a);
  i.b = b;
  g_->code.push_back(i);
  g_->depth += stackEffect(op, a, b);
  if (g_->depth > g_->maxDepth) g_->maxDepth = g_->depth;
  return g_->code.size() - 1;
}

void CodeGen::line(SourceLoc l) {
  if (l.line == g_->lastLine || l.line == 0) return;
  g_->lastLine = l.line;
  g_->lines.push_back({static_cast<std::uint32_t>(g_->code.size()), l.line});
}

int CodeGen::allocSlot() {
  const int s = g_->nextSlot++;
  if (g_->nextSlot > g_->maxSlot) g_->maxSlot = g_->nextSlot;
  return s;
}

int CodeGen::constInt(std::int64_t v) {
  auto it = intConsts_.find(v);
  if (it != intConsts_.end()) return it->second;
  Constant c;
  c.kind = Constant::Kind::Int;
  c.i = v;
  p_.constants.push_back(std::move(c));
  return intConsts_[v] = static_cast<int>(p_.constants.size() - 1);
}

int CodeGen::constFloat(double v) {
  std::uint64_t bits;
  std::memcpy(&bits, &v, sizeof bits);
  auto it = floatConsts_.find(bits);
  if (it != floatConsts_.end()) return it->second;
  Constant c;
  c.kind = Constant::Kind::Float;
  c.f = v;
  p_.constants.push_back(std::move(c));
  return floatConsts_[bits] = static_cast<int>(p_.constants.size() - 1);
}

int CodeGen::constString(const std::string& s) {
  auto it = strConsts_.find(s);
  if (it != strConsts_.end()) return it->second;
  Constant c;
  c.kind = Constant::Kind::String;
  c.s = s;
  p_.constants.push_back(std::move(c));
  return strConsts_[s] = static_cast<int>(p_.constants.size() - 1);
}

int CodeGen::typeDesc(const Type* t) {
  for (std::size_t i = 0; i < p_.typeDescs.size(); ++i) {
    if (p_.typeDescs[i] == t) return static_cast<int>(i);
  }
  p_.typeDescs.push_back(t);
  return static_cast<int>(p_.typeDescs.size() - 1);
}

void CodeGen::pushInt(std::int64_t v) {
  if (v >= INT32_MIN && v <= INT32_MAX) emit(Op::PushInt, 0, static_cast<int>(v));
  else emit(Op::PushConst, 0, constInt(v));
}

void CodeGen::pushDefault(const Type* t) {
  switch (t->kind) {
    case TypeKind::Int: case TypeKind::Enum: emit(Op::PushInt, 0, 0); break;
    case TypeKind::Float: emit(Op::PushConst, 0, constFloat(0.0)); break;
    case TypeKind::Bool: emit(Op::PushFalse); break;
    case TypeKind::String: emit(Op::PushConst, 0, constString("")); break;
    default: emit(Op::PushNull); break;
  }
}

void CodeGen::coerce(const Type* from, const Type* to) {
  if (from != nullptr && to != nullptr && from->kind == TypeKind::Int && to->kind == TypeKind::Float) emit(Op::I2F);
}

ElemKind CodeGen::elemKind(const Type* t) const {
  switch (t->kind) {
    case TypeKind::Int: case TypeKind::Enum: return ElemKind::Int;
    case TypeKind::Float: return ElemKind::Float;
    case TypeKind::Bool: return ElemKind::Bool;
    case TypeKind::String: return ElemKind::String;
    default: return ElemKind::Ref;
  }
}

void CodeGen::toStr(const Type* t) {
  switch (t->kind) {
    case TypeKind::Int: emit(Op::ToStr, 0); break;
    case TypeKind::Float: emit(Op::ToStr, 1); break;
    case TypeKind::Bool: emit(Op::ToStr, 2); break;
    case TypeKind::Enum: emit(Op::ToStr, 3, t->cls->id); break;
    default: break;  // string
  }
}

// ---- driver ---------------------------------------------------------------------------------------------

void CodeGen::run() {
  // Reserve function slots for the synthesised initialisers first: constructors call them.
  for (auto& c : p_.classes) {
    if (c->fieldInit != nullptr) {
      FuncProto fp;
      fp.name = c->name + ".<fieldinit>";
      fp.hasThis = true;
      fp.numParams = 1;
      c->fieldInitFunc = static_cast<int>(p_.funcs.size());
      c->fieldInit->funcId = c->fieldInitFunc;
      p_.funcs.push_back(std::move(fp));
    }
    if (c->staticInit != nullptr) {
      FuncProto fp;
      fp.name = c->name + ".<staticinit>";
      c->staticInitFunc = static_cast<int>(p_.funcs.size());
      c->staticInit->funcId = c->staticInitFunc;
      p_.funcs.push_back(std::move(fp));
      p_.staticInitFuncs.push_back(c->staticInitFunc);
    }
  }
  for (auto& c : p_.classes) {
    if (c->kind != TypeDeclKind::Class) continue;
    if (c->fieldInit != nullptr) genFieldInit(*c, false);
    if (c->staticInit != nullptr) genFieldInit(*c, true);
    for (auto& m : c->methods) {
      if (m->mods.isNative || m->funcId < 0) continue;
      if (m->mkind == MethodKind::Constructor) genConstructor(*m);
      else if (m->decl != nullptr && m->decl->body) genMethod(*m);
    }
  }
}

void CodeGen::beginFn(Fn& f, FuncInfo* info, int funcId, std::string name) {
  f.info = info;
  f.funcId = funcId;
  f.name = std::move(name);
  g_ = &f;
}

void CodeGen::endFn(Fn& f, int numParams) {
  FuncProto& fp = p_.funcs[static_cast<std::size_t>(f.funcId)];
  fp.code = std::move(f.code);
  fp.handlers = std::move(f.handlers);
  fp.lines = std::move(f.lines);
  fp.numLocals = f.maxSlot;
  fp.numParams = numParams;
  fp.numCaptures = static_cast<int>(f.info->captures.size());
  fp.maxStack = f.maxDepth + 8;
  if (fp.name.empty()) fp.name = f.name;
}

void CodeGen::assignParamSlots(FuncInfo& info) {
  for (auto& v : info.vars) {
    if (!v->isParam) break;
    v->slot = allocSlot();
  }
}

void CodeGen::boxParams(FuncInfo& info) {
  for (auto& v : info.vars) {
    if (!v->isParam) break;
    if (v->boxed()) emit(Op::BoxLocal, v->slot);
  }
}

void CodeGen::callFieldInit(ClassSym& c) {
  if (c.fieldInit == nullptr) return;
  emit(Op::LoadLocal, 0);
  emit(Op::Call, 1, c.fieldInitFunc);
  emit(Op::Pop);
}

void CodeGen::genFieldInit(ClassSym& c, bool isStatic) {
  FuncInfo* info = isStatic ? c.staticInit : c.fieldInit;
  Fn f;
  Fn* saved = g_;
  beginFn(f, info, info->funcId, c.name + (isStatic ? ".<staticinit>" : ".<fieldinit>"));
  assignParamSlots(*info);
  p_.funcs[static_cast<std::size_t>(f.funcId)].file = c.unit->file;
  for (auto& fld : c.fields) {
    if (fld->isStatic != isStatic || fld->decl == nullptr || !fld->decl->init) continue;
    line(fld->decl->init->loc);
    if (isStatic) {
      exprConv(*fld->decl->init, fld->type);
      emit(Op::StoreStatic, 0, fld->index);
    } else {
      emit(Op::LoadLocal, 0);
      exprConv(*fld->decl->init, fld->type);
      emit(Op::StoreField, fld->index);
    }
  }
  emit(Op::PushNull);
  emit(Op::Return);
  endFn(f, isStatic ? 0 : 1);
  g_ = saved;
}

void CodeGen::genMethod(MethodSym& m) {
  Fn f;
  Fn* saved = g_;
  FuncInfo* info = m.info;
  beginFn(f, info, m.funcId, m.owner->name + "." + m.signature());
  FuncProto& fp = p_.funcs[static_cast<std::size_t>(m.funcId)];
  fp.file = m.owner->unit->file;
  fp.name = f.name;
  assignParamSlots(*info);
  boxParams(*info);
  line(m.decl->body->loc);
  stmt(*m.decl->body);
  emit(Op::PushNull);
  emit(Op::Return);
  int np = static_cast<int>(m.params.size()) + ((!m.mods.isStatic && m.mkind != MethodKind::Operator) ? 1 : 0);
  endFn(f, np);
  g_ = saved;
}

void CodeGen::genConstructor(MethodSym& m) {
  ClassSym& c = *m.owner;
  Fn f;
  Fn* saved = g_;
  FuncInfo* info = m.info;
  beginFn(f, info, m.funcId, c.name + "." + m.signature());
  FuncProto& fp = p_.funcs[static_cast<std::size_t>(m.funcId)];
  fp.file = c.unit->file;
  fp.name = f.name;
  fp.hasThis = true;
  assignParamSlots(*info);
  boxParams(*info);

  auto callBaseDefault = [&]() {
    if (c.base == nullptr || c.base == p_.objectClass) return;
    for (auto& bm : c.base->methods) {
      if (bm->mkind == MethodKind::Constructor && bm->params.empty()) {
        emit(Op::LoadLocal, 0);
        emit(Op::Call, 1, bm->funcId);
        emit(Op::Pop);
        return;
      }
    }
  };

  if (m.decl == nullptr) {
    callBaseDefault();
    callFieldInit(c);
  } else {
    Stmt& body = *m.decl->body;
    std::size_t first = 0;
    bool chainedThis = false;
    bool chainedSuper = false;
    if (!body.stmts.empty() && body.stmts[0]->kind == StmtKind::ExprStmt) {
      Expr& e = *body.stmts[0]->exprs[0];
      if (e.kind == ExprKind::Call && (e.aux == R(Res::CtorSuper) || e.aux == R(Res::CtorThis))) {
        auto* target = static_cast<MethodSym*>(e.sym);
        line(e.loc);
        emit(Op::LoadLocal, 0);
        genArgs(target->params, e, 1);
        emit(Op::Call, static_cast<int>(target->params.size()) + 1, target->funcId);
        emit(Op::Pop);
        first = 1;
        chainedThis = e.aux == R(Res::CtorThis);
        chainedSuper = !chainedThis;
      }
    }
    (void)chainedSuper;
    if (first == 0) callBaseDefault();
    if (!chainedThis) callFieldInit(c);
    line(body.loc);
    for (std::size_t i = first; i < body.stmts.size(); ++i) stmt(*body.stmts[i]);
  }
  emit(Op::PushNull);
  emit(Op::Return);
  endFn(f, static_cast<int>(m.params.size()) + 1);
  g_ = saved;
}

int CodeGen::genLambdaBody(FuncInfo& li, Expr& lambda, const std::string& name) {
  FuncProto fp;
  fp.name = name;
  fp.isLambda = true;
  fp.file = g_->info != nullptr ? p_.funcs[static_cast<std::size_t>(g_->funcId)].file : 0;
  const int id = static_cast<int>(p_.funcs.size());
  p_.funcs.push_back(std::move(fp));
  li.funcId = id;

  Fn f;
  Fn* saved = g_;
  beginFn(f, &li, id, name);
  assignParamSlots(li);
  boxParams(li);
  LambdaData& ld = *lambda.lambda;
  if (ld.bodyExpr) {
    line(ld.bodyExpr->loc);
    if (li.ret != nullptr && li.ret->kind != TypeKind::Void) exprConv(*ld.bodyExpr, li.ret);
    else expr(*ld.bodyExpr);
    emit(Op::Return);
  } else {
    line(ld.bodyBlock->loc);
    stmt(*ld.bodyBlock);
    emit(Op::PushNull);
    emit(Op::Return);
  }
  endFn(f, static_cast<int>(ld.params.size()));
  g_ = saved;
  return id;
}

// ---- variables ------------------------------------------------------------------------------------------

int CodeGen::captureIndex(VarSym* v) const {
  if (v->owner == g_->info) return -1;
  const auto& caps = g_->info->captures;
  for (std::size_t i = 0; i < caps.size(); ++i) {
    if (caps[i] == v) return static_cast<int>(i);
  }
  MINGOL_VERIFY(false);  // the checker registers every capture
  return -1;
}

void CodeGen::loadRaw(VarSym* v) {
  const int ci = captureIndex(v);
  if (ci < 0) emit(Op::LoadLocal, v->slot);
  else emit(Op::LoadCapture, ci);
}

void CodeGen::loadVar(VarSym* v) {
  const int ci = captureIndex(v);
  if (ci < 0) {
    emit(Op::LoadLocal, v->slot);
    if (v->isRefParam) emit(Op::LoadRef);
    else if (v->boxed()) { g_->code.back().op = Op::LoadBoxed; }
  } else {
    emit(Op::LoadCapture, ci);
    if (v->isRefParam) emit(Op::LoadRef);
    else if (v->boxed()) g_->code.back().op = Op::LoadCaptureBoxed;
  }
}

void CodeGen::storeVar(VarSym* v) {
  // Value on top of the stack; not usable for ref parameters (they store through a prefix).
  const int ci = captureIndex(v);
  if (ci < 0) {
    emit(v->boxed() ? Op::StoreBoxed : Op::StoreLocal, v->slot);
  } else {
    MINGOL_VERIFY(v->boxed());
    emit(Op::StoreCaptureBoxed, ci);
  }
}

void CodeGen::declareLocal(VarSym* v) { v->slot = allocSlot(); }

void CodeGen::initLocalFromStack(VarSym* v) {
  emit(Op::StoreLocal, v->slot);
  if (v->boxed()) emit(Op::BoxLocal, v->slot);
}

// ---- statements ---------------------------------------------------------------------------------------------

void CodeGen::block(Stmt& s) {
  const int saved = g_->nextSlot;
  for (auto& st : s.stmts) stmt(*st);
  g_->nextSlot = saved;
}

void CodeGen::stmt(Stmt& s) {
  g_->depth = 0;
  line(s.loc);
  switch (s.kind) {
    case StmtKind::Block: block(s); break;
    case StmtKind::VarDecl: genVarDecl(s); break;
    case StmtKind::ExprStmt: discard(*s.exprs[0]); break;
    case StmtKind::If: genIf(s); break;
    case StmtKind::While: genWhile(s); break;
    case StmtKind::DoWhile: genDoWhile(s); break;
    case StmtKind::For: genFor(s); break;
    case StmtKind::ForEach: genForEach(s); break;
    case StmtKind::Switch: genSwitch(s); break;
    case StmtKind::Break: genBreakContinue(true); break;
    case StmtKind::Continue: genBreakContinue(false); break;
    case StmtKind::Return: genReturn(s); break;
    case StmtKind::Throw:
      expr(*s.exprs[0]);
      emit(Op::Throw);
      break;
    case StmtKind::Try: genTry(s); break;
    case StmtKind::Yield:
      if (s.exprs.empty()) emit(Op::PushNull);
      else expr(*s.exprs[0]);
      emit(Op::Yield);
      break;
  }
}

void CodeGen::genVarDecl(Stmt& s) {
  VarSym* v = static_cast<VarSym*>(s.sym);
  declareLocal(v);
  if (v->boxed()) {
    // The box exists before the initialiser runs, so a closure in it can refer to the variable itself.
    pushDefault(v->type);
    emit(Op::StoreLocal, v->slot);
    emit(Op::BoxLocal, v->slot);
    if (!s.exprs.empty()) {
      exprConv(*s.exprs[0], v->type);
      emit(Op::StoreBoxed, v->slot);
    }
    return;
  }
  if (s.exprs.empty()) pushDefault(v->type);
  else exprConv(*s.exprs[0], v->type);
  emit(Op::StoreLocal, v->slot);
}

void CodeGen::genIf(Stmt& s) {
  exprConv(*s.exprs[0], p_.types.boolType());
  const std::size_t jf = jump(Op::JmpIfFalse);
  stmt(*s.stmts[0]);
  if (s.stmts.size() > 1) {
    const std::size_t je = jump(Op::Jmp);
    patch(jf, here());
    stmt(*s.stmts[1]);
    patch(je, here());
  } else {
    patch(jf, here());
  }
}

void CodeGen::genWhile(Stmt& s) {
  const std::size_t top = here();
  expr(*s.exprs[0]);
  const std::size_t jf = jump(Op::JmpIfFalse);
  g_->loops.push_back({{}, {}, static_cast<int>(g_->tries.size()), false});
  stmt(*s.stmts[0]);
  LoopCtx lc = std::move(g_->loops.back());
  g_->loops.pop_back();
  for (std::size_t c : lc.continues) patch(c, top);
  emit(Op::Jmp, 0, static_cast<int>(top));
  patch(jf, here());
  for (std::size_t b : lc.breaks) patch(b, here());
}

void CodeGen::genDoWhile(Stmt& s) {
  const std::size_t top = here();
  g_->loops.push_back({{}, {}, static_cast<int>(g_->tries.size()), false});
  stmt(*s.stmts[0]);
  LoopCtx lc = std::move(g_->loops.back());
  g_->loops.pop_back();
  const std::size_t condPc = here();
  for (std::size_t c : lc.continues) patch(c, condPc);
  expr(*s.exprs[0]);
  emit(Op::JmpIfTrue, 0, static_cast<int>(top));
  for (std::size_t b : lc.breaks) patch(b, here());
}

void CodeGen::genFor(Stmt& s) {
  const int saved = g_->nextSlot;
  if (s.stmts[0]) stmt(*s.stmts[0]);
  const std::size_t top = here();
  std::size_t jf = 0;
  const bool hasCond = s.exprs[0] != nullptr;
  if (hasCond) {
    expr(*s.exprs[0]);
    jf = jump(Op::JmpIfFalse);
  }
  g_->loops.push_back({{}, {}, static_cast<int>(g_->tries.size()), false});
  stmt(*s.stmts[1]);
  LoopCtx lc = std::move(g_->loops.back());
  g_->loops.pop_back();
  const std::size_t stepPc = here();
  for (std::size_t c : lc.continues) patch(c, stepPc);
  if (s.exprs[1]) discard(*s.exprs[1]);
  emit(Op::Jmp, 0, static_cast<int>(top));
  if (hasCond) patch(jf, here());
  for (std::size_t b : lc.breaks) patch(b, here());
  g_->nextSlot = saved;
}

void CodeGen::genForEach(Stmt& s) {
  const int saved = g_->nextSlot;
  const int arr = allocSlot();
  const int idx = allocSlot();
  expr(*s.exprs[0]);
  emit(Op::StoreLocal, arr);
  pushInt(0);
  emit(Op::StoreLocal, idx);
  VarSym* v = static_cast<VarSym*>(s.sym);
  declareLocal(v);
  const std::size_t top = here();
  emit(Op::LoadLocal, idx);
  emit(Op::LoadLocal, arr);
  emit(Op::ArrayLen);
  emit(Op::LtI);
  const std::size_t jf = jump(Op::JmpIfFalse);
  emit(Op::LoadLocal, arr);
  emit(Op::LoadLocal, idx);
  emit(Op::LoadIndex);
  coerce(s.type, v->type);
  initLocalFromStack(v);
  g_->loops.push_back({{}, {}, static_cast<int>(g_->tries.size()), false});
  stmt(*s.stmts[0]);
  LoopCtx lc = std::move(g_->loops.back());
  g_->loops.pop_back();
  const std::size_t stepPc = here();
  for (std::size_t c : lc.continues) patch(c, stepPc);
  emit(Op::LoadLocal, idx);
  pushInt(1);
  emit(Op::AddI);
  emit(Op::StoreLocal, idx);
  emit(Op::Jmp, 0, static_cast<int>(top));
  patch(jf, here());
  for (std::size_t b : lc.breaks) patch(b, here());
  g_->nextSlot = saved;
}

void CodeGen::genSwitch(Stmt& s) {
  const int saved = g_->nextSlot;
  const Type* st = s.exprs[0]->type;
  const int tmp = allocSlot();
  expr(*s.exprs[0]);
  emit(Op::StoreLocal, tmp);
  std::vector<std::vector<std::size_t>> caseJumps(s.cases.size());
  std::size_t defaultIdx = s.cases.size();
  for (std::size_t i = 0; i < s.cases.size(); ++i) {
    SwitchCase& c = s.cases[i];
    if (c.isDefault) defaultIdx = i;
    for (auto& l : c.labels) {
      emit(Op::LoadLocal, tmp);
      if (st->kind == TypeKind::String) {
        emit(Op::PushConst, 0, constString(l->text));
        emit(Op::EqStr);
      } else {
        pushInt(labelValue(*l));
        emit(Op::EqI);
      }
      caseJumps[i].push_back(jump(Op::JmpIfTrue));
    }
  }
  const std::size_t toDefault = jump(Op::Jmp);
  g_->loops.push_back({{}, {}, static_cast<int>(g_->tries.size()), true});
  std::vector<std::size_t> ends;
  std::size_t defaultPc = 0;
  bool haveDefault = false;
  for (std::size_t i = 0; i < s.cases.size(); ++i) {
    SwitchCase& c = s.cases[i];
    const std::size_t pc = here();
    for (std::size_t j : caseJumps[i]) patch(j, pc);
    if (i == defaultIdx) { defaultPc = pc; haveDefault = true; }
    const int scopeSlots = g_->nextSlot;
    for (auto& b : c.body) stmt(*b);
    g_->nextSlot = scopeSlots;
    ends.push_back(jump(Op::Jmp));
  }
  LoopCtx lc = std::move(g_->loops.back());
  g_->loops.pop_back();
  const std::size_t endPc = here();
  patch(toDefault, haveDefault ? defaultPc : endPc);
  for (std::size_t e : ends) patch(e, endPc);
  for (std::size_t b : lc.breaks) patch(b, endPc);
  g_->nextSlot = saved;
}

void CodeGen::closeSegments(TryCtx& t) {
  const auto pc = static_cast<std::uint32_t>(here());
  t.catchWasOpen = t.catchOpen;
  t.finWasOpen = t.finOpen;
  if (t.catchOpen) { t.catchRanges.push_back({t.catchStart, pc}); t.catchOpen = false; }
  if (t.finOpen) { t.finRanges.push_back({t.finStart, pc}); t.finOpen = false; }
}

void CodeGen::reopen(TryCtx& t) {
  const auto pc = static_cast<std::uint32_t>(here());
  if (t.catchWasOpen) { t.catchStart = pc; t.catchOpen = true; }
  if (t.finWasOpen) { t.finStart = pc; t.finOpen = true; }
  t.catchWasOpen = t.finWasOpen = false;
}

void CodeGen::exitThrough(int targetDepth) {
  for (int i = static_cast<int>(g_->tries.size()) - 1; i >= targetDepth; --i) {
    closeSegments(g_->tries[static_cast<std::size_t>(i)]);
    Stmt* fin = g_->tries[static_cast<std::size_t>(i)].fin;
    if (fin != nullptr) stmt(*fin);
  }
}

void CodeGen::reopenSegments(int targetDepth) {
  for (int i = targetDepth; i < static_cast<int>(g_->tries.size()); ++i) reopen(g_->tries[static_cast<std::size_t>(i)]);
}

void CodeGen::genBreakContinue(bool isBreak) {
  // The innermost loop (or switch, for break) is the target.
  int target = -1;
  for (int i = static_cast<int>(g_->loops.size()) - 1; i >= 0; --i) {
    if (isBreak || !g_->loops[static_cast<std::size_t>(i)].isSwitch) { target = i; break; }
  }
  MINGOL_VERIFY(target >= 0);
  const int depth = g_->loops[static_cast<std::size_t>(target)].tryDepth;
  exitThrough(depth);
  const std::size_t j = jump(Op::Jmp);
  auto& lc = g_->loops[static_cast<std::size_t>(target)];
  (isBreak ? lc.breaks : lc.continues).push_back(j);
  reopenSegments(depth);
}

void CodeGen::genReturn(Stmt& s) {
  const Type* ret = g_->info->ret;
  const bool hasFinally = [&] {
    for (auto& t : g_->tries) if (t.fin != nullptr) return true;
    return false;
  }();
  if (s.exprs.empty() || g_->info->isCoroutine) {
    emit(Op::PushNull);
  } else if (ret != nullptr && ret->kind != TypeKind::Void) {
    exprConv(*s.exprs[0], ret);
  } else {
    expr(*s.exprs[0]);
  }
  if (hasFinally) {
    const int tmp = allocSlot();
    emit(Op::StoreLocal, tmp);
    exitThrough(0);
    emit(Op::LoadLocal, tmp);
    emit(Op::Return);
    reopenSegments(0);
  } else {
    emit(Op::Return);
  }
}

void CodeGen::genTry(Stmt& s) {
  const std::size_t idx = g_->tries.size();
  const bool hasFinally = s.stmts.size() > 1;
  const bool hasCatch = !s.catches.empty();
  g_->tries.emplace_back();
  {
    TryCtx& t = g_->tries[idx];
    t.fin = hasFinally ? s.stmts[1].get() : nullptr;
    t.catchStart = t.finStart = static_cast<std::uint32_t>(here());
    t.catchOpen = hasCatch;
    t.finOpen = hasFinally;
  }
  stmt(*s.stmts[0]);
  {
    TryCtx& t = g_->tries[idx];
    const auto pc = static_cast<std::uint32_t>(here());
    if (t.catchOpen) { t.catchRanges.push_back({t.catchStart, pc}); t.catchOpen = false; }
    if (t.finOpen) { t.finRanges.push_back({t.finStart, pc}); t.finOpen = false; }
  }
  std::vector<std::size_t> toEnd;
  // Normal completion of the body: run the finally block (unprotected by this try) and leave.
  auto inlineFinally = [&]() {
    if (hasFinally) stmt(*s.stmts[1]);
    toEnd.push_back(jump(Op::Jmp));
  };
  inlineFinally();

  struct CatchEntry { std::uint32_t pc; std::int32_t cls; std::uint16_t slot; };
  std::vector<CatchEntry> entries;
  for (auto& c : s.catches) {
    VarSym* v = static_cast<VarSym*>(c.sym);
    const int saved = g_->nextSlot;
    declareLocal(v);
    const auto pc = static_cast<std::uint32_t>(here());
    g_->depth = 0;
    entries.push_back({pc, v->type->cls->id, static_cast<std::uint16_t>(v->slot)});
    {
      TryCtx& t = g_->tries[idx];
      if (hasFinally) { t.finStart = pc; t.finOpen = true; }
    }
    line(c.loc);
    if (v->boxed()) emit(Op::BoxLocal, v->slot);
    stmt(*c.body);
    {
      TryCtx& t = g_->tries[idx];
      if (t.finOpen) { t.finRanges.push_back({t.finStart, static_cast<std::uint32_t>(here())}); t.finOpen = false; }
    }
    g_->nextSlot = saved;
    inlineFinally();
  }

  TryCtx ctx = std::move(g_->tries[idx]);
  g_->tries.pop_back();

  for (std::size_t i = 0; i < entries.size(); ++i) {
    for (const Range& r : ctx.catchRanges) {
      if (r.s == r.e) continue;
      g_->handlers.push_back({r.s, r.e, entries[i].pc, entries[i].cls, entries[i].slot});
    }
  }
  if (hasFinally) {
    const int saved = g_->nextSlot;
    const int tmp = allocSlot();
    const auto pc = static_cast<std::uint32_t>(here());
    g_->depth = 0;
    for (const Range& r : ctx.finRanges) {
      if (r.s == r.e) continue;
      g_->handlers.push_back({r.s, r.e, pc, -1, static_cast<std::uint16_t>(tmp)});
    }
    stmt(*s.stmts[1]);
    emit(Op::LoadLocal, tmp);
    emit(Op::Throw);
    g_->nextSlot = saved;
  }
  for (std::size_t j : toEnd) patch(j, here());
}

}  // namespace mingol::script
