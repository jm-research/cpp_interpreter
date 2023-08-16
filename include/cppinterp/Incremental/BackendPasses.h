#ifndef CPPINTERP_INCREMENTAL_BACKEND_PASSES_H
#define CPPINTERP_INCREMENTAL_BACKEND_PASSES_H

#include <array>
#include <memory>

#include "llvm/IR/LegacyPassManager.h"

namespace llvm {
class Function;
class LLVMContext;
class Module;
class PassManagerBuilder;
class TargetMachine;

namespace legacy {
class FunctionPassManager;
class PassManager;
}  // namespace legacy
}  // namespace llvm

namespace clang {
class CodeGenOptions;
class LangOptions;
class TargetOptions;
}  // namespace clang

namespace cppinterp {
class IncrementalJIT;

/// 在IR上运行pass。
/// 一旦我们可以从ModuleBuilder迁移到clang的CodeGen/BackendUtil中，就删除它。
class BackendPasses {
  std::array<std::unique_ptr<llvm::legacy::PassManager>, 4> pm_;
  std::array<std::unique_ptr<llvm::legacy::FunctionPassManager>, 4> fpm_;

  llvm::TargetMachine& tm_;
  IncrementalJIT& jit_;
  const clang::CodeGenOptions& cgopts_;

  void CreatePasses(llvm::Module& module, int opt_level);

 public:
  BackendPasses(const clang::CodeGenOptions& cgopts, IncrementalJIT& jit,
                llvm::TargetMachine& tm);
  ~BackendPasses();

  void runOnModule(llvm::Module& module, int opt_level);
};
}  // namespace cppinterp

#endif  // CPPINTERP_INCREMENTAL_BACKEND_PASSES_H