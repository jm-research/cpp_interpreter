#ifndef CPPINTERP_UTILS_PRINT_HELPER_H
#define CPPINTERP_UTILS_PRINT_HELPER_H

#include "clang/Basic/IdentifierTable.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "llvm/Support/raw_ostream.h"

namespace cppinterp {

void PrintPPMacro(llvm::raw_ostream& OS, const clang::IdentifierInfo* II,
                  const clang::MacroDirective* MD,
                  const clang::Preprocessor& pp) {
  OS << "<MD: " << MD << ">";
  OS << II->getName() << " ";
  OS << "(Tokens:)";
  const clang::MacroInfo* MI = MD->getMacroInfo();
  for (unsigned i = 0, e = MI->getNumTokens(); i != e; ++i) {
    const clang::Token& Tok = MI->getReplacementToken(i);
    OS << clang::tok::getTokenName(Tok.getKind()) << " '" << pp.getSpelling(Tok)
       << "'";
    OS << "\t";
    if (Tok.isAtStartOfLine())
      OS << " [StartOfLine]";
    if (Tok.hasLeadingSpace())
      OS << " [LeadingSpace]";
    if (Tok.isExpandDisabled())
      OS << " [ExpandDisabled]";
    OS << "  ";
  }
  OS << "\n";
}

}  // namespace cppinterp

#endif  // CPPINTERP_UTILS_PRINT_HELPER_H