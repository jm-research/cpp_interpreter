#ifndef CPPINTERP_UTILS_PLATFORM_H
#define CPPINTERP_UTILS_PLATFORM_H

#include <string>

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"

namespace cppinterp {
namespace utils {
namespace platform {

/// 返回当前工作目录。
std::string GetCwd();

/// 获取系统库路径。
bool GetSystemLibraryPaths(llvm::SmallVectorImpl<std::string>& paths);

/// 返回给定路径的规范化版本。
std::string NormalizePath(const std::string& path);

/// Open a handle to a shared library. On Unix the lib is opened with
/// RTLD_LAZY|RTLD_GLOBAL flags.
const void* DLOpen(const std::string& path, std::string* err = nullptr);

/// 在当前进程加载的所有模块中查找给定的符号。
const void* DLSym(const std::string& name, std::string* err = nullptr);

/// 关闭共享库的句柄。
void DLClose(const void* lib, std::string* err = nullptr);

/// 要求给定的符号名称。
std::string Demangle(const std::string& symbol);

/// 如果给定的指针位于有效的内存区域，则返回true。
bool IsMemoryValid(const void* ptr);

///\brief Invoke a command and read it's output.
///
/// \param [in] cmd - Command and arguments to invoke.
/// \param [out] buf - Buffer to write output to.
/// \param [in] std_err_to_std_out - Redirect stderr to stdout.
///
/// \returns whether any output was written to buf.
///
bool Popen(const std::string& cmd, llvm::SmallVectorImpl<char>& buf,
           bool std_err_to_std_out = false);

}  // namespace platform
}  // namespace utils

namespace platform = utils::platform;

}  // namespace cppinterp

#endif  // CPPINTERP_UTILS_PLATFORM_H