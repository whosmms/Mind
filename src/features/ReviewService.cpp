#include "features/ReviewService.h"

#include <QCryptographicHash>
#include <QPair>
#include <QUuid>
#include <algorithm>

namespace lingya {

namespace {

QString normalize(const QString& text) {
    return text.trimmed();
}

const TreeSheet* findTreeSheet(const Project& project, const ID& sheetId) {
    for (const Sheet& sheet : project.sheets) {
        if (const auto* tree = std::get_if<TreeSheet>(&sheet);
            tree != nullptr && tree->id == sheetId) {
            return tree;
        }
    }
    return nullptr;
}

// 直属子节点标题按 order 排序后逐条拼接（用于答案与来源哈希）。
QString directChildrenAnswer(const TreeSheet& sheet, const ID& nodeId) {
    QVector<QPair<int, QString>> children;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        const TreeNode& node = it.value();
        if (node.parentId.has_value() && *node.parentId == nodeId) {
            children.append({node.order, node.title});
        }
    }
    std::stable_sort(children.begin(), children.end(),
                     [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
                         return a.first < b.first;
                     });
    QStringList lines;
    for (const auto& child : children) {
        lines << child.second;
    }
    return lines.join(QLatin1Char('\n'));
}

}  // namespace

std::optional<ReviewCard> ReviewService::createCard(const Project& project, const ID& sheetId,
                                                    const ID& nodeId) {
    const TreeSheet* sheet = findTreeSheet(project, sheetId);
    if (sheet == nullptr || !sheet->nodes.contains(nodeId)) {
        return std::nullopt;
    }
    const TreeNode& node = sheet->nodes.value(nodeId);

    ReviewCard card;
    card.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    card.sheetId = sheetId;
    card.nodeId = nodeId;
    card.front = node.title;
    card.back = normalize(node.note).isEmpty() ? directChildrenAnswer(*sheet, nodeId) : node.note;

    if (normalize(card.back).isEmpty()) {
        return std::nullopt;   // 无内容可提取，不生成空卡
    }

    card.sourceHash = sourceHash(node, *sheet);
    card.status = QStringLiteral("new");
    card.mode = QStringLiteral("linked");
    card.sourceState = QStringLiteral("ok");
    return card;
}

QString ReviewService::sourceHash(const TreeNode& node, const TreeSheet& sheet) {
    const QString material = normalize(node.title) + QChar(0x1F) +
                             normalize(node.note) + QChar(0x1F) +
                             normalize(directChildrenAnswer(sheet, node.id));
    return QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString ReviewService::detectSourceState(const ReviewCard& card, const Project& project) {
    const TreeSheet* sheet = findTreeSheet(project, card.sheetId);
    if (sheet == nullptr || !sheet->nodes.contains(card.nodeId)) {
        return QStringLiteral("missing");
    }
    const QString current = sourceHash(sheet->nodes.value(card.nodeId), *sheet);
    return current == card.sourceHash ? QStringLiteral("ok") : QStringLiteral("changed");
}

std::optional<ReviewCard> ReviewService::refreshCard(const ReviewCard& card, const Project& project) {
    const auto fresh = createCard(project, card.sheetId, card.nodeId);
    if (!fresh.has_value()) {
        return std::nullopt;
    }
    ReviewCard updated = *fresh;
    updated.id = card.id;           // 保留卡片 ID
    updated.status = card.status;   // 保留学习状态
    // 保留最近复习时间：刷新正反面不应让“最近复习”倒退回空（BUG-017）
    updated.lastReviewedAt = card.lastReviewedAt;
    return updated;
}

void ReviewService::keepCustom(ReviewCard& card) {
    card.mode = QStringLiteral("custom");
}

void ReviewService::makeStandalone(ReviewCard& card) {
    card.mode = QStringLiteral("custom");
    card.sourceState = QStringLiteral("missing");
}

}  // namespace lingya
