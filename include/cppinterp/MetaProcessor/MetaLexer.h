#ifndef CPPINTERP_METAPROCESSOR_META_LEXER_H
#define CPPINTERP_METAPROCESSOR_META_LEXER_H

#include <llvm-15/llvm/ADT/StringRef.h>

#include "llvm/ADT/StringRef.h"

namespace cppinterp {

namespace tok {
enum TokenKind {
  l_square,    // "["
  r_square,    // "]"
  l_paren,     // "("
  r_paren,     // ")"
  l_brace,     // "{"
  r_brace,     // "}"
  stringlit,   // ""...""
  charlit,     // "'.'"
  comma,       // ","
  dot,         // "."
  excl_mark,   // "!"
  quest_mark,  // "?"
  slash,       // "/"
  backslash,   // "\"
  less,        // "<"
  greater,     // ">"
  ampersand,   // "&"
  hash,        // "#"
  ident,       // (a-zA-Z)[(0-9a-zA-Z)*]
  raw_ident,   // .*^(' '|'\t')
  comment,     // //
  l_comment,   // "/*"
  r_comment,   // "*/"
  space,       // (' ' | '\t')*
  constant,    // {0-9}
  at,          // @
  asterik,     // *
  semicolon,   // ;
  eof,
  unknown
};
}  // namespace tok

class Token {
 public:
  Token(const char* buffer = nullptr) {}

  void startToken(const char* pos = nullptr) {
    kind_ = tok::unknown;
    buf_start_ = pos;
    length_ = 0;
    value_ = ~0U;
  }

  tok::TokenKind getKind() const { return kind_; }

  void setKind(tok::TokenKind kind) { kind_ = kind; }

  unsigned getLength() const { return length_; }

  void setLength(unsigned length) { length_ = length; }

  const char* getBufStart() const { return buf_start_; }

  void setBufStart(const char* pos) { buf_start_ = pos; }

  bool isNot(tok::TokenKind kind) const { return kind_ != kind; }

  bool is(tok::TokenKind kind) const { return kind_ == kind; }

  bool isClosingBrace() const {
    return kind_ == tok::r_square || kind_ == tok::r_paren ||
           kind_ == tok::r_brace;
  }

  /// 判断是否当前token匹配得上左开放括号kind。
  bool ClosesBrace(tok::TokenKind kind) const {
    return isClosingBrace() && (kind_ == kind + 1);
  }

  llvm::StringRef getIdent() const;

  /// 获取没有引号的ident
  llvm::StringRef getIdentNoQuotes() const {
    if (getKind() >= tok::stringlit && getKind() <= tok::charlit) {
      return getIdent().drop_back().drop_front();
    }
    return getIdent();
  }

  bool getConstantAsBool() const;

  unsigned getConstant() const;

 private:
  tok::TokenKind kind_;
  const char* buf_start_;
  unsigned length_;
  mutable unsigned value_;
};

class MetaLexer {
 public:
  struct RAII {
    MetaLexer& lexer;
    const char* saved_pos;
    RAII(MetaLexer& lexer) : lexer(lexer), saved_pos(lexer.cur_pos_) {}
    ~RAII() { lexer.cur_pos_ = saved_pos; }
  };

  MetaLexer(llvm::StringRef input, bool skip_ws = false);

  void reset(llvm::StringRef line);

  void lex(Token& token);

  void lexAnyString(Token& token);

  void readToEndOfLine(Token& token, tok::TokenKind kind = tok::unknown);

  static void lexPunctuator(const char* ch, Token& token);

  static void lexQuotedStringAndAdvance(const char*& cur_pos, Token& token);

  void lexConstant(char ch, Token& token);

  void lexIdentifier(char ch, Token& token);

  void lexEndOfFile(char ch, Token& token);

  void lexWhitespace(Token& token);

  void skipWhiteSpace();

  const char* getLocation() const { return cur_pos_; }

 protected:
  const char* buf_start_;
  const char* cur_pos_;
};

}  // namespace cppinterp

#endif  // CPPINTERP_METAPROCESSOR_META_LEXER_H