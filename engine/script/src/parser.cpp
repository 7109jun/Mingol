// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/script/parser.hpp"

#include <utility>

#include "mingol/script/lexer.hpp"

namespace mingol::script {
namespace {

struct SyntaxError {};  // thrown after the error was reported; caught at recovery points only

constexpr int kMaxDepth = 256;
constexpr int kMaxChain = 1000;  // operators in one flat expression / postfix chain (tree depth is bounded for every later pass)

bool isPrim(Tok t) {
  return t == Tok::KwInt || t == Tok::KwFloat || t == Tok::KwBool || t == Tok::KwString || t == Tok::KwVoid;
}

bool isAssignOp(Tok t) {
  switch (t) {
    case Tok::Assign: case Tok::PlusAssign: case Tok::MinusAssign: case Tok::StarAssign: case Tok::SlashAssign:
    case Tok::PercentAssign: case Tok::AmpAssign: case Tok::PipeAssign: case Tok::CaretAssign: case Tok::ShlAssign:
    case Tok::ShrAssign:
      return true;
    default: return false;
  }
}

class Parser {
 public:
  Parser(std::vector<Token> toks, Diagnostics& d) : t_(std::move(toks)), diags_(d) {}

  std::unique_ptr<CompilationUnit> run(std::uint32_t file) {
    auto unit = std::make_unique<CompilationUnit>();
    unit->file = file;
    try {
      if (at(Tok::KwModule)) {
        unit->moduleLoc = cur().loc;
        ++p_;
        unit->moduleName = qualifiedName();
        expect(Tok::Semi);
      }
    } catch (const SyntaxError&) { syncTopLevel(); }
    while (at(Tok::KwImport)) {
      try {
        Import im;
        im.loc = cur().loc;
        ++p_;
        im.path = qualifiedName();
        expect(Tok::Semi);
        unit->imports.push_back(std::move(im));
      } catch (const SyntaxError&) { syncTopLevel(); }
    }
    while (!at(Tok::End)) {
      const std::size_t before = p_;
      try {
        unit->types.push_back(typeDecl());
      } catch (const SyntaxError&) {
        syncTopLevel();
      }
      if (p_ == before) ++p_;  // guarantee progress
    }
    return unit;
  }

 private:
  std::vector<Token> t_;
  Diagnostics& diags_;
  std::size_t p_ = 0;
  int depth_ = 0;
  std::string currentType_;

  // ---- token helpers ----
  const Token& cur() const { return t_[p_]; }
  const Token& peekTok(std::size_t k) const { return t_[std::min(p_ + k, t_.size() - 1)]; }
  bool at(Tok k) const { return cur().kind == k; }
  bool atAt(std::size_t k, Tok kind) const { return peekTok(k).kind == kind; }
  bool accept(Tok k) {
    if (at(k)) { ++p_; return true; }
    return false;
  }
  [[noreturn]] void fail(SourceLoc l, std::string msg) {
    diags_.error(l, std::move(msg));
    throw SyntaxError{};
  }
  [[noreturn]] void unexpected(const char* what) {
    std::string got = tokName(cur().kind);
    if (cur().kind == Tok::Ident) got = "identifier '" + cur().text + "'";
    fail(cur().loc, std::string("expected ") + what + ", found " + got);
  }
  const Token& expect(Tok k) {
    if (!at(k)) unexpected(tokName(k));
    return t_[p_++];
  }
  std::string expectIdent(const char* what = "identifier") {
    if (!at(Tok::Ident)) unexpected(what);
    return t_[p_++].text;
  }
  std::vector<std::string> qualifiedName() {
    std::vector<std::string> path;
    path.push_back(expectIdent());
    while (at(Tok::Dot) && atAt(1, Tok::Ident)) {
      ++p_;
      path.push_back(t_[p_++].text);
    }
    return path;
  }

  struct DepthGuard {
    Parser& p;
    explicit DepthGuard(Parser& pp) : p(pp) {
      if (++p.depth_ > kMaxDepth) {
        p.depth_ = 0;  // avoid cascading reports while unwinding
        p.fail(p.cur().loc, "nesting is too deep");
      }
    }
    ~DepthGuard() { if (p.depth_ > 0) --p.depth_; }
  };

