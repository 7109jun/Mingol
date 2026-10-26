// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/script/lexer.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace mingol::script {

const char* tokName(Tok t) noexcept {
  switch (t) {
    case Tok::End: return "end of file";
    case Tok::Ident: return "identifier";
    case Tok::IntLit: return "integer literal";
    case Tok::FloatLit: return "float literal";
    case Tok::StringLit: return "string literal";
    case Tok::KwModule: return "'module'";
    case Tok::KwImport: return "'import'";
    case Tok::KwClass: return "'class'";
    case Tok::KwInterface: return "'interface'";
    case Tok::KwEnum: return "'enum'";
    case Tok::KwExtends: return "'extends'";
    case Tok::KwImplements: return "'implements'";
    case Tok::KwStatic: return "'static'";
    case Tok::KwPublic: return "'public'";
    case Tok::KwPrivate: return "'private'";
    case Tok::KwProtected: return "'protected'";
    case Tok::KwFinal: return "'final'";
    case Tok::KwAbstract: return "'abstract'";
    case Tok::KwOverride: return "'override'";
    case Tok::KwNative: return "'native'";
    case Tok::KwNew: return "'new'";
    case Tok::KwNull: return "'null'";
    case Tok::KwThis: return "'this'";
    case Tok::KwSuper: return "'super'";
    case Tok::KwTrue: return "'true'";
    case Tok::KwFalse: return "'false'";
    case Tok::KwIf: return "'if'";
    case Tok::KwElse: return "'else'";
    case Tok::KwWhile: return "'while'";
    case Tok::KwDo: return "'do'";
    case Tok::KwFor: return "'for'";
    case Tok::KwSwitch: return "'switch'";
    case Tok::KwCase: return "'case'";
    case Tok::KwDefault: return "'default'";
    case Tok::KwBreak: return "'break'";
    case Tok::KwContinue: return "'continue'";
    case Tok::KwReturn: return "'return'";
    case Tok::KwThrow: return "'throw'";
    case Tok::KwTry: return "'try'";
    case Tok::KwCatch: return "'catch'";
    case Tok::KwFinally: return "'finally'";
    case Tok::KwYield: return "'yield'";
    case Tok::KwCoroutine: return "'coroutine'";
    case Tok::KwVar: return "'var'";
    case Tok::KwIs: return "'is'";
    case Tok::KwAs: return "'as'";
    case Tok::KwTypeof: return "'typeof'";
    case Tok::KwOperator: return "'operator'";
    case Tok::KwFn: return "'fn'";
    case Tok::KwRef: return "'ref'";
    case Tok::KwInt: return "'int'";
    case Tok::KwFloat: return "'float'";
    case Tok::KwBool: return "'bool'";
    case Tok::KwString: return "'string'";
    case Tok::KwVoid: return "'void'";
    case Tok::LParen: return "'('";
    case Tok::RParen: return "')'";
    case Tok::LBrace: return "'{'";
    case Tok::RBrace: return "'}'";
    case Tok::LBracket: return "'['";
    case Tok::RBracket: return "']'";
    case Tok::Semi: return "';'";
    case Tok::Comma: return "','";
    case Tok::Dot: return "'.'";
    case Tok::Colon: return "':'";
    case Tok::Question: return "'?'";
    case Tok::Arrow: return "'=>'";
    case Tok::Plus: return "'+'";
    case Tok::Minus: return "'-'";
    case Tok::Star: return "'*'";
    case Tok::Slash: return "'/'";
    case Tok::Percent: return "'%'";
    case Tok::Amp: return "'&'";
    case Tok::Pipe: return "'|'";
    case Tok::Caret: return "'^'";
    case Tok::Tilde: return "'~'";
    case Tok::Bang: return "'!'";
    case Tok::Lt: return "'<'";
    case Tok::Gt: return "'>'";
    case Tok::Assign: return "'='";
    case Tok::PlusPlus: return "'++'";
    case Tok::MinusMinus: return "'--'";
    case Tok::AmpAmp: return "'&&'";
    case Tok::PipePipe: return "'||'";
    case Tok::EqEq: return "'=='";
    case Tok::NotEq: return "'!='";
    case Tok::LtEq: return "'<='";
    case Tok::GtEq: return "'>='";
    case Tok::Shl: return "'<<'";
    case Tok::Shr: return "'>>'";
    case Tok::PlusAssign: return "'+='";
    case Tok::MinusAssign: return "'-='";
    case Tok::StarAssign: return "'*='";
    case Tok::SlashAssign: return "'/='";
    case Tok::PercentAssign: return "'%='";
    case Tok::AmpAssign: return "'&='";
    case Tok::PipeAssign: return "'|='";
    case Tok::CaretAssign: return "'^='";
    case Tok::ShlAssign: return "'<<='";
    case Tok::ShrAssign: return "'>>='";
    case Tok::Error: return "invalid token";
  }
  return "?";
}

