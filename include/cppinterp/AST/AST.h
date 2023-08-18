#ifndef CPPINTERP_AST_AST_H
#define CPPINTERP_AST_AST_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
class ASTContext;
class Expr;
class Decl;
class DeclContext;
class DeclarationName;
class FunctionDecl;
class GlobalDecl;
class IntegerLiteral;
class LookupResult;
class NamedDecl;
class NamespaceDecl;
class NestedNameSpecifier;
class QualType;
class Sema;
class TagDecl;
class TemplateDecl;
class Type;
class TypedefNameDecl;
}  // namespace clang

namespace cppinterp {
namespace ast {

/// 包括一些分析ASTNodes或者types的静态工具函数。
namespace analyze {

/// 检查是否declaration是一个解释器生成的wrapper函数。
/// 传入参数decl需要被检查，如果为null则返回false。
/// 如果为true则decl是解释器生成的wrapper函数。
bool IsWrapper(const clang::FunctionDecl* decl);

/// 获取GlobalDecl的mangled name。
void MaybeMangleDeclName(const clang::GlobalDecl& gd,
                         std::string& mangled_name);

/// 检索函数体的最后一个表达式。如果它是带有变量声明
/// 的DeclStmt，则创建DeclRefExpr并将其添加到函数体中。
/// 在value printing和value evaluation时使用。
///\param[in] fd               - 需要被分析的FunctionDecl。
///\param[in] found_at         - 要在函数体中返回的表达式的位置。
///\param[in] omit_decl_stmts  - 如果存在DeclStmt，是否合成DeclRefExpr。
///\param[in] sema             - 用于合成DeclRefExpr的语义分析对象。
clang::Expr* GetOrCreateLastExpr(clang::FunctionDecl* fd,
                                 int* found_at = nullptr,
                                 bool omit_decl_stmts = true,
                                 clang::Sema* sema = nullptr);

}  // namespace analyze

/// 包括一些整合ASTNodes或者types的静态工具函数。
namespace synthesize {

extern const char* const UniquePrefix;

}

/// 包括一些转换ASTNodes或者types的静态工具函数。
namespace transform {}
}  // namespace ast
}  // namespace cppinterp

#endif  // CPPINTERP_AST_AST_H