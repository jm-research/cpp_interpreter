#ifndef CPPINTERP_INTERPRETER_VALUE_H
#define CPPINTERP_INTERPRETER_VALUE_H

#include <cstdint>  // for uintptr_t

namespace llvm {
class raw_ostream;
}

namespace clang {
class ASTContext;
class QualType;
}  // namespace clang

#define CPPINTERP_VALUE_BUILTIN_TYPES \
  X(bool, Bool)                       \
  X(char, Char_S)                     \
  X(signed char, SChar)               \
  X(short, Short)                     \
  X(int, Int)                         \
  X(long, Long)                       \
  X(long long, LongLong)              \
  X(unsigned char, UChar)             \
  X(unsigned short, UShort)           \
  X(unsigned int, UInt)               \
  X(unsigned long, ULong)             \
  X(unsigned long long, ULongLong)    \
  X(float, Float)                     \
  X(double, Double)                   \
  X(long double, LongDouble)          \
  X(wchar_t, WChar_S)                 \
  X(char16_t, Char16)                 \
  X(char32_t, Char32)

namespace cppinterp {

class Interpreter;

/// 类型安全的值访问和设置。可以使用简单的(内置的)强制转换，
/// 但最好使用与value类型匹配的模板参数提取值。
class Value {
 public:
  union Storage {
#define X(type, name) type name##_;
    CPPINTERP_VALUE_BUILTIN_TYPES
#undef X

    void* ptr_;
  };

  enum TypeKind : short {
    kInvalid = 0,

#define X(type, name) k##name,
    CPPINTERP_VALUE_BUILTIN_TYPES
#undef X

        kVoid,
    kPtrOrObjTy
  };

 protected:
  /// 实际的值。
  Storage storage_;

  /// 是否Value类需要分配和释放内存。
  bool needs_managed_alloc_ = false;

  TypeKind type_kind_ = Value::kInvalid;

  /// 值类型。
  void* type_ = nullptr;

  Interpreter* interpreter_ = nullptr;

  /// 根据类型的需要分配存储空间。
  void ManagedAllocate();

  /// 在不支持类型转换的情况下断言。
  void AssertOnUnsupportedTypeCast() const;

  bool isPointerOrObjectType() const {
    return type_kind_ == TypeKind::kPtrOrObjTy;
  }

  bool isBuiltinType() const {
    return type_kind_ != Value::kInvalid && !isPointerOrObjectType();
  }

  /// 允许对象部分cast。
  template <typename T>
  struct CastFwd {
    static T cast(const Value& value) {
      if (value.isPointerOrObjectType()) {
        return (T)(uintptr_t)value.getAs<void*>();
      }
      if (value.isInvalid() || value.isVoid()) {
#ifndef NDEBUG
        value.AssertOnUnsupportedTypeCast();
#endif  // NDEBUG
        return T();
      }
      return value.getAs<T>();
    }
  };

  template <typename T>
  struct CastFwd<T*> {
    static T* cast(const Value& value) {
      if (value.isPointerOrObjectType()) {
        return (T*)(uintptr_t)value.getAs<void*>();
      }
#ifndef NDEBUG
      value.AssertOnUnsupportedTypeCast();
#endif  // NDEBUG
      return nullptr;
    }
  };

  /// 通过类型检查将底层存储值强制转换为T来获取该值。
  template <typename T>
  T getAs() const {
    switch (type_kind_) {
      default:
#ifndef NDEBUG
        AssertOnUnsupportedTypeCast();
#endif  // NDEBUG
        return T();
#define X(type, name)  \
  case Value::k##name: \
    return (T)storage_.name##_;
        CPPINTERP_VALUE_BUILTIN_TYPES
#undef X
    }
  }

  void AssertTypeMismatch(const char* type) const;

 public:
  Value() = default;

  Value(const Value& other);

  Value(Value&& other)
      : storage_(other.storage_),
        needs_managed_alloc_(other.needs_managed_alloc_),
        type_kind_(other.type_kind_),
        type_(other.type_),
        interpreter_(other.interpreter_) {
    other.needs_managed_alloc_ = false;
    other.type_kind_ = Value::kInvalid;
  }

  /// 构造一个有效但未初始化的值。
  /// 调用后可以访问该值的存储空间，例如调用ManagedAllocate()。
  Value(clang::QualType type, Interpreter& interp);

  /// 销毁该值，调用ManagedFree()。
  ~Value();

  Value& operator=(const Value& other);
  Value& operator=(Value&& other);

  // Avoid including type_traits.
  template <typename T>
  struct dependent_false {
    static constexpr bool value = false;
    constexpr operator bool() const noexcept { return value; }
  };

  /// 创建一个有效值，包含从argument推导出的clang::Type类型。
  /// 当从编译代码中创建一个具有特定值的Value时很有用。
  template <class T>
  static Value Create(Interpreter& interp, T val) {
    static_assert(dependent_false<T>::value,
                  "Can not instantiate for this type.");
    return {};
  }

  clang::QualType getType() const;
  clang::ASTContext& getASTContext() const;
  Interpreter* getInterpreter() const { return interpreter_; }

  /// 此类型是否需要托管堆，例如storage_成员提供的存储不足，或者需要析构。
  bool needsManagedAllocation() const { return needs_managed_alloc_; }

  /// 确定是否设置了该值。
  bool isValid() const { return type_kind_ != Value::kInvalid; }
  bool isInvalid() const { return !isValid(); }

  /// 确定值是否已设置但为空。
  bool isVoid() const { return type_kind_ == Value::kVoid; }

  /// 确定该值是否已设置且不为空。
  bool hasValue() const { return isValid() && !isVoid(); }

  void** getPtrAddress() { return &storage_.ptr_; }
  void* getPtr() const { return storage_.ptr_; }
  void setPtr(void* value) { storage_.ptr_ = value; }

#define X(type, name)                                 \
  type get##name() const { return storage_.name##_; } \
  void set##name(type value) { storage_.name##_ = value; }

  CPPINTERP_VALUE_BUILTIN_TYPES
#undef X

  /// 使用cast获取值。
  template <typename T>
  T castAs() const {
    return CastFwd<T>::cast(*this);
  }

  /// 值打印的通用接口。
  void print(llvm::raw_ostream& out, bool escape = false) const;
  void dump(bool escape = true) const;
};

template <>
inline void* Value::getAs() const {
  if (isPointerOrObjectType())
    return storage_.ptr_;
  return (void*)getAs<uintptr_t>();
}

#define X(type, name) \
  template <>         \
  Value Value::Create(Interpreter& interp, type val);

CPPINTERP_VALUE_BUILTIN_TYPES

#undef X

}  // namespace cppinterp

#endif  // CPPINTERP_INTERPRETER_VALUE_H