  // ---- recovery ----
  void syncTopLevel() {
    int braces = 0;
    while (!at(Tok::End)) {
      if (at(Tok::LBrace)) ++braces;
      else if (at(Tok::RBrace)) {
        if (braces > 0) --braces;
        if (braces == 0) { ++p_; return; }
      } else if (braces == 0 && (at(Tok::KwClass) || at(Tok::KwInterface) || at(Tok::KwEnum) || at(Tok::KwImport))) {
        return;
      } else if (braces == 0 && at(Tok::Semi)) {
        ++p_;
        return;
      }
      ++p_;
    }
  }
  void syncMember() {
    int braces = 0;
    while (!at(Tok::End)) {
      if (at(Tok::LBrace)) ++braces;
      else if (at(Tok::RBrace)) {
        if (braces == 0) return;  // end of the class: let the caller close it
        --braces;
        if (braces == 0) { ++p_; return; }
      } else if (braces == 0 && at(Tok::Semi)) { ++p_; return; }
      ++p_;
    }
  }
  void syncStmt() {
    while (!at(Tok::End)) {
      if (at(Tok::Semi)) { ++p_; return; }
      if (at(Tok::RBrace)) return;
      switch (cur().kind) {
        case Tok::KwIf: case Tok::KwWhile: case Tok::KwFor: case Tok::KwReturn: case Tok::KwDo: case Tok::KwSwitch:
        case Tok::KwTry: case Tok::KwThrow: case Tok::KwBreak: case Tok::KwContinue: case Tok::KwYield:
          return;
        default: ++p_;
      }
    }
  }

  // ---- types ----
  // Scans a type without consuming or reporting. Returns the index after it, or 0 when no type starts here.
  std::size_t scanType(std::size_t i) const {
    auto kind = [&](std::size_t k) { return t_[std::min(k, t_.size() - 1)].kind; };
    if (isPrim(kind(i))) {
      ++i;
    } else if (kind(i) == Tok::Ident) {
      ++i;
      while (kind(i) == Tok::Dot && kind(i + 1) == Tok::Ident) i += 2;
    } else if (kind(i) == Tok::LParen) {  // parenthesised type, e.g. (fn():int)[]
      const std::size_t n = scanType(i + 1);
      if (n == 0 || kind(n) != Tok::RParen) return 0;
      i = n + 1;
    } else if (kind(i) == Tok::KwFn) {
      ++i;
      if (kind(i) != Tok::LParen) return 0;
      ++i;
      if (kind(i) != Tok::RParen) {
        for (;;) {
          if (kind(i) == Tok::KwRef) ++i;
          const std::size_t n = scanType(i);
          if (n == 0) return 0;
          i = n;
          if (kind(i) == Tok::Comma) { ++i; continue; }
          break;
        }
      }
      if (kind(i) != Tok::RParen) return 0;
      ++i;
      if (kind(i) != Tok::Colon) return 0;
      const std::size_t n = scanType(i + 1);
      if (n == 0) return 0;
      i = n;
    } else {
      return 0;
    }
    for (;;) {
      if (kind(i) == Tok::LBracket && kind(i + 1) == Tok::RBracket) i += 2;
      else if (kind(i) == Tok::Star) ++i;
      else break;
    }
    return i;
  }

  TypeRefPtr parseType() {
    DepthGuard g(*this);
    auto ty = std::make_unique<TypeRef>();
    ty->loc = cur().loc;
    if (isPrim(cur().kind)) {
      ty->kind = TypeRef::Kind::Prim;
      ty->prim = cur().kind;
      ++p_;
    } else if (at(Tok::Ident)) {
      ty->kind = TypeRef::Kind::Named;
      ty->path = qualifiedName();
    } else if (at(Tok::LParen)) {
      ++p_;
      ty = parseType();
      expect(Tok::RParen);
    } else if (at(Tok::KwFn)) {
      ++p_;
      ty->kind = TypeRef::Kind::Func;
      expect(Tok::LParen);
      if (!at(Tok::RParen)) {
        do {
          ty->paramRef.push_back(accept(Tok::KwRef));
          ty->params.push_back(parseType());
        } while (accept(Tok::Comma));
      }
      expect(Tok::RParen);
      expect(Tok::Colon);
      ty->ret = parseType();
    } else {
      unexpected("a type");
    }
    return typeSuffixes(std::move(ty));
  }

