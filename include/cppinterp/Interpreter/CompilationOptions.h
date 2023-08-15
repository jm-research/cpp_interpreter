#ifndef CPPINTERP_INTERPRETER_COMPILATION_OPTIONS_H
#define CPPINTERP_INTERPRETER_COMPILATION_OPTIONS_H

namespace cppinterp {

/// 控制增量编译的选项，描述一系列自定义的启用或者禁用的AST消费者集合。
class CompilationOptions {
 public:
  // 是否从处理过的输入中提取声明。
  unsigned DeclarationExtraction : 1;

  // 是否允许全局声明被包含在' __cppinterp_N5xxx'内联命名空间中(用于定义遮蔽)。
  unsigned EnableShadowing : 1;

  // 是否打印运行输入的结果。
  // 0 -> Disabled; 1 -> Enabled; 2 -> Auto;
  unsigned ValuePrinting : 2;
  enum ValuePrint { VPDisabled, VPEnabled, VPAuto };

  // 是否返回执行的结果。
  unsigned ResultEvaluation : 1;

  // 是否使用仅在运行时可用的名称的新信息来扩展静态作用域。
  unsigned DynamicScoping : 1;

  // 是否动态打印调试信息。
  unsigned Debug : 1;

  // 是否为输入生成可执行代码(LLVM IR)，还是将传入声明缓存在队列中。
  unsigned CodeGeneration : 1;

  // 在生成可执行文件时，选择是生成所有代码(当为false时)，还是只生成输入
  // 描述来自现有库的代码时所需的代码。
  unsigned CodeGenerationForModule : 1;

  // 提示输入对于编译器来说可能看起来很奇怪，例如
  // void __cppinterp_prompt() {sin(0.1);} 会有警告，
  // 该标志抑制这些警告，它应该在input被wrap（包装）时设置。
  unsigned IgnorePromptDiags : 1;

  // 启用/禁用指针有效性检查。
  unsigned CheckPointerValidity : 1;

  // 优化级别。
  unsigned OptLevel : 2;

  // 偏移到输入行以启用代码完成点的设置。-1禁用代码完成。
  int CodeCompletionOffset = -1;

  CompilationOptions() {
    DeclarationExtraction = 0;
    EnableShadowing = 0;
    ValuePrinting = VPDisabled;
    ResultEvaluation = 0;
    DynamicScoping = 0;
    Debug = 0;
    CodeGeneration = 1;
    CodeGenerationForModule = 0;
    IgnorePromptDiags = 0;
    OptLevel = 1;
    CheckPointerValidity = 1;
  }

  bool operator==(CompilationOptions other) const {
    return DeclarationExtraction == other.DeclarationExtraction &&
           EnableShadowing == other.EnableShadowing &&
           ValuePrinting == other.ValuePrinting &&
           ResultEvaluation == other.ResultEvaluation &&
           DynamicScoping == other.DynamicScoping && Debug == other.Debug &&
           CodeGeneration == other.CodeGeneration &&
           CodeGenerationForModule == other.CodeGenerationForModule &&
           IgnorePromptDiags == other.IgnorePromptDiags &&
           CheckPointerValidity == other.CheckPointerValidity &&
           OptLevel == other.OptLevel &&
           CodeCompletionOffset == other.CodeCompletionOffset;
  }

  bool operator!=(CompilationOptions other) const {
    return DeclarationExtraction != other.DeclarationExtraction ||
           EnableShadowing != other.EnableShadowing ||
           ValuePrinting != other.ValuePrinting ||
           ResultEvaluation != other.ResultEvaluation ||
           DynamicScoping != other.DynamicScoping || Debug != other.Debug ||
           CodeGeneration != other.CodeGeneration ||
           CodeGenerationForModule != other.CodeGenerationForModule ||
           IgnorePromptDiags != other.IgnorePromptDiags ||
           CheckPointerValidity != other.CheckPointerValidity ||
           OptLevel != other.OptLevel ||
           CodeCompletionOffset != other.CodeCompletionOffset;
  }
};

}  // namespace cppinterp

#endif  // CPPINTERP_INTERPRETER_COMPILATION_OPTIONS_H