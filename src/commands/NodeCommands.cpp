#include "commands/NodeCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/TitleRules.h"

#include <QSet>

#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <cmath>

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

// 文本字段语义：去首尾空白；空白=清除（返回nullopt），否则返回原值
std::optional<QString> optionalFromText(const QString& raw) {
    const QString trimmed = raw.trimmed();
    return trimmed.isEmpty() ? std::optional<QString>{} : trimmed;
}

bool isHttpLink(const QString& raw) {
    const QUrl url(raw.trimmed());
    return url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https");
}

bool isHexColor(const QString& raw) {
    static const QRegularExpression pattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    return pattern.match(raw).hasMatch();
}

bool sameOverride(const NodeStyleOverride& a, const NodeStyleOverride& b) {
    return a.fill == b.fill && a.color == b.color && a.borderColor == b.borderColor &&
           a.fontSize == b.fontSize && a.fontFamily == b.fontFamily && a.shape == b.shape &&
           a.bold == b.bold && a.borderWidth == b.borderWidth;
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

// 收集某父节点的兄弟ID序列，按(order, ID)稳定排序（D-M1-03：先规范化再定位）
QVector<ID> siblingSequence(const TreeSheet& tree, const ID& parentId) {
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
    QVector<ID> sequence;
    for (const auto& pair : siblings) sequence.append(pair.second);
    return sequence;
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

std::variant<ID, AppError> NodeCommands::addSibling(const ID& sheetId, const ID& referenceId,
                                                    const QString& title) {
    if (sheetId.isEmpty() || referenceId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与参照节点ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    const auto reference = tree->nodes.constFind(referenceId);
    if (reference == tree->nodes.constEnd()) {
        return AppError{"NOT_FOUND", QStringLiteral("参照节点不存在：%1").arg(referenceId)};
    }
    if (referenceId == tree->rootId || !reference->parentId.has_value()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("根节点没有同级主题")};
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
    const auto result = m_commands->applyEdit(QStringLiteral("新增同级主题"),
                                              [sheetId, referenceId, newId, normalizedTitle](
                                                  Project& project) {
        TreeSheet* target = findTree(project, sheetId);
        if (target == nullptr || !target->nodes.contains(referenceId)) return;
        const TreeNode& referenceNode = target->nodes[referenceId];
        if (!referenceNode.parentId.has_value()) return;
        const ID parentId = *referenceNode.parentId;

        // D-M1-03：先按(order,ID)形成兄弟序列，在参照之后插入，再写回连续order
        const QVector<ID> sequence = siblingSequence(*target, parentId);
        const int at = sequence.indexOf(referenceId);
        if (at < 0) return;
        QVector<ID> ordered = sequence;
        ordered.insert(at + 1, newId);

        TreeNode node;
        node.id = newId;
        node.parentId = parentId;
        node.title = normalizedTitle;
        node.contentRevision = 1;
        target->nodes.insert(newId, node);
        for (int i = 0; i < ordered.size(); ++i) target->nodes[ordered.at(i)].order = i;
        ++target->nodes[parentId].contentRevision;
    });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return newId;
}

std::variant<bool, AppError> NodeCommands::deleteSubtree(const ID& sheetId, const ID& nodeId,
                                                         TaskDeletePolicy policy) {
    if (sheetId.isEmpty() || nodeId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与节点ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    const auto targetNode = tree->nodes.constFind(nodeId);
    if (targetNode == tree->nodes.constEnd()) {
        return AppError{"NOT_FOUND", QStringLiteral("节点不存在：%1").arg(nodeId)};
    }
    if (nodeId == tree->rootId) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("根节点不能删除")};
    }

    const auto result = m_commands->applyEdit(
        QStringLiteral("删除子树"), [sheetId, nodeId, policy](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr || !target->nodes.contains(nodeId)) return;
            const ID parentId = target->nodes[nodeId].parentId.value_or(ID{});

            // 收集子树（沿parentId向下）
            QSet<ID> subtree;
            QVector<ID> stack{nodeId};
            while (!stack.isEmpty()) {
                const ID current = stack.takeLast();
                if (!subtree.contains(current)) subtree.insert(current);
                for (auto it = target->nodes.constBegin(); it != target->nodes.constEnd(); ++it) {
                    if (it.value().parentId.has_value() && *it.value().parentId == current &&
                        !subtree.contains(it.key()))
                        stack.append(it.key());
                }
            }

            // 关系线：任一端点在子树内即删（校验器规则7：端点必须存在）
            QVector<Relation> keptRelations;
            for (const Relation& rel : target->relations) {
                if (!subtree.contains(rel.sourceId) && !subtree.contains(rel.targetId))
                    keptRelations.append(rel);
            }
            target->relations = keptRelations;

            // 分组注释：父级或任一成员在子树内即删（校验器规则6/7）
            QVector<GroupAnnotation> keptAnnotations;
            for (const GroupAnnotation& ga : target->annotations) {
                bool touches = subtree.contains(ga.parentId);
                for (const ID& member : ga.memberIds)
                    if (subtree.contains(member)) touches = true;
                if (!touches) keptAnnotations.append(ga);
            }
            target->annotations = keptAnnotations;

            // 批注：指向子树节点即删（校验器：批注节点必须存在）
            QVector<Comment> keptComments;
            for (const Comment& c : target->comments) {
                if (!subtree.contains(c.nodeId)) keptComments.append(c);
            }
            target->comments = keptComments;

            // 任务联动（D-M1-05）：默认清除source保留独立任务；DeleteTogether连任务一起删
            // 并清理其他任务对被删任务的前置依赖（同一事务，规范§12）
            QVector<ID> removedTaskIds;
            for (Task& task : project.tasks) {
                if (!task.source.has_value() || task.source->sheetId != sheetId) continue;
                if (!subtree.contains(task.source->nodeId)) continue;
                if (policy == TaskDeletePolicy::DeleteTogether) {
                    removedTaskIds.append(task.id);
                } else {
                    task.source.reset();
                }
            }
            if (policy == TaskDeletePolicy::DeleteTogether && !removedTaskIds.isEmpty()) {
                const QSet<ID> removed(removedTaskIds.cbegin(), removedTaskIds.cend());
                QVector<Task> keptTasks;
                for (const Task& task : project.tasks)
                    if (!removed.contains(task.id)) keptTasks.append(task);
                for (Task& task : keptTasks) {
                    if (task.predecessorIds.isEmpty()) continue;
                    QVector<ID> keptPreds;
                    for (const ID& pred : task.predecessorIds)
                        if (!removed.contains(pred)) keptPreds.append(pred);
                    task.predecessorIds = keptPreds;
                }
                project.tasks = keptTasks;
            }

            // 复习卡片：源节点被删保留卡片并置missing（D-M1-05、§7.1运行时处理）
            for (ReviewCard& card : project.cards) {
                if (card.sheetId == sheetId && subtree.contains(card.nodeId))
                    card.sourceState = QStringLiteral("missing");
            }
            // 讲解路线与文稿不自动改写（D-M1-05：运行时重校验提示跳过）

            for (const ID& id : subtree) target->nodes.remove(id);
            normalizeSiblings(*target, parentId); // 删除中间节点后，同级order仍须连续
            if (!parentId.isEmpty() && target->nodes.contains(parentId))
                ++target->nodes[parentId].contentRevision;
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<bool, AppError> NodeCommands::updateNodeMeta(const ID& sheetId, const ID& nodeId,
                                                          const NodeMetaUpdate& update) {
    if (sheetId.isEmpty() || nodeId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与节点ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    const auto node = tree->nodes.constFind(nodeId);
    if (node == tree->nodes.constEnd()) {
        return AppError{"NOT_FOUND", QStringLiteral("节点不存在：%1").arg(nodeId)};
    }

    // 字段级预检（D-M1-04：命令层基础码；标签数量/长度与校验器规则一致）
    if (update.note.has_value() &&
        TitleRules::codePointCount(*update.note) > Limits::kNoteMaxChars) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("节点备注超过%1个字符").arg(Limits::kNoteMaxChars)};
    }
    if (update.link.has_value() && !update.link->trimmed().isEmpty() && !isHttpLink(*update.link)) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("链接只允许http/https协议")};
    }
    QStringList tags;
    if (update.tags.has_value()) {
        for (const QString& raw : *update.tags) {
            const QString trimmed = raw.trimmed();
            if (trimmed.isEmpty()) {
                return AppError{"INVALID_ARGUMENT", QStringLiteral("标签不能为空白")};
            }
            tags.append(trimmed);
        }
        if (tags.size() > Limits::kMaxTags) {
            return AppError{"LIMIT_EXCEEDED",
                            QStringLiteral("节点标签最多%1个").arg(Limits::kMaxTags)};
        }
        for (const QString& tag : tags) {
            if (TitleRules::codePointCount(tag) > Limits::kTagMaxChars) {
                return AppError{"LIMIT_EXCEEDED",
                                QStringLiteral("单个标签不超过%1个字符").arg(Limits::kTagMaxChars)};
            }
        }
    }

    // 无操作短路（D-M1-02）：补丁产生的值与当前完全相同→成功但不入历史
    const bool unchanged =
        (!update.note.has_value() || *update.note == node->note) &&
        (!update.iconId.has_value() || optionalFromText(*update.iconId) == node->iconId) &&
        (!update.link.has_value() || optionalFromText(*update.link) == node->link) &&
        (!update.tags.has_value() || tags == node->tags);
    if (unchanged) return true;

    const auto result = m_commands->applyEdit(
        QStringLiteral("更新节点信息"), [sheetId, nodeId, update, tags](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            const auto it = target->nodes.find(nodeId);
            if (it == target->nodes.end()) return;
            if (update.note.has_value()) it->note = *update.note;
            if (update.iconId.has_value()) it->iconId = optionalFromText(*update.iconId);
            if (update.link.has_value()) it->link = optionalFromText(*update.link);
            if (update.tags.has_value()) it->tags = tags;
            ++it->contentRevision;
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<bool, AppError> NodeCommands::setStyle(const ID& sheetId, const ID& nodeId,
                                                    const NodeStyleOverride& style) {
    if (sheetId.isEmpty() || nodeId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与节点ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    const auto node = tree->nodes.constFind(nodeId);
    if (node == tree->nodes.constEnd()) {
        return AppError{"NOT_FOUND", QStringLiteral("节点不存在：%1").arg(nodeId)};
    }

    // 预检与ProjectJson读写规则一致（颜色#RRGGBB、shape五枚举、字号范围、边宽正数）
    const auto validColor = [](const std::optional<QString>& color) {
        return !color.has_value() || isHexColor(*color);
    };
    if (!validColor(style.fill) || !validColor(style.color) || !validColor(style.borderColor)) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("颜色必须为#RRGGBB格式")};
    }
    if (style.shape.has_value() &&
        *style.shape != QStringLiteral("roundRect") && *style.shape != QStringLiteral("rect") &&
        *style.shape != QStringLiteral("ellipse") && *style.shape != QStringLiteral("diamond") &&
        *style.shape != QStringLiteral("parallelogram")) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("节点形状不在支持列表内")};
    }
    if (style.fontSize.has_value() &&
        (*style.fontSize < Limits::kFontSizeMin || *style.fontSize > Limits::kFontSizeMax)) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("字号必须在%1至%2之间")
                            .arg(Limits::kFontSizeMin)
                            .arg(Limits::kFontSizeMax)};
    }
    if (style.borderWidth.has_value() &&
        (!std::isfinite(*style.borderWidth) || *style.borderWidth <= 0.0)) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("边框宽度必须为正数")};
    }

    if (sameOverride(style, node->style)) return true;   // 无操作短路（D-M1-02）

    const auto result = m_commands->applyEdit(
        QStringLiteral("设置节点样式"), [sheetId, nodeId, style](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            const auto it = target->nodes.find(nodeId);
            if (it == target->nodes.end()) return;
            it->style = style;
            ++it->contentRevision;
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

}  // namespace lingya
