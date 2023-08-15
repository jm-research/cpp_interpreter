#include "cppinterp/AST/AST.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"

namespace cppinterp {
namespace ast {

namespace analyze {

bool IsWrapper(const clang::FunctionDecl* decl) {
  if (!decl) {
    return false;
  }
  if (!decl->getDeclName().isIdentifier()) {
    return false;
  }
  return decl->getName().startswith(synthesize::UniquePrefix);
}

}  // namespace analyze

namespace synthesize {

const char* const UniquePrefix = "__cppinterp_unique";

}
}  // namespace ast
}  // namespace cppinterp