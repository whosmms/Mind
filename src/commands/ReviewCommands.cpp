#include "commands/ReviewCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "features/ReviewService.h"

#include <QDateTime>

namespace lingya {

namespace {

const ReviewCard* findCard(const Project& project, const ID& cardId) {
    for (const ReviewCard& card : project.cards) {
        if (card.id == cardId) {
            return &card;
        }
    }
    return nullptr;
}

bool hasCardForNode(const Project& project, const ID& sheetId, const ID& nodeId) {
    for (const ReviewCard& card : project.cards) {
        if (card.sheetId == sheetId && card.nodeId == nodeId) {
            return true;
        }
    }
    return false;
}

AppError notFound(const ID& cardId) {
    return AppError{"NOT_FOUND", QStringLiteral("复习卡片不存在：%1").arg(cardId)};
}

}  // namespace

ReviewCommands::ReviewCommands(CommandService* commands, const ProjectDocument* document)
    : m_commands(commands), m_document(document) {
}

std::variant<bool, AppError> ReviewCommands::gradeCard(const ID& cardId, ReviewMark mark) {
    if (findCard(m_document->project(), cardId) == nullptr) {
        return notFound(cardId);
    }
    // Again：只回到队尾，不写status也不写时间；不入历史，避免自评撤销栈被空操作塞满
    const QString status = reviewStatusForMark(mark);
    if (status.isEmpty()) {
        return true;
    }
    const QDateTime reviewedAt = QDateTime::currentDateTimeUtc();
    return m_commands->applyEdit(QStringLiteral("复习自评"), [cardId, status, reviewedAt](Project& project) {
        for (ReviewCard& card : project.cards) {
            if (card.id == cardId) {
                card.status = status;
                card.lastReviewedAt = reviewedAt;
                return;
            }
        }
    });
}

std::variant<ID, AppError> ReviewCommands::addCard(const ID& sheetId, const ID& nodeId) {
    const Project& project = m_document->project();
    if (hasCardForNode(project, sheetId, nodeId)) {
        return AppError{"CARD_EXISTS", QStringLiteral("该节点已有复习卡片：%1").arg(nodeId)};
    }
    const auto created = ReviewService::createCard(project, sheetId, nodeId);
    if (!created.has_value()) {
        return AppError{"NO_CONTENT", QStringLiteral("节点没有可提取的复习内容：%1").arg(nodeId)};
    }
    const ID cardId = created->id;
    const auto result = m_commands->applyEdit(QStringLiteral("新增复习卡片"),
                                              [card = *created](Project& target) {
                                                  target.cards.push_back(card);
                                              });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return cardId;
}

std::variant<bool, AppError> ReviewCommands::removeCard(const ID& cardId) {
    if (findCard(m_document->project(), cardId) == nullptr) {
        return notFound(cardId);
    }
    return m_commands->applyEdit(QStringLiteral("删除复习卡片"), [cardId](Project& project) {
        for (int i = 0; i < project.cards.size(); ++i) {
            if (project.cards[i].id == cardId) {
                project.cards.removeAt(i);
                return;
            }
        }
    });
}

std::variant<bool, AppError> ReviewCommands::refreshFromSource(const ID& cardId) {
    const Project& project = m_document->project();
    const ReviewCard* card = findCard(project, cardId);
    if (card == nullptr) {
        return notFound(cardId);
    }
    const auto refreshed = ReviewService::refreshCard(*card, project);
    if (!refreshed.has_value()) {
        return AppError{"NO_CONTENT", QStringLiteral("源节点已无可提取内容：%1").arg(cardId)};
    }
    const ReviewCard updated = *refreshed;
    return m_commands->applyEdit(QStringLiteral("更新复习卡片"), [updated](Project& target) {
        for (ReviewCard& item : target.cards) {
            if (item.id == updated.id) {
                item = updated;
                return;
            }
        }
    });
}

std::variant<bool, AppError> ReviewCommands::keepCustom(const ID& cardId) {
    if (findCard(m_document->project(), cardId) == nullptr) {
        return notFound(cardId);
    }
    return m_commands->applyEdit(QStringLiteral("保留自定义卡片"), [cardId](Project& project) {
        for (ReviewCard& card : project.cards) {
            if (card.id == cardId) {
                ReviewService::keepCustom(card);
                return;
            }
        }
    });
}

std::variant<bool, AppError> ReviewCommands::makeStandalone(const ID& cardId) {
    if (findCard(m_document->project(), cardId) == nullptr) {
        return notFound(cardId);
    }
    return m_commands->applyEdit(QStringLiteral("转为独立卡片"), [cardId](Project& project) {
        for (ReviewCard& card : project.cards) {
            if (card.id == cardId) {
                ReviewService::makeStandalone(card);
                return;
            }
        }
    });
}

}  // namespace lingya
