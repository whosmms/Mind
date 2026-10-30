#pragma once
#include "features/CheckService.h"

#include <QString>

namespace lingya {

// 可执行修复动作（X04）：界面据此显示按钮并派发到对应编辑器
struct CheckAction {
    QString actionId;   // 空=只能定位
    QString label;      // 按钮文字（中文）
};

// 检查助手的规则映射（X04，AI规范§7.4）：ruleId → 定位目标 + 可执行修复动作。
// 纯查表、无副作用；定位目标直接用 Issue 的 sheetId/targetId，由界面负责跳转。
// 只给“能安全自动改”的规则动作；结构性错误只定位，避免替用户改坏数据。
class CheckAssistant {
public:
    // ruleId 的中文类别名（未知规则回退为 ruleId 本身）
    static QString ruleTitle(const QString& ruleId);

    // 可执行动作（actionId 为空表示只能定位）
    static CheckAction actionFor(const QString& ruleId);
};

}  // namespace lingya
