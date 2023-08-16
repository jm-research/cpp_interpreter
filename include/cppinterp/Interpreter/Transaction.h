#ifndef CPPINTERP_INTERPRETER_TRANSACTION_H
#define CPPINTERP_INTERPRETER_TRANSACTION_H

#include <memory>

#include "clang/AST/DeclGroup.h"
#include "clang/Basic/SourceLocation.h"
#include "cppinterp/Interpreter/CompilationOptions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Module.h"

namespace clang {
class ASTContext;
class Decl;
class FunctionDecl;
class IdentifierInfo;
class NamedDecl;
class NamespaceDecl;
class MacroDirective;
class Preprocessor;
struct PrintingPolicy;
class Sema;
}  // namespace clang

namespace llvm {
class raw_ostream;
}  // namespace llvm

namespace cppinterp {

class IncrementalExecutor;
class TransactionPool;

/// 立即包含关于已经消费的输入信息。
///
/// 一个事务可以:
/// - 转换: 事务中的一些声明可以被修改，
///         删除或添加一些新的声明。
/// - 回滚: 事务的声明可以被回滚，
///         以便让它们根本没有被看到。
/// - 提交: 可以为事务的内容生成代码。
///
class Transaction {
 public:
  enum ConsumerCallInfo {
    kCCINone,
    kCCIHandleTopLevelDecl,
    kCCIHandleInterestingDecl,
    kCCIHandleTagDeclDefinition,
    kCCIHandleVTable,
    kCCIHandleCXXImplicitFunctionInstantiation,
    kCCIHandleCXXStaticMemberVarInstantiation,
    kCCICompleteTentativeDefinition,
    kCCINumStates
  };

  /// 每个声明组在不同的时间经过不同的接口。
  /// 保留所有最初发生在clang中的调用序列。
  struct DelayCallInfo {
    clang::DeclGroupRef dgr_;
    ConsumerCallInfo call_;

    DelayCallInfo(clang::DeclGroupRef dgr, ConsumerCallInfo cci)
        : dgr_(dgr), call_(cci) {}

    inline bool operator==(const DelayCallInfo& rhs) const {
      return dgr_.getAsOpaquePtr() == rhs.dgr_.getAsOpaquePtr() &&
             call_ == rhs.call_;
    }

    inline bool operator!=(const DelayCallInfo& rhs) const {
      return !operator==(rhs);
    }

    void dump() const;

    void print(llvm::raw_ostream& out, const clang::PrintingPolicy& policy,
               unsigned indent, bool print_instantiation,
               llvm::StringRef prepend_info = "") const;
  };

  /// 每个宏对(macro pair)(是否与decls相同?)在不同的时间通过不同的接口。
  struct MacroDirectiveInfo {
    clang::IdentifierInfo* ii_;
    const clang::MacroDirective* md_;

    MacroDirectiveInfo(clang::IdentifierInfo* ii,
                       const clang::MacroDirective* md)
        : ii_(ii), md_(md) {}

    inline bool operator==(const MacroDirectiveInfo& rhs) const {
      return ii_ == rhs.ii_ && md_ == rhs.md_;
    }

    inline bool operator!=(const MacroDirectiveInfo& rhs) const {
      return !operator==(rhs);
    }

    void dump(const clang::Preprocessor& pp) const;

    void print(llvm::raw_ostream& out, const clang::Preprocessor& pp) const;
  };

 private:
  typedef llvm::SmallVector<DelayCallInfo, 64> DeclQueue;
  typedef llvm::SmallVector<Transaction*, 2> NestedTransactions;

  /// 除了反序列化的声明，所有的声明都可见。
  /// 如果通过遍历clang::DeclContext来收集声明则将错过一次注入(例如模板实例化)。
  DeclQueue decl_queue_;

  /// 所有被反序列化事务的声明，要么来自PCH要么来自PCM。
  DeclQueue deserialized_decl_queue_;

  /// 嵌套性事务列表。
  std::unique_ptr<NestedTransactions> nested_transactions_;

  /// 封闭性事务是嵌套的。
  Transaction* parent_;

  unsigned state_ : 3;

  unsigned issued_diags_ : 2;

  /// 事务当前正在被卸载。目前用于卸载事务时保证系统一致性。
  bool unloading_ : 1;

  /// 控制转换器和代码生成器的选项。
  CompilationOptions opts_;

  /// 如果DefinitionShadower是启用的，'__cppinterp_N5xxx'命名空间将嵌套全局定义(如果有的话)。
  clang::NamespaceDecl* definition_shadow_ns_ = nullptr;

  /// llvm模块包含我们将要恢复的信息。
  std::unique_ptr<llvm::Module> module_;

  /// 这是一个让代码卸载与ORCv2一起工作的hack。
  /// ORCv2引入了资源跟踪器，允许从任何实体化状态卸载代码。
  /// ORCv2 IncrementalJIT立即将模块报告为非挂起，并会设置这个原始指针。
  /// TransactionUnloader检查这个指针以保持当前基础设施的完整性。
  const llvm::Module* compiled_module_ = nullptr;

  /// 使用exe_unload_ on来启动。
  IncrementalExecutor* exe_;