  TypeRefPtr typeSuffixes(TypeRefPtr ty) {
    for (;;) {
      if (at(Tok::LBracket) && atAt(1, Tok::RBracket)) {
        auto a = std::make_unique<TypeRef>();
        a->kind = TypeRef::Kind::Array;
        a->loc = ty->loc;
        a->elem = std::move(ty);
        p_ += 2;
        ty = std::move(a);
      } else if (at(Tok::Star)) {
        auto a = std::make_unique<TypeRef>();
        a->kind = TypeRef::Kind::Pointer;
        a->loc = ty->loc;
        a->elem = std::move(ty);
        ++p_;
        ty = std::move(a);
      } else {
        return ty;
      }
    }
  }

  // ---- declarations ----
  Modifiers parseModifiers() {
    Modifiers m;
    for (;;) {
      const Tok k = cur().kind;
      const SourceLoc l = cur().loc;
      auto setAccess = [&](Access a) {
        if (m.explicitAccess) fail(l, "more than one access modifier");
        m.access = a;
        m.explicitAccess = true;
      };
      auto flag = [&](bool& f, const char* name) {
        if (f) fail(l, std::string("duplicate modifier '") + name + "'");
        f = true;
      };
      if (k == Tok::KwPublic) setAccess(Access::Public);
      else if (k == Tok::KwPrivate) setAccess(Access::Private);
      else if (k == Tok::KwProtected) setAccess(Access::Protected);
      else if (k == Tok::KwStatic) flag(m.isStatic, "static");
      else if (k == Tok::KwFinal) flag(m.isFinal, "final");
      else if (k == Tok::KwAbstract) flag(m.isAbstract, "abstract");
      else if (k == Tok::KwOverride) flag(m.isOverride, "override");
      else if (k == Tok::KwNative) flag(m.isNative, "native");
      else if (k == Tok::KwCoroutine) flag(m.isCoroutine, "coroutine");
      else return m;
      ++p_;
    }
  }

  TypeDeclPtr typeDecl() {
    auto d = std::make_unique<TypeDecl>();
    d->mods = parseModifiers();
    d->loc = cur().loc;
    if (at(Tok::KwClass)) d->kind = TypeDeclKind::Class;
    else if (at(Tok::KwInterface)) d->kind = TypeDeclKind::Interface;
    else if (at(Tok::KwEnum)) d->kind = TypeDeclKind::Enum;
    else unexpected("'class', 'interface' or 'enum'");
    ++p_;
    d->name = expectIdent("a type name");
    const std::string saved = currentType_;
    currentType_ = d->name;
    if (d->kind == TypeDeclKind::Class) {
      if (accept(Tok::KwExtends)) d->base = parseType();
      if (accept(Tok::KwImplements)) {
        do d->interfaces.push_back(parseType()); while (accept(Tok::Comma));
      }
    } else if (d->kind == TypeDeclKind::Interface) {
      if (accept(Tok::KwExtends)) {
        do d->interfaces.push_back(parseType()); while (accept(Tok::Comma));
      }
    }
    expect(Tok::LBrace);
    if (d->kind == TypeDeclKind::Enum) enumBody(*d);
    else classBody(*d);
    expect(Tok::RBrace);
    currentType_ = saved;
    return d;
  }

  void enumBody(TypeDecl& d) {
    while (!at(Tok::RBrace) && !at(Tok::End)) {
      try {
        Member m;
        m.kind = MemberKind::EnumConstant;
        m.loc = cur().loc;
        m.name = expectIdent("an enum constant name");
        if (accept(Tok::Assign)) m.init = expression();
        d.members.push_back(std::move(m));
        if (!accept(Tok::Comma)) {
          if (!at(Tok::RBrace)) unexpected("',' or '}'");
        }
      } catch (const SyntaxError&) {
        while (!at(Tok::End) && !at(Tok::Comma) && !at(Tok::RBrace)) ++p_;
        accept(Tok::Comma);
      }
    }
  }

  void classBody(TypeDecl& d) {
    while (!at(Tok::RBrace) && !at(Tok::End)) {
      const std::size_t before = p_;
      try {
        d.members.push_back(member(d));
      } catch (const SyntaxError&) {
        syncMember();
      }
      if (p_ == before) ++p_;
    }
  }

  static bool isOverloadable(Tok k) {
    switch (k) {
      case Tok::Plus: case Tok::Minus: case Tok::Star: case Tok::Slash: case Tok::Percent: case Tok::Amp: case Tok::Pipe:
      case Tok::Caret: case Tok::Shl: case Tok::Shr: case Tok::EqEq: case Tok::NotEq: case Tok::Lt: case Tok::Gt:
      case Tok::LtEq: case Tok::GtEq: case Tok::Bang: case Tok::Tilde:
        return true;
      default: return false;
    }
  }

