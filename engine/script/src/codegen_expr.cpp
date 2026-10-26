// SPDX-License-Identifier: GPL-2.0-only
// Code generation: expressions, lvalues, calls.
#include "codegen.hpp"

namespace mingol::script {
namespace {
constexpr int R(Res r) { return static_cast<int>(r); }
}  // namespace

VarSym* CodeGen::findThis() const {
  for (auto& v : g_->info->vars) {
    if (v->name == "this") return v.get();
  }
  for (VarSym* v : g_->info->captures) {
    if (v->name == "this") return v;
  }
  MINGOL_VERIFY(false);
  return nullptr;
}

std::int64_t CodeGen::labelValue(const Expr& l) const {
  if (l.aux == R(Res::EnumConst)) return static_cast<EnumConstSym*>(l.sym)->value;
  return l.ival;
}

void CodeGen::exprConv(Expr& e, const Type* to) {
  expr(e);
  coerce(e.type, to);
}

void CodeGen::discard(Expr& e) {
  if (e.kind == ExprKind::Assign) {
    genAssign(e, false);
  } else if (e.kind == ExprKind::Unary && (e.uop == UnaryOp::PreInc || e.uop == UnaryOp::PreDec || e.uop == UnaryOp::PostInc || e.uop == UnaryOp::PostDec)) {
    genIncDec(e, false);
  } else {
    expr(e);
    emit(Op::Pop);
  }
}

void CodeGen::expr(Expr& e) {
  line(e.loc);
  switch (e.kind) {
    case ExprKind::IntLit: pushInt(e.ival); break;
    case ExprKind::FloatLit: emit(Op::PushConst, 0, constFloat(e.fval)); break;
    case ExprKind::StringLit: emit(Op::PushConst, 0, constString(e.text)); break;
    case ExprKind::BoolLit: emit(e.ival != 0 ? Op::PushTrue : Op::PushFalse); break;
    case ExprKind::NullLit: emit(Op::PushNull); break;
    case ExprKind::This: loadVar(static_cast<VarSym*>(e.sym)); break;
    case ExprKind::Name: genName(e); break;
    case ExprKind::Member: genMember(e); break;
    case ExprKind::Super: loadThis(); break;
    case ExprKind::Index: genIndex(e); break;
    case ExprKind::Call: genCall(e); break;
    case ExprKind::New: genNew(e); break;
    case ExprKind::NewArray:
      exprConv(*e.kids[0], p_.types.intType());
      emit(Op::NewArray, static_cast<int>(elemKind(e.type->elem)));
      break;
    case ExprKind::ArrayLit:
      for (auto& k : e.kids) exprConv(*k, e.type->elem);
      emit(Op::ArrayLit, static_cast<int>(elemKind(e.type->elem)), static_cast<int>(e.kids.size()));
      break;
    case ExprKind::Unary: genUnary(e); break;
    case ExprKind::Binary: genBinary(e); break;
    case ExprKind::Assign: genAssign(e, true); break;
    case ExprKind::Ternary: {
      exprConv(*e.kids[0], p_.types.boolType());
      const std::size_t jf = jump(Op::JmpIfFalse);
      const int d = g_->depth;
      exprConv(*e.kids[1], e.type);
      const std::size_t je = jump(Op::Jmp);
      patch(jf, here());
      g_->depth = d;
      exprConv(*e.kids[2], e.type);
      patch(je, here());
      break;
    }
    case ExprKind::Cast: genCast(e); break;
    case ExprKind::Is:
      expr(*e.kids[0]);
      emit(Op::IsType, 0, static_cast<ClassSym*>(e.sym)->id);
      break;
    case ExprKind::Typeof: {
      const Type* ty = static_cast<const Type*>(e.extra);
      if (e.aux == 1) {
        expr(*e.kids[0]);
        emit(Op::TypeOfValue);
      } else {
        if (!e.kids.empty()) { expr(*e.kids[0]); emit(Op::Pop); }
        emit(Op::TypeOfStatic, 0, typeDesc(ty));
      }
      break;
    }
    case ExprKind::Lambda: genLambda(e); break;
  }
}

// ---- names, members ---------------------------------------------------------------------------------------

void CodeGen::genName(Expr& e) {
  switch (static_cast<Res>(e.aux)) {
    case Res::Local: loadVar(static_cast<VarSym*>(e.sym)); break;
    case Res::Field: loadThis(); emit(Op::LoadField, static_cast<FieldSym*>(e.sym)->index); break;
    case Res::StaticField: emit(Op::LoadStatic, 0, static_cast<FieldSym*>(e.sym)->index); break;
    case Res::MethodValue: {
      loadThis();
      auto* m = static_cast<MethodSym*>(e.sym);
      if (m->vtableSlot >= 0) emit(Op::MakeBoundVirtual, 0, m->vtableSlot);
      else emit(Op::MakeBound, 0, m->funcId);
      break;
    }
    case Res::StaticMethodValue: emit(Op::MakeClosure, 0, static_cast<MethodSym*>(e.sym)->funcId); break;
    default: MINGOL_VERIFY(false);
  }
}

void CodeGen::genMember(Expr& e) {
  Expr& obj = *e.kids[0];
  switch (static_cast<Res>(e.aux)) {
    case Res::Field:
      if (obj.kind == ExprKind::Super) loadThis(); else expr(obj);
      emit(Op::LoadField, static_cast<FieldSym*>(e.sym)->index);
      break;
    case Res::StaticField: emit(Op::LoadStatic, 0, static_cast<FieldSym*>(e.sym)->index); break;
    case Res::EnumConst: pushInt(static_cast<EnumConstSym*>(e.sym)->value); break;
    case Res::ArrayLength: expr(obj); emit(Op::ArrayLen); break;
    case Res::StringLength: expr(obj); emit(Op::StrLen); break;
    case Res::MethodValue: {
      expr(obj);
      auto* m = static_cast<MethodSym*>(e.sym);
      if (m->vtableSlot >= 0) emit(Op::MakeBoundVirtual, 0, m->vtableSlot);
      else emit(Op::MakeBound, 0, m->funcId);
      break;
    }
    case Res::StaticMethodValue: emit(Op::MakeClosure, 0, static_cast<MethodSym*>(e.sym)->funcId); break;
    default: MINGOL_VERIFY(false);
  }
}

void CodeGen::genIndex(Expr& e) {
  expr(*e.kids[0]);
  switch (static_cast<Res>(e.aux)) {
    case Res::ArrayIndex: exprConv(*e.kids[1], p_.types.intType()); emit(Op::LoadIndex); break;
    case Res::StringIndex: exprConv(*e.kids[1], p_.types.intType()); emit(Op::StrIndex); break;
    case Res::OperatorIndex: {
      auto* g = static_cast<MethodSym*>(e.sym);
      exprConv(*e.kids[1], g->params[1].type);
      emit(Op::Call, 2, g->funcId);
      break;
    }
    default: MINGOL_VERIFY(false);
  }
}

// ---- calls --------------------------------------------------------------------------------------------------

void CodeGen::genArgs(const std::vector<ParamInfo>& params, Expr& call, std::size_t from) {
  for (std::size_t i = 0; i < params.size(); ++i) {
    Expr& a = *call.kids[from + i];
    if (params[i].isRef) genRef(a);
    else exprConv(a, params[i].type);
  }
}

void CodeGen::genReceiver(Expr& callee) {
  if (callee.kind == ExprKind::Member) {
    Expr& obj = *callee.kids[0];
    if (obj.kind == ExprKind::Super) loadThis(); else expr(obj);
  } else {
    loadThis();  // implicit this
  }
}

void CodeGen::genCall(Expr& e) {
  Expr& callee = *e.kids[0];
  const int argc = static_cast<int>(e.kids.size()) - 1;
  switch (static_cast<Res>(e.aux)) {
    case Res::StaticCall: {
      auto* m = static_cast<MethodSym*>(e.sym);
      genArgs(m->params, e, 1);
      emit(Op::Call, argc, m->funcId);
      break;
    }
    case Res::VirtualCall: {
      auto* m = static_cast<MethodSym*>(e.sym);
      genReceiver(callee);
      genArgs(m->params, e, 1);
      emit(Op::CallVirtual, argc + 1, m->vtableSlot);
      break;
    }
    case Res::InterfaceCall: {
      auto* m = static_cast<MethodSym*>(e.sym);
      genReceiver(callee);
      genArgs(m->params, e, 1);
      emit(Op::CallInterface, argc + 1, m->interfaceKey);
      break;
    }
    case Res::SuperCall: {
      auto* m = static_cast<MethodSym*>(e.sym);
      genReceiver(callee);
      genArgs(m->params, e, 1);
      emit(Op::Call, argc + 1, m->funcId);
      break;
    }
    case Res::CoroutineCall: {
      auto* m = static_cast<MethodSym*>(e.sym);
      if (m->mods.isStatic) {
        genArgs(m->params, e, 1);
        emit(Op::StartCoroutine, argc, m->funcId);
      } else {
        genReceiver(callee);
        genArgs(m->params, e, 1);
        const bool viaSuper = callee.kind == ExprKind::Member && callee.kids[0]->kind == ExprKind::Super;
        if (m->vtableSlot >= 0 && !viaSuper) emit(Op::StartCoroutineVirtual, argc + 1, m->vtableSlot);
        else emit(Op::StartCoroutine, argc + 1, m->funcId);
      }
      break;
    }
    case Res::ClosureCall: {
      const Type* ft = callee.type;
      expr(callee);
      std::vector<ParamInfo> ps;
      for (const FuncParam& fp : ft->params) ps.push_back({"", fp.type, fp.isRef});
      genArgs(ps, e, 1);
      emit(Op::CallClosure, argc);
      break;
    }
    default: MINGOL_VERIFY(false);
  }
}

void CodeGen::genNew(Expr& e) {
  auto* ctor = static_cast<MethodSym*>(e.sym);
  emit(Op::NewObject, 0, ctor->owner->id);
  emit(Op::Dup);
  genArgs(ctor->params, e, 0);
  emit(Op::Call, static_cast<int>(ctor->params.size()) + 1, ctor->funcId);
  emit(Op::Pop);
}

void CodeGen::genLambda(Expr& e) {
  auto* li = static_cast<FuncInfo*>(e.extra);
  const std::string name = g_->name + "$lambda" + std::to_string(g_->lambdaCount++);
  genLambdaBody(*li, e, name);
  for (VarSym* v : li->captures) loadRaw(v);
  emit(Op::MakeClosure, static_cast<int>(li->captures.size()), li->funcId);
}

void CodeGen::genRef(Expr& e) {
  switch (e.kind) {
    case ExprKind::Name:
      switch (static_cast<Res>(e.aux)) {
        case Res::Local: {
          auto* v = static_cast<VarSym*>(e.sym);
          loadRaw(v);
          if (!v->isRefParam) emit(Op::MakeRefBox);
          break;
        }
        case Res::Field: loadThis(); emit(Op::MakeRefField, static_cast<FieldSym*>(e.sym)->index); break;
        case Res::StaticField: emit(Op::MakeRefStatic, 0, static_cast<FieldSym*>(e.sym)->index); break;
        default: MINGOL_VERIFY(false);
      }
      break;
    case ExprKind::Member:
      if (e.aux == R(Res::StaticField)) {
        emit(Op::MakeRefStatic, 0, static_cast<FieldSym*>(e.sym)->index);
      } else {
        if (e.kids[0]->kind == ExprKind::Super) loadThis(); else expr(*e.kids[0]);
        emit(Op::MakeRefField, static_cast<FieldSym*>(e.sym)->index);
      }
      break;
    case ExprKind::Index:
      expr(*e.kids[0]);
      exprConv(*e.kids[1], p_.types.intType());
      emit(Op::MakeRefElem);
      break;
    case ExprKind::Unary:
      MINGOL_VERIFY(e.uop == UnaryOp::Deref);
      expr(*e.kids[0]);
      break;
    default: MINGOL_VERIFY(false);
  }
}

// ---- operators ------------------------------------------------------------------------------------------------

void CodeGen::binaryOp(Tok op, const Type* common) {
  const bool isF = common->kind == TypeKind::Float;
  const bool isS = common->kind == TypeKind::String;
  const bool isRef = common->isRefType();
  switch (op) {
    case Tok::Plus: emit(isF ? Op::AddF : Op::AddI); break;
    case Tok::Minus: emit(isF ? Op::SubF : Op::SubI); break;
    case Tok::Star: emit(isF ? Op::MulF : Op::MulI); break;
    case Tok::Slash: emit(isF ? Op::DivF : Op::DivI); break;
    case Tok::Percent: emit(isF ? Op::ModF : Op::ModI); break;
    case Tok::Amp: emit(Op::BitAnd); break;
    case Tok::Pipe: emit(Op::BitOr); break;
    case Tok::Caret: emit(Op::BitXor); break;
    case Tok::Shl: emit(Op::Shl); break;
    case Tok::Shr: emit(Op::Shr); break;
    case Tok::Lt: emit(isS ? Op::LtStr : isF ? Op::LtF : Op::LtI); break;
    case Tok::LtEq: emit(isS ? Op::LeStr : isF ? Op::LeF : Op::LeI); break;
    case Tok::Gt: emit(isS ? Op::GtStr : isF ? Op::GtF : Op::GtI); break;
    case Tok::GtEq: emit(isS ? Op::GeStr : isF ? Op::GeF : Op::GeI); break;
    case Tok::EqEq: emit(isS ? Op::EqStr : isF ? Op::EqF : isRef ? Op::EqRef : Op::EqI); break;
    case Tok::NotEq: emit(isS ? Op::NeStr : isF ? Op::NeF : isRef ? Op::NeRef : Op::NeI); break;
    default: MINGOL_VERIFY(false);
  }
}

void CodeGen::genUserCall(MethodSym& m) { emit(Op::Call, static_cast<int>(m.params.size()), m.funcId); }

void CodeGen::genUnary(Expr& e) {
  Expr& k = *e.kids[0];
  switch (e.uop) {
    case UnaryOp::AddrOf: genRef(k); return;
    case UnaryOp::Deref: expr(k); emit(Op::LoadRef); return;
    case UnaryOp::PreInc: case UnaryOp::PreDec: case UnaryOp::PostInc: case UnaryOp::PostDec: genIncDec(e, true); return;
    default: break;
  }
  if (e.aux == R(Res::Operator)) {
    auto* m = static_cast<MethodSym*>(e.sym);
    exprConv(k, m->params[0].type);
    genUserCall(*m);
    return;
  }
  expr(k);
  switch (e.uop) {
    case UnaryOp::Neg: emit(e.type->kind == TypeKind::Float ? Op::NegF : Op::NegI); break;
    case UnaryOp::Not: emit(Op::Not); break;
    case UnaryOp::BitNot: emit(Op::BitNot); break;
    default: break;  // unary plus
  }
}

void CodeGen::genBinary(Expr& e) {
  Expr& a = *e.kids[0];
  Expr& b = *e.kids[1];
  if (e.op == Tok::AmpAmp || e.op == Tok::PipePipe) {
    const bool isAnd = e.op == Tok::AmpAmp;
    exprConv(a, p_.types.boolType());
    const std::size_t j = jump(isAnd ? Op::JmpIfFalse : Op::JmpIfTrue);
    const int d = g_->depth;
    exprConv(b, p_.types.boolType());
    const std::size_t je = jump(Op::Jmp);
    patch(j, here());
    g_->depth = d;
    emit(isAnd ? Op::PushFalse : Op::PushTrue);
    patch(je, here());
    return;
  }
  if (e.aux == R(Res::Operator)) {
    auto* m = static_cast<MethodSym*>(e.sym);
    exprConv(a, m->params[0].type);
    exprConv(b, m->params[1].type);
    genUserCall(*m);
    if (e.ival == 1) emit(Op::Not);  // != derived from a user ==
    return;
  }
  const Type* ta = a.type;
  const Type* tb = b.type;
  if (e.op == Tok::Plus && (ta->kind == TypeKind::String || tb->kind == TypeKind::String)) {
    expr(a);
    toStr(ta);
    expr(b);
    toStr(tb);
    emit(Op::StrConcat);
    return;
  }
  const bool numeric = ta->isNumeric() && tb->isNumeric();
  const Type* common = numeric ? ((ta->kind == TypeKind::Float || tb->kind == TypeKind::Float) ? p_.types.floatType() : p_.types.intType()) : ta;
  if (e.op == Tok::Shl || e.op == Tok::Shr) common = p_.types.intType();
  if (!numeric && ta->kind == TypeKind::Null) common = tb;
  exprConv(a, common);
  exprConv(b, common);
  binaryOp(e.op, common);
}

void CodeGen::genCast(Expr& e) {
  Expr& k = *e.kids[0];
  switch (static_cast<Res>(e.aux)) {
    case Res::UpCast: case Res::EnumToInt: expr(k); break;
    case Res::NumConv:
      expr(k);
      if (k.type->kind == TypeKind::Int && e.type->kind == TypeKind::Float) emit(Op::I2F);
      else if (k.type->kind == TypeKind::Float && e.type->kind == TypeKind::Int) emit(Op::F2I);
      break;
    case Res::ToString: expr(k); toStr(k.type); break;
    case Res::IntToEnum: expr(k); emit(Op::EnumFromInt, 0, e.type->cls->id); break;
    case Res::RefCast: expr(k); emit(Op::CastType, 0, static_cast<ClassSym*>(e.sym)->id); break;
    default: MINGOL_VERIFY(false);
  }
}

// ---- lvalues and assignment ----------------------------------------------------------------------------------

CodeGen::LK CodeGen::lvKind(Expr& t) const {
  switch (t.kind) {
    case ExprKind::Name:
      if (t.aux == R(Res::Local)) return static_cast<VarSym*>(t.sym)->isRefParam ? LK::VarRef : LK::Var;
      return t.aux == R(Res::Field) ? LK::Field : LK::Static;
    case ExprKind::Member: return t.aux == R(Res::Field) ? LK::Field : LK::Static;
    case ExprKind::Index: return t.aux == R(Res::ArrayIndex) ? LK::Elem : LK::OpIndex;
    case ExprKind::Unary: return LK::Deref;
    default: MINGOL_VERIFY(false); return LK::Var;
  }
}

void CodeGen::lvPrefix(Expr& t, LK k) {
  switch (k) {
    case LK::Var: case LK::Static: break;
    case LK::VarRef: loadRaw(static_cast<VarSym*>(t.sym)); break;
    case LK::Field:
      if (t.kind == ExprKind::Name) loadThis();
      else if (t.kids[0]->kind == ExprKind::Super) loadThis();
      else expr(*t.kids[0]);
      break;
    case LK::Elem:
      expr(*t.kids[0]);
      exprConv(*t.kids[1], p_.types.intType());
      break;
    case LK::OpIndex:
      expr(*t.kids[0]);
      exprConv(*t.kids[1], static_cast<MethodSym*>(t.sym)->params[1].type);
      break;
    case LK::Deref: expr(*t.kids[0]); break;
  }
}

void CodeGen::lvDup(LK k) {
  switch (k) {
    case LK::Var: case LK::Static: break;
    case LK::VarRef: case LK::Field: case LK::Deref: emit(Op::Dup); break;
    case LK::Elem: case LK::OpIndex: emit(Op::Dup2); break;
  }
}

void CodeGen::lvLoad(Expr& t, LK k) {
  switch (k) {
    case LK::Var: loadVar(static_cast<VarSym*>(t.sym)); break;
    case LK::VarRef: case LK::Deref: emit(Op::LoadRef); break;
    case LK::Field: emit(Op::LoadField, static_cast<FieldSym*>(t.sym)->index); break;
    case LK::Static: emit(Op::LoadStatic, 0, static_cast<FieldSym*>(t.sym)->index); break;
    case LK::Elem: emit(Op::LoadIndex); break;
    case LK::OpIndex: emit(Op::Call, 2, static_cast<MethodSym*>(t.sym)->funcId); break;
  }
}

void CodeGen::lvStore(Expr& t, LK k) {
  switch (k) {
    case LK::Var: storeVar(static_cast<VarSym*>(t.sym)); break;
    case LK::VarRef: case LK::Deref: emit(Op::StoreRef); break;
    case LK::Field: emit(Op::StoreField, static_cast<FieldSym*>(t.sym)->index); break;
    case LK::Static: emit(Op::StoreStatic, 0, static_cast<FieldSym*>(t.sym)->index); break;
    case LK::Elem: emit(Op::StoreIndex); break;
    case LK::OpIndex:
      emit(Op::Call, 3, static_cast<MethodSym*>(t.extra)->funcId);
      emit(Op::Pop);
      break;
  }
}

void CodeGen::genAssign(Expr& e, bool wantValue) {
  Expr& lhs = *e.kids[0];
  Expr& rhs = *e.kids[1];
  const LK k = lvKind(lhs);
  const Type* lt = lhs.type;
  lvPrefix(lhs, k);
  if (e.op == Tok::Assign) {
    exprConv(rhs, lt);
  } else {
    lvDup(k);
    lvLoad(lhs, k);
    if (e.aux == R(Res::Operator)) {
      auto* m = static_cast<MethodSym*>(e.sym);
      exprConv(rhs, m->params[1].type);
      genUserCall(*m);
    } else {
      Tok bop = Tok::End;
      switch (e.op) {
        case Tok::PlusAssign: bop = Tok::Plus; break;
        case Tok::MinusAssign: bop = Tok::Minus; break;
        case Tok::StarAssign: bop = Tok::Star; break;
        case Tok::SlashAssign: bop = Tok::Slash; break;
        case Tok::PercentAssign: bop = Tok::Percent; break;
        case Tok::AmpAssign: bop = Tok::Amp; break;
        case Tok::PipeAssign: bop = Tok::Pipe; break;
        case Tok::CaretAssign: bop = Tok::Caret; break;
        case Tok::ShlAssign: bop = Tok::Shl; break;
        case Tok::ShrAssign: bop = Tok::Shr; break;
        default: MINGOL_VERIFY(false);
      }
      if (bop == Tok::Plus && lt->kind == TypeKind::String) {
        expr(rhs);
        toStr(rhs.type);
        emit(Op::StrConcat);
      } else {
        exprConv(rhs, lt->kind == TypeKind::Float ? p_.types.floatType() : lt);
        binaryOp(bop, lt);
      }
    }
  }
  int tmp = -1;
  if (wantValue) {
    tmp = allocSlot();
    emit(Op::Dup);
    emit(Op::StoreLocal, tmp);
  }
  lvStore(lhs, k);
  if (wantValue) emit(Op::LoadLocal, tmp);
}

void CodeGen::genIncDec(Expr& e, bool wantValue) {
  Expr& t = *e.kids[0];
  const LK k = lvKind(t);
  const bool isPost = e.uop == UnaryOp::PostInc || e.uop == UnaryOp::PostDec;
  const bool isInc = e.uop == UnaryOp::PreInc || e.uop == UnaryOp::PostInc;
  const bool isF = t.type->kind == TypeKind::Float;
  lvPrefix(t, k);
  lvDup(k);
  lvLoad(t, k);
  int tmp = -1;
  if (wantValue) tmp = allocSlot();
  if (wantValue && isPost) { emit(Op::Dup); emit(Op::StoreLocal, tmp); }
  if (isF) emit(Op::PushConst, 0, constFloat(1.0)); else pushInt(1);
  emit(isInc ? (isF ? Op::AddF : Op::AddI) : (isF ? Op::SubF : Op::SubI));
  if (wantValue && !isPost) { emit(Op::Dup); emit(Op::StoreLocal, tmp); }
  lvStore(t, k);
  if (wantValue) emit(Op::LoadLocal, tmp);
}

}  // namespace mingol::script
