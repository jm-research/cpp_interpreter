#ifndef CPPINTERP_INTERPRETER_INVOCATION_OPTIONS_H
#define CPPINTERP_INTERPRETER_INVOCATION_OPTIONS_H

#include <string>
#include <vector>

namespace clang {
class LangOptions;
}  // namespace clang

namespace cppinterp {

/// 该类存储同时被cppinterp和clang::CompilerInvocation使用的选项（options）。
class CompilerOptions {
 public:
  /// 从给定的参数构造CompilerOptions。
  /// 当argc和argv为0时，所有参数都被保存到Remaining中以传递给clang。
  /// 如果argc或argv为0，调用者必须在Remaining中填写应该传递给clang的任何参数。
  CompilerOptions(int argc = 0, const char* const* argv = nullptr);

  void Parse(int argc, const char* const argv[],
             std::vector<std::string>* inputs = nullptr);

  /// './cppinterp -std=gnu++11' or './cppinterp -x c'
  bool DefaultLanguage(const clang::LangOptions* = nullptr) const;

  unsigned Language : 1;
  unsigned ResourceDir : 1;
  unsigned SysRoot : 1;
  unsigned NoBuiltinInc : 1;
  unsigned NoCXXInc : 1;
  unsigned StdVersion : 1;
  unsigned StdLib : 1;
  unsigned HasOutput : 1;
  unsigned Verbose : 1;
  unsigned CxxModules : 1;
  unsigned CUDAHost : 1;
  unsigned CUDADevice : 1;
  /// The output path of any C++ PCMs we're building on demand.
  /// Equal to ModuleCachePath in the HeaderSearchOptions.
  std::string CachePath;
  // If not empty, the name of the module we're currently compiling.
  std::string ModuleName;
  /// Custom path of the CUDA toolkit
  std::string CUDAPath;
  /// Architecture level of the CUDA gpu. Necessary for the
  /// NVIDIA fatbinary tool.
  std::string CUDAGpuArch;

  /// The remaining arguments to pass to clang.
  std::vector<const char*> Remaining;
};

class InvocationOptions {
 public:
  InvocationOptions(int argc, const char* const argv[]);

  /// 假设以这个字符串开头的行包含一个用于MetaProcessor的指令。默认为"."
  std::string MetaString;

  std::vector<std::string> LibsToLoad;
  std::vector<std::string> LibSearchPath;
  std::vector<std::string> Inputs;
  CompilerOptions CompilerOpts;

  unsigned ErrorOut : 1;
  unsigned NoLogo : 1;
  unsigned ShowVersion : 1;
  unsigned Help : 1;
  unsigned NoRuntime : 1;
  unsigned PtrCheck : 1;  /// Enable NullDerefProtectionTransformer
  bool Verbose() const { return CompilerOpts.Verbose; }

  static void PrintHelp();

  // Interactive means no input (or one input that's "-")
  bool IsInteractive() const {
    return Inputs.empty() || (Inputs.size() == 1 && Inputs[0] == "-");
  }
};

}  // namespace cppinterp

#endif  // CPPINTERP_INTERPRETER_INVOCATION_OPTIONS_H