  /// 解释器产生的包装器函数。
  clang::FunctionDecl* wrapper_fd_;

  /// 下一个事务。
  const Transaction* next_;

  /// 保存ASTContext和Preprocessor的Sema。
  clang::Sema& sema_;

  /// 添加macro decls以便能够在错误恢复时恢复。
  typedef llvm::SmallVector<MacroDirectiveInfo, 2> MacroDirectiveInfoQueue;

  /// 所有可见的macro。
  MacroDirectiveInfoQueue macro_directive_info_queue_;

  /// 最顶层内存缓冲区的FileID开启事务。
  clang::FileID buffer_fid_;

  friend class TransactionPool;
  friend class IncrementalJIT;

  void Initialize();

 public:
  enum State {
    kCollecting,
    kCompleted,
    kRolledBack,
    kRolledBackWithErrors,
    kCommitted,
    kNumStates
  };

  enum IssuedDiags { kErrors, kWarnings, kNone };

  typedef DeclQueue::iterator iterator;
  typedef DeclQueue::const_iterator const_iterator;
  typedef DeclQueue::const_reverse_iterator const_reverse_iterator;
  typedef NestedTransactions::const_iterator const_nested_iterator;
  typedef NestedTransactions::const_reverse_iterator
      const_reverse_nested_iterator;
  typedef MacroDirectiveInfoQueue::iterator macros_iterator;
  typedef MacroDirectiveInfoQueue::const_iterator const_macros_iterator;
  typedef MacroDirectiveInfoQueue::const_reverse_iterator
      const_reverse_macros_iterator;

  Transaction(clang::Sema& sema);
  Transaction(const CompilationOptions& opts, clang::Sema& sema);
  ~Transaction();

  iterator decls_begin() { return decl_queue_.begin(); }

  iterator decls_end() { return decl_queue_.end(); }

  const_iterator decls_begin() const { return decl_queue_.begin(); }

  const_iterator decls_end() const { return decl_queue_.end(); }

  const_reverse_iterator rdecls_begin() const { return decl_queue_.rbegin(); }

  const_reverse_iterator rdecls_end() const { return decl_queue_.rend(); }

  iterator deserialized_decls_begin() {
    return deserialized_decl_queue_.begin();
  }

  iterator deserialized_decls_end() { return deserialized_decl_queue_.end(); }

  const_iterator deserialized_decls_begin() const {
    return deserialized_decl_queue_.begin();
  }

  const_iterator deserialized_decls_end() const {
    return deserialized_decl_queue_.end();
  }

  const_reverse_iterator deserialized_rdecls_begin() const {
    return deserialized_decl_queue_.rbegin();
  }

  const_reverse_iterator deserialized_rdecls_end() const {
    return deserialized_decl_queue_.rend();
  }

  const_nested_iterator nested_begin() const {
    if (hasNestedTransactions()) {
      return nested_transactions_->begin();
    }
    return nullptr;
  }

  const_nested_iterator nested_end() const {
    if (hasNestedTransactions()) {
      return nested_transactions_->end();
    }
    return nullptr;
  }

  const_reverse_nested_iterator rnested_begin() const {
    if (hasNestedTransactions()) {
      return nested_transactions_->rbegin();
    }
    return const_reverse_nested_iterator(nullptr);
  }

  const_reverse_nested_iterator rnested_end() const {
    if (hasNestedTransactions()) {
      return nested_transactions_->rend();
    }
    return const_reverse_nested_iterator(nullptr);
  }

  macros_iterator macros_begin() { return macro_directive_info_queue_.begin(); }

  macros_iterator macros_end() { return macro_directive_info_queue_.end(); }

  const_macros_iterator macros_begin() const {
    return macro_directive_info_queue_.begin();
  }

  const_macros_iterator macros_end() const {
    return macro_directive_info_queue_.end();
  }

  const_reverse_macros_iterator rmacros_begin() const {
    return macro_directive_info_queue_.rbegin();
  }

  const_reverse_macros_iterator rmacros_end() const {
    return macro_directive_info_queue_.rend();
  }

  State getState() const { return static_cast<State>(state_); }
  void setState(State val) {
    assert(state_ != kNumStates && "Transaction already returned in the pool");
    state_ = val;
  }

  void setUnloading() { unloading_ = true; }

  IssuedDiags getIssuedDiags() const {
    return static_cast<IssuedDiags>(getTopmostParent()->issued_diags_);
  }
  void setIssuedDiags(IssuedDiags val) {
    getTopmostParent()->issued_diags_ = val;
  }

  const CompilationOptions& getCompilationOpts() const { return opts_; }
  CompilationOptions& getCompilationOpts() { return opts_; }
  void setCompilationOpts(const CompilationOptions& co) {
    assert(getState() == kCollecting && "Something wrong with you?");
    opts_ = co;
  }

  clang::NamespaceDecl* getDefinitionShadowNS() const {
    return definition_shadow_ns_;
  }

  void setDefinitionShadowNS(clang::NamespaceDecl* ns);

