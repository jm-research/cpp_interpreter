#ifndef CPPINTERP_INTERPRETER_CLANG_INTERNAL_STATE_H
#define CPPINTERP_INTERPRETER_CLANG_INTERNAL_STATE_H

#include <memory>
#include <string>

#include "cppinterp/Interpreter/Interpreter.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
class ASTContext;
class CodeGenerator;
class SourceManager;
class Preprocessor;
}  // namespace clang

namespace llvm {
class Module;
class raw_fd_ostream;
class raw_ostream;
}  // namespace llvm

namespace cppinterp {

/// 一个存储底层编译器(clang)当前状态的助手类。它可以用来比较事件发生之前和之后的状态。
class ClangInternalState {
 private:
  std::string lookup_tables_file_;
  std::string included_files_file_;
  std::string ast_file_;
  std::string llvm_module_file_;
  std::string macros_file_;
  const clang::ASTContext& ast_context_;
  const clang::Preprocessor& preprocessor_;
  clang::CodeGenerator* codegen_;
  const llvm::Module* module_;
  const std::string diff_command_;
  const std::string name_;
  /// 取比较后的所有权。
  std::unique_ptr<ClangInternalState> diff_pair_;

 public:
  ClangInternalState(const clang::ASTContext& ac, const clang::Preprocessor& pp,
                     const llvm::Module* module, clang::CodeGenerator* cg,
                     const std::string& name);
  ~ClangInternalState();

  /// 在多个状态对象的情况下可以很容易地引用。
  const std::string& getName() const { return name_; }

  /// 将编译器的所有内部结构存储到流中。
  void store();

  /// 将这些状态与相同对象的当前状态进行比较。
  void compare(const std::string& name, bool verbose);

  ///\brief Runs diff on two files.
  ///\param[in] file1 - A file to diff
  ///\param[in] file2 - A file to diff
  ///\param[in] type - The type/name of the differences to print.
  ///\param[in] verbose - Verbose output.
  ///\param[in] ignores - A list of differences to ignore.
  ///\returns true if there is difference in the contents.
  bool differentContent(
      const std::string& file1, const std::string& file2,
      const char* type = nullptr, bool verbose = false,
      const llvm::SmallVectorImpl<llvm::StringRef>* ignores = nullptr) const;

  ///\brief Return the llvm::Module this state is bound too.
  const llvm::Module* getModule() const { return module_; }

  static void printLookupTables(llvm::raw_ostream& out,
                                const clang::ASTContext& ac);
  static void printIncludedFiles(llvm::raw_ostream& out,
                                 const clang::SourceManager& sm);
  static void printAST(llvm::raw_ostream& out, const clang::ASTContext& ac);
  static void printLLVMModule(llvm::raw_ostream& out,
                              const llvm::Module& module,
                              clang::CodeGenerator& cg);
  static void printMacroDefinitions(llvm::raw_ostream& out,
                                    const clang::Preprocessor& pp);

 private:
  llvm::raw_fd_ostream* createOutputFile(llvm::StringRef out_file,
                                         std::string* temp_path_name = nullptr,
                                         bool remove_file_on_signal = true);
};

}  // namespace cppinterp

#endif  // CPPINTERP_INTERPRETER_CLANG_INTERNAL_STATE_H