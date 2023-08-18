#include "cppinterp/AST/AST.h"

#include <llvm-15/llvm/ADT/ArrayRef.h>
#include <llvm-15/llvm/Support/Casting.h>
#include <stdio.h>

#include <memory>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/Stmt.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace cppinterp {
namespace ast {

namespace analyze {

bool IsWrapper(const clang::FunctionDecl* decl) {
  if (!decl) {
    return false;
  }
  if (!decl->getDeclName().isIdentifier()) {
    return false;
  }
  return decl->getName().startswith(synthesize::UniquePrefix);
}

void MaybeMangleDeclName(const clang::GlobalDecl& gd,
                         std::string& mangled_name) {
  clang::NamedDecl* nd =
      llvm::cast<clang::NamedDecl>(const_cast<clang::Decl*>(gd.getDecl()));
  std::unique_ptr<clang::MangleContext> mangle_ctx;
  mangle_ctx.reset(nd->getASTContext().createMangleContext());
  if (!mangle_ctx->shouldMangleDeclName(nd)) {
    clang::IdentifierInfo* ii = nd->getIdentifier();
    assert(ii && "Attempt to mangle unnamed decl.");
    mangled_name = ii->getName().str();
    return;
  }
  llvm::raw_string_ostream raw_str(mangled_name);
  mangle_ctx->mangleName(gd, raw_str);
  raw_str.flush();
}

clang::Expr* GetOrCreateLastExpr(clang::FunctionDecl* fd, int* found_at,
                                 bool omit_decl_stmts, clang::Sema* sema) {
  assert(fd && "We need a function declaration!");
  assert((omit_decl_stmts || sema) &&
         "Sema needs to be set when omit_decl_stmts is false");

  if (found_at) {
    *found_at = -1;
  }

  clang::Expr* result = nullptr;
  if (clang::CompoundStmt* cs =
          llvm::dyn_cast<clang::CompoundStmt>(fd->getBody())) {
    llvm::ArrayRef<clang::Stmt*> stmts =
        llvm::makeArrayRef(cs->body_begin(), cs->size());
    int index_of_last_expr = stmts.size();
    while (index_of_last_expr--) {
      if (!llvm::isa<clang::NullStmt>(stmts[index_of_last_expr])) {
        break;
      }
    }

    if (found_at) {
      *found_at = index_of_last_expr;
    }

    if ((result = llvm::dyn_cast<clang::Expr>(stmts[index_of_last_expr]))) {
      return result;
    }

    if (!omit_decl_stmts) {
      if (clang::DeclStmt* ds =
              llvm::dyn_cast<clang::DeclStmt>(stmts[index_of_last_expr])) {
        std::vector<clang::Stmt*> new_body = stmts.vec();
        for (clang::DeclStmt::reverse_decl_iterator iter = ds->decl_rbegin(),
                                                    end = ds->decl_rend();
             iter != end; ++iter) {
          if (clang::VarDecl* vd = llvm::dyn_cast<clang::VarDecl>(*iter)) {
            // 更改void函数的返回类型。
            // 不能PushDeclContext，因为没有作用域(scope)。
            clang::Sema::ContextRAII pushed_dc(*sema, fd);

            clang::QualType vd_type = vd->getType().getNonReferenceType();
            // 获取要插入的位置。
            clang::SourceLocation loc =
                new_body[index_of_last_expr]->getEndLoc().getLocWithOffset(1);
            clang::DeclRefExpr* dre =
                sema->BuildDeclRefExpr(vd, vd_type, clang::VK_LValue, loc);
            assert(dre && "Cannot be null");
            index_of_last_expr++;
            new_body.insert(new_body.begin() + index_of_last_expr, dre);
            // 附加一个新的主体。
            auto new_cs = clang::CompoundStmt::Create(
                sema->getASTContext(), new_body, {}, cs->getLBracLoc(),
                cs->getRBracLoc());
            fd->setBody(new_cs);
            if (found_at) {
              *found_at = index_of_last_expr;
            }
            return dre;
          }
        }
      }
      return result;
    }
    return result;
  }
}

}  // namespace analyze

namespace synthesize {

const char* const UniquePrefix = "__cppinterp_unique";

}
}  // namespace ast
}  // namespace cppinterp