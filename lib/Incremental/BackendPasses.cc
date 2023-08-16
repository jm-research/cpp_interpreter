#include "cppinterp/Incremental/BackendPasses.h"

#include "clang/Basic/CharInfo.h"
#include "clang/Basic/CodeGenOptions.h"
#include "cppinterp/Incremental/IncrementalJIT.h"
#include "cppinterp/Utils/Platform.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/Inliner.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"

namespace {

class KeepLocalGVPass : public llvm::ModulePass {
  static char ID;

  bool runOnGlobal(llvm::GlobalValue& gv) {
    if (gv.isDeclaration()) {
      return false;  // no change
    }

    // 保留未命名的常量是没有意义的，因为不知道如何引用它们。
    if (!gv.hasName()) {
      return false;
    }

    if (gv.getName().startswith(".str")) {
      return false;
    }

    llvm::GlobalValue::LinkageTypes lt = gv.getLinkage();
    if (!gv.isDiscardableIfUnused(lt)) {
      return false;
    }

    if (lt == llvm::GlobalValue::InternalLinkage) {
      // 我们希望保留这个GlobalValue，但必须告诉JIT链接器，
      // 它不应该在重复的符号上出错。
      gv.setLinkage(llvm::GlobalValue::WeakAnyLinkage);
      return true;  // a change!
    }
    return false;
  }

 public:
  KeepLocalGVPass() : llvm::ModulePass(ID) {}

  bool runOnModule(llvm::Module& module) override {
    bool ret = false;
    for (auto&& f : module)
      ret |= runOnGlobal(f);
    for (auto&& global : module.globals())
      ret |= runOnGlobal(global);
    return ret;
  }
};

class PreventLocalOptPass : public llvm::ModulePass {
  static char ID;

  bool runOnGlobal(llvm::GlobalValue& gv) {
    if (!gv.isDeclaration()) {
      return false;  // no change.
    }

    // gv是一个没有定义的声明。
    // 确保防止任何试图利用实际定义为“局部”的优化，
    // 因为我们无法影响数据段的内存布局以及它们与代码的“接近”程度。

    bool changed = false;

    if (gv.hasLocalLinkage()) {
      gv.setLinkage(llvm::GlobalValue::ExternalLinkage);
      changed = true;
    }

    if (!gv.hasDefaultVisibility()) {
      gv.setVisibility(llvm::GlobalValue::DefaultVisibility);
      changed = true;
    }

    // 最后设置DSO位置，因为setLinkage()和setVisibility()会检查isImplicitDSOLocal()。
    if (gv.isDSOLocal()) {
      gv.setDSOLocal(false);
      changed = true;
    }

    return changed;
  }

 public:
  PreventLocalOptPass() : llvm::ModulePass(ID) {}

  bool runOnModule(llvm::Module& module) override {
    bool ret = false;
    for (auto&& f : module)
      ret |= runOnGlobal(f);
    for (auto&& global : module.globals())
      ret |= runOnGlobal(global);
    return ret;
  }
};

class WeakTypeinfoVTablePass : public llvm::ModulePass {
  static char ID;

  bool runOnGlobalVariable(llvm::GlobalVariable& gv) {
    // 只需要考虑具有外部链接的符号，因为只有这些符号才能被报告为重复。
    if (gv.getLinkage() != llvm::GlobalValue::ExternalLinkage) {
      return false;
    }

    // _ZT 是一个特定的前缀，通常表示类型信息（Type Information）
    if (gv.getName().startswith("_ZT")) {
      // 如果看到一个虚类的虚函数，
      // 它会在每个事务llvm::Module中emit typeinfo和vtable变量来引用它们。
      // 将它们转换为弱链接，以避免来自JIT链接器的重复符号错误。
      gv.setLinkage(llvm::GlobalValue::WeakAnyLinkage);
      return true;  // a change!
    }

    return false;
  }

 public:
  WeakTypeinfoVTablePass() : ModulePass(ID) {}

  bool runOnModule(llvm::Module& M) override {
    bool ret = false;
    for (auto&& GV : M.globals())
      ret |= runOnGlobalVariable(GV);
    return ret;
  }
};

/// 给CUDA模块添加一个后缀，为CUDA特定的函数和变量生成一个唯一的名称。
/// 这对于延迟编译是必要的。如果没有后缀，则无法区分后续模块的tor/dtor、寄存器函数和ptx代码字符串。
class UniqueCUDAStructorName : public llvm::ModulePass {
  static char ID;

