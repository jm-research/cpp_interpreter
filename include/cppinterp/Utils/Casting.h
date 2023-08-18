#ifndef CPPINTERP_UTILS_CASTING_H
#define CPPINTERP_UTILS_CASTING_H

#include <cstdint>

namespace cppinterp {
namespace utils {

template <class T>
void* FunctionToVoidPtr(T* funptr) {
  union {
    T* f;
    void* v;
  } tmp;
  tmp.f = funptr;
  return tmp.v;
}

template <class T>
T UIntToFunctionPtr(uintptr_t ptr) {
  union {
    T f;
    uintptr_t v;
  } tmp;
  tmp.v = ptr;
  return tmp.f;
}

template <class T>
T VoidToFunctionPtr(void* ptr) {
  union {
    T f;
    void* v;
  } tmp;
  tmp.v = ptr;
  return tmp.f;
}

}  // namespace utils
}  // namespace cppinterp

#endif  // CPPINTERP_UTILS_CASTING_H