namespace {

const std::unordered_map<std::string_view, Tok>& keywords() {
  static const std::unordered_map<std::string_view, Tok> k = {
      {"module", Tok::KwModule}, {"import", Tok::KwImport}, {"class", Tok::KwClass}, {"interface", Tok::KwInterface},
      {"enum", Tok::KwEnum}, {"extends", Tok::KwExtends}, {"implements", Tok::KwImplements}, {"static", Tok::KwStatic},
      {"public", Tok::KwPublic}, {"private", Tok::KwPrivate}, {"protected", Tok::KwProtected}, {"final", Tok::KwFinal},
      {"abstract", Tok::KwAbstract}, {"override", Tok::KwOverride}, {"native", Tok::KwNative}, {"new", Tok::KwNew},
      {"null", Tok::KwNull}, {"this", Tok::KwThis}, {"super", Tok::KwSuper}, {"true", Tok::KwTrue}, {"false", Tok::KwFalse},
      {"if", Tok::KwIf}, {"else", Tok::KwElse}, {"while", Tok::KwWhile}, {"do", Tok::KwDo}, {"for", Tok::KwFor},
      {"switch", Tok::KwSwitch}, {"case", Tok::KwCase}, {"default", Tok::KwDefault}, {"break", Tok::KwBreak},
      {"continue", Tok::KwContinue}, {"return", Tok::KwReturn}, {"throw", Tok::KwThrow}, {"try", Tok::KwTry},
      {"catch", Tok::KwCatch}, {"finally", Tok::KwFinally}, {"yield", Tok::KwYield}, {"coroutine", Tok::KwCoroutine},
      {"var", Tok::KwVar}, {"is", Tok::KwIs}, {"as", Tok::KwAs}, {"typeof", Tok::KwTypeof}, {"operator", Tok::KwOperator},
      {"fn", Tok::KwFn}, {"ref", Tok::KwRef}, {"int", Tok::KwInt}, {"float", Tok::KwFloat}, {"bool", Tok::KwBool},
      {"string", Tok::KwString}, {"void", Tok::KwVoid}};
  return k;
}

bool isIdentStart(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80; }
bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool isIdentPart(unsigned char c) { return isIdentStart(c) || isDigit(c); }
int hexVal(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void appendUtf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

class Lexer {
 public:
  Lexer(std::string_view s, std::uint32_t file, Diagnostics& d) : src_(s), file_(file), diags_(d) {}

  std::vector<Token> run() {
    std::vector<Token> out;
    for (;;) {
      skipTrivia();
      Token t = next();
      const bool end = t.kind == Tok::End;
      if (t.kind != Tok::Error) out.push_back(std::move(t));
      if (end) break;
    }
    return out;
  }

 private:
  std::string_view src_;
  std::uint32_t file_;
  Diagnostics& diags_;
  std::size_t pos_ = 0;
  std::uint32_t line_ = 1, col_ = 1;

  bool atEnd() const { return pos_ >= src_.size(); }
  unsigned char peek(std::size_t k = 0) const { return pos_ + k < src_.size() ? static_cast<unsigned char>(src_[pos_ + k]) : 0; }
  SourceLoc loc() const { return {file_, line_, col_}; }
  void advance() {
    if (atEnd()) return;
    if (src_[pos_] == '\n') { ++line_; col_ = 1; } else { ++col_; }
    ++pos_;
  }
  void err(SourceLoc l, std::string m) { diags_.error(l, std::move(m)); }

  void skipTrivia() {
    for (;;) {
      const unsigned char c = peek();
      if (atEnd()) return;
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
      if (c == '/' && peek(1) == '/') {
        while (!atEnd() && peek() != '\n') advance();
        continue;
      }
      if (c == '/' && peek(1) == '*') {
        const SourceLoc start = loc();
        advance(); advance();
        bool closed = false;
        while (!atEnd()) {
          if (peek() == '*' && peek(1) == '/') { advance(); advance(); closed = true; break; }
          advance();
        }
        if (!closed) err(start, "unterminated block comment");
        continue;
      }
      return;
    }
  }

  Token make(Tok k, SourceLoc l, std::string text = {}) {
    Token t;
    t.kind = k;
    t.loc = l;
    t.text = std::move(text);
    return t;
  }

  Token next() {
    const SourceLoc l = loc();
    if (atEnd()) return make(Tok::End, l);
    const unsigned char c = peek();
    if (isIdentStart(c)) return identifier(l);
    if (isDigit(c)) return number(l);
    if (c == '"') return string(l);
    return punct(l);
  }

  Token identifier(SourceLoc l) {
    const std::size_t start = pos_;
    while (!atEnd() && isIdentPart(peek())) advance();
    std::string text(src_.substr(start, pos_ - start));
    auto it = keywords().find(text);
    if (it != keywords().end()) return make(it->second, l, std::move(text));
    return make(Tok::Ident, l, std::move(text));
  }

  Token number(SourceLoc l) {
    const std::size_t start = pos_;
    // Prefixed integers.
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'b' || peek(1) == 'B')) {
      const bool hex = peek(1) == 'x' || peek(1) == 'X';
      advance(); advance();
      std::uint64_t v = 0;
      int digits = 0;
      bool overflow = false;
      while (!atEnd()) {
        const unsigned char d = peek();
        if (d == '_') { advance(); continue; }
        const int h = hexVal(d);
        if (h < 0 || (!hex && h > 1)) break;
        const std::uint64_t base = hex ? 16 : 2;
        if (v > (UINT64_MAX - static_cast<std::uint64_t>(h)) / base) overflow = true;
        v = v * base + static_cast<std::uint64_t>(h);
        ++digits;
        advance();
      }
      if (digits == 0) { err(l, hex ? "hexadecimal literal has no digits" : "binary literal has no digits"); return make(Tok::Error, l); }
      if (!atEnd() && isIdentPart(peek())) { err(l, "invalid character in number literal"); skipIdent(); return make(Tok::Error, l); }
      if (overflow) { err(l, "integer literal is too large"); return make(Tok::Error, l); }
      Token t = make(Tok::IntLit, l, std::string(src_.substr(start, pos_ - start)));
      t.ival = static_cast<std::int64_t>(v);  // 0xFFFF_FFFF_FFFF_FFFF is -1: bit patterns are allowed
      return t;
    }
    std::string digits;
    bool isFloat = false;
    while (!atEnd() && (isDigit(peek()) || peek() == '_')) { if (peek() != '_') digits += static_cast<char>(peek()); advance(); }
    if (peek() == '.' && isDigit(peek(1))) {
      isFloat = true;
      digits += '.';
      advance();
      while (!atEnd() && (isDigit(peek()) || peek() == '_')) { if (peek() != '_') digits += static_cast<char>(peek()); advance(); }
    }
    if (peek() == 'e' || peek() == 'E') {
      const bool signedExp = peek(1) == '+' || peek(1) == '-';
      if (isDigit(peek(signedExp ? 2 : 1))) {
        isFloat = true;
        digits += 'e';
        advance();
        if (signedExp) { digits += static_cast<char>(peek()); advance(); }
        while (!atEnd() && isDigit(peek())) { digits += static_cast<char>(peek()); advance(); }
      }
    }
    if (!atEnd() && isIdentPart(peek())) { err(l, "invalid character in number literal"); skipIdent(); return make(Tok::Error, l); }
    Token t = make(isFloat ? Tok::FloatLit : Tok::IntLit, l, std::string(src_.substr(start, pos_ - start)));
    if (isFloat) {
      char* endp = nullptr;
      t.fval = std::strtod(digits.c_str(), &endp);
      if (!std::isfinite(t.fval)) { err(l, "float literal is out of range"); return make(Tok::Error, l); }
    } else {
      std::uint64_t v = 0;
      auto r = std::from_chars(digits.data(), digits.data() + digits.size(), v);
      if (r.ec != std::errc() || v > static_cast<std::uint64_t>(INT64_MAX)) {
        // 9223372036854775808 is only valid as the operand of unary minus; the parser handles that
        // case by checking `text`, so keep the exact bit pattern here.
        if (r.ec == std::errc() && v == static_cast<std::uint64_t>(INT64_MAX) + 1) {
          t.ival = INT64_MIN;
        } else {
          err(l, "integer literal is too large");
          return make(Tok::Error, l);
        }
      } else {
        t.ival = static_cast<std::int64_t>(v);
      }
    }
    return t;
  }