  /// 返回事务的第一个声明。
  clang::DeclGroupRef getFirstDecl() const {
    if (!decl_queue_.empty()) {
      return decl_queue_.front().dgr_;
    }
    return clang::DeclGroupRef();
  }

  /// 返回事务的最后一个声明。
  clang::DeclGroupRef getLastDecl() const {
    if (!decl_queue_.empty() && isCompleted())
      return decl_queue_.back().dgr_;
    return clang::DeclGroupRef();
  }

  /// 如果存在带有name的Decl，则返回NamedDecl*，否则返回0。
  clang::NamedDecl* containsNamedDecl(llvm::StringRef name) const;

  /// 返回当前最后一个事务。在事务尚未完成时有用。
  clang::DeclGroupRef getCurrentLastDecl() const {
    if (!decl_queue_.empty()) {
      return decl_queue_.back().dgr_;
    }
    return clang::DeclGroupRef();
  }

  /// 我们假设当设置了事务的最后一个声明时，事务就完成了。
  bool isCompleted() const { return state_ >= kCompleted; }

  /// 如果事务嵌套到另一个事务中，则返回父事务。
  Transaction* getParent() { return parent_; }

  const Transaction* getParent() const { return parent_; }

  /// 如果事务嵌套到另一个事务中，则返回最顶层的事务，否则为this。
  Transaction* getTopmostParent() {
    const Transaction* ConstThis = const_cast<const Transaction*>(this);
    return const_cast<Transaction*>(ConstThis->getTopmostParent());
  }

  const Transaction* getTopmostParent() const {
    const Transaction* ret = this;
    while (ret->getParent()) {
      ret = ret->getParent();
    }
    return ret;
  }

  /// 设置嵌套事务的嵌套事务。
  void setParent(Transaction* parent) { parent_ = parent; }

  bool isNestedTransaction() const { return parent_; }
  bool hasNestedTransactions() const { return nested_transactions_.get(); }

  /// 向事务添加嵌套事务。
  void addNestedTransaction(Transaction* nested);

  /// 删除嵌套事务。
  void removeNestedTransaction(Transaction* nested);

  Transaction* getLastNestedTransaction() const {
    if (!hasNestedTransactions()) {
      return nullptr;
    }
    return nested_transactions_->back();
  }

  /// 返回事务中是否有声明。
  bool empty() const {
    return decl_queue_.empty() && deserialized_decl_queue_.empty() &&
           (!nested_transactions_ || nested_transactions_->empty()) &&
           macro_directive_info_queue_.empty();
  }

  /// 将声明组及其消费接口来源追加到事务。
  void append(DelayCallInfo dci);

  /// 将声明组追加到事务，即使该事务已完成并准备进行codegen。
  /// 谨慎使用。
  void forceAppend(DelayCallInfo dci);

  /// 将声明组附加到事务中，就像通过HandleTopLevelDecl看到它一样。
  void append(clang::DeclGroupRef dgr);

  /// 将声明包装到声明组中，并将其追加到事务。
  void append(clang::Decl* decl);

  void forceAppend(clang::Decl* decl);

  /// 附加宏的声明。
  void append(MacroDirectiveInfo mde);

  /// 清除事务中的所有声明。
  void clear() {
    decl_queue_.clear();
    deserialized_decl_queue_.clear();
    if (nested_transactions_) {
      nested_transactions_->clear();
    }
  }

  llvm::Module* getModule() const { return module_.get(); }
  std::unique_ptr<llvm::Module> takeModule() {
    assert(getModule());
    return std::move(module_);
  }
  void setModule(std::unique_ptr<llvm::Module> module) {
    module_ = std::move(module);
  }

  const llvm::Module* getCompiledModule() const { return compiled_module_; }

  IncrementalExecutor* getExecutor() const { return exe_; }

  clang::FunctionDecl* getWrapperFD() const { return wrapper_fd_; }

  const Transaction* getNext() const { return next_; }
  void setNext(Transaction* transaction) { next_ = transaction; }

  void setBufferFID(clang::FileID fid) { buffer_fid_ = fid; }
  clang::FileID getBufferFID() const { return buffer_fid_; }
  clang::SourceLocation getSourceStart(const clang::SourceManager& sm) const;

  /// 事务可以被重用，并且事务指针不能作为事务的唯一句柄。
  /// 客户端使用唯一句柄来检查解释器是否看到了更多输入。
  unsigned getUniqueID() const;

  /// 擦除给定位置的元素。
  void erase(iterator pos);

  /// 打印出事务中的所有声明。
  void dump() const;

  /// 优雅打印出事务中的所有声明。
  void dumpPretty() const;

  /// 定制打印输出事务中所有声明。
  void print(llvm::raw_ostream& out, const clang::PrintingPolicy& policy,
             unsigned indent = 0, bool print_instantiation = false) const;

  /// 递归地打印事务和所有子事务，而不打印任何decls。
  void printStructure(size_t nindent = 0) const;

  void printStructureBrief(size_t nindent = 0) const;

 private:
  bool comesFromASTReader(clang::DeclGroupRef dgr) const;
};

}  // namespace cppinterp

#endif  // CPPINTERP_INTERPRETER_TRANSACTION_H