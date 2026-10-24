#include "ai/AiValidator.h"

#include <QSet>
#include <algorithm>
#include <functional>

namespace lingya {

namespace {

bool isOriginValid(const QString& origin) {
    return origin == QStringLiteral("source") || origin == QStringLiteral("supplement");
}

AiValidationResult makeFailure(const QString& code, const QString& message,
                               const QStringList& warnings) {
    AiValidationResult result;
    result.ok = false;
    result.code = code;
    result.message = message;
    result.warnings = warnings;
    return result;
}

AiValidationResult makeSuccess(const QStringList& warnings) {
    AiValidationResult result;
    result.ok = true;
    result.warnings = warnings;
    return result;
}

}  // namespace

AiValidationResult AiValidator::validateMapProposal(const MapProposal& proposal, int maxNodes,
                                                    int maxDepth) {
    if (proposal.title.trimmed().isEmpty()) {
        return makeFailure(QStringLiteral("SCHEMA_INVALID"), QStringLiteral("提案标题为空"),
                           proposal.warnings);
    }
    if (proposal.root.title.trimmed().isEmpty()) {
        return makeFailure(QStringLiteral("EMPTY_TITLE"), QStringLiteral("根节点标题为空"),
                           proposal.warnings);
    }

    int count = 0;
    int depth = 0;
    QString errorCode;
    QString errorMessage;

    std::function<void(const GeneratedNode&, int)> visit = [&](const GeneratedNode& node,
                                                               int level) {
        if (!errorCode.isEmpty()) {
            return;
        }
        ++count;
        depth = std::max(depth, level);
        if (node.title.trimmed().isEmpty()) {
            errorCode = QStringLiteral("EMPTY_TITLE");
            errorMessage = QStringLiteral("第%1个节点标题为空").arg(count);
            return;
        }
        if (!isOriginValid(node.origin)) {
            errorCode = QStringLiteral("BAD_ORIGIN");
            errorMessage = QStringLiteral("节点 origin 非法：%1").arg(node.origin);
            return;
        }
        for (const GeneratedNode& child : node.children) {
            visit(child, level + 1);
        }
    };
    visit(proposal.root, 1);

    if (!errorCode.isEmpty()) {
        return makeFailure(errorCode, errorMessage, proposal.warnings);
    }
    if (count > maxNodes) {
        return makeFailure(QStringLiteral("TOO_MANY_NODES"),
                           QStringLiteral("节点数%1超过上限%2").arg(count).arg(maxNodes),
                           proposal.warnings);
    }
    if (depth > maxDepth) {
        return makeFailure(QStringLiteral("TOO_DEEP"),
                           QStringLiteral("深度%1超过上限%2").arg(depth).arg(maxDepth),
                           proposal.warnings);
    }
    return makeSuccess(proposal.warnings);
}

AiValidationResult AiValidator::validateDraftProposal(const DraftProposal& proposal,
                                                      const QVector<DraftSourceNode>& source) {
    if (proposal.title.trimmed().isEmpty()) {
        return makeFailure(QStringLiteral("SCHEMA_INVALID"), QStringLiteral("稿件标题为空"),
                           proposal.warnings);
    }
    if (proposal.sections.isEmpty()) {
        return makeFailure(QStringLiteral("SCHEMA_INVALID"), QStringLiteral("稿件没有任何段落"),
                           proposal.warnings);
    }

    QSet<ID> sourceIds;
    for (const DraftSourceNode& node : source) {
        sourceIds.insert(node.id);
    }

    for (int i = 0; i < proposal.sections.size(); ++i) {
        const DraftSection& section = proposal.sections.at(i);
        if (section.heading.trimmed().isEmpty() && section.text.trimmed().isEmpty()) {
            return makeFailure(QStringLiteral("EMPTY_SECTION"),
                               QStringLiteral("第%1段既无标题也无正文").arg(i + 1),
                               proposal.warnings);
        }
        if (!isOriginValid(section.origin)) {
            return makeFailure(QStringLiteral("BAD_ORIGIN"),
                               QStringLiteral("第%1段 origin 非法：%2").arg(i + 1).arg(section.origin),
                               proposal.warnings);
        }
        for (const QString& id : section.sourceNodeIds) {
            if (!sourceIds.contains(id)) {
                return makeFailure(
                    QStringLiteral("UNKNOWN_SOURCE"),
                    QStringLiteral("第%1段引用了不存在的来源节点：%2").arg(i + 1).arg(id),
                    proposal.warnings);
            }
        }
    }
    return makeSuccess(proposal.warnings);
}

}  // namespace lingya