  // 给符号附加后缀以使其唯一，
  // 后缀是 "_cppinterp_module_<module number>"
  llvm::SmallString<128> add_module_suffix(const llvm::StringRef symbol_name,
                                           const llvm::StringRef module_name) {
    llvm::SmallString<128> NewFunctionName;
    NewFunctionName.append(symbol_name);
    NewFunctionName.append("_");
    NewFunctionName.append(module_name);

    for (size_t i = 0; i < NewFunctionName.size(); ++i) {
      // Replace everything that is not [a-zA-Z0-9._] with a _. This set
      // happens to be the set of C preprocessing numbers.
      if (!clang::isPreprocessingNumberBody(NewFunctionName[i])) {
        NewFunctionName[i] = '_';
      }
    }

    return NewFunctionName;
  }

  // make CUDA specific variables unique
  bool runOnGlobal(llvm::GlobalValue& gv, const llvm::StringRef module_name) {
    if (gv.isDeclaration()) {
      return false;  // no change.
    }

    if (!gv.hasName()) {
      return false;
    }

    if (gv.getName().equals("__cuda_fatbin_wrapper") ||
        gv.getName().equals("__cuda_gpubin_handle")) {
      gv.setName(add_module_suffix(gv.getName(), module_name));
      return true;
    }

    return false;
  }

  // make CUDA specific functions unique
  bool runOnFunction(llvm::Function& f, const llvm::StringRef module_name) {
    if (f.hasName() && (f.getName().equals("__cuda_module_ctor") ||
                        f.getName().equals("__cuda_module_dtor") ||
                        f.getName().equals("__cuda_register_globals"))) {
      f.setName(add_module_suffix(f.getName(), module_name));
      return true;
    }

    return false;
  }

 public:
  UniqueCUDAStructorName() : llvm::ModulePass(ID) {}

  bool runOnModule(llvm::Module& m) override {
    bool ret = false;
    const llvm::StringRef ModuleName = m.getName();
    for (auto&& F : m)
      ret |= runOnFunction(F, ModuleName);
    for (auto&& G : m.globals())
      ret |= runOnGlobal(G, ModuleName);
    return ret;
  }
};

/// 将已经存在的弱符号的定义替换为声明。这减少了发射符号的数量。
class ReuseExistingWeakSymbols : public llvm::ModulePass {
  static char ID;
  cppinterp::IncrementalJIT& jit_;

  bool shouldRemoveGlobalDefinition(llvm::GlobalValue& gv) {
    // Existing *weak* symbols can be re-used thanks to ODR.
    llvm::GlobalValue::LinkageTypes LT = gv.getLinkage();
    if (!gv.isDiscardableIfUnused(LT) || !gv.isWeakForLinker(LT)) {
      return false;
    }

    // Find the symbol as existing, previously compiled symbol in the JIT...
    if (jit_.doesSymbolAlreadyExist(gv.getName())) {
      return true;
    }

    // ...or in shared libraries (without auto-loading).
    std::string Name = gv.getName().str();
    return llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(Name);
  }

  bool runOnVar(llvm::GlobalVariable& gv) {
    if (gv.isDeclaration()) {
      return false;  // no change.
    }
    if (shouldRemoveGlobalDefinition(gv)) {
      gv.setInitializer(nullptr);  // make this a declaration
      return true;                 // a change!
    }
    return false;  // no change.
  }

  bool runOnFunc(llvm::Function& func) {
    if (func.isDeclaration()) {
      return false;  // no change.
    }

    if (func.getInstructionCount() < 50) {
      // 这是一个小函数。保留它的定义以保留它用于内联:
      // jit它的成本很小，并且调用被内联的可能性很高。
      return false;
    }
    if (shouldRemoveGlobalDefinition(func)) {
      func.deleteBody();  // make this a declaration
      return true;        // a change!
    }
    return false;  // no change.
  }

 public:
  ReuseExistingWeakSymbols(cppinterp::IncrementalJIT& jit)
      : ModulePass(ID), jit_(jit) {}

  bool runOnModule(llvm::Module& m) override {
    bool ret = false;
    for (auto&& F : m)
      ret |= runOnFunc(F);
    for (auto&& G : m.globals())
      ret |= runOnVar(G);
    return ret;
  }
};

}  // namespace

char KeepLocalGVPass::ID = 0;
char PreventLocalOptPass::ID = 0;
char WeakTypeinfoVTablePass::ID = 0;
char UniqueCUDAStructorName::ID = 0;
char ReuseExistingWeakSymbols::ID = 0;

