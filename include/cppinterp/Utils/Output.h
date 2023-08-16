#ifndef CPPINTERP_UTILS_OUTPUT_H
#define CPPINTERP_UTILS_OUTPUT_H

#include <string>

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

namespace cppinterp {
namespace utils {

/// The 'stdout' stream. llvm::raw_ostream wrapper of std::cout
llvm::raw_ostream& outs();

/// The 'stderr' stream. llvm::raw_ostream wrapper of std::cerr
llvm::raw_ostream& errs();

/// “日志”流。当前返回cppinterp::errors()。
/// 这将匹配clang和gcc打印到stderr的某些信息。
/// 如果主机进程需要为自身或实际错误保留stderr，
/// 则可以编辑该函数以返回一个单独的流。
llvm::raw_ostream& log();

/// Wrappers around buffered llvm::raw_ostreams.
/// outstring<N> with N > 0 are the fastest, using a stack allocated buffer.
/// outstring<0> outputs directly into a std:string.
template <size_t N = 512>
class outstring {
  llvm::SmallString<N> buf_;
  llvm::raw_svector_ostream stream_;

 public:
  outstring() : stream_(buf_) {}

  template <typename T>
  llvm::raw_ostream& operator<<(const T& v) {
    stream_ << v;
    return stream_;
  }

  llvm::StringRef str() { return stream_.str(); }

  operator llvm::raw_ostream&() { return stream_; }
};

template <>
class outstring<0> {
  std::string str_;
  llvm::raw_string_ostream stream_;

 public:
  outstring() : stream_(str_) {}

  template <typename T>
  llvm::raw_ostream& operator<<(const T& v) {
    stream_ << v;
    return stream_;
  }

  std::string& str() { return stream_.str(); }

  operator llvm::raw_ostream&() { return stream_; }
};

typedef outstring<512> ostrstream;
typedef outstring<128> smallstream;
typedef outstring<1024> largestream;
typedef outstring<0> stdstrstream;
}  // namespace utils

using utils::errs;
using utils::log;
using utils::outs;

using utils::largestream;
using utils::ostrstream;
using utils::smallstream;
using utils::stdstrstream;

}  // namespace cppinterp

#endif  // CPPINTERP_UTILS_OUTPUT_H