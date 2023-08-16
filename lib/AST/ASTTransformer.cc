#include "cppinterp/AST/ASTTransformer.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclGroup.h"

namespace cppinterp {

// pin the vtable here since there is no point to create dedicated to that
// cpp file.
ASTTransformer::~ASTTransformer() {}

void ASTTransformer::Emit(clang::DeclGroupRef dgr) {
  consumer_->HandleTopLevelDecl(dgr);
}

}  // namespace cppinterp