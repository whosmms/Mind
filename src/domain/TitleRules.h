#pragma once
#include "domain/Limits.h"

#include <QString>

namespace lingya {

// 节点/任务标题统一规范化（R01、X03）。
// renameNode与renameTask必须共用同一规则，避免两个方向出现不同标题。
namespace TitleRules {

// 去首尾空白；空白标题按设计转为“未命名主题”。
inline QString normalize(const QString& raw) {
    const QString trimmed = raw.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("未命名主题") : trimmed;
}

// 文本长度按Unicode码点计数（规范§4.3.8），不使用QString::size()。
inline int codePointCount(const QString& text) {
    return text.toUcs4().size();
}

inline bool exceedsTitleLimit(const QString& text) {
    return codePointCount(text) > Limits::kTitleMaxChars;
}

}  // namespace TitleRules

}  // namespace lingya
