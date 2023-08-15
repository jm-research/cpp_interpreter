#ifndef CPPINTERP_INTERPRETER_INTERPRETER_H
#define CPPINTERP_INTERPRETER_INTERPRETER_H

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

#include "llvm/ADT/StringRef.h"

namespace llvm {
class raw_ostream;
struct GenericValue;
class ExecutionEngine;
class LLVMContext;
class Module;
class StringRef;
class Type;
template <typename T>
class SmallVectorImpl;
namespace orc {
class DefinitionGenerator;
}
}  // namespace llvm

namespace clang {
class ASTContext;
class ASTDeserializationListener;
class CompilerInstance;
class Decl;
class DeclContext;
class DiagnosticConsumer;
class DiagnosticsEngine;
class FunctionDecl;
class GlobalDecl;
class MacroInfo;
class Module;
class ModuleFileExtension;
class NamedDecl;
class Parser;
class Preprocessor;
class PresumedLoc;
class QualType;
class RecordDecl;
class Sema;
class SourceLocation;
class SourceManager;
class Type;
}  // namespace clang

namespace cppinterp {

/// 实现类似解释器的行为并且管理增量编译。
class Interpreter {

};

}

#endif  // CPPINTERP_INTERPRETER_INTERPRETER_H