#ifndef CPPINTERP_AST_DECL_COLLECTOR_H
#define CPPINTERP_AST_DECL_COLLECTOR_H

#include <memory>
#include <vector>

#include "clang/AST/ASTConsumer.h"
#include "cppinterp/AST/ASTTransformer.h"

namespace clang {
class ASTContext;
class CodeGenerator;
class Decl;
class DeclGroupRef;
class Preprocessor;
class Token;
}  // namespace clang

namespace cppinterp {

class ASTTransformer;
class WrapperTransformer;
class DeclCollector;
class IncrementalParser;
class Transaction;

/// 收集声明并将其填充在Transaction中。
/// 事务成为解释器中的主要构建块。
/// DeclCollector负责附加clang看到的所有声明。
class DeclCollector : public clang::ASTConsumer {
  /// PPCallbacks overrides/ Macro support
  class PPAdapter;

  /// 包含事务AST转换器。
  std::vector<std::unique_ptr<ASTTransformer>> transaction_transformers_;

  /// 包含在包装器上操作的AST转换器。
  std::vector<std::unique_ptr<WrapperTransformer>> wrapper_transformers_;

  IncrementalParser* incr_parser_ = nullptr;
  std::unique_ptr<clang::ASTConsumer> consumer_;
  Transaction* cur_transaction_ = nullptr;

  /// Whether Transform() is active; prevents recursion.
  bool transforming_ = false;

  /// 测试DeclGroupRef的第一个decl是否来自AST文件。
  bool comesFromASTReader(clang::DeclGroupRef dgr) const;
  bool comesFromASTReader(const clang::Decl* decl) const;

  bool Transform(clang::DeclGroupRef& dgr);

  /// 在事务上运行AST转换器。
  ASTTransformer::Result TransformDecl(clang::Decl* decl) const;

 public:
  virtual ~DeclCollector();

  void SetTransformers(
      std::vector<std::unique_ptr<ASTTransformer>>&& all_tt,
      std::vector<std::unique_ptr<WrapperTransformer>>&& all_wt) {
    transaction_transformers_.swap(all_tt);
    wrapper_transformers_.swap(all_wt);
    for (auto&& tt : transaction_transformers_) {
      tt->SetConsumer(this);
    }
    for (auto&& wt : wrapper_transformers_) {
      wt->SetConsumer(this);
    }
  }

  void Setup(IncrementalParser* incr_parser,
             std::unique_ptr<ASTConsumer> consumer, clang::Preprocessor& pp);

  /// \name ASTConsumer overrides
  /// \{
  bool HandleTopLevelDecl(clang::DeclGroupRef dgr) final;
  void HandleInterestingDecl(clang::DeclGroupRef dgr) final;
  void HandleTagDeclDefinition(clang::TagDecl* td) final;
  void HandleVTable(clang::CXXRecordDecl* rd) final;
  void CompleteTentativeDefinition(clang::VarDecl* vd) final;
  void HandleTranslationUnit(clang::ASTContext& ctx) final;
  void HandleCXXImplicitFunctionInstantiation(clang::FunctionDecl* fd) final;
  void HandleCXXStaticMemberVarInstantiation(clang::VarDecl* vd) final;
  /// \}

  Transaction* getTransaction() { return cur_transaction_; }
  const Transaction* getTransaction() const { return cur_transaction_; }
  void setTransaction(Transaction* transaction) {
    cur_transaction_ = transaction;
  }

  // dyn_cast/isa support
  static bool classof(const clang::ASTConsumer*) { return true; }
};

}  // namespace cppinterp

#endif  // CPPINTERP_AST_DECL_COLLECTOR_H