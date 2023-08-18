#include "cppinterp/Interpreter/Value.h"

#include <cstring>

#include "clang/AST/ASTContext.h"
#include "clang/AST/CanonicalType.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Type.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/Sema.h"
#include "cppinterp/Interpreter/Interpreter.h"
#include "cppinterp/Utils/Casting.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_os_ostream.h"

namespace {

/// 一种侵入式管理Value分配的类。
class AllocatedValue {
 public:
  using DtorFunc_t = void (*)(void*);

 private:
  mutable unsigned ref_cnt_;
  DtorFunc_t dtor_func_;
  unsigned long alloc_size_;
  unsigned long elems_num_;
  char payload_[1];  // 分配的开始。

  static const unsigned char kCanaryUnconstructedObject[8];

  /// 返回所包含的对象是否已被构造，或者说是否改变了小型对象（金丝雀）。
  bool IsAlive() const {
    // If the canary values are still there
    return (std::memcmp(getPayload(), kCanaryUnconstructedObject,
                        sizeof(kCanaryUnconstructedObject)) != 0);
  }

  AllocatedValue(void* dtor_func, size_t alloc_size, size_t elems_num)
      : ref_cnt_(1),
        dtor_func_(cppinterp::utils::VoidToFunctionPtr<DtorFunc_t>(dtor_func)),
        alloc_size_(alloc_size),
        elems_num_(elems_num) {}

 public:
  /// 分配管理payload_size字节的对象的AllocatedValue所需的内存，并返回负载对象的地址。
  static char* CreatePayload(unsigned payload_size, void* dtor_func,
                             size_t elems_num) {
    if (payload_size < sizeof(kCanaryUnconstructedObject)) {
      payload_size = sizeof(kCanaryUnconstructedObject);
    }

    char* alloc = new char[AllocatedValue::getPayloadOffset() + payload_size];
    AllocatedValue* alloc_val =
        new (alloc) AllocatedValue(dtor_func, payload_size, elems_num);
    std::memcpy(alloc_val->getPayload(), kCanaryUnconstructedObject,
                sizeof(kCanaryUnconstructedObject));
    return alloc_val->getPayload();
  }

  const char* getPayload() const { return payload_; }
  char* getPayload() { return payload_; }

  static unsigned getPayloadOffset() {
    static const AllocatedValue Dummy(0, 0, 0);
    return Dummy.payload_ - (const char*)&Dummy;
  }

  static AllocatedValue* getFromPayload(void* payload) {
    return reinterpret_cast<AllocatedValue*>((char*)payload -
                                             getPayloadOffset());
  }

  void Retain() { ++ref_cnt_; }

  void Release() {
    assert(ref_cnt_ > 0 && "Reference count is already zero.");
    if (--ref_cnt_ == 0) {
      if (dtor_func_ && IsAlive()) {
        assert(elems_num_ && "No elements!");
        char* payload = getPayload();
        const auto skip = alloc_size_ / elems_num_;
        while (elems_num_-- != 0)
          (*dtor_func_)(payload + elems_num_ * skip);
      }
      delete[] (char*)this;
    }
  }
};

/// random
const unsigned char AllocatedValue::kCanaryUnconstructedObject[8] = {
    0x4c, 0x37, 0xad, 0x8f, 0x2d, 0x23, 0x95, 0x91};

}  // namespace

