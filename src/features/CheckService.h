#pragma once
#include "domain/Domain.h"

#include <QString>
#include <QVector>
#include <optional>

namespace lingya {

// 检查项严重度（AI规范§7.4）：错误 / 提醒 / 建议。
enum class IssueSeverity {
    Error,
    Warning,
    Suggestion
};

// 一条检查问题（AI规范§7.4）：ruleId、severity、sheetId、targetId、message、suggestion；
// 可修复时附 actionId（仅标识，不携带可执行脚本）。
struct Issue {
    QString ruleId;
    IssueSeverity severity = IssueSeverity::Warning;
    ID sheetId;
    ID targetId;
    QString message;
    QString suggestion;
    std::optional<QString> actionId;
};

// 规则检查助手（X04，AI规范§7.4）：纯函数，同样输入得到同样结果。
// 覆盖规则 C01-C09；不评判知识真假，不输出综合分数。
class CheckService {
public:
    static QVector<Issue> checkProject(const Project& project);
};

}  // namespace lingya
