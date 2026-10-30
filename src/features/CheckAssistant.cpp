#include "features/CheckAssistant.h"

namespace lingya {

namespace {

struct RuleInfo {
    const char* ruleId;
    const char* title;
    const char* actionId;
    const char* label;
};

// C01-C09 的类别与可修复动作（规范§7.4）。
// 结构性错误（C03 孤立/环、C04 流程断头、C05 判定分支不足、C07 依赖环）只定位不自动改，
// 避免界面替用户改坏工程数据。
const RuleInfo kRules[] = {
    {"C01", "未命名主题", "edit-node", "编辑主题"},
    {"C02", "标题或层级过长", "", ""},
    {"C03", "结构错误", "", ""},
    {"C04", "流程结构", "", ""},
    {"C05", "判定分支", "", ""},
    {"C06", "任务日期", "edit-task", "编辑任务"},
    {"C07", "依赖关系", "edit-task", "编辑任务"},
    {"C08", "来源失效", "fix-source", "处理来源"},
    {"C09", "对比度", "fix-contrast", "应用建议色"},
};

}  // namespace

QString CheckAssistant::ruleTitle(const QString& ruleId) {
    for (const RuleInfo& rule : kRules) {
        if (ruleId == QLatin1String(rule.ruleId)) {
            return QString::fromUtf8(rule.title);
        }
    }
    return ruleId;
}

CheckAction CheckAssistant::actionFor(const QString& ruleId) {
    for (const RuleInfo& rule : kRules) {
        if (ruleId == QLatin1String(rule.ruleId)) {
            return CheckAction{QString::fromLatin1(rule.actionId),
                               QString::fromUtf8(rule.label)};
        }
    }
    return CheckAction{};
}

}  // namespace lingya
