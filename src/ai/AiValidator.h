#pragma once
#include "ai/AiTypes.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace lingya {

// AI 响应校验（AI规范§10.6）：在预览/采用前严格检查结构与引用，失败不写工程。
// 纯逻辑，不联网、不解析JSON（JSON→DTO 由响应解析层负责）。
struct AiValidationResult {
    bool ok = false;
    // SCHEMA_INVALID / EMPTY_TITLE / BAD_ORIGIN / TOO_MANY_NODES / TOO_DEEP /
    // EMPTY_SECTION / UNKNOWN_SOURCE
    QString code;
    QString message;  // 中文说明，供界面显示
    QStringList warnings;
};

class AiValidator {
public:
    // 导图提案：标题非空、根非空、origin 合法、节点数 ≤ maxNodes、深度 ≤ maxDepth
    static AiValidationResult validateMapProposal(const MapProposal& proposal,
                                                  int maxNodes = 50, int maxDepth = 4);

    // 文字稿提案：标题非空、sections 非空、每段 heading/text 至少一项、origin 合法、
    // sourceNodeIds 必须属于本次源节点集合
    static AiValidationResult validateDraftProposal(const DraftProposal& proposal,
                                                    const QVector<DraftSourceNode>& source);
};

}  // namespace lingya