namespace cppinterp {

Value::Value(const Value& other)
    : storage_(other.storage_),
      needs_managed_alloc_(other.needs_managed_alloc_),
      type_kind_(other.type_kind_),
      type_(other.type_),
      interpreter_(other.interpreter_) {
  if (other.needsManagedAllocation()) {
    AllocatedValue::getFromPayload(storage_.ptr_)->Retain();
  }
}

static Value::TypeKind getCorrespondingTypeKind(clang::QualType qt) {
  if (qt->isVoidType()) {
    return Value::kVoid;
  }

  if (const auto* et = llvm::dyn_cast<clang::EnumType>(qt.getTypePtr())) {
    qt = et->getDecl()->getIntegerType();
  }

  if (!qt->isBuiltinType() ||
      qt->castAs<clang::BuiltinType>()->isNullPtrType()) {
    return Value::kPtrOrObjTy;
  }

  switch (qt->getAs<clang::BuiltinType>()->getKind()) {
    default:
#ifndef NDEBUG
      qt->dump();
#endif  // NDEBUG
      assert(false && "Type not supported");
      return Value::kInvalid;
#define X(type, name)            \
  case clang::BuiltinType::name: \
    return Value::k##name;
      CPPINTERP_VALUE_BUILTIN_TYPES
#undef X
  }
}

Value::Value(clang::QualType clang_type, Interpreter& interp)
    : type_kind_(getCorrespondingTypeKind(clang_type)),
      type_(clang_type.getAsOpaquePtr()),
      interpreter_(&interp) {
  if (type_kind_ == Value::kPtrOrObjTy) {
    clang::QualType canon = clang_type.getCanonicalType();
    if ((canon->isPointerType() || canon->isObjectType() ||
         canon->isReferenceType()) &&
        (canon->isRecordType() || canon->isConstantArrayType() ||
         canon->isMemberPointerType())) {
      needs_managed_alloc_ = true;
    }
  }
  if (needsManagedAllocation()) {
    ManagedAllocate();
  }
}

Value& Value::operator=(const Value& other) {
  // 释放old value
  if (needsManagedAllocation()) {
    AllocatedValue::getFromPayload(storage_.ptr_)->Release();
  }

  type_ = other.type_;
  storage_ = other.storage_;
  needs_managed_alloc_ = other.needs_managed_alloc_;
  type_kind_ = other.type_kind_;
  interpreter_ = other.interpreter_;
  if (needsManagedAllocation()) {
    AllocatedValue::getFromPayload(storage_.ptr_)->Retain();
  }

  return *this;
}

Value& Value::operator=(Value&& other) {
  // 释放old value
  if (needsManagedAllocation()) {
    AllocatedValue::getFromPayload(storage_.ptr_)->Release();
  }

  type_ = other.type_;
  storage_ = other.storage_;
  needs_managed_alloc_ = other.needs_managed_alloc_;
  type_kind_ = other.type_kind_;
  interpreter_ = other.interpreter_;
  other.needs_managed_alloc_ = false;
  other.type_kind_ = Value::kInvalid;

  return *this;
}

Value::~Value() {
  if (needsManagedAllocation()) {
    AllocatedValue::getFromPayload(storage_.ptr_)->Release();
  }
}

clang::QualType Value::getType() const {
  return clang::QualType::getFromOpaquePtr(type_);
}

clang::ASTContext& Value::getASTContext() const {
  return interpreter_->getCI()->getASTContext();
}

static size_t GetNumberOfElements(clang::QualType qt) {
  if (const clang::ConstantArrayType* arr_type =
          llvm::dyn_cast<clang::ConstantArrayType>(qt.getTypePtr())) {
    llvm::APInt arr_size(sizeof(size_t) * 8, 1);
    do {
      arr_size *= arr_type->getSize();
      arr_type = llvm::dyn_cast<clang::ConstantArrayType>(
          arr_type->getElementType().getTypePtr());
    } while (arr_type);
    return static_cast<size_t>(arr_size.getZExtValue());
  }
  return 1;
}

void Value::ManagedAllocate() {
  assert(needsManagedAllocation() && "Does not need managed allocation");
  void* dtor_func = nullptr;
  clang::QualType dtor_type = getType();
  if (const clang::ConstantArrayType* arr_type =
          llvm::dyn_cast<clang::ConstantArrayType>(dtor_type.getTypePtr())) {
    dtor_type = arr_type->getElementType();
  }
  if (const clang::RecordType* record_type =
          dtor_type->getAs<clang::RecordType>()) {
    LockCompilationDuringUserCodeExecutionRAII LCDUCER(*interpreter_);
    dtor_func = interpreter_->compileDtorCallFor(record_type->getDecl());
  }

  const clang::ASTContext& ctx = getASTContext();
  unsigned payload_size = ctx.getTypeSizeInChars(getType()).getQuantity();
  storage_.ptr_ = AllocatedValue::CreatePayload(payload_size, dtor_func,
                                                GetNumberOfElements(getType()));
}

void Value::AssertTypeMismatch(const char* type) const {
#ifndef NDEBUG
  assert(isBuiltinType() && "Must be a builtin!");
  const clang::BuiltinType* bt = getType()->castAs<clang::BuiltinType>();
  clang::PrintingPolicy policy = getASTContext().getPrintingPolicy();
#endif  // NDEBUG
  assert(bt->getName(policy).equals(type));
}

static clang::QualType getCorrespondingBuiltin(clang::ASTContext& ac,
                                               clang::BuiltinType::Kind kind) {
  switch (kind) {
    default:
      assert(false && "Type not supported");
      return {};
#define BUILTIN_TYPE(Id, SingletonId) \
  case clang::BuiltinType::Id:        \
    return ac.SingletonId;
#include "clang/AST/BuiltinTypes.def"
  }
}

#define X(type, name)                                             \
  template <>                                                     \
  Value Value::Create(Interpreter& interp, type val) {            \
    clang::ASTContext& ac = interp.getCI()->getASTContext();      \
    clang::BuiltinType::Kind kind = clang::BuiltinType::name;     \
    Value res = Value(getCorrespondingBuiltin(ac, kind), interp); \
    res.set##name(val);                                           \
    return res;                                                   \
  }

CPPINTERP_VALUE_BUILTIN_TYPES

#undef X

void Value::AssertOnUnsupportedTypeCast() const {
  assert("unsupported type in Value, cannot cast!" && 0);
}

}  // namespace cppinterp