namespace cppinterp {

BackendPasses::~BackendPasses() {}

void BackendPasses::CreatePasses(llvm::Module& module, int opt_level) {
  // 处理禁用LLVM优化，其中我们希望保留任何优化之前的内部模块。
  if (cgopts_.DisableLLVMPasses) {
    opt_level = 0;
    // 至少要总是保持ForceInline -不内联对lib++来说是致命的。
    // Inlining = cgopts_.NoInlining;
  }

  llvm::PassManagerBuilder PMBuilder;
  PMBuilder.OptLevel = opt_level;
  PMBuilder.SizeLevel = cgopts_.OptimizeSize;
  PMBuilder.SLPVectorize = opt_level > 1 ? 1 : 0;   // cgopts_.VectorizeSLP
  PMBuilder.LoopVectorize = opt_level > 1 ? 1 : 0;  // cgopts_.VectorizeLoop

  PMBuilder.DisableUnrollLoops = !cgopts_.UnrollLoops;
  PMBuilder.MergeFunctions = cgopts_.MergeFunctions;
  PMBuilder.RerollLoops = cgopts_.RerollLoops;

  PMBuilder.LibraryInfo =
      new llvm::TargetLibraryInfoImpl(tm_.getTargetTriple());

  // At O0 and O1 we only run the always inliner which is more efficient. At
  // higher optimization levels we run the normal inliner.
  // See also call to `CGOpts.setInlining()` in CIFactory!
  if (PMBuilder.OptLevel <= 1) {
    bool InsertLifetimeIntrinsics = PMBuilder.OptLevel != 0;
    PMBuilder.Inliner =
        llvm::createAlwaysInlinerLegacyPass(InsertLifetimeIntrinsics);
  } else {
    PMBuilder.Inliner = llvm::createFunctionInliningPass(
        opt_level, PMBuilder.SizeLevel,
        (!cgopts_.SampleProfileFile.empty() && cgopts_.PrepareForThinLTO));
  }

  // Set up the per-module pass manager.
  pm_[opt_level].reset(new llvm::legacy::PassManager());

  pm_[opt_level]->add(new KeepLocalGVPass());
  pm_[opt_level]->add(new PreventLocalOptPass());
  pm_[opt_level]->add(new WeakTypeinfoVTablePass());
  pm_[opt_level]->add(new ReuseExistingWeakSymbols(jit_));

  // The function __cuda_module_ctor and __cuda_module_dtor will just generated,
  // if a CUDA fatbinary file exist. Without file path there is no need for the
  // function pass.
  if (!cgopts_.CudaGpuBinaryFileName.empty())
    pm_[opt_level]->add(new UniqueCUDAStructorName());
  pm_[opt_level]->add(
      createTargetTransformInfoWrapperPass(tm_.getTargetIRAnalysis()));

  tm_.adjustPassManager(PMBuilder);

  PMBuilder.addExtension(
      llvm::PassManagerBuilder::EP_EarlyAsPossible,
      [&](const llvm::PassManagerBuilder&, llvm::legacy::PassManagerBase& PM) {
        PM.add(llvm::createAddDiscriminatorsPass());
      });

  // if (!cgopts_.RewriteMapFiles.empty())
  //   addSymbolRewriterPass(cgopts_, pm_);

  PMBuilder.populateModulePassManager(*pm_[opt_level]);

  fpm_[opt_level].reset(new llvm::legacy::FunctionPassManager(&module));
  fpm_[opt_level]->add(
      createTargetTransformInfoWrapperPass(tm_.getTargetIRAnalysis()));
  if (cgopts_.VerifyModule)
    fpm_[opt_level]->add(llvm::createVerifierPass());
  PMBuilder.populateFunctionPassManager(*fpm_[opt_level]);
}

void BackendPasses::runOnModule(llvm::Module& module, int opt_level) {
  if (opt_level < 0)
    opt_level = 0;
  if (opt_level > 3)
    opt_level = 3;

  if (!pm_[opt_level])
    CreatePasses(module, opt_level);

  static constexpr std::array<llvm::CodeGenOpt::Level, 4> CGOptLevel{
      {llvm::CodeGenOpt::None, llvm::CodeGenOpt::Less,
       llvm::CodeGenOpt::Default, llvm::CodeGenOpt::Aggressive}};
  // TM's OptLevel is used to build orc::SimpleCompiler passes for every Module.
  tm_.setOptLevel(CGOptLevel[opt_level]);

  // Run the per-function passes on the module.
  fpm_[opt_level]->doInitialization();
  for (auto&& I : module.functions())
    if (!I.isDeclaration())
      fpm_[opt_level]->run(I);
  fpm_[opt_level]->doFinalization();

  pm_[opt_level]->run(module);
}

}  // namespace cppinterp
