#include "cppinterp/Interpreter/ClangInternalState.h"

#include <time.h>

#include <cstdio>
#include <sstream>
#include <string>

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Lex/Preprocessor.h"
#include "cppinterp/Utils/Output.h"
#include "cppinterp/Utils/Platform.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"

namespace cppinterp {

ClangInternalState::ClangInternalState(const clang::ASTContext& ac,
                                       const clang::Preprocessor& pp,
                                       const llvm::Module* module,
                                       clang::CodeGenerator* cg,
                                       const std::string& name)
    : ast_context_(ac),
      preprocessor_(pp),
      codegen_(cg),
      module_(module),
      diff_command_("diff -u --text "),
      name_(name),
      diff_pair_(nullptr) {
  store();
}

ClangInternalState::~ClangInternalState() {
  // cleanup the temporary files:
  remove(lookup_tables_file_.c_str());
  remove(included_files_file_.c_str());
  remove(ast_file_.c_str());
  remove(llvm_module_file_.c_str());
  remove(macros_file_.c_str());
}

void ClangInternalState::store() {
  // Cannot use the stack (private copy ctor)
  std::unique_ptr<llvm::raw_fd_ostream> lookup_tables_os;
  std::unique_ptr<llvm::raw_fd_ostream> included_files_os;
  std::unique_ptr<llvm::raw_fd_ostream> ast_os;
  std::unique_ptr<llvm::raw_fd_ostream> llvm_module_os;
  std::unique_ptr<llvm::raw_fd_ostream> macros_os;

  lookup_tables_os.reset(createOutputFile("lookup", &lookup_tables_file_));
  included_files_os.reset(createOutputFile("included", &included_files_file_));
  ast_os.reset(createOutputFile("ast", &ast_file_));
  llvm_module_os.reset(createOutputFile("module", &llvm_module_file_));
  macros_os.reset(createOutputFile("macros", &macros_file_));

  printLookupTables(*lookup_tables_os, ast_context_);
  printIncludedFiles(*included_files_os, ast_context_.getSourceManager());
  printAST(*ast_os, ast_context_);
  if (module_) {
    printLLVMModule(*llvm_module_os, *module_, *codegen_);
  }
  printMacroDefinitions(*macros_os, preprocessor_);
}

namespace {
std::string getCurrentTimeAsString() {
  time_t rawtime;
  struct tm* timeinfo;
  char buffer[80];

  time(&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer, 80, "%I_%M_%S", timeinfo);
  return buffer;
}
}  // namespace

llvm::raw_fd_ostream* ClangInternalState::createOutputFile(
    llvm::StringRef out_file, std::string* temp_path_name /*= nullptr*/,
    bool remove_file_on_signal /*= true*/) {
  std::unique_ptr<llvm::raw_fd_ostream> OS;
  std::string OSFile;
  llvm::SmallString<256> OutputPath;
  llvm::sys::path::system_temp_directory(/*erasedOnReboot*/ false, OutputPath);

  // 只有在父目录存在时才创建临时目录(或者创建丢失的目录是真的)，
  // 并且我们可以实际写入OutPath，否则我们希望尽早失败。
  llvm::SmallString<256> TempPath(OutputPath);
  llvm::sys::fs::make_absolute(TempPath);
  assert(llvm::sys::fs::is_directory(TempPath.str()) && "Must be a folder.");
  // Create a temporary file.
  llvm::sys::path::append(TempPath, "cppinterp-" + out_file);
  TempPath += "-" + getCurrentTimeAsString();
  TempPath += "-%%%%%%%%";
  int fd;
  if (llvm::sys::fs::createUniqueFile(TempPath.str(), fd, TempPath) !=
      std::errc::no_such_file_or_directory) {
    OS.reset(new llvm::raw_fd_ostream(fd, /*shouldClose=*/true));
    OSFile = TempPath.str().str();
  }

  // Make sure the out stream file gets removed if we crash.
  if (remove_file_on_signal)
    llvm::sys::RemoveFileOnSignal(OSFile);

  if (temp_path_name)
    *temp_path_name = OSFile;

  return OS.release();
}

void ClangInternalState::compare(const std::string& name, bool verbose) {
  assert(name == name_ && "Different names!?");
  diff_pair_.reset(new ClangInternalState(ast_context_, preprocessor_, module_,
                                          codegen_, name));
  std::string differences = "";
  // Ignore the builtins
  llvm::SmallVector<llvm::StringRef, 1024> builtinNames;
  const clang::Builtin::Context& BuiltinCtx = ast_context_.BuiltinInfo;
  for (auto i = clang::Builtin::NotBuiltin + 1;
       i != clang::Builtin::FirstTSBuiltin; ++i) {
    llvm::StringRef Name(BuiltinCtx.getName(i));
    if (Name.startswith("__builtin"))
      builtinNames.emplace_back(Name);
  }

  for (auto&& BuiltinInfo : ast_context_.getTargetInfo().getTargetBuiltins()) {
    llvm::StringRef Name(BuiltinInfo.Name);
    if (!Name.startswith("__builtin"))
      builtinNames.emplace_back(Name);
#ifndef NDEBUG
    else  // Make sure it's already in the list
      assert(std::find(builtinNames.begin(), builtinNames.end(), Name) ==
                 builtinNames.end() &&
             "Not in list!");
#endif
  }

  builtinNames.push_back(".*__builtin.*");

  differentContent(lookup_tables_file_, diff_pair_->lookup_tables_file_,
                   "lookup tables", verbose, &builtinNames);

  // We create a virtual file for each input line in the format input_line_N.
  llvm::SmallVector<llvm::StringRef, 2> input_lines;
  input_lines.push_back("input_line_[0-9].*");
  differentContent(included_files_file_, diff_pair_->included_files_file_,
                   "included files", verbose, &input_lines);

  differentContent(ast_file_, diff_pair_->ast_file_, "AST", verbose);

  if (module_) {
    assert(codegen_ && "Must have CodeGen set");
    // We want to skip the intrinsics
    builtinNames.clear();
    for (const auto& Func : module_->getFunctionList()) {
      if (Func.isIntrinsic())
        builtinNames.emplace_back(Func.getName());
    }
    differentContent(llvm_module_file_, diff_pair_->llvm_module_file_,
                     "llvm Module", verbose, &builtinNames);
  }

  differentContent(macros_file_, diff_pair_->macros_file_, "Macro Definitions",
                   verbose);
}

bool ClangInternalState::differentContent(
    const std::string& file1, const std::string& file2, const char* type,
    bool verbose,
    const llvm::SmallVectorImpl<llvm::StringRef>* ignores /*=0*/) const {
  std::string diffCall = diff_command_;
  if (ignores) {
    for (const llvm::StringRef& ignore : *ignores) {
      diffCall += " --ignore-matching-lines=\".*";
      diffCall += ignore;
      diffCall += ".*\"";
    }
  }
  diffCall += " ";
  diffCall += file1;
  diffCall += " ";
  diffCall += file2;

  llvm::SmallString<1024> Difs;
  platform::Popen(diffCall, Difs);

  if (verbose)
    cppinterp::log() << diffCall << "\n";

  if (Difs.empty())
    return false;

  if (type) {
    cppinterp::log() << "Differences in the " << type << ":\n";
    cppinterp::log() << Difs << "\n";
  }
  return true;
}

class DumpLookupTables : public clang::RecursiveASTVisitor<DumpLookupTables> {
 private:
  llvm::raw_ostream& m_OS;

 public:
  DumpLookupTables(llvm::raw_ostream& OS) : m_OS(OS) {}
  bool VisitDecl(clang::Decl* D) {
    if (clang::DeclContext* DC = llvm::dyn_cast<clang::DeclContext>(D))
      VisitDeclContext(DC);
    return true;
  }

  bool VisitDeclContext(clang::DeclContext* DC) {
    // If the lookup is pending for building, force its creation.
    if (DC == DC->getPrimaryContext() && !DC->getLookupPtr())
      DC->buildLookup();
    DC->dumpLookups(m_OS);
    return true;
  }
};

void ClangInternalState::printLookupTables(llvm::raw_ostream& Out,
                                           const clang::ASTContext& C) {
  DumpLookupTables dumper(Out);
  dumper.TraverseDecl(C.getTranslationUnitDecl());
}

void ClangInternalState::printIncludedFiles(llvm::raw_ostream& Out,
                                            const clang::SourceManager& SM) {
  // FileInfos are stored as a mapping, and invalidating the cache
  // can change iteration order.
  std::vector<std::string> Parsed, AST;
  for (clang::SourceManager::fileinfo_iterator I = SM.fileinfo_begin(),
                                               E = SM.fileinfo_end();
       I != E; ++I) {
    const clang::FileEntry* FE = I->first;
    // 我们的错误恢复清除了FileEntry的缓存，但保留了FileEntry的指针，
    // 这样如果它被smb(如SourceManager)使用，它就不会悬空。
    // 在这种情况下，我们不应该打印FileName，因为在语义上它不存在。
    if (!I->second)
      continue;
    std::string fileName(FE->getName());
    if (!(fileName.compare(0, 5, "/usr/") == 0 &&
          fileName.find("/bits/") != std::string::npos) &&
        fileName.compare("-")) {
      if (I->second->getBufferDataIfLoaded()) {
        // There is content - a memory buffer or a file.
        // We know it's a file because we started off the FileEntry.
        Parsed.emplace_back(std::move(fileName));
      } else
        AST.emplace_back(std::move(fileName));
    }
  }
  auto DumpFiles = [&Out](const char* What, std::vector<std::string>& Files) {
    if (Files.empty())
      return;
    Out << What << ":\n";
    std::sort(Files.begin(), Files.end());
    for (auto&& FileName : Files)
      Out << " " << FileName << '\n';
  };
  DumpFiles("Parsed", Parsed);
  DumpFiles("From AST file", AST);
}

void ClangInternalState::printAST(llvm::raw_ostream& Out, const clang::ASTContext& C) {
  clang::TranslationUnitDecl* TU = C.getTranslationUnitDecl();
  unsigned Indentation = 0;
  bool PrintInstantiation = false;
  std::string ErrMsg;
  clang::PrintingPolicy policy = C.getPrintingPolicy();
  TU->print(Out, policy, Indentation, PrintInstantiation);
  // TODO: For future when we relpace the bump allocation with slab.
  //
  // Out << "Allocated memory: " << C.getAllocatedMemory();
  // Out << "Side table allocated memory: " << C.getSideTableAllocatedMemory();
  Out.flush();
}

void ClangInternalState::printLLVMModule(llvm::raw_ostream& Out,
                                         const llvm::Module& M,
                                         clang::CodeGenerator& CG) {
  M.print(Out, /*AssemblyAnnotationWriter*/ nullptr);
  //CG.print(Out);
}

void ClangInternalState::printMacroDefinitions(llvm::raw_ostream& Out,
                                               const clang::Preprocessor& PP) {
  stdstrstream contentsOS;
  //PP.printMacros(contentsOS);
  Out << "Ordered Alphabetically:\n";
  std::vector<std::string> elems;
  {
    // Split the string into lines.
    char delim = '\n';
    std::stringstream ss(contentsOS.str());
    std::string item;
    while (std::getline(ss, item, delim)) {
      elems.push_back(item);
    }
    // Sort them alphabetically
    std::sort(elems.begin(), elems.end());
  }
  for (std::vector<std::string>::iterator I = elems.begin(), E = elems.end();
       I != E; ++I)
    Out << *I << '\n';
  Out.flush();
}

}  // namespace cppinterp