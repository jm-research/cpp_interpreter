#include "cppinterp/Interpreter/Transaction.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"
#include "cppinterp/AST/AST.h"
#include "cppinterp/Utils/Output.h"
#include "llvm/IR/Module.h"

namespace cppinterp {

void Transaction::DelayCallInfo::dump() const {
  clang::PrintingPolicy policy((clang::LangOptions()));
  print(cppinterp::log(), policy, /*indent*/ 0, /*print_instantiation*/ true);
}

void Transaction::DelayCallInfo::print(
    llvm::raw_ostream& out, const clang::PrintingPolicy& policy,
    unsigned indent, bool print_instantiation,
    llvm::StringRef prepend_info /*= ""*/) const {
  static const char* const state_names[Transaction::kCCINumStates] = {
      "kCCINone",
      "kCCIHandleTopLevelDecl",
      "kCCIHandleInterestingDecl",
      "kCCIHandleTagDeclDefinition",
      "kCCIHandleVTable",
      "kCCIHandleCXXImplicitFunctionInstantiation",
      "kCCIHandleCXXStaticMemberVarInstantiation",
      "kCCICompleteTentativeDefinition",
  };
  assert((sizeof(state_names) / sizeof(void*)) == Transaction::kCCINumStates &&
         "Missing states?");

  if (!prepend_info.empty()) {
    out.changeColor(llvm::raw_ostream::RED);
    out << prepend_info;
    out.resetColor();
    out << ", ";
  }

  out.changeColor(llvm::raw_ostream::BLUE);
  out << state_names[call_];
  out.changeColor(llvm::raw_ostream::GREEN);
  out << " <- ";
  out.resetColor();
  for (auto i = dgr_.begin(), e = dgr_.end(); i != e; ++i) {
    if (*i) {
      (*i)->print(out, policy, indent, print_instantiation);
    } else {
      out << "<<NULL DECL>>";
    }
    out << '\n';
  }
}

void Transaction::MacroDirectiveInfo::dump(
    const clang::Preprocessor& pp) const {
  print(cppinterp::log(), pp);
}

void Transaction::MacroDirectiveInfo::print(
    llvm::raw_ostream& out, const clang::Preprocessor& pp) const {
  out << "<MacroDirective: " << md_ << ">";
  out << ii_->getName() << " ";
  out << "(Tokens:)";
  const clang::MacroInfo* mi = md_->getMacroInfo();
  for (unsigned i = 0, e = mi->getNumTokens(); i != e; ++i) {
    const clang::Token& token = mi->getReplacementToken(i);
    pp.DumpToken(token);
  }
  out << "\n";
}

Transaction::Transaction(clang::Sema& sema) : sema_(sema) { Initialize(); }

Transaction::Transaction(const CompilationOptions& opts, clang::Sema& sema)
    : sema_(sema) {
  Initialize();
  opts_ = opts;
}

void Transaction::Initialize() {
  nested_transactions_.reset(0);
  parent_ = 0;
  state_ = kCollecting;
  unloading_ = false;
  issued_diags_ = kNone;
  opts_ = CompilationOptions();
  definition_shadow_ns_ = 0;
  module_ = 0;
  wrapper_fd_ = 0;
  next_ = 0;
  buffer_fid_ = clang::FileID();  // sets it to invalid.
  exe_ = 0;
}

Transaction::~Transaction() {
  if (hasNestedTransactions()) {
    for (size_t i = 0; i < nested_transactions_->size(); ++i) {
      assert(((*nested_transactions_)[i]->getState() == kCommitted ||
              (*nested_transactions_)[i]->getState() == kRolledBack) &&
             "All nested transactions must be committed!");
      delete (*nested_transactions_)[i];
    }
  }
}

void Transaction::setDefinitionShadowNS(clang::NamespaceDecl* ns) {
  assert(!definition_shadow_ns_ && "Transaction has a __cppinterp_N5xxx NS?");
  definition_shadow_ns_ = ns;
  append(static_cast<clang::Decl*>(ns));
}

clang::NamedDecl* Transaction::containsNamedDecl(llvm::StringRef name) const {
  for (auto i = decls_begin(), e = decls_end(); i != e; ++i) {
    for (auto di : i->dgr_) {
      if (clang::NamedDecl* ND = llvm::dyn_cast<clang::NamedDecl>(di)) {
        if (name.equals(ND->getNameAsString()))
          return ND;
      }
    }
  }

  for (auto i = decls_begin(), e = decls_end(); i != e; ++i) {
    for (auto di : i->dgr_) {
      if (clang::LinkageSpecDecl* lsd =
              llvm::dyn_cast<clang::LinkageSpecDecl>(di)) {
        for (clang::Decl* di : lsd->decls()) {
          if (clang::NamedDecl* nd = llvm::dyn_cast<clang::NamedDecl>(di)) {
            if (name.equals(nd->getNameAsString()))
              return nd;
          }
        }
      }
    }
  }
  return nullptr;
}

}  // namespace cppinterp