#ifndef CPPINTERP_INTERPRETER_INTERPRETER_H
#define CPPINTERP_INTERPRETER_INTERPRETER_H

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

#include "cppinterp/Interpreter/InvocationOptions.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
class raw_ostream;
struct GenericValue;
class ExecutionEngine;
class LLVMContext;
class Module;
class StringRef;
class Type;
template <typename T>
class SmallVectorImpl;
namespace orc {
class DefinitionGenerator;
}
}  // namespace llvm

namespace clang {
class ASTContext;
class ASTDeserializationListener;
class CompilerInstance;
class Decl;
class DeclContext;
class DiagnosticConsumer;
class DiagnosticsEngine;
class FunctionDecl;
class GlobalDecl;
class MacroInfo;
class Module;
class ModuleFileExtension;
class NamedDecl;
class Parser;
class Preprocessor;
class PresumedLoc;
class QualType;
class RecordDecl;
class Sema;
class SourceLocation;
class SourceManager;
class Type;
}  // namespace clang

namespace cppinterp {

class ClangInternalState;
class CompilationOptions;
class DynamicLibraryManager;
class IncrementalCUDADeviceCompiler;
class IncrementalExecutor;
class IncrementalParser;
class InterpreterCallbacks;
class LookupHelper;
class Transaction;
class Value;

/// 实现类似解释器的行为并且管理增量编译。
class Interpreter {
 public:
  // ignore_files_func_t接受const引用，以避免必须包含PresumedLoc的实际定义。
  using ignore_files_func_t = bool (*)(const clang::PresumedLoc&);

  /// 推送一个新事务，该事务将收集来自RAII对象范围内的声明（decls)。
  /// 在销毁时提交事务。
  class PushTransactionRAII {
   public:
    PushTransactionRAII(const Interpreter* interpreter);
    ~PushTransactionRAII();
    void pop() const;

   private:
    Transaction* transaction_;
    const Interpreter* interpreter_;
  };

  class StateDebuggerRAII {
   private:
    const Interpreter* interpreter_;
    std::unique_ptr<ClangInternalState> state_;

   public:
    StateDebuggerRAII(const Interpreter* i);
    ~StateDebuggerRAII();
  };

  /// 描述执行增量编译的不同例程(routines)的返回结果。
  enum CompilationResult { kSuccess, kFailure, kMoreInputExpected };

  /// 描述运行函数的结果。
  enum ExecutionResult {
    /// 函数运行成功。
    kExeSuccess,
    /// 代码生成器不可用;不是错误。
    kExeNoCodeGen,

    /// 第一个错误值。
    kExeFirstError,
    /// 该函数未知，无法调用。
    kExeFunctionNotCompiled = kExeFirstError,
    /// 在编译函数时，遇到未知符号。
    kExeUnresolvedSymbols,
    /// 编译错误
    kExeCompilationError,
    /// 函数是未知的。
    kExeUnkownFunction,
    /// 事务没有模块(可能是CodeGen中的错误)。
    kExeNoModule,

    /// 可能结果的数目。
    kNumExeResults
  };

  /// 提供有关发生解析的上下文的附加信息的标志。这些标志可以按位或组合在一起。
  enum InputFlags {
    /// 输入来自外部文件
    kInputFromFile = 0x01,
    /// 如果`kInputFromFile == 1`，是否每行调用`Interpreter::process()`一次
    kIFFLineByLine = 0x02
  };

  /// 一个临时设置`Interpreter::input_flags_`并在销毁时恢复它的RAII对象。
  struct InputFlagsRAII {
    Interpreter& interp;
    unsigned old_flags;
    InputFlagsRAII(Interpreter& interpreter, unsigned flags)
        : interp(interpreter), old_flags(interpreter.input_flags_) {
      interpreter.input_flags_ = flags;
    }
    ~InputFlagsRAII() { interp.input_flags_ = old_flags; }
  };

 public:
  using ModuleFileExtensions =
      std::vector<std::shared_ptr<clang::ModuleFileExtension>>;

 private:
  InvocationOptions opts_;

  /// llvm库状态，一个线程对象。
  std::unique_ptr<llvm::LLVMContext> llvm_context_;

  /// 实现增量编译的worker类。
  std::unique_ptr<IncrementalParser> incr_parser_;

  /// 提供有关解析发生的上下文的附加信息的标志(参见InputFlags)。
  unsigned input_flags_{};

 public:
  CompilationOptions makeDefaultCompilationOpts() const;
};

}  // namespace cppinterp

#endif  // CPPINTERP_INTERPRETER_INTERPRETER_H