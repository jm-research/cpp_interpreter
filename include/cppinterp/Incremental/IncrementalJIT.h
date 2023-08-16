#ifndef CPPINTERP_INCREMENTAL_INCREMENTAL_JIT_H
#define CPPINTERP_INCREMENTAL_INCREMENTAL_JIT_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Target/TargetMachine.h"

namespace clang {
class CompilerInstance;
}

namespace cppinterp {

class IncrementalExecutor;
class Transaction;

class SharedAtomicFlag {
 public:
  SharedAtomicFlag(bool unlocked_state)
      : lock_(std::make_shared<std::atomic<bool>>(unlocked_state)),
        locked_state_(!unlocked_state) {}

  void lock() { lock_->store(locked_state_); }
  void unlock() { lock_->store(!locked_state_); }

  operator bool() const { return lock_->load(); }

 private:
  std::shared_ptr<std::atomic<bool>> lock_;
  const bool locked_state_;
};

class IncrementalJIT {
 public:
  IncrementalJIT(IncrementalExecutor& executor,
                 const clang::CompilerInstance& ci,
                 std::unique_ptr<llvm::orc::ExecutorProcessControl> epc,
                 llvm::Error& err, void* extra_lib_handle, bool verbose);

  /// 注册一个DefinitionGenerator来动态地为进程中不可用的生成代码提供符号。
  void addGenerator(std::unique_ptr<llvm::orc::DefinitionGenerator> dg) {
    jit_->getMainJITDylib().addGenerator(std::move(dg));
  }

  /// 返回一个DefinitionGenerator，它可以为从这个IncrementalJIT对象可访问的符号提供地址。
  /// 此函数可以与addGenerator()结合使用，以跨不同的IncrementalJIT实例提供符号解析。
  std::unique_ptr<llvm::orc::DefinitionGenerator> getGenerator();

  void addModule(Transaction& transaction);

  llvm::Error removeModule(const Transaction& transaction);

  /// 根据其IR名称(来自clang的mangler)获取符号的地址。
  /// include_host_symbols参数控制查找是否应该包含来自主机进程的符号(通过dlsym)。
  void* getSymbolAddress(llvm::StringRef name, bool include_host_symbols);

  /// 检查JIT是否已经发出或知道如何根据其IR名称(来自clang的mangler)发出符号。
  bool doesSymbolAlreadyExist(llvm::StringRef unmangled_name);

  /// 注入一个已知地址的符号。名称没有链接器损坏，即由IR所知。
  llvm::JITTargetAddress addOrReplaceDefinition(llvm::StringRef name,
                                                llvm::JITTargetAddress known_addr);

  llvm::Error runCtors() const {
    return jit_->initialize(jit_->getMainJITDylib());
  }

  /// 获取JIT使用的TargetMachine。
  /// 非const函数因为BackendPasses需要更新OptLevel。
  llvm::TargetMachine &getTargetMachine() { return *tm_; }

 private:
  std::unique_ptr<llvm::orc::LLJIT> jit_;
  llvm::orc::SymbolMap injected_symbols_;
  SharedAtomicFlag skip_host_process_lookup_;
  llvm::StringSet<> forbid_dl_symbols_;
  llvm::orc::ResourceTrackerSP current_rt_;
  std::map<const Transaction*, llvm::orc::ResourceTrackerSP> resource_trackers_;
  std::map<const llvm::Module*, llvm::orc::ThreadSafeModule> compiled_modules_;
  bool jit_link_;
  std::unique_ptr<llvm::TargetMachine> tm_;
  llvm::orc::ThreadSafeContext single_threaded_context_;
};

}  // namespace cppinterp

#endif  // CPPINTERP_INCREMENTAL_INCREMENTAL_JIT_H