  void skipIdent() { while (!atEnd() && isIdentPart(peek())) advance(); }

  Token string(SourceLoc l) {
    advance();  // opening quote
    std::string out;
    bool bad = false;
    for (;;) {
      if (atEnd() || peek() == '\n') { err(l, "unterminated string literal"); return make(Tok::Error, l); }
      const unsigned char c = peek();
      if (c == '"') { advance(); break; }
      if (c != '\\') { out += static_cast<char>(c); advance(); continue; }
      const SourceLoc el = loc();
      advance();
      if (atEnd()) { err(l, "unterminated string literal"); return make(Tok::Error, l); }
      const unsigned char e = peek();
      advance();
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '0': out += '\0'; break;
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        case '\'': out += '\''; break;
        case 'x': {
          const int h1 = hexVal(peek()), h2 = hexVal(peek(1));
          if (h1 < 0 || h2 < 0) { err(el, "\\x needs two hexadecimal digits"); bad = true; break; }
          advance(); advance();
          out += static_cast<char>(h1 * 16 + h2);
          break;
        }
        case 'u': {
          if (peek() != '{') { err(el, "\\u must be followed by {hex}"); bad = true; break; }
          advance();
          std::uint32_t cp = 0;
          int n = 0;
          while (!atEnd() && hexVal(peek()) >= 0 && n < 8) { cp = cp * 16 + static_cast<std::uint32_t>(hexVal(peek())); advance(); ++n; }
          if (peek() != '}' || n == 0) { err(el, "malformed \\u{...} escape"); bad = true; break; }
          advance();
          if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { err(el, "\\u{...} is not a valid Unicode scalar value"); bad = true; break; }
          appendUtf8(out, cp);
          break;
        }
        default:
          err(el, std::string("unknown escape sequence '\\") + static_cast<char>(e) + "'");
          bad = true;
      }
    }
    if (bad) return make(Tok::Error, l);
    return make(Tok::StringLit, l, std::move(out));
  }

  bool take(char c) {
    if (peek() == static_cast<unsigned char>(c) && !atEnd()) { advance(); return true; }
    return false;
  }

  Token punct(SourceLoc l) {
    const unsigned char c = peek();
    advance();
    switch (c) {
      case '(': return make(Tok::LParen, l);
      case ')': return make(Tok::RParen, l);
      case '{': return make(Tok::LBrace, l);
      case '}': return make(Tok::RBrace, l);
      case '[': return make(Tok::LBracket, l);
      case ']': return make(Tok::RBracket, l);
      case ';': return make(Tok::Semi, l);
      case ',': return make(Tok::Comma, l);
      case '.': return make(Tok::Dot, l);
      case ':': return make(Tok::Colon, l);
      case '?': return make(Tok::Question, l);
      case '~': return make(Tok::Tilde, l);
      case '+': return take('+') ? make(Tok::PlusPlus, l) : take('=') ? make(Tok::PlusAssign, l) : make(Tok::Plus, l);
      case '-': return take('-') ? make(Tok::MinusMinus, l) : take('=') ? make(Tok::MinusAssign, l) : make(Tok::Minus, l);
      case '*': return take('=') ? make(Tok::StarAssign, l) : make(Tok::Star, l);
      case '/': return take('=') ? make(Tok::SlashAssign, l) : make(Tok::Slash, l);
      case '%': return take('=') ? make(Tok::PercentAssign, l) : make(Tok::Percent, l);
      case '^': return take('=') ? make(Tok::CaretAssign, l) : make(Tok::Caret, l);
      case '!': return take('=') ? make(Tok::NotEq, l) : make(Tok::Bang, l);
      case '=': return take('=') ? make(Tok::EqEq, l) : take('>') ? make(Tok::Arrow, l) : make(Tok::Assign, l);
      case '&': return take('&') ? make(Tok::AmpAmp, l) : take('=') ? make(Tok::AmpAssign, l) : make(Tok::Amp, l);
      case '|': return take('|') ? make(Tok::PipePipe, l) : take('=') ? make(Tok::PipeAssign, l) : make(Tok::Pipe, l);
      case '<':
        if (take('<')) return take('=') ? make(Tok::ShlAssign, l) : make(Tok::Shl, l);
        return take('=') ? make(Tok::LtEq, l) : make(Tok::Lt, l);
      case '>':
        // '>>' is never produced here: the parser needs '>' tokens individually only for generics,
        // which the language does not have, so shifts are lexed greedily.
        if (take('>')) return take('=') ? make(Tok::ShrAssign, l) : make(Tok::Shr, l);
        return take('=') ? make(Tok::GtEq, l) : make(Tok::Gt, l);
      default: {
        char buf[32];
        if (c >= 0x20 && c < 0x7F) std::snprintf(buf, sizeof buf, "unexpected character '%c'", c);
        else std::snprintf(buf, sizeof buf, "unexpected byte 0x%02X", c);
        err(l, buf);
        return make(Tok::Error, l);
      }
    }
  }
};

}  // namespace

std::vector<Token> lex(std::string_view source, std::uint32_t fileIndex, Diagnostics& diags) {
  return Lexer(source, fileIndex, diags).run();
}

}  // namespace mingol::script