  std::vector<Param> paramList(bool allowUntyped) {
    std::vector<Param> ps;
    expect(Tok::LParen);
    if (!at(Tok::RParen)) {
      do {
        Param pr;
        pr.loc = cur().loc;
        pr.isRef = accept(Tok::KwRef);
        if (allowUntyped && at(Tok::Ident) && (atAt(1, Tok::Comma) || atAt(1, Tok::RParen))) {
          pr.name = t_[p_++].text;
        } else {
          pr.type = parseType();
          pr.name = expectIdent("a parameter name");
        }
        ps.push_back(std::move(pr));
      } while (accept(Tok::Comma));
    }
    expect(Tok::RParen);
    return ps;
  }

  Member member(TypeDecl& owner) {
    Member m;
    m.mods = parseModifiers();
    m.loc = cur().loc;
    // Constructor: Name '('
    if (at(Tok::Ident) && cur().text == owner.name && atAt(1, Tok::LParen)) {
      if (owner.kind != TypeDeclKind::Class) fail(cur().loc, "only classes have constructors");
      ++p_;
      m.kind = MemberKind::Constructor;
      m.name = "<init>";
      m.params = paramList(false);
      m.body = block();
      return m;
    }
    m.type = parseType();
    if (at(Tok::KwOperator)) {
      ++p_;
      m.kind = MemberKind::Operator;
      if (at(Tok::LBracket) && atAt(1, Tok::RBracket)) {
        m.opToken = Tok::LBracket;
        p_ += 2;
        m.name = "operator[]";
      } else if (isOverloadable(cur().kind)) {
        m.opToken = cur().kind;
        m.name = std::string("operator") + tokName(cur().kind);  // e.g. operator'+'
        ++p_;
      } else {
        unexpected("an overloadable operator");
      }
      m.params = paramList(false);
      m.body = block();
      return m;
    }
    m.name = expectIdent("a member name");
    if (at(Tok::LParen)) {
      m.kind = MemberKind::Method;
      m.params = paramList(false);
      if (at(Tok::LBrace)) {
        m.body = block();
      } else {
        expect(Tok::Semi);  // abstract / native / interface method
      }
      return m;
    }
    m.kind = MemberKind::Field;
    if (accept(Tok::Assign)) m.init = expression();
    expect(Tok::Semi);
    return m;
  }

  // ---- statements ----
  StmtPtr newStmt(StmtKind k, SourceLoc l) {
    auto s = std::make_unique<Stmt>();
    s->kind = k;
    s->loc = l;
    return s;
  }

  StmtPtr block() {
    DepthGuard g(*this);
    auto b = newStmt(StmtKind::Block, cur().loc);
    expect(Tok::LBrace);
    while (!at(Tok::RBrace) && !at(Tok::End)) {
      const std::size_t before = p_;
      try {
        b->stmts.push_back(statement());
      } catch (const SyntaxError&) {
        syncStmt();
      }
      if (p_ == before) ++p_;
    }
    b->endLoc = cur().loc;
    expect(Tok::RBrace);
    return b;
  }

  bool startsDeclaration() const {
    if (at(Tok::KwVar)) return atAt(1, Tok::Ident);
    const std::size_t n = scanType(p_);
    return n != 0 && t_[std::min(n, t_.size() - 1)].kind == Tok::Ident;
  }

  StmtPtr varDecl(bool needSemi) {
    auto s = newStmt(StmtKind::VarDecl, cur().loc);
    if (accept(Tok::KwVar)) {
      s->declType = nullptr;
    } else {
      s->declType = parseType();
      if (s->declType->kind == TypeRef::Kind::Prim && s->declType->prim == Tok::KwVoid) fail(s->loc, "a variable cannot have type 'void'");
    }
    s->name = expectIdent("a variable name");
    if (accept(Tok::Assign)) s->exprs.push_back(expression());
    if (at(Tok::Comma)) fail(cur().loc, "declare one variable per statement");
    if (needSemi) expect(Tok::Semi);
    return s;
  }

