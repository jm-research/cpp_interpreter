#ifndef CPPINTERP_INTERPRETER_RUNTIME_OPTIONS_H
#define CPPINTERP_INTERPRETER_RUNTIME_OPTIONS_H

namespace cppinterp {
namespace runtime {

/// 可以在运行时被user改变的解释器配置bit位。
/// 例如enable或者disable扩展（extensions）。
struct RuntimeOptions {
  RuntimeOptions() : AllowRedefinition(0) {}

  // 允许用户去重新定义实体（entity），请求启用
  // `DefinitionShadower` AST transformer
  bool AllowRedefinition : 1;
};

}  // namespace runtime
}  // namespace cppinterp

#endif  // CPPINTERP_INTERPRETER_RUNTIME_OPTIONS_H