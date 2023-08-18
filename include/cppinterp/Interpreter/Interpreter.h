#ifndef CPPINTERP_INTERPRETER_INTERPRETER_H
#define CPPINTERP_INTERPRETER_INTERPRETER_H

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

#include "cppinterp/Interpreter/InvocationOptions.h"
#include "cppinterp/Interpreter/RuntimeOptions.h"
#include "llvm/ADT/StringRef.h"

#ifndef LLVM_PATH
#define LLVM_PATH nullptr
#endif

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

  /// 编译的析构函数包装的缓存。
  std::unordered_map<const clang::RecordDecl*, void*> dtor_wrappers_;

  /// 需要唯一名称时使用的计数器。
  mutable unsigned long long unique_counter_;

  /// 打开或关闭调试打印的标志。
  bool print_debug_;

  /// 是否DynamicLookupRuntimeUniverse.h已被解析
  bool dynamic_lookup_declared_;

  /// 打开或关闭动态作用域的标志。
  bool dynamic_lookup_enabled_;

  /// 打开或关闭原始输入的标志。
  bool raw_input_enabled_;

  /// 提供有关解析发生的上下文的附加信息的标志(参见InputFlags)。
  unsigned input_flags_{};

  /// 可以在运行时更改的配置位。这允许用户启用/禁用特定的解释器扩展。
  cppinterp::runtime::RuntimeOptions runtime_options_;

  /// 切换要使用的优化级别的标志。
  int opt_level_;

  /// Interpreter callbacks.
  std::unique_ptr<InterpreterCallbacks> callbacks_;

  /// 关于通过.storeState存储的最后状态的信息
  mutable std::vector<ClangInternalState*> stored_states_;

  enum {
    kStdStringTransaction = 0,  // Transaction known to contain std::string
    kNumTransactions
  };
  mutable const Transaction* cached_transactions_[kNumTransactions] = {};

  CompilationResult DeclareInternal(const std::string& input,
                                    const CompilationOptions& co,
                                    Transaction** transaction = nullptr) const;

  CompilationResult EvaluateInternal(const std::string& input,
                                     CompilationOptions co,
                                     Value* value = nullptr,
                                     Transaction** transaction = nullptr,
                                     size_t wrap_point = 0);

  CompilationResult CodeCompleteInternal(const std::string& input,
                                         unsigned offset);

  const std::string& WrapInput(const std::string& input, std::string& buffer,
                               size_t& wrap_point) const;

  ExecutionResult RunFunction(const clang::FunctionDecl* FD,
                              Value* res = nullptr);

  const clang::FunctionDecl* DeclareCFunction(llvm::StringRef name,
                                              llvm::StringRef code,
                                              bool with_access_control,
                                              Transaction*& transaction);

  /// 初始化运行时和C/C++标准
  Transaction* Initialize(bool no_runtime, bool syntax_only,
                          llvm::SmallVectorImpl<llvm::StringRef>& globals);

  void ShutDown();

  /// 从两个委托构造函数调用的目标构造函数。parent_interp可能是nullptr。
  Interpreter(int argc, const char* const* argv, const char* llvmdir,
              const ModuleFileExtensions& module_extensions,
              void* extra_lib_handle, bool no_runtime,
              const Interpreter* parent_interp);

 public:
  Interpreter(int argc, const char* const* argv,
              const char* llvmdir = LLVM_PATH,
              const ModuleFileExtensions& module_extensions = {},
              void* extra_lib_handle = nullptr, bool no_runtime = false)
      : Interpreter(argc, argv, llvmdir, module_extensions, extra_lib_handle,
                    no_runtime, nullptr) {}

  /// 子解释器的构造函数。
  Interpreter(const Interpreter& parent_interpreter, int argc,
              const char* const* argv, const char* llvmdir = LLVM_PATH,
              const ModuleFileExtensions& module_extensions = {},
              void* extra_lib_handle = nullptr, bool no_runtime = true);

  virtual ~Interpreter();

  /// 解释器是否设置好并准备好使用。
  bool isValid() const;

  const InvocationOptions& getOptions() const { return opts_; }
  InvocationOptions& getOptions() { return opts_; }

  const cppinterp::runtime::RuntimeOptions& getRuntimeOptions() const {
    return runtime_options_;
  }
  cppinterp::runtime::RuntimeOptions& getRuntimeOptions() {
    return runtime_options_;
  }

  const llvm::LLVMContext* getLLVMContext() const {
    return llvm_context_.get();
  }

  llvm::LLVMContext* getLLVMContext() { return llvm_context_.get(); }

  const clang::Parser& getParser() const;
  clang::Parser& getParser();

  /// 返回当前或最后一个事务源位置。
  clang::SourceLocation getSourceLocation(bool skip_wrapper = true) const;

  /// 返回下一个可用的有效空闲源代码位置。
  clang::SourceLocation getNextAvailableLoc() const;

  /// true if -fsyntax-only flag passed.
  bool isInSyntaxOnlyMode() const;

  /// 显示项目的当前版本。
  static const char* getVersion();

  /// 创建可用于各种目的的唯一名称。
  void createUniqueName(std::string& out);

  /// 检查名称是否由解释器的唯一名称生成器生成。
  bool isUniqueName(llvm::StringRef name);

  /// 添加以分隔符分隔的多个包含路径。
  void AddIncludePaths(llvm::StringRef paths_str, const char* delim = ":");

  /// Adds a single include path (-I).
  void AddIncludePath(llvm::StringRef paths_str);

  /// 打印当前使用的包含路径。
  void GetIncludePaths(llvm::SmallVectorImpl<std::string>& incpaths,
                       bool with_system, bool with_flags);

  std::string toString(const char* type, void* obj);

  /// 打印当前使用的包含路径。
  void DumpIncludePath(llvm::raw_ostream* stream = nullptr) const;

  /// 打印当前库路径和加载的库。
  void DumpDynamicLibraryInfo(llvm::raw_ostream* stream = nullptr) const;

  /// 转储各种内部数据。
  void dump(llvm::StringRef what, llvm::StringRef filter);

  /// 将解释器状态存储在文件中，存储AST、包含的文件和查找表。
  void storeInterpreterState(const std::string& name) const;

  /// 将实际的解释器状态与前面存储的解释器状态进行比较。
  void compareInterpreterState(const std::string& name) const;

  /// 将包含的文件打印到临时文件中
  void printIncludedFiles(llvm::raw_ostream& out) const;

  /// 编译给定的输入。
  /// 这个接口运行所有可以运行的程序，包括从声明头文件到运行或求值单个语句。
  /// 只在不知道要处理哪种输入的情况下使用，如果已知，运行特定的接口会更快。
  CompilationResult process(const std::string& input, Value* value = nullptr,
                            Transaction** transaction = nullptr,
                            bool disable_value_printing = false);

  /// 解析不包含语句的输入行，没有代码生成。
  CompilationResult parse(const std::string& input,
                          Transaction** T = nullptr) const;

  /// 通过合成Import decl来加载具有给定名称的c++模块。
  /// 这个接口检查当前目录中是否存在modulemap并加载它。
  bool loadModule(const std::string& module_name, bool complain = true);

  bool loadModule(clang::Module* module, bool complain = true);

  /// 解析不包含语句的输入行。
  CompilationResult parseForModule(const std::string& input);

  /// 补全用户输入。
  CompilationResult codeComplete(const std::string& line, size_t& cursor,
                                 std::vector<std::string>& completions) const;

  /// 编译不包含statement的输入行。
  CompilationResult declare(const std::string& input,
                            Transaction** transaction = nullptr);

  /// 编译只包含表达式的输入行。
  CompilationResult evaluate(const std::string& input, Value& value);

  /// 编译输入行，其中只包含表达式和打印输出执行结果。
  CompilationResult echo(const std::string& input, Value* value = nullptr);

  /// 编译输入行并运行。
  CompilationResult execute(const std::string& input);

  /// 为事务的所有decl生成代码。
  CompilationResult emitAllDecls(Transaction* transaction);

  /// 根据当前解释器查找文件或库包含路径和系统包含路径。
  std::string lookupFileOrLibrary(llvm::StringRef file);

  /// 加载共享(shared)库。
  CompilationResult loadLibrary(const std::string& filename,
                                bool lookup = true);

  /// 加载头文件
  CompilationResult loadHeader(const std::string& filename,
                               Transaction** transaction = nullptr);

  /// 加载头文件或共享库。
  CompilationResult loadFile(const std::string& filename,
                             bool allow_shared_lib = true,
                             Transaction** transaction = nullptr);

  /// 从AST和jit符号中卸载事务。
  void unload(Transaction& transaction);

  /// 卸载给定数量的事务。
  void unload(unsigned number_of_transaction);

  void runAndRemoveStaticDestructors();
  void runAndRemoveStaticDestructors(unsigned number_of_transaction);

  bool isPrintingDebug() const { return print_debug_; }
  void enablePrintDebug(bool print = true) { print_debug_ = print; }

  void enableDynamicLookup(bool value = true);
  bool isDynamicLookupEnabled() const { return dynamic_lookup_enabled_; }

  bool isRawInputEnabled() const { return raw_input_enabled_; }
  void enableRawInput(bool raw = true) { raw_input_enabled_ = raw; }

  unsigned getInputFlags() const { return input_flags_; }
  void setInputFlags(unsigned value) { input_flags_ = value; }

  int getDefaultOptLevel() const { return opt_level_; }
  void setDefaultOptLevel(int opt_level) { opt_level_ = opt_level; }

  clang::CompilerInstance* getCI() const;
  clang::CompilerInstance* getCIOrNull() const;
  clang::Sema& getSema() const;
  clang::DiagnosticsEngine& getDiagnostics() const;

  /// 替换默认的DiagnosticConsumer。
  void replaceDiagnosticConsumer(clang::DiagnosticConsumer* consumer,
                                 bool own = false);
  bool hasReplacedDiagnosticConsumer() const;

  /// 创建合适的默认编译选项。
  CompilationOptions makeDefaultCompilationOpts() const;

  /// 注册一个DefinitionGenerator来动态地为进程中不可用的生成代码提供符号。
  void addGenerator(std::unique_ptr<llvm::orc::DefinitionGenerator> dg);

  ExecutionResult executeTransaction(Transaction& transaction);

  /// 在给定的声明上下文中计算给定的表达式
  Value Evaluate(const char* expr, clang::DeclContext* dc,
                 bool value_printer_req = false);

  /// 解释器回调访问器。
  void setCallbacks(std::unique_ptr<InterpreterCallbacks> C);
  const InterpreterCallbacks* getCallbacks() const { return callbacks_.get(); }
  InterpreterCallbacks* getCallbacks() { return callbacks_.get(); }

  const DynamicLibraryManager* getDynamicLibraryManager() const;
  DynamicLibraryManager* getDynamicLibraryManager();

  const Transaction* getFirstTransaction() const;
  const Transaction* getLastTransaction() const;
  const Transaction* getLastWrapperTransaction() const;
  const Transaction* getCurrentTransaction() const;

  /// 返回当前或最后一个事务。
  const Transaction* getLatestTransaction() const;

  /// 返回对已知包含std::string的事务的引用。
  const Transaction*& getStdStringTransaction() const {
    return cached_transactions_[kStdStringTransaction];
  }

  /// 编译extern "C"函数并返回其地址。
  void* compileFunction(llvm::StringRef name, llvm::StringRef code,
                        bool if_uniq = true, bool with_access_control = true);

  /// 编译(和缓存)析构函数调用记录decl。
  void* compileDtorCallFor(const clang::RecordDecl* rc);

  /// 获取现有全局变量的地址以及它是否被jit编译。
  void* getAddressOfGlobal(const clang::GlobalDecl& gd,
                           bool* from_jit = nullptr) const;

  void* getAddressOfGlobal(llvm::StringRef sym_name,
                           bool* from_jit = nullptr) const;

  /// 按名称获取给定的宏定义。
  const clang::MacroInfo* getMacro(llvm::StringRef name) const;

  /// 按名称获取给定的宏值。
  std::string getMacroValue(llvm::StringRef name,
                            const char* strip = "\"") const;

  /// 添加一个atexit函数。
  void AddAtExitFunc(void (*func)(void*), void* arg);

  /// 运行一次注册的atexit函数列表。
  void runAtExitFuncs();

  void GenerateAutoLoadingMap(llvm::StringRef in_file, llvm::StringRef out_file,
                              bool enable_macros = false,
                              bool enable_logs = true);

  void forwardDeclare(
      Transaction& transaction, clang::Preprocessor& preprocessor,
      clang::ASTContext& ctx, llvm::raw_ostream& out,
      bool enable_macros = false, llvm::raw_ostream* logs = nullptr,
      ignore_files_func_t ignore_files = [](const clang::PresumedLoc&) {
        return false;
      }) const;
};

}  // namespace cppinterp

#endif  // CPPINTERP_INTERPRETER_INTERPRETER_H