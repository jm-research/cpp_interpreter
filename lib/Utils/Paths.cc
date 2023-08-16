#include "cppinterp/Utils/Paths.h"

#include "clang/Basic/FileManager.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "cppinterp/Utils/Output.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

namespace cppinterp {
namespace utils {

namespace platform {
const char* const kEnvDelim = ":";
}  // namespace platform

bool ExpandEnvVars(std::string& str, bool path) {
  std::size_t dollar_pos = str.find('$');
  while (dollar_pos != std::string::npos) {
    std::size_t s_pos = str.find('/', dollar_pos + 1);
    std::size_t length = str.length();

    if (s_pos != std::string::npos)  // if we found a "/"
      length = s_pos - dollar_pos;

    std::string env_var = str.substr(dollar_pos + 1, length - 1);  //"HOME"
    std::string full_path;
    if (const char* token = ::getenv(env_var.c_str())) {
      full_path = token;
    }

    str.replace(dollar_pos, length, full_path);
    dollar_pos = str.find('$', dollar_pos + 1);  // search for next env variable
  }
  if (!path) {
    return true;
  }
  return llvm::sys::fs::exists(str);
}

using namespace clang;

// Adapted from clang/lib/Frontend/CompilerInvocation.cpp

void CopyIncludePaths(const clang::HeaderSearchOptions& Opts,
                      llvm::SmallVectorImpl<std::string>& incpaths,
                      bool withSystem, bool withFlags) {
  if (withFlags && Opts.Sysroot != "/") {
    incpaths.push_back("-isysroot");
    incpaths.push_back(Opts.Sysroot);
  }

  /// User specified include entries.
  for (unsigned i = 0, e = Opts.UserEntries.size(); i != e; ++i) {
    const HeaderSearchOptions::Entry &E = Opts.UserEntries[i];
    if (E.IsFramework && E.Group != frontend::Angled)
      llvm::report_fatal_error("Invalid option set!");
    switch (E.Group) {
    case frontend::After:
      if (withFlags) incpaths.push_back("-idirafter");
      break;

    case frontend::Quoted:
      if (withFlags) incpaths.push_back("-iquote");
      break;

    case frontend::System:
      if (!withSystem) continue;
      if (withFlags) incpaths.push_back("-isystem");
      break;

    case frontend::IndexHeaderMap:
      if (!withSystem) continue;
      if (withFlags) incpaths.push_back("-index-header-map");
      if (withFlags) incpaths.push_back(E.IsFramework? "-F" : "-I");
      break;

    case frontend::CSystem:
      if (!withSystem) continue;
      if (withFlags) incpaths.push_back("-c-isystem");
      break;

    case frontend::ExternCSystem:
      if (!withSystem) continue;
      if (withFlags) incpaths.push_back("-extern-c-isystem");
      break;

    case frontend::CXXSystem:
      if (!withSystem) continue;
      if (withFlags) incpaths.push_back("-cxx-isystem");
      break;

    case frontend::ObjCSystem:
      if (!withSystem) continue;
      if (withFlags) incpaths.push_back("-objc-isystem");
      break;

    case frontend::ObjCXXSystem:
      if (!withSystem) continue;
      if (withFlags) incpaths.push_back("-objcxx-isystem");
      break;

    case frontend::Angled:
      if (withFlags) incpaths.push_back(E.IsFramework ? "-F" : "-I");
      break;
    }
    incpaths.push_back(E.Path);
  }

  if (withSystem && !Opts.ResourceDir.empty()) {
    if (withFlags) incpaths.push_back("-resource-dir");
    incpaths.push_back(Opts.ResourceDir);
  }
  if (withSystem && withFlags && !Opts.ModuleCachePath.empty()) {
    incpaths.push_back("-fmodule-cache-path");
    incpaths.push_back(Opts.ModuleCachePath);
  }
  if (withSystem && withFlags && !Opts.UseStandardSystemIncludes)
    incpaths.push_back("-nostdinc");
  if (withSystem && withFlags && !Opts.UseStandardCXXIncludes)
    incpaths.push_back("-nostdinc++");
  if (withSystem && withFlags && Opts.UseLibcxx)
    incpaths.push_back("-stdlib=libc++");
  if (withSystem && withFlags && Opts.Verbose)
    incpaths.push_back("-v");
}

void DumpIncludePaths(const clang::HeaderSearchOptions& Opts,
                      llvm::raw_ostream& Out,
                      bool WithSystem, bool WithFlags) {
  llvm::SmallVector<std::string, 100> IncPaths;
  CopyIncludePaths(Opts, IncPaths, WithSystem, WithFlags);
  // print'em all
  for (unsigned i = 0; i < IncPaths.size(); ++i) {
    Out << IncPaths[i] <<"\n";
  }
}

void LogNonExistantDirectory(llvm::StringRef Path) {
  cppinterp::log() << "  ignoring nonexistent directory \"" << Path << "\"\n";
}

static void LogFileStatus(const char* Prefix, const char* FileType,
                          llvm::StringRef Path) {
  cppinterp::log() << Prefix << " " << FileType << " '" << Path << "'\n";
}

bool LookForFile(const std::vector<const char*>& Args, std::string& Path,
                 const clang::FileManager* FM, const char* FileType) {
  if (llvm::sys::fs::is_regular_file(Path)) {
    if (FileType)
      LogFileStatus("Using", FileType, Path);
    return true;
  }
  if (FileType)
    LogFileStatus("Ignoring", FileType, Path);

  SmallString<1024> FilePath;
  if (FM) {
    FilePath.assign(Path);
    if (FM->FixupRelativePath(FilePath) &&
        llvm::sys::fs::is_regular_file(FilePath)) {
      if (FileType)
        LogFileStatus("Using", FileType, FilePath.str());
      Path = FilePath.str().str();
      return true;
    }
    // Don't write same same log entry twice when FilePath == Path
    if (FileType && !FilePath.str().equals(Path))
      LogFileStatus("Ignoring", FileType, FilePath);
  }
  else if (llvm::sys::path::is_absolute(Path))
    return false;

  for (std::vector<const char*>::const_iterator It = Args.begin(),
       End = Args.end(); It < End; ++It) {
    const char* Arg = *It;
    // TODO: Suppport '-iquote' and MSVC equivalent
    if (!::strncmp("-I", Arg, 2) || !::strncmp("/I", Arg, 2)) {
      if (!Arg[2]) {
        if (++It >= End)
          break;
        FilePath.assign(*It);
      }
      else
        FilePath.assign(Arg + 2);

      llvm::sys::path::append(FilePath, Path.c_str());
      if (llvm::sys::fs::is_regular_file(FilePath)) {
        if (FileType)
          LogFileStatus("Using", FileType, FilePath.str());
        Path = FilePath.str().str();
        return true;
      }
      if (FileType)
        LogFileStatus("Ignoring", FileType, FilePath);
    }
  }
  return false;
}

bool SplitPaths(llvm::StringRef PathStr,
                llvm::SmallVectorImpl<llvm::StringRef>& Paths,
                SplitMode Mode, llvm::StringRef Delim, bool Verbose) {
  assert(Delim.size() && "Splitting without a delimiter");

  bool AllExisted = true;
  for (std::pair<llvm::StringRef, llvm::StringRef> Split = PathStr.split(Delim);
       !Split.second.empty(); Split = PathStr.split(Delim)) {

    if (!Split.first.empty()) {
      bool Exists = llvm::sys::fs::is_directory(Split.first);

      AllExisted = AllExisted && Exists;

      if (!Exists) {
        if (Mode == kFailNonExistant) {
          if (Verbose) {
            // Exiting early, but still log all non-existant paths that we have
            LogNonExistantDirectory(Split.first);
            while (!Split.second.empty()) {
              Split = PathStr.split(Delim);
              if (llvm::sys::fs::is_directory(Split.first)) {
                cppinterp::log() << "  ignoring directory that exists \""
                             << Split.first << "\"\n";
              } else
                LogNonExistantDirectory(Split.first);
              Split = Split.second.split(Delim);
            }
            if (!llvm::sys::fs::is_directory(Split.first))
              LogNonExistantDirectory(Split.first);
          }
          return false;
        } else if (Mode == kAllowNonExistant)
          Paths.push_back(Split.first);
        else if (Verbose)
          LogNonExistantDirectory(Split.first);
      } else
        Paths.push_back(Split.first);
    }

    PathStr = Split.second;
  }

  // Trim trailing sep in case of A:B:C:D:
  if (!PathStr.empty() && PathStr.endswith(Delim))
    PathStr = PathStr.substr(0, PathStr.size()-Delim.size());

  if (!PathStr.empty()) {
    if (!llvm::sys::fs::is_directory(PathStr)) {
      AllExisted = false;
      if (Mode == kAllowNonExistant)
        Paths.push_back(PathStr);
      else if (Verbose)
        LogNonExistantDirectory(PathStr);
    } else
      Paths.push_back(PathStr);
  }

  return AllExisted;
}

void AddIncludePaths(llvm::StringRef PathStr, clang::HeaderSearchOptions& HOpts,
                     const char* Delim) {

  llvm::SmallVector<llvm::StringRef, 10> Paths;
  if (Delim && *Delim)
    SplitPaths(PathStr, Paths, kAllowNonExistant, Delim, HOpts.Verbose);
  else
    Paths.push_back(PathStr);

  // Avoid duplicates
  llvm::SmallVector<llvm::StringRef, 10> PathsChecked;
  for (llvm::StringRef Path : Paths) {
    bool Exists = false;
    for (const clang::HeaderSearchOptions::Entry& E : HOpts.UserEntries) {
      if ((Exists = E.Path == Path))
        break;
    }
    if (!Exists)
      PathsChecked.push_back(Path);
  }

  const bool IsFramework = false;
  const bool IsSysRootRelative = true;
  for (llvm::StringRef Path : PathsChecked)
      HOpts.AddPath(Path, clang::frontend::Angled,
                    IsFramework, IsSysRootRelative);

  if (HOpts.Verbose) {
    cppinterp::log() << "Added include paths:\n";
    for (llvm::StringRef Path : PathsChecked)
      cppinterp::log() << "  " << Path << "\n";
  }
}

}  // namespace utils
}  // namespace cppinterp