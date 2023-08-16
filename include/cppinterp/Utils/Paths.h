#ifndef CPPINTERP_UTILS_PATHS_H
#define CPPINTERP_UTILS_PATHS_H

#include <string>
#include <vector>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
class raw_ostream;
}

namespace clang {
class HeaderSearchOptions;
class FileManager;
}  // namespace clang

namespace cppinterp {
namespace utils {

namespace platform {
/// 用于拆分环境变量的平台特定分隔符。
/// ':' on Unix, and ';' on Windows
extern const char* const kEnvDelim;
}  // namespace platform

/// 将字符串中的所有$TOKENS替换为环境变量值。
/// \param [in,out] str - String with tokens to replace (in place)
/// \param [in] path - Check if the result is a valid filesystem path.
///
/// \returns 当path为true时，返回str是否被扩展为现有的文件系统对象。
/// 当path为false时，返回值没有意义。
bool ExpandEnvVars(std::string& str, bool path = false);

enum SplitMode {
  kPruneNonExistant,  ///< 不要在输出中添加不存在的路径
  kFailNonExistant,   ///< 在任何不存在的路径上失败
  kAllowNonExistant   ///< 添加所有路径，无论是否存在
};

/// 从PATH字符串中收集组成路径。
/// /bin:/usr/bin:/usr/local/bin -> {/bin, /usr/bin, /usr/local/bin}
///
/// All paths returned existed at the time of the call
/// \param [in] path_str - The PATH string to be split
/// \param [out] paths - All the paths in the string that exist
/// \param [in] mode - If any path doesn't exist stop and return false
/// \param [in] delim - The delimeter to use
/// \param [in] verbose - Whether to print out details as 'clang -v' would
///
/// \return true if all paths existed, otherwise false
///
bool SplitPaths(llvm::StringRef path_str,
                llvm::SmallVectorImpl<llvm::StringRef>& paths,
                SplitMode mode = kPruneNonExistant,
                llvm::StringRef delim = platform::kEnvDelim,
                bool verbose = false);

///\brief Look for given file that can be reachable from current working
/// directory or any user supplied include paths in Args. This is useful
/// to look for a file (precompiled header) before a Preprocessor instance
/// has been created.
///
/// \param [in] Args - The argv vector to look for '-I' & '/I' flags
/// \param [in,out] File - File to look for, may mutate to an absolute path
/// \param [in] FM - File manger to resolve current dir with (can be null)
/// \param [in] FileType - File type for logging or nullptr for no logging
///
/// \return true if File is reachable and is a regular file
///
bool LookForFile(const std::vector<const char*>& Args, std::string& File,
                 const clang::FileManager* FM = nullptr,
                 const char* FileType = nullptr);

///\brief Adds multiple include paths separated by a delimter into the
/// given HeaderSearchOptions.  This adds the paths but does no further
/// processing. See Interpreter::AddIncludePaths or CIFactory::createCI
/// for examples of what needs to be done once the paths have been added.
///
///\param[in] PathStr - Path(s)
///\param[in] Opts - HeaderSearchOptions to add paths into
///\param[in] Delim - Delimiter to separate paths or NULL if a single path
///
void AddIncludePaths(llvm::StringRef PathStr, clang::HeaderSearchOptions& Opts,
                     const char* Delim = platform::kEnvDelim);

///\brief Write to cling::errs that directory does not exist in a format
/// matching what 'clang -v' would do
///
void LogNonExistantDirectory(llvm::StringRef Path);

///\brief Copies the current include paths into the HeaderSearchOptions.
///
///\param[in] Opts - HeaderSearchOptions to read from
///\param[out] Paths - Vector to output elements into
///\param[in] WithSystem - if true, incpaths will also contain system
///       include paths (framework, STL etc).
///\param[in] WithFlags - if true, each element in incpaths will be prefixed
///       with a "-I" or similar, and some entries of incpaths will signal
///       a new include path region (e.g. "-cxx-isystem"). Also, flags
///       defining header search behavior will be included in incpaths, e.g.
///       "-nostdinc".
///
void CopyIncludePaths(const clang::HeaderSearchOptions& Opts,
                      llvm::SmallVectorImpl<std::string>& Paths,
                      bool WithSystem, bool WithFlags);

///\brief Prints the current include paths into the HeaderSearchOptions.
///
///\param[in] Opts - HeaderSearchOptions to read from
///\param[in] Out - Stream to dump to
///\param[in] WithSystem - dump contain system paths (framework, STL etc).
///\param[in] WithFlags - if true, each line will be prefixed
///       with a "-I" or similar, and some entries of incpaths will signal
///       a new include path region (e.g. "-cxx-isystem"). Also, flags
///       defining header search behavior will be included in incpaths, e.g.
///       "-nostdinc".
///
void DumpIncludePaths(const clang::HeaderSearchOptions& Opts,
                      llvm::raw_ostream& Out, bool WithSystem, bool WithFlags);

}  // namespace utils
}  // namespace cppinterp

#endif  // CPPINTERP_UTILS_PATHS_H