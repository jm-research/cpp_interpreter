#include "cppinterp/Interpreter/Interpreter.h"

#include "cppinterp/Incremental/IncrementalParser.h"
#include "cppinterp/Interpreter/CompilationOptions.h"

namespace cppinterp {

Interpreter::PushTransactionRAII::PushTransactionRAII(
    const Interpreter* interpreter)
    : interpreter_(interpreter) {
  CompilationOptions co = interpreter_->makeDefaultCompilationOpts();
  co.ResultEvaluation = 0;
  co.DynamicScoping = 0;

  transaction_ = interpreter_->incr_parser_->beginTransaction(co);
}

}  // namespace cppinterp