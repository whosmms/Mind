#include "commands/NodeCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/TitleRules.h"

#include <QSet>
#include <QHash>

#include <QDateTime>
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

// 工作表基类查找（setSheetStyle对四类工作表通用；其余命令只处理树图）
const SheetBase* findSheetBase(const Project& project, const ID& sheetId) {
    for (const Sheet& sheet : project.sheets) {
        const SheetBase* base = std::visit(
            [](const auto& value) -> const SheetBase* { return &value; }, sheet);
        if (base->id == sheetId) return base;
    }
    return nullptr;
}

SheetBase* findSheetBase(Project& project, const ID& sheetId) {
    for (Sheet& sheet : project.sheets) {
        SheetBase* base =
            std::visit([](auto& value) -> SheetBase* { return &value; }, sheet);
        if (base->id == sheetId) return base;
    }
    return nullptr;
}

bool sameNodeStyle(const NodeStyle& a, const NodeStyle& b) {
    return a.fill == b.fill && a.color == b.color && a.borderColor == b.borderColor &&
           a.fontSize == b.fontSize && a.fontFamily == b.fontFamily && a.bold == b.bold &&
           a.shape == b.shape && std::abs(a.borderWidth - b.borderWidth) < 1e-9;
}

bool sameEdgeStyle(const EdgeStyle& a, const EdgeStyle& b) {
    return a.color == b.color && std::abs(a.width - b.width) < 1e-9 && a.dashed == b.dashed;
}

bool isSupportedShape(const QString& shape) {
    return shape == QStringLiteral("roundRect") || shape == QStringLiteral("rect") ||
           shape == QStringLiteral("ellipse") || shape == QStringLiteral("diamond") ||
           shape == QStringLiteral("parallelogram");
}

bool isSupportedArrow(const QString& arrow) {
    return arrow == QStringLiteral("none") || arrow == QStringLiteral("end") ||
           arrow == QStringLiteral("both");
}

// 子树节点集合（含自身）
QSet<ID> subtreeIds(const TreeSheet& tree, const ID& nodeId) {
    QSet<ID> subtree;
    QVector<ID> stack{nodeId};
    while (!stack.isEmpty()) {
        const ID current = stack.takeLast();
        if (subtree.contains(current)) continue;
        subtree.insert(current);
        for (auto it = tree.nodes.constBegin(); it != tree.nodes.constEnd(); ++it) {
            if (it.value().parentId.has_value() && *it.value().parentId == current &&
                !subtree.contains(it.key()))
                stack.append(it.key());
        }
    }
    return subtree;
}

// 自根向下的树顺序（用于片段根排序与子节点遍历）
QVector<ID> treeOrder(const TreeSheet& tree) {
    QVector<ID> order;
    if (!tree.nodes.contains(tree.rootId)) return order;
    QVector<ID> stack{tree.rootId};
    while (!stack.isEmpty()) {
        const ID current = stack.takeLast();
        order.append(current);
        const QVector<ID> children = siblingSequence(tree, current);
        for (int i = children.size() - 1; i >= 0; --i) stack.append(children.at(i));
    }
    return order;
}

// 外框/概要成员校验（规范§4.3.6）：必须是同一父级下连续排列的兄弟，至少2个、不重复。
// 返回按树顺序排列的成员表。
std::variant<QStringList, AppError> orderedAnnotationMembers(const TreeSheet& tree,
                                                             const ID& parentId,
                                                             const QStringList& rawMembers) {
    if (rawMembers.size() < 2) {
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("外框/概要至少需要2个同父连续兄弟节点")};
    }
    QSet<ID> unique;
    for (const QString& raw : rawMembers) {
        if (raw.isEmpty()) return AppError{"INVALID_ARGUMENT", QStringLiteral("成员节点ID不能为空")};
        if (unique.contains(raw)) {
            return AppError{"INVALID_ARGUMENT",
                            QStringLiteral("外框/概要成员重复：%1").arg(raw)};
        }
        unique.insert(raw);
        const auto it = tree.nodes.constFind(raw);
        if (it == tree.nodes.constEnd()) {
            return AppError{"NOT_FOUND", QStringLiteral("成员节点不存在：%1").arg(raw)};
        }
        if (!it->parentId.has_value() || *it->parentId != parentId) {
            return AppError{"INVALID_ARGUMENT",
                            QStringLiteral("外框/概要只支持同一父级的兄弟节点：%1").arg(raw)};
        }
        if (raw == parentId) {
            return AppError{"INVALID_ARGUMENT", QStringLiteral("成员不能是父级自身")};
        }
    }

    const QVector<ID> sequence = siblingSequence(tree, parentId);
    QVector<int> positions;
    for (const QString& member : rawMembers) {
        const int at = sequence.indexOf(member);
        if (at < 0) return AppError{"NOT_FOUND", QStringLiteral("成员节点不在父级之下：%1").arg(member)};
        positions.append(at);
    }
    std::sort(positions.begin(), positions.end());
    for (int i = 1; i < positions.size(); ++i) {
        if (positions.at(i) != positions.at(i - 1) + 1) {
            return AppError{"INVALID_ARGUMENT",
                            QStringLiteral("外框/概要只支持连续排列的兄弟节点")};
        }
    }
    QStringList ordered;
    for (int position : positions) ordered.append(sequence.at(position));
    return ordered;
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

std::variant<bool, AppError> NodeCommands::moveSubtree(const ID& sheetId, const ID& nodeId,
                                                       const ID& newParentId,
                                                       const ID& beforeNodeId) {
    if (sheetId.isEmpty() || nodeId.isEmpty() || newParentId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID、节点ID与新父级ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    if (!tree->nodes.contains(nodeId)) {
        return AppError{"NOT_FOUND", QStringLiteral("节点不存在：%1").arg(nodeId)};
    }
    if (nodeId == tree->rootId) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("根节点不能移动")};
    }
    if (!tree->nodes.contains(newParentId)) {
        return AppError{"NOT_FOUND", QStringLiteral("目标父级不存在：%1").arg(newParentId)};
    }
    if (newParentId == nodeId) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("不能移动到自身之下")};
    }
    if (subtreeIds(*tree, nodeId).contains(newParentId)) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("不能移动到自身的后代节点之下")};
    }
    if (!beforeNodeId.isEmpty()) {
        if (beforeNodeId == nodeId) {
            return AppError{"INVALID_ARGUMENT", QStringLiteral("参照节点不能是自身")};
        }
        const auto before = tree->nodes.constFind(beforeNodeId);
        if (before == tree->nodes.constEnd()) {
            return AppError{"NOT_FOUND", QStringLiteral("参照节点不存在：%1").arg(beforeNodeId)};
        }
        if (!before->parentId.has_value() || *before->parentId != newParentId) {
            return AppError{"INVALID_ARGUMENT",
                            QStringLiteral("参照节点必须是目标父级的子节点")};
        }
    }

    // 无操作短路（D-M1-02）：已经处在目标位置时不产生历史
    const auto moving = tree->nodes.constFind(nodeId);
    if (moving->parentId.has_value() && *moving->parentId == newParentId) {
        const QVector<ID> sequence = siblingSequence(*tree, newParentId);
        const int at = sequence.indexOf(nodeId);
        const bool unchanged =
            beforeNodeId.isEmpty()
                ? (at == sequence.size() - 1)
                : (at >= 0 && at + 1 < sequence.size() && sequence.at(at + 1) == beforeNodeId);
        if (unchanged) return true;
    }

    const auto result = m_commands->applyEdit(
        QStringLiteral("移动主题"), [sheetId, nodeId, newParentId, beforeNodeId](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            const auto it = target->nodes.find(nodeId);
            if (it == target->nodes.end() || !it->parentId.has_value()) return;
            const ID oldParentId = *it->parentId;

            // 先写回旧父级的连续order（排除被移动节点）
            QVector<ID> oldSequence = siblingSequence(*target, oldParentId);
            oldSequence.removeAll(nodeId);
            for (int i = 0; i < oldSequence.size(); ++i)
                target->nodes[oldSequence.at(i)].order = i;

            // 再插入新父级的目标位置，并写回连续order
            QVector<ID> newSequence = siblingSequence(*target, newParentId);
            newSequence.removeAll(nodeId);
            int at = beforeNodeId.isEmpty() ? newSequence.size() : newSequence.indexOf(beforeNodeId);
            if (at < 0) at = newSequence.size();
            newSequence.insert(at, nodeId);
            target->nodes[nodeId].parentId = newParentId;
            for (int i = 0; i < newSequence.size(); ++i)
                target->nodes[newSequence.at(i)].order = i;

            ++target->nodes[newParentId].contentRevision;
            if (oldParentId != newParentId && target->nodes.contains(oldParentId))
                ++target->nodes[oldParentId].contentRevision;
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<bool, AppError> NodeCommands::setSheetStyle(const ID& sheetId,
                                                         const NodeStyle& nodeStyle,
                                                         const EdgeStyle& edgeStyle) {
    if (sheetId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID不能为空")};
    }
    const SheetBase* base = findSheetBase(m_document->project(), sheetId);
    if (base == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("工作表不存在：%1").arg(sheetId)};
    }

    // 预检与ProjectJson读写规则一致（颜色#RRGGBB、shape五枚举、字号/边宽范围）
    if (!isHexColor(nodeStyle.fill) || !isHexColor(nodeStyle.color) ||
        !isHexColor(nodeStyle.borderColor)) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("颜色必须为#RRGGBB格式")};
    }
    if (!isSupportedShape(nodeStyle.shape)) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("节点形状不在支持列表内")};
    }
    if (nodeStyle.fontSize < Limits::kFontSizeMin || nodeStyle.fontSize > Limits::kFontSizeMax) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("字号必须在%1至%2之间")
                            .arg(Limits::kFontSizeMin)
                            .arg(Limits::kFontSizeMax)};
    }
    if (!std::isfinite(nodeStyle.borderWidth) || nodeStyle.borderWidth < 0.1 ||
        nodeStyle.borderWidth > 20.0) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("边框宽度必须在0.1至20之间")};
    }
    if (nodeStyle.fontFamily.trimmed().isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("字体族不能为空白")};
    }
    if (!isHexColor(edgeStyle.color)) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("连线颜色必须为#RRGGBB格式")};
    }
    if (!std::isfinite(edgeStyle.width) || edgeStyle.width < 0.1 || edgeStyle.width > 20.0) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("连线宽度必须在0.1至20之间")};
    }

    if (sameNodeStyle(nodeStyle, base->defaultNodeStyle) &&
        sameEdgeStyle(edgeStyle, base->defaultEdgeStyle)) {
        return true;   // 无操作短路（D-M1-02）
    }

    const auto result = m_commands->applyEdit(
        QStringLiteral("设置工作表默认样式"),
        [sheetId, nodeStyle, edgeStyle](Project& project) {
            SheetBase* target = findSheetBase(project, sheetId);
            if (target == nullptr) return;
            target->defaultNodeStyle = nodeStyle;
            target->defaultEdgeStyle = edgeStyle;
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<ID, AppError> NodeCommands::addRelation(const ID& sheetId, const ID& sourceId,
                                                     const ID& targetId, const QString& label,
                                                     const QString& arrow) {
    if (sheetId.isEmpty() || sourceId.isEmpty() || targetId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与关系端点不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    if (!tree->nodes.contains(sourceId)) {
        return AppError{"NOT_FOUND", QStringLiteral("关系起点不存在：%1").arg(sourceId)};
    }
    if (!tree->nodes.contains(targetId)) {
        return AppError{"NOT_FOUND", QStringLiteral("关系终点不存在：%1").arg(targetId)};
    }
    if (sourceId == targetId) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("关系线两端不能是同一节点")};
    }
    if (!isSupportedArrow(arrow)) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("箭头样式只允许none/end/both")};
    }
    const QString normalizedLabel = label.trimmed();
    if (TitleRules::codePointCount(normalizedLabel) > Limits::kTitleMaxChars) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("关系线标签超过%1个字符").arg(Limits::kTitleMaxChars)};
    }
    for (const Relation& relation : tree->relations) {
        if (relation.sourceId == sourceId && relation.targetId == targetId) {
            return AppError{"INVALID_ARGUMENT", QStringLiteral("这两个节点之间已有关系线")};
        }
    }

    const ID relationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto result = m_commands->applyEdit(
        QStringLiteral("新增关系线"),
        [sheetId, relationId, sourceId, targetId, normalizedLabel, arrow](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            Relation relation;
            relation.id = relationId;
            relation.sourceId = sourceId;
            relation.targetId = targetId;
            relation.label = normalizedLabel;
            relation.arrow = arrow;
            relation.style = target->defaultEdgeStyle;
            target->relations.append(relation);
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return relationId;
}

std::variant<bool, AppError> NodeCommands::updateRelation(const ID& sheetId, const ID& relationId,
                                                          const RelationUpdate& update) {
    if (sheetId.isEmpty() || relationId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与关系线ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    const Relation* current = nullptr;
    for (const Relation& relation : tree->relations)
        if (relation.id == relationId) current = &relation;
    if (current == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("关系线不存在：%1").arg(relationId)};
    }
    if (update.arrow.has_value() && !isSupportedArrow(*update.arrow)) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("箭头样式只允许none/end/both")};
    }
    if (update.label.has_value() &&
        TitleRules::codePointCount(update.label->trimmed()) > Limits::kTitleMaxChars) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("关系线标签超过%1个字符").arg(Limits::kTitleMaxChars)};
    }
    const QString label = update.label.has_value() ? update.label->trimmed() : current->label;
    const QString arrow = update.arrow.value_or(current->arrow);
    if (label == current->label && arrow == current->arrow) return true;   // 无操作短路

    const auto result = m_commands->applyEdit(
        QStringLiteral("修改关系线"), [sheetId, relationId, label, arrow](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            for (Relation& relation : target->relations) {
                if (relation.id != relationId) continue;
                relation.label = label;
                relation.arrow = arrow;
                return;
            }
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<bool, AppError> NodeCommands::removeRelation(const ID& sheetId, const ID& relationId) {
    if (sheetId.isEmpty() || relationId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与关系线ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    bool found = false;
    for (const Relation& relation : tree->relations)
        if (relation.id == relationId) found = true;
    if (!found) {
        return AppError{"NOT_FOUND", QStringLiteral("关系线不存在：%1").arg(relationId)};
    }

    const auto result = m_commands->applyEdit(
        QStringLiteral("删除关系线"), [sheetId, relationId](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            QVector<Relation> kept;
            for (const Relation& relation : target->relations)
                if (relation.id != relationId) kept.append(relation);
            target->relations = kept;
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<ID, AppError> NodeCommands::createAnnotation(const ID& sheetId, const ID& parentId,
                                                          const QString& kind,
                                                          const QStringList& memberIds,
                                                          const QString& text,
                                                          const QString& color) {
    if (sheetId.isEmpty() || parentId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与父节点ID不能为空")};
    }
    if (kind != QStringLiteral("boundary") && kind != QStringLiteral("summary")) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("外框/概要类型只允许boundary/summary")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    if (!tree->nodes.contains(parentId)) {
        return AppError{"NOT_FOUND", QStringLiteral("父节点不存在：%1").arg(parentId)};
    }
    const auto ordered = orderedAnnotationMembers(*tree, parentId, memberIds);
    if (const auto* error = std::get_if<AppError>(&ordered)) {
        return *error;
    }
    const QString normalizedText = text.trimmed();
    if (TitleRules::codePointCount(normalizedText) > Limits::kTitleMaxChars) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("外框/概要文本超过%1个字符").arg(Limits::kTitleMaxChars)};
    }
    const QString normalizedColor = color.trimmed();
    if (!normalizedColor.isEmpty() && !isHexColor(normalizedColor)) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("颜色必须为#RRGGBB格式")};
    }

    const QStringList members = std::get<QStringList>(ordered);
    const ID annotationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto result = m_commands->applyEdit(
        QStringLiteral("新增%1").arg(kind == QStringLiteral("boundary") ? QStringLiteral("外框")
                                                                       : QStringLiteral("概要")),
        [sheetId, annotationId, parentId, kind, members, normalizedText,
         normalizedColor](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            GroupAnnotation annotation;
            annotation.id = annotationId;
            annotation.parentId = parentId;
            annotation.kind = kind;
            annotation.memberIds = members;
            annotation.text = normalizedText;
            annotation.color = normalizedColor;
            target->annotations.append(annotation);
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return annotationId;
}

std::variant<bool, AppError> NodeCommands::updateAnnotation(const ID& sheetId,
                                                            const ID& annotationId,
                                                            const AnnotationUpdate& update) {
    if (sheetId.isEmpty() || annotationId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与外框/概要ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    const GroupAnnotation* current = nullptr;
    for (const GroupAnnotation& annotation : tree->annotations)
        if (annotation.id == annotationId) current = &annotation;
    if (current == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("外框/概要不存在：%1").arg(annotationId)};
    }

    QStringList members = current->memberIds;
    if (update.memberIds.has_value()) {
        const auto ordered = orderedAnnotationMembers(*tree, current->parentId, *update.memberIds);
        if (const auto* error = std::get_if<AppError>(&ordered)) {
            return *error;
        }
        members = std::get<QStringList>(ordered);
    }
    if (update.text.has_value() &&
        TitleRules::codePointCount(update.text->trimmed()) > Limits::kTitleMaxChars) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("外框/概要文本超过%1个字符").arg(Limits::kTitleMaxChars)};
    }
    if (update.color.has_value() && !update.color->trimmed().isEmpty() &&
        !isHexColor(update.color->trimmed())) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("颜色必须为#RRGGBB格式")};
    }
    const QString text = update.text.has_value() ? update.text->trimmed() : current->text;
    const QString color = update.color.has_value() ? update.color->trimmed() : current->color;
    if (members == current->memberIds && text == current->text && color == current->color) {
        return true;   // 无操作短路
    }

    const auto result = m_commands->applyEdit(
        QStringLiteral("修改外框/概要"),
        [sheetId, annotationId, members, text, color](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            for (GroupAnnotation& annotation : target->annotations) {
                if (annotation.id != annotationId) continue;
                annotation.memberIds = members;
                annotation.text = text;
                annotation.color = color;
                return;
            }
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<bool, AppError> NodeCommands::removeAnnotation(const ID& sheetId,
                                                            const ID& annotationId) {
    if (sheetId.isEmpty() || annotationId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与外框/概要ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    bool found = false;
    for (const GroupAnnotation& annotation : tree->annotations)
        if (annotation.id == annotationId) found = true;
    if (!found) {
        return AppError{"NOT_FOUND", QStringLiteral("外框/概要不存在：%1").arg(annotationId)};
    }

    const auto result = m_commands->applyEdit(
        QStringLiteral("删除外框/概要"), [sheetId, annotationId](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            QVector<GroupAnnotation> kept;
            for (const GroupAnnotation& annotation : target->annotations)
                if (annotation.id != annotationId) kept.append(annotation);
            target->annotations = kept;
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<ID, AppError> NodeCommands::addComment(const ID& sheetId, const ID& nodeId,
                                                    const QString& author, const QString& body) {
    if (sheetId.isEmpty() || nodeId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与节点ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    if (!tree->nodes.contains(nodeId)) {
        return AppError{"NOT_FOUND", QStringLiteral("节点不存在：%1").arg(nodeId)};
    }
    const QString normalizedBody = body.trimmed();
    if (normalizedBody.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("批注正文不能为空白")};
    }
    if (TitleRules::codePointCount(normalizedBody) > Limits::kCommentMaxChars) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("单条批注不超过%1个字符").arg(Limits::kCommentMaxChars)};
    }

    const ID commentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString normalizedAuthor = author.trimmed();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto result = m_commands->applyEdit(
        QStringLiteral("新增批注"), [sheetId, commentId, nodeId, normalizedAuthor, normalizedBody,
                                     now](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            Comment comment;
            comment.id = commentId;
            comment.nodeId = nodeId;
            comment.author = normalizedAuthor;
            comment.body = normalizedBody;
            comment.createdAt = now;
            comment.updatedAt = now;
            comment.resolved = false;
            target->comments.append(comment);
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return commentId;
}

std::variant<bool, AppError> NodeCommands::resolveComment(const ID& sheetId, const ID& commentId,
                                                          bool resolved) {
    if (sheetId.isEmpty() || commentId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与批注ID不能为空")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    const Comment* current = nullptr;
    for (const Comment& comment : tree->comments)
        if (comment.id == commentId) current = &comment;
    if (current == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("批注不存在：%1").arg(commentId)};
    }
    if (current->resolved == resolved) return true;   // 状态相同则短路

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString label = resolved ? QStringLiteral("标记批注已解决")
                                   : QStringLiteral("重新打开批注");
    const auto result = m_commands->applyEdit(
        label, [sheetId, commentId, resolved, now](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;
            for (Comment& comment : target->comments) {
                if (comment.id != commentId) continue;
                comment.resolved = resolved;
                comment.updatedAt = now;
                return;
            }
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<Fragment, AppError> NodeCommands::copyFragment(const ID& sheetId,
                                                           const QVector<ID>& nodeIds) const {
    if (sheetId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID不能为空")};
    }
    if (nodeIds.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("没有可复制的节点")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    QSet<ID> selected;
    for (const ID& nodeId : nodeIds) {
        if (!tree->nodes.contains(nodeId)) {
            return AppError{"NOT_FOUND", QStringLiteral("节点不存在：%1").arg(nodeId)};
        }
        selected.insert(nodeId);
    }

    // 祖先与后代同选时只处理最高层一次（规范§5.3）
    QSet<ID> rootSet;
    for (const ID& nodeId : selected) {
        bool hasSelectedAncestor = false;
        auto it = tree->nodes.constFind(nodeId);
        while (it != tree->nodes.constEnd() && it->parentId.has_value()) {
            if (selected.contains(*it->parentId)) {
                hasSelectedAncestor = true;
                break;
            }
            it = tree->nodes.constFind(*it->parentId);
        }
        if (!hasSelectedAncestor) rootSet.insert(nodeId);
    }

    Fragment fragment;
    const QVector<ID> order = treeOrder(*tree);
    for (const ID& nodeId : order) {
        if (rootSet.contains(nodeId)) fragment.rootIds.append(nodeId);
    }
    if (fragment.rootIds.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("没有可复制的顶层节点")};
    }

    QSet<ID> inside;
    for (const ID& rootId : fragment.rootIds) inside.unite(subtreeIds(*tree, rootId));

    for (const ID& nodeId : order) {
        if (!inside.contains(nodeId)) continue;
        TreeNode copy = tree->nodes.value(nodeId);
        if (rootSet.contains(nodeId)) copy.parentId.reset();
        fragment.nodes.insert(nodeId, copy);
    }
    for (const Relation& relation : tree->relations) {
        if (inside.contains(relation.sourceId) && inside.contains(relation.targetId))
            fragment.relations.append(relation);
    }
    for (const GroupAnnotation& annotation : tree->annotations) {
        if (!inside.contains(annotation.parentId)) continue;
        bool allInside = !annotation.memberIds.isEmpty();
        for (const ID& member : annotation.memberIds)
            if (!inside.contains(member)) allInside = false;
        if (allInside) fragment.annotations.append(annotation);
    }
    for (const Comment& comment : tree->comments) {
        if (inside.contains(comment.nodeId)) fragment.comments.append(comment);
    }
    for (const Task& task : m_document->project().tasks) {
        if (!task.source.has_value() || task.source->sheetId != sheetId) continue;
        if (inside.contains(task.source->nodeId)) fragment.tasks.append(task);
    }
    return fragment;
}

std::variant<ID, AppError> NodeCommands::pasteFragment(const ID& sheetId, const ID& parentId,
                                                       const Fragment& fragment,
                                                       const ID& beforeNodeId) {
    if (sheetId.isEmpty() || parentId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID与目标父级ID不能为空")};
    }
    if (fragment.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("片段为空，没有可粘贴的内容")};
    }
    const TreeSheet* tree = findTree(m_document->project(), sheetId);
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    if (!tree->nodes.contains(parentId)) {
        return AppError{"NOT_FOUND", QStringLiteral("目标父级不存在：%1").arg(parentId)};
    }
    if (totalNodeCount(m_document->project()) + fragment.nodes.size() > Limits::kMaxNodes) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("粘贴后节点数量将超过上限%1").arg(Limits::kMaxNodes)};
    }
    if (!beforeNodeId.isEmpty()) {
        const auto before = tree->nodes.constFind(beforeNodeId);
        if (before == tree->nodes.constEnd()) {
            return AppError{"NOT_FOUND", QStringLiteral("参照节点不存在：%1").arg(beforeNodeId)};
        }
        if (!before->parentId.has_value() || *before->parentId != parentId) {
            return AppError{"INVALID_ARGUMENT",
                            QStringLiteral("参照节点必须是目标父级的子节点")};
        }
    }

    // 全部ID重新生成（规范§5.3：复制不保留原ID，避免与目标工程冲突）
    QHash<ID, ID> idMap;
    for (auto it = fragment.nodes.constBegin(); it != fragment.nodes.constEnd(); ++it)
        idMap.insert(it.key(), QUuid::createUuid().toString(QUuid::WithoutBraces));
    QHash<ID, ID> taskIdMap;
    for (const Task& task : fragment.tasks)
        taskIdMap.insert(task.id, QUuid::createUuid().toString(QUuid::WithoutBraces));

    ID firstRootId;
    for (const ID& rootId : fragment.rootIds) {
        if (!idMap.contains(rootId)) continue;
        firstRootId = idMap.value(rootId);
        break;
    }
    if (firstRootId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("片段缺少有效的顶层节点")};
    }

    const auto result = m_commands->applyEdit(
        QStringLiteral("粘贴主题"),
        [sheetId, parentId, beforeNodeId, fragment, idMap, taskIdMap](Project& project) {
            TreeSheet* target = findTree(project, sheetId);
            if (target == nullptr) return;

            // 目标父级下的兄弟序列，先占住插入位置
            QVector<ID> sequence = siblingSequence(*target, parentId);
            int at = beforeNodeId.isEmpty() ? sequence.size() : sequence.indexOf(beforeNodeId);
            if (at < 0) at = sequence.size();
            int offset = 0;
            for (const ID& rootId : fragment.rootIds) {
                const auto mapped = idMap.constFind(rootId);
                if (mapped == idMap.constEnd()) continue;
                sequence.insert(at + offset, mapped.value());
                ++offset;
            }

            for (auto it = fragment.nodes.constBegin(); it != fragment.nodes.constEnd(); ++it) {
                const ID newId = idMap.value(it.key());
                TreeNode copy = it.value();
                copy.id = newId;
                if (it.value().parentId.has_value())
                    copy.parentId = idMap.value(*it.value().parentId);
                else
                    copy.parentId = parentId;
                if (copy.contentRevision < 1) copy.contentRevision = 1;
                target->nodes.insert(newId, copy);
            }
            for (int i = 0; i < sequence.size(); ++i)
                target->nodes[sequence.at(i)].order = i;

            for (const Relation& relation : fragment.relations) {
                Relation copy = relation;
                copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                copy.sourceId = idMap.value(relation.sourceId);
                copy.targetId = idMap.value(relation.targetId);
                if (copy.sourceId.isEmpty() || copy.targetId.isEmpty()) continue;
                target->relations.append(copy);
            }
            for (const GroupAnnotation& annotation : fragment.annotations) {
                GroupAnnotation copy = annotation;
                copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                copy.parentId = idMap.value(annotation.parentId);
                copy.memberIds.clear();
                for (const ID& member : annotation.memberIds)
                    copy.memberIds.append(idMap.value(member));
                if (copy.parentId.isEmpty()) continue;
                target->annotations.append(copy);
            }
            for (const Comment& comment : fragment.comments) {
                Comment copy = comment;
                copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                copy.nodeId = idMap.value(comment.nodeId);
                if (copy.nodeId.isEmpty()) continue;
                target->comments.append(copy);
            }

            for (const Task& task : fragment.tasks) {
                Task copy = task;
                copy.id = taskIdMap.value(task.id);
                if (copy.id.isEmpty()) continue;
                if (task.source.has_value()) {
                    const ID mappedNode = idMap.value(task.source->nodeId);
                    if (mappedNode.isEmpty())
                        copy.source.reset();
                    else
                        copy.source = TaskSource{sheetId, mappedNode};
                }
                QVector<ID> keptPredecessors;
                for (const ID& predecessor : task.predecessorIds) {
                    const ID mappedTask = taskIdMap.value(predecessor);
                    if (!mappedTask.isEmpty()) keptPredecessors.append(mappedTask);
                }
                copy.predecessorIds = keptPredecessors;
                project.tasks.append(copy);
            }

            ++target->nodes[parentId].contentRevision;
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return firstRootId;
}

}  // namespace lingya