  StmtPtr statement() {
    DepthGuard g(*this);
    const SourceLoc l = cur().loc;
    switch (cur().kind) {
      case Tok::LBrace: return block();
      case Tok::Semi: { ++p_; return newStmt(StmtKind::Block, l); }  // empty statement
      case Tok::KwIf: {
        ++p_;
        auto s = newStmt(StmtKind::If, l);
        expect(Tok::LParen);
        s->exprs.push_back(expression());
        expect(Tok::RParen);
        s->stmts.push_back(statement());
        if (accept(Tok::KwElse)) s->stmts.push_back(statement());
        return s;
      }
      case Tok::KwWhile: {
        ++p_;
        auto s = newStmt(StmtKind::While, l);
        expect(Tok::LParen);
        s->exprs.push_back(expression());
        expect(Tok::RParen);
        s->stmts.push_back(statement());
        return s;
      }
      case Tok::KwDo: {
        ++p_;
        auto s = newStmt(StmtKind::DoWhile, l);
        s->stmts.push_back(statement());
        expect(Tok::KwWhile);
        expect(Tok::LParen);
        s->exprs.push_back(expression());
        expect(Tok::RParen);
        expect(Tok::Semi);
        return s;
      }
      case Tok::KwFor: return forStatement();
      case Tok::KwSwitch: return switchStatement();
      case Tok::KwBreak: { ++p_; expect(Tok::Semi); return newStmt(StmtKind::Break, l); }
      case Tok::KwContinue: { ++p_; expect(Tok::Semi); return newStmt(StmtKind::Continue, l); }
      case Tok::KwReturn: {
        ++p_;
        auto s = newStmt(StmtKind::Return, l);
        if (!at(Tok::Semi)) s->exprs.push_back(expression());
        expect(Tok::Semi);
        return s;
      }
      case Tok::KwThrow: {
        ++p_;
        auto s = newStmt(StmtKind::Throw, l);
        s->exprs.push_back(expression());
        expect(Tok::Semi);
        return s;
      }
      case Tok::KwYield: {
        ++p_;
        auto s = newStmt(StmtKind::Yield, l);
        if (!at(Tok::Semi)) s->exprs.push_back(expression());
        expect(Tok::Semi);
        return s;
      }
      case Tok::KwTry: return tryStatement();
      default: break;
    }
    if (startsDeclaration()) return varDecl(true);
    auto s = newStmt(StmtKind::ExprStmt, l);
    s->exprs.push_back(expression());
    expect(Tok::Semi);
    return s;
  }

  StmtPtr forStatement() {
    const SourceLoc l = cur().loc;
    ++p_;
    expect(Tok::LParen);
    // for-each: ( [var | Type] name : expr )
    if (startsDeclaration()) {
      const std::size_t save = p_;
      TypeRefPtr ty;
      if (!accept(Tok::KwVar)) ty = parseType();
      if (at(Tok::Ident) && atAt(1, Tok::Colon)) {
        auto s = newStmt(StmtKind::ForEach, l);
        s->declType = std::move(ty);
        s->name = t_[p_++].text;
        ++p_;  // ':'
        s->exprs.push_back(expression());
        expect(Tok::RParen);
        s->stmts.push_back(statement());
        return s;
      }
      p_ = save;
    }
    auto s = newStmt(StmtKind::For, l);
    // init
    if (at(Tok::Semi)) {
      s->stmts.push_back(nullptr);
      ++p_;
    } else if (startsDeclaration()) {
      s->stmts.push_back(varDecl(true));
    } else {
      auto e = newStmt(StmtKind::ExprStmt, cur().loc);
      e->exprs.push_back(expression());
      expect(Tok::Semi);
      s->stmts.push_back(std::move(e));
    }
    // cond
    s->exprs.push_back(at(Tok::Semi) ? nullptr : expression());
    expect(Tok::Semi);
    // step
    s->exprs.push_back(at(Tok::RParen) ? nullptr : expression());
    expect(Tok::RParen);
    s->stmts.push_back(statement());
    return s;
  }

  StmtPtr switchStatement() {
    auto s = newStmt(StmtKind::Switch, cur().loc);
    ++p_;
    expect(Tok::LParen);
    s->exprs.push_back(expression());
    expect(Tok::RParen);
    expect(Tok::LBrace);
    bool sawDefault = false;
    while (!at(Tok::RBrace) && !at(Tok::End)) {
      SwitchCase c;
      c.loc = cur().loc;
      if (accept(Tok::KwDefault)) {
        if (sawDefault) fail(c.loc, "more than one 'default' label");
        sawDefault = true;
        c.isDefault = true;
        expect(Tok::Colon);
      } else if (accept(Tok::KwCase)) {
        do c.labels.push_back(expression()); while (accept(Tok::Comma));
        expect(Tok::Colon);
      } else {
        unexpected("'case' or 'default'");
      }
      while (!at(Tok::KwCase) && !at(Tok::KwDefault) && !at(Tok::RBrace) && !at(Tok::End)) {
        const std::size_t before = p_;
        try {
          c.body.push_back(statement());
        } catch (const SyntaxError&) {
          syncStmt();
        }
        if (p_ == before) ++p_;
      }
      s->cases.push_back(std::move(c));
    }
    s->endLoc = cur().loc;
    expect(Tok::RBrace);
    return s;
  }

  StmtPtr tryStatement() {
    auto s = newStmt(StmtKind::Try, cur().loc);
    ++p_;
    s->stmts.push_back(block());
    while (at(Tok::KwCatch)) {
      CatchClause c;
      c.loc = cur().loc;
      ++p_;
      expect(Tok::LParen);
      c.type = parseType();
      c.name = expectIdent("a variable name");
      expect(Tok::RParen);
      c.body = block();
      s->catches.push_back(std::move(c));
    }
    if (accept(Tok::KwFinally)) s->stmts.push_back(block());
    if (s->catches.empty() && s->stmts.size() == 1) fail(s->loc, "'try' needs at least one 'catch' or a 'finally'");
    return s;
  }

  // ---- expressions ----
  ExprPtr newExpr(ExprKind k, SourceLoc l) {
    auto e = std::make_unique<Expr>();
    e->kind = k;
    e->loc = l;
    return e;
  }

  ExprPtr expression() { return assignment(); }

  ExprPtr assignment() {
    DepthGuard g(*this);
    ExprPtr lhs = ternary();
    if (isAssignOp(cur().kind)) {
      auto e = newExpr(ExprKind::Assign, cur().loc);
      e->op = cur().kind;
      ++p_;
      e->kids.push_back(std::move(lhs));
      e->kids.push_back(assignment());  // right associative
      return e;
    }
    return lhs;
  }

  ExprPtr ternary() {
    ExprPtr c = binary(0);
    if (at(Tok::Question)) {
      auto e = newExpr(ExprKind::Ternary, cur().loc);
      ++p_;
      e->kids.push_back(std::move(c));
      e->kids.push_back(assignment());
      expect(Tok::Colon);
      e->kids.push_back(ternary());
      return e;
    }
    return c;
  }

  // Binary precedence levels, lowest first. `is` / `as` live on the relational level.
  static int precedence(Tok k) {
    switch (k) {
      case Tok::PipePipe: return 1;
      case Tok::AmpAmp: return 2;
      case Tok::Pipe: return 3;
      case Tok::Caret: return 4;
      case Tok::Amp: return 5;
      case Tok::EqEq: case Tok::NotEq: return 6;
      case Tok::Lt: case Tok::Gt: case Tok::LtEq: case Tok::GtEq: case Tok::KwIs: case Tok::KwAs: return 7;
      case Tok::Shl: case Tok::Shr: return 8;
      case Tok::Plus: case Tok::Minus: return 9;
      case Tok::Star: case Tok::Slash: case Tok::Percent: return 10;
      default: return 0;
    }
  }

  ExprPtr binary(int minPrec) {
    ExprPtr lhs = unary();
    int chain = 0;
    for (;;) {
      const Tok k = cur().kind;
      const int prec = precedence(k);
      if (prec == 0 || prec <= minPrec) break;
      const SourceLoc l = cur().loc;
      if (++chain > kMaxChain) fail(l, "expression has too many chained operators; split it into parts");
      ++p_;
      if (k == Tok::KwIs || k == Tok::KwAs) {
        auto e = newExpr(k == Tok::KwIs ? ExprKind::Is : ExprKind::Cast, l);
        e->kids.push_back(std::move(lhs));
        e->typeRef = parseType();
        lhs = std::move(e);
        continue;
      }
      ExprPtr rhs = binary(prec);
      auto e = newExpr(ExprKind::Binary, l);
      e->op = k;
      e->kids.push_back(std::move(lhs));
      e->kids.push_back(std::move(rhs));
      lhs = std::move(e);
    }
    return lhs;
  }

  ExprPtr unary() {
    DepthGuard g(*this);
    const SourceLoc l = cur().loc;
    auto mk = [&](UnaryOp op) {
      ++p_;
      auto e = newExpr(ExprKind::Unary, l);
      e->uop = op;
      e->kids.push_back(unary());
      return e;
    };
    switch (cur().kind) {
      case Tok::Minus:
        // -9223372036854775808 is the one literal that only fits with its sign.
        if (atAt(1, Tok::IntLit) && peekTok(1).ival == INT64_MIN && !isPrefixed(peekTok(1).text)) {
          auto e = newExpr(ExprKind::IntLit, l);
          e->ival = INT64_MIN;
          p_ += 2;
          return postfix(std::move(e));
        }
        return mk(UnaryOp::Neg);
      case Tok::Plus: return mk(UnaryOp::Plus);
      case Tok::Bang: return mk(UnaryOp::Not);
      case Tok::Tilde: return mk(UnaryOp::BitNot);
      case Tok::Amp: return mk(UnaryOp::AddrOf);
      case Tok::Star: return mk(UnaryOp::Deref);
      case Tok::PlusPlus: return mk(UnaryOp::PreInc);
      case Tok::MinusMinus: return mk(UnaryOp::PreDec);
      default: return postfix(primary());
    }
  }

  static bool isPrefixed(const std::string& s) { return s.size() > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X' || s[1] == 'b' || s[1] == 'B'); }

  std::vector<ExprPtr> arguments() {
    std::vector<ExprPtr> args;
    expect(Tok::LParen);
    if (!at(Tok::RParen)) {
      do args.push_back(expression()); while (accept(Tok::Comma));
    }
    expect(Tok::RParen);
    return args;
  }

  ExprPtr postfix(ExprPtr e) {
    int chain = 0;
    for (;;) {
      const SourceLoc l = cur().loc;
      if (++chain > kMaxChain) fail(l, "expression has too many chained operations; split it into parts");
      if (at(Tok::Dot)) {
        ++p_;
        auto m = newExpr(ExprKind::Member, l);
        m->text = expectIdent("a member name");
        m->kids.push_back(std::move(e));
        e = std::move(m);
      } else if (at(Tok::LBracket)) {
        ++p_;
        auto m = newExpr(ExprKind::Index, l);
        m->kids.push_back(std::move(e));
        m->kids.push_back(expression());
        expect(Tok::RBracket);
        e = std::move(m);
      } else if (at(Tok::LParen)) {
        auto c = newExpr(ExprKind::Call, l);
        c->kids.push_back(std::move(e));
        for (auto& a : arguments()) c->kids.push_back(std::move(a));
        e = std::move(c);
      } else if (at(Tok::PlusPlus) || at(Tok::MinusMinus)) {
        auto u = newExpr(ExprKind::Unary, l);
        u->uop = at(Tok::PlusPlus) ? UnaryOp::PostInc : UnaryOp::PostDec;
        ++p_;
        u->kids.push_back(std::move(e));
        e = std::move(u);
      } else {
        return e;
      }
    }
  }

  // `(` ... `)` `=>` : scan to the matching parenthesis.
  bool looksLikeLambda() const {
    if (at(Tok::Ident) && atAt(1, Tok::Arrow)) return true;
    if (!at(Tok::LParen)) return false;
    int depth = 0;
    std::size_t i = p_;
    for (; i < t_.size(); ++i) {
      if (t_[i].kind == Tok::LParen) ++depth;
      else if (t_[i].kind == Tok::RParen) {
        if (--depth == 0) break;
      } else if (t_[i].kind == Tok::End) {
        return false;
      }
    }
    return i + 1 < t_.size() && t_[i + 1].kind == Tok::Arrow;
  }

  ExprPtr lambdaExpr() {
    auto e = newExpr(ExprKind::Lambda, cur().loc);
    e->lambda = std::make_unique<LambdaData>();
    if (at(Tok::Ident)) {
      Param pr;
      pr.loc = cur().loc;
      pr.name = t_[p_++].text;
      e->lambda->params.push_back(std::move(pr));
    } else {
      e->lambda->params = paramList(true);
    }
    expect(Tok::Arrow);
    if (at(Tok::LBrace)) e->lambda->bodyBlock = block();
    else e->lambda->bodyExpr = assignment();
    return e;
  }

  ExprPtr primary() {
    const SourceLoc l = cur().loc;
    if (looksLikeLambda()) return lambdaExpr();
    switch (cur().kind) {
      case Tok::IntLit: {
        auto e = newExpr(ExprKind::IntLit, l);
        e->ival = cur().ival;
        if (cur().ival == INT64_MIN && !isPrefixed(cur().text)) fail(l, "integer literal is too large");
        ++p_;
        return e;
      }
      case Tok::FloatLit: {
        auto e = newExpr(ExprKind::FloatLit, l);
        e->fval = cur().fval;
        ++p_;
        return e;
      }
      case Tok::StringLit: {
        auto e = newExpr(ExprKind::StringLit, l);
        e->text = cur().text;
        ++p_;
        return e;
      }
      case Tok::KwTrue: case Tok::KwFalse: {
        auto e = newExpr(ExprKind::BoolLit, l);
        e->ival = at(Tok::KwTrue) ? 1 : 0;
        ++p_;
        return e;
      }
      case Tok::KwNull: ++p_; return newExpr(ExprKind::NullLit, l);
      case Tok::KwThis: ++p_; return newExpr(ExprKind::This, l);
      case Tok::KwSuper: {
        ++p_;
        if (!at(Tok::Dot) && !at(Tok::LParen)) fail(l, "'super' must be followed by '.' or '('");
        return newExpr(ExprKind::Super, l);
      }
      case Tok::Ident: {
        auto e = newExpr(ExprKind::Name, l);
        e->text = cur().text;
        ++p_;
        return e;
      }
      case Tok::LParen: {
        ++p_;
        ExprPtr e = expression();
        expect(Tok::RParen);
        return e;
      }
      case Tok::LBracket: {
        ++p_;
        auto e = newExpr(ExprKind::ArrayLit, l);
        if (!at(Tok::RBracket)) {
          do {
            if (at(Tok::RBracket)) break;  // trailing comma
            e->kids.push_back(expression());
          } while (accept(Tok::Comma));
        }
        expect(Tok::RBracket);
        return e;
      }
      case Tok::KwNew: return newExpression();
      case Tok::KwTypeof: {
        ++p_;
        auto e = newExpr(ExprKind::Typeof, l);
        expect(Tok::LParen);
        if (isPrim(cur().kind) || at(Tok::KwFn)) e->typeRef = parseType();
        else e->kids.push_back(expression());
        expect(Tok::RParen);
        return e;
      }
      default:
        unexpected("an expression");
    }
  }

  ExprPtr newExpression() {
    const SourceLoc l = cur().loc;
    ++p_;
    auto ty = std::make_unique<TypeRef>();
    ty->loc = cur().loc;
    if (isPrim(cur().kind)) {
      ty->kind = TypeRef::Kind::Prim;
      ty->prim = cur().kind;
      ++p_;
    } else if (at(Tok::Ident)) {
      ty->kind = TypeRef::Kind::Named;
      ty->path = qualifiedName();
    } else if (at(Tok::LParen)) {  // new (fn():int)[n]
      ++p_;
      ty = parseType();
      expect(Tok::RParen);
    } else {
      unexpected("a type after 'new'");
    }
    if (at(Tok::LBracket)) {
      auto e = newExpr(ExprKind::NewArray, l);
      ++p_;
      if (at(Tok::RBracket)) fail(cur().loc, "an array size is required: new T[n]");
      e->kids.push_back(expression());
      expect(Tok::RBracket);
      // new T[n][]: array of arrays
      while (at(Tok::LBracket) && atAt(1, Tok::RBracket)) {
        auto a = std::make_unique<TypeRef>();
        a->kind = TypeRef::Kind::Array;
        a->loc = ty->loc;
        a->elem = std::move(ty);
        ty = std::move(a);
        p_ += 2;
      }
      e->typeRef = std::move(ty);
      return e;
    }
    auto e = newExpr(ExprKind::New, l);
    if (ty->kind == TypeRef::Kind::Prim) fail(l, "cannot construct a primitive type with 'new'");
    e->typeRef = std::move(ty);
    for (auto& a : arguments()) e->kids.push_back(std::move(a));
    return e;
  }
};

}  // namespace

std::unique_ptr<CompilationUnit> parse(std::string_view source, std::uint32_t fileIndex, Diagnostics& diags) {
  std::vector<Token> toks = lex(source, fileIndex, diags);
  Parser p(std::move(toks), diags);
  return p.run(fileIndex);
}

}  // namespace mingol::script
