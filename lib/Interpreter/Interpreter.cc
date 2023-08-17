#include "cppinterp/Interpreter/Interpreter.h"

#include "cppinterp/Incremental/IncrementalParser.h"
#include "cppinterp/Interpreter/CompilationOptions.h"
#include "cppinterp/Interpreter/Transaction.h"

namespace cppinterp {

Interpreter::PushTransactionRAII::PushTransactionRAII(
    const Interpreter* interpreter)
    : interpreter_(interpreter) {
  CompilationOptions co = interpreter_->makeDefaultCompilationOpts();
  co.ResultEvaluation = 0;
  co.DynamicScoping = 0;

  transaction_ = interpreter_->incr_parser_->beginTransaction(co);
}

Interpreter::PushTransactionRAII::~PushTransactionRAII() { pop(); }

void Interpreter::PushTransactionRAII::pop() const {
  if (transaction_->getState() == Transaction::kRolledBack) {
    return;
  }
  IncrementalParser::ParseResultTransaction PRT =
      interpreter_->incr_parser_->endTransaction(transaction_);
  if (PRT.getPointer()) {
    assert(PRT.getPointer() == transaction_ && "Ended different transaction?");
    interpreter_->incr_parser_->commitTransaction(PRT);
  }
}

Interpreter::StateDebuggerRAII::StateDebuggerRAII(const Interpreter* i)
    : interpreter_(i) {
  if (interpreter_->isPrintingDebug()) {
    const CompilerInstance& CI = *interpreter_->getCI();
    CodeGenerator* CG = i->incr_parser_->getCodeGenerator();

    // The ClangInternalState constructor can provoke deserialization,
    // we need a transaction.
    PushTransactionRAII pushedT(i);

    state_.reset(
        new ClangInternalState(CI.getASTContext(), CI.getPreprocessor(),
                               CG ? CG->GetModule() : nullptr, CG, "aName"));
  }
}

Interpreter::StateDebuggerRAII::~StateDebuggerRAII() {
  if (state_) {
    // The ClangInternalState destructor can provoke deserialization,
    // we need a transaction.
    PushTransactionRAII pushedT(interpreter_);
    state_->compare("aName", interpreter_->m_Opts.Verbose());
    state_.reset();
  }
}

}  // namespace cppinterp