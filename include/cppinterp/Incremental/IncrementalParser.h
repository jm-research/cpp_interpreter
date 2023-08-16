#ifndef CPPINTERP_INCREMENTAL_INCREMENTAL_PARSER_H
#define CPPINTERP_INCREMENTAL_INCREMENTAL_PARSER_H

#include <deque>
#include <memory>
#include <vector>

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
struct GenericValue;
class MemoryBuffer;
class Module;
}  // namespace llvm

namespace clang {
class ASTConsumer;
class CodeGenerator;
class CompilerInstance;
class DiagnosticConsumer;
class Decl;
class FileID;
class ModuleFileExtension;
class Parser;
}  // namespace clang

namespace cppinterp {

class CompilationOptions;
class DeclCollector;
class ExecutionContext;
class Interpreter;
class Transaction;
class TransactionPool;
class ASTTransformer;

/// 负责输入代码的增量解析和编译。
///
/// 该类通过将编译后的增量附加到clang AST来逐行管理编译的整个过程，
/// 它提供对已编译代码的基本操作。参见事务类。
class IncrementalParser {
 private:
  // 解释器上下文（context）
  Interpreter* interpreter_;

  // compiler instance
  std::unique_ptr<clang::CompilerInstance> ci_;

  // parser (incremental)
  std::unique_ptr<clang::Parser> parser_;

  // 每个命令行一个缓冲区，由源文件管理器所有
  std::deque<std::pair<llvm::MemoryBuffer*, clang::FileID>> memory_buffers_;

  // file ID of the memory buffer
  clang::FileID virtual_file_id_;

  // 下一个可用的唯一源位置偏移量。跳过系统sloc
  // 0和虚拟文件中可能实际存在的任何偏移量。
  unsigned virtual_file_loc_offset_ = 100;

  // compiler instance(ci) owns it
  DeclCollector* consumer_;

  // 存储事务。
  // 不需要元素在内存中是连续的，这就是为什么不使用std::vector。
  // 不需要在每次超出容量时都复制元素。
  std::deque<Transaction*> transactions_;

  /// Number of created modules.
  unsigned module_no_ = 0;

  /// Code generator
  clang::CodeGenerator* codegen_ = nullptr;

  /// 可重用的块分配事务池。
  std::unique_ptr<TransactionPool> transaction_pool_;

  /// DiagnosticConsumer instance
  std::unique_ptr<clang::DiagnosticConsumer> diag_consumer_;

  using ModuleFileExtensions =
      std::vector<std::shared_ptr<clang::ModuleFileExtension>>;

 public:
  enum EParseResult { kSuccess, kSuccessWithWarnings, kFailed };

  using ParseResultTransaction =
      llvm::PointerIntPair<Transaction*, 2, EParseResult>;

  IncrementalParser(Interpreter* interp, const char* llvmdir,
                    const ModuleFileExtensions& module_extensions);
  ~IncrementalParser();

  bool isValid(bool initialized = true) const;

  bool Initialize(llvm::SmallVectorImpl<ParseResultTransaction>& result,
                  bool is_child_interpreter);
  clang::CompilerInstance* getCI() const { return ci_.get(); }
  clang::Parser* getParser() const { return parser_.get(); }
  clang::CodeGenerator* getCodeGenerator() const { return codegen_; }
  bool hasCodeGenerator() const { return codegen_; }

  void setDiagnosticConsumer(clang::DiagnosticConsumer* consumer, bool own);
  clang::DiagnosticConsumer* getDiagnosticConsumer() const;

  /// 返回下一个可用的唯一源位置。
  /// 它是无限虚拟文件的偏移量。
  /// 每次使用这个接口时，它都会触发一个内部计数器。
  /// 这对于使用clang中需要有效源位置的各种API非常有用。
  clang::SourceLocation getNextAvailableUniqueSourceLoc();

  Transaction* beginTransaction(const CompilationOptions& opts);

  ParseResultTransaction endTransaction(Transaction* transaction);

  /// 如果事务已完成，则提交事务。即通过消费者链(包括codegen)将其编排管道。
  ///\param[in] prt - 将要提交的事务。
  ///\param[in] clear_diag_client - 是否重置DiagnosticsEngine client。
  void commitTransaction(ParseResultTransaction& prt,
                         bool clear_diag_client = true);

  /// 在未解析的事务上运行消费者(例如CodeGen)。
  void emitTransaction(Transaction* transaction);

  /// 从事务集合中删除事务。
  void deregisterTransaction(Transaction& transaction);

  /// 返回增量解析器看到的第一个事务。
  const Transaction* getFirstTransaction() const {
    if (transactions_.empty()) {
      return nullptr;
    }
    return transactions_.front();
  }

  /// 返回增量解析器看到的最后一个事务。
  Transaction* getLastTransaction() {
    if (transactions_.empty()) {
      return nullptr;
    }
    return transactions_.back();
  }

  const Transaction* getLastTransaction() const {
    if (transactions_.empty()) {
      return nullptr;
    }
    return transactions_.back();
  }

  /// 使用输入行包装器返回最近的事务，可以是当前的。
  const Transaction* getLastWrapperTransaction() const;

  /// 返回当前活动的事务。
  const Transaction* getCurrentTransaction() const;

  /// 添加用户生成(user-generated)的事务。
  void addTransaction(Transaction* transaction);

  /// 返回解释器看到的事务列表。
  /// 有意地创建一个副本：该函数的目的是用于调试。
  std::vector<const Transaction*> getAllTransactions();

  /// 使用给定的编译选项编译给定的输入。
  ///\param[in] input - The code to compile.
  ///\param[in] Opts - The compilation options to use.
  ///\returns the declarations that were compiled.
  ParseResultTransaction Compile(llvm::StringRef input,
                                 const CompilationOptions& opts);

  void printTransactionStructure() const;

  /// 运行通过对事务进行编码创建的静态初始化器。
  bool runStaticInitOnTransaction(Transaction* transaction) const;

  /// 将transformer添加到增量解析器中。
  void SetTransformers(bool is_child_interpreter);

 private:
  /// 完成(Finalizes)事务的消费者(例如CodeGen)。
  ///\param[in] transaction - the transaction to be finalized
  void codeGenTransaction(Transaction* transaction);

  /// 初始化一个虚拟文件，它将能够生成有效的源位置，带有适当的偏移量。
  void initializeVirtualFile();

  /// 解析的主力，直接查询clang。
  EParseResult ParseInternal(llvm::StringRef input);

  /// Create a unique name for the next llvm::Module
  std::string makeModuleName();

  /// Create a new llvm::Module
  llvm::Module* StartModule();
};

}  // namespace cppinterp

#endif  // CPPINTERP_INCREMENTAL_INCREMENTAL_PARSER_H