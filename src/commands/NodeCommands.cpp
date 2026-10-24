#include "commands/NodeCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/TitleRules.h"

#include <QUuid>
#include <algorithm>

namespace lingya {

namespace {

const TreeSheet* findTree(const Project& project, const ID& sheetId) {
    for (const Sheet& sheet : project.sheets) {
        if (const auto* tree = std::get_if<TreeSheet>(&sheet)) {
            if (tree->id == sheetId) {
                return tree;
            }
        }
    }
    return nullptr;
}

TreeSheet* findTree(Project& project, const ID& sheetId) {
    for (Sheet& sheet : project.sheets) {
        if (auto* tree = std::get_if<TreeSheet>(&sheet)) {
            if (tree->id == sheetId) {
                return tree;
            }
        }
    }
    return nullptr;
}

int totalNodeCount(const Project& project) {
    int count = 0;
    for (const Sheet& sheet : project.sheets) {
        if (const auto* tree = std::get_if<TreeSheet>(&sheet)) {
            count += tree->nodes.size();
        } else if (const auto* flow = std::get_if<FlowSheet>(&sheet)) {
            count += flow->nodes.size();
        }
    }
    return count;
}

// 按(order, ID)稳定排序后写回连续order，不依赖QMap遍历顺序。
void normalizeSiblings(TreeSheet& tree, const ID& parentId) {
    QVector<QPair<int, ID>> siblings;
    for (auto it = tree.nodes.constBegin(); it != tree.nodes.constEnd(); ++it) {
        if (it.value().parentId.has_value() && *it.value().parentId == parentId) {
            siblings.append({it.value().order, it.key()});
        }
    }
    std::sort(siblings.begin(), siblings.end(),
              [](const QPair<int, ID>& left, const QPair<int, ID>& right) {
                  if (left.first != right.first) {
                      return left.first < right.first;
                  }
                  return left.second < right.second;
              });
    for (int i = 0; i < siblings.size(); ++i) {
        tree.nodes[siblings.at(i).second].order = i;
    }
}

}  // namespace

NodeCommands::NodeCommands(CommandService* commands, const ProjectDocument* document)
    : m_commands(commands), m_document(document) {
}

std::variant<ID, AppError> NodeCommands::addChild(const ID& sheetId, const ID& parentId,
                                                  const QString& title) {
    if (sheetId.isEmpty() || parentId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与父节点ID不能为空")};
    }

    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    if (!tree->nodes.contains(parentId)) {
        return AppError{"NOT_FOUND", QStringLiteral("父节点不存在：%1").arg(parentId)};
    }
    if (totalNodeCount(m_document->project()) >= Limits::kMaxNodes) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("工程节点数量已达上限%1").arg(Limits::kMaxNodes)};
    }

    const QString normalizedTitle = TitleRules::normalize(title);
    if (TitleRules::exceedsTitleLimit(normalizedTitle)) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("节点标题超过%1个字符").arg(Limits::kTitleMaxChars)};
    }

    const ID newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto result = m_commands->applyEdit(
        QStringLiteral("新增子主题"), [sheetId, parentId, newId, normalizedTitle](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr || !target->nodes.contains(parentId)) {
                return;
            }

            int maxOrder = -1;
            for (auto it = target->nodes.constBegin(); it != target->nodes.constEnd(); ++it) {
                if (it.value().parentId.has_value() && *it.value().parentId == parentId) {
                    maxOrder = std::max(maxOrder, it.value().order);
                }
            }

            TreeNode node;
            node.id = newId;
            node.parentId = parentId;
            node.order = maxOrder + 1;
            node.title = normalizedTitle;
            node.contentRevision = 1;
            target->nodes.insert(newId, node);
            normalizeSiblings(*target, parentId);
            ++target->nodes[parentId].contentRevision;
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return newId;
}

}  // namespace lingya
