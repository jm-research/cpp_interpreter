#ifndef CPPINTERP_AST_AST_TRANSFORMER_H
#define CPPINTERP_AST_AST_TRANSFORMER_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclGroup.h"
#include "cppinterp/Interpreter/Transaction.h"
#include "llvm/ADT/PointerIntPair.h"

namespace clang {
class ASTConsumer;
class Decl;
class DeclGroupRef;
class Sema;
}  // namespace clang

namespace cppinterp {

class CompilationOptions;

/// 如果您想在生成代码之前更改/分析声明，则继承该类。
class ASTTransformer {
 protected:
  clang::Sema* sema_;

 private:
  clang::ASTConsumer* consumer_;
  Transaction* transaction_;

 public:
  using Result = llvm::PointerIntPair<clang::Decl*, 1, bool>;

  ASTTransformer(clang::Sema* sema)
      : sema_(sema), consumer_(nullptr), transaction_(nullptr) {}

  virtual ~ASTTransformer();

  clang::Sema* getSemaPtr() const { return sema_; }

  void SetConsumer(clang::ASTConsumer* consumer) { consumer_ = consumer; }

  Transaction* getTransaction() const { return transaction_; }

  CompilationOptions getCompilationOpts() const {
    return transaction_->getCompilationOpts();
  }

  CompilationOptions& getCompilationOpts() {
    return transaction_->getCompilationOpts();
  }

  /// Emit declarations that are created during the transformation.
  void Emit(clang::DeclGroupRef dgr);

  void Emit(clang::Decl* decl) { Emit(clang::DeclGroupRef(decl)); }

  Result Transform(clang::Decl* decl, Transaction* transaction) {
    transaction_ = transaction;
    return Transform(decl);
  }

 protected:
  virtual Result Transform(clang::Decl* decl) = 0;
};

class WrapperTransformer : public ASTTransformer {
 public:
  WrapperTransformer(clang::Sema* sema) : ASTTransformer(sema) {}
};

}  // namespace cppinterp

#endif  // CPPINTERP_AST_AST_TRANSFORMER_H