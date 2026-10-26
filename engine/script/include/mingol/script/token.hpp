// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <string>

#include "mingol/script/source.hpp"

namespace mingol::script {

enum class Tok : std::uint8_t {
  End, Ident, IntLit, FloatLit, StringLit,
  // keywords
  KwModule, KwImport, KwClass, KwInterface, KwEnum, KwExtends, KwImplements, KwStatic, KwPublic, KwPrivate,
  KwProtected, KwFinal, KwAbstract, KwOverride, KwNative, KwNew, KwNull, KwThis, KwSuper, KwTrue, KwFalse,
  KwIf, KwElse, KwWhile, KwDo, KwFor, KwSwitch, KwCase, KwDefault, KwBreak, KwContinue, KwReturn, KwThrow,
  KwTry, KwCatch, KwFinally, KwYield, KwCoroutine, KwVar, KwIs, KwAs, KwTypeof, KwOperator, KwFn, KwRef,
  KwInt, KwFloat, KwBool, KwString, KwVoid,
  // punctuation
  LParen, RParen, LBrace, RBrace, LBracket, RBracket, Semi, Comma, Dot, Colon, Question, Arrow /* => */,
  Plus, Minus, Star, Slash, Percent, Amp, Pipe, Caret, Tilde, Bang, Lt, Gt, Assign,
  PlusPlus, MinusMinus, AmpAmp, PipePipe, EqEq, NotEq, LtEq, GtEq, Shl, Shr,
  PlusAssign, MinusAssign, StarAssign, SlashAssign, PercentAssign, AmpAssign, PipeAssign, CaretAssign, ShlAssign, ShrAssign,
  Error,
};

const char* tokName(Tok t) noexcept;

struct Token {
  Tok kind = Tok::End;
  SourceLoc loc;
  std::string text;      // identifier name, decoded string literal, or the lexeme
  std::int64_t ival = 0; // IntLit
  double fval = 0;       // FloatLit
};

}  // namespace mingol::script
