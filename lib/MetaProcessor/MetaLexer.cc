#include "cppinterp/MetaProcessor/MetaLexer.h"

namespace cppinterp {

llvm::StringRef Token::getIdent() const {
  assert((is(tok::ident) || is(tok::raw_ident) || is(tok::stringlit) ||
          is(tok::charlit)) &&
         "Token not an ident or literal.");
  return llvm::StringRef(buf_start_, getLength());
}

bool Token::getConstantAsBool() const {
  assert(kind_ == tok::constant && "Not a constant");
  return getConstant() != 0;
}

const static unsigned int kPow10[10] = {1,      10,      100,      1000, 10000,
                                        100000, 1000000, 10000000, ~0U};

unsigned Token::getConstant() const {
  assert(kind_ == tok::constant && "Not a constant");
  if (value_ == ~0U) {
    value_ = 0;
    for (size_t i = 0, e = length_; i < e; ++i) {
      value_ += (*(buf_start_ + i) - '0') * kPow10[length_ - i - 1];
    }
  }
  return value_;
}

MetaLexer::MetaLexer(llvm::StringRef input, bool skip_ws)
    : buf_start_(input.data()), cur_pos_(input.data()) {
  if (skip_ws) {
    skipWhiteSpace();
  }
}

void MetaLexer::reset(llvm::StringRef line) {
  buf_start_ = line.data();
  cur_pos_ = line.data();
}

void MetaLexer::lex(Token& token) {
  // clang-format off
  token.startToken(cur_pos_);
  char ch = *cur_pos_++;
  switch (ch) {
    case '"': case '\'':
      return lexQuotedStringAndAdvance(cur_pos_, token);
    case '[': case ']': case '(': case ')': case '{': case '}':
    case '\\': case ',': case '.': case '!': case '?': case '<': case '>':
    case '&': case '#': case '@': case ';':
      return lexPunctuator(cur_pos_ - 1, token);

    case '/':
      if (*cur_pos_ == '/' || *cur_pos_ == '*') {
        token.setKind((*cur_pos_++ == '/') ? tok::comment : tok::l_comment);
        token.setLength(2);
        return;
      }
      return lexPunctuator(cur_pos_ - 1, token);
    case '*':
      if (*cur_pos_ == '/') {
        ++cur_pos_;
        token.setKind(tok::r_comment);
        token.setLength(2);
        return;
      }
      return lexPunctuator(cur_pos_ - 1, token);

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      return lexConstant(ch, token);

    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
    case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
    case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
    case 'V': case 'W': case 'X': case 'Y': case 'Z':
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
    case 'v': case 'w': case 'x': case 'y': case 'z':
    case '_':
      return lexIdentifier(ch, token);
    case ' ': case '\t':
      return lexWhitespace(token);
    case '\0':
      return lexEndOfFile(ch, token);
  }
  // clang-format on
}

void MetaLexer::lexAnyString(Token& token) {
  token.startToken(cur_pos_);
  // 一直消费到分隔符或者eof
  while (*cur_pos_ != ' ' && *cur_pos_ != '\t' && *cur_pos_ != '\0') {
    cur_pos_++;
  }
  assert(token.getBufStart() != cur_pos_ && "It must consume at least on char");

  token.setKind(tok::raw_ident);
  token.setLength(cur_pos_ - token.getBufStart());
}

void MetaLexer::readToEndOfLine(Token& token, tok::TokenKind kind) {
  token.startToken(cur_pos_);
  while (*cur_pos_ != '\r' && *cur_pos_ != '\n' && *cur_pos_ != '\0') {
    cur_pos_++;
  }

  token.setKind(kind);
  token.setLength(cur_pos_ - token.getBufStart());
}

void MetaLexer::lexPunctuator(const char* ch, Token& token) {
  token.startToken(ch);
  token.setLength(1);
  switch (*ch) {
    case '[':
      token.setKind(tok::l_square);
      break;
    case ']':
      token.setKind(tok::r_square);
      break;
    case '(':
      token.setKind(tok::l_paren);
      break;
    case ')':
      token.setKind(tok::r_paren);
      break;
    case '{':
      token.setKind(tok::l_brace);
      break;
    case '}':
      token.setKind(tok::r_brace);
      break;
    case '"':
      token.setKind(tok::stringlit);
      break;
    case '\'':
      token.setKind(tok::charlit);
      break;
    case ',':
      token.setKind(tok::comma);
      break;
    case '.':
      token.setKind(tok::dot);
      break;
    case '!':
      token.setKind(tok::excl_mark);
      break;
    case '?':
      token.setKind(tok::quest_mark);
      break;
    case '/':
      token.setKind(tok::slash);
      break;
    case '\\':
      token.setKind(tok::backslash);
      break;
    case '<':
      token.setKind(tok::less);
      break;
    case '>':
      token.setKind(tok::greater);
      break;
    case '@':
      token.setKind(tok::at);
      break;
    case '&':
      token.setKind(tok::ampersand);
      break;
    case '#':
      token.setKind(tok::hash);
      break;
    case '*':
      token.setKind(tok::asterik);
      break;
    case ';':
      token.setKind(tok::semicolon);
      break;
    case '\0':
      token.setKind(tok::eof);
      token.setLength(0);
      break;
    default:
      token.setLength(0);
      break;
  }
}

void MetaLexer::lexQuotedStringAndAdvance(const char*& cur_pos, Token& token) {
  // cur_pos_必须是在"双引号或者'单引号之后的位置，然后将执行lex直到下一个双引号或单引号。
  assert((cur_pos[-1] == '"' || cur_pos[-1] == '\'') &&
         "Not a string / character literal!");
  if (cur_pos[-1] == '"') {
    token.setKind(tok::stringlit);
  } else {
    token.setKind(tok::charlit);
  }
  token.setBufStart(cur_pos - 1);

  // 消费引号后的string
  while (true) {
    if (*cur_pos == '\\') {
      // 如果当前位置是分号字符，\"或者\'会造成字符串结束的假象，则跳过该位置，向后移两位。
      cur_pos += 2;
      continue;
    }
    if (*cur_pos == '\0') {
      token.setBufStart(cur_pos);
      token.setKind(tok::eof);
      token.setLength(0);
      return;
    }

    // 只要当前字符不等于起始位置引号字符，则一直向后推进当前字符。
    if (*cur_pos++ == *token.getBufStart()) {
      token.setLength(cur_pos - token.getBufStart());
      assert((token.getIdent().front() == '"' ||
              token.getIdent().front() == '\'') &&
             "Not a string literal");
      assert(
          (token.getIdent().back() == '"' || token.getIdent().back() == '\'') &&
          "Missing string literal end quote");
      assert((token.getIdent().front() == token.getIdent().back()) &&
             "Inconsistent string literal quotes");
      return;
    }
  }
}

void MetaLexer::lexConstant(char ch, Token& token) {
  while (ch >= '0' && ch <= '9') {
    ch = *cur_pos_++;
  }

  --cur_pos_;
  token.setLength(cur_pos_ - token.getBufStart());
  token.setKind(tok::constant);
}

void MetaLexer::lexIdentifier(char ch, Token& token) {
  while (ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9')) {
    ch = *cur_pos_++;
  }

  // 回退一个pos
  --cur_pos_;
  token.setLength(cur_pos_ - token.getBufStart());
  if (token.getLength()) {
    token.setKind(tok::ident);
  }
}

void MetaLexer::lexEndOfFile(char ch, Token& token) {
  if (ch == '\0') {
    token.setKind(tok::eof);
    token.setLength(1);
  }
}

void MetaLexer::skipWhiteSpace() {
  char ch = *cur_pos_;
  while ((ch == ' ' || ch == '\t') && ch != '\0') {
    ch = *(++cur_pos_);
  }
}

void MetaLexer::lexWhitespace(Token& token) {
  skipWhiteSpace();
  token.setLength(cur_pos_ - token.getBufStart());
  token.setKind(tok::space);
}

}  // namespace cppinterp