#include "document/Fragment.h"
#include "domain/ProjectFactory.h"
#include "services/ProjectJson.h"

#include <QJsonArray>
#include <QSet>

#include <cmath>

namespace lingya {

namespace {

// 载体工程的固定标识：只用于复用ProjectJson的严格读写，不属于片段内容。
// 合成根的ID须满足ProjectJson的UUID约束，取固定保留值，粘贴时不会进入目标工程
const QString kCarrierRootId = QStringLiteral("00000000-0000-4000-8000-000000000000");

AppError invalid(const QString& message) {
    return AppError{"INVALID_ARGUMENT", message};
}

bool isPlainInteger(const QJsonValue& value) {
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return std::isfinite(number) && number == std::floor(number);
}

}  // namespace

bool Fragment::isEmpty() const {
    return rootIds.isEmpty() || nodes.isEmpty();
}

QJsonObject Fragment::toJson() const {
    // 载体工程取ProjectFactory的合法默认值（UUID主键、合法#RRGGBB样式），
    // 只保留一张树图与合成根，避免自造字段被ProjectJson的严格读取拒绝。
    Project carrier = ProjectFactory::createMindmap(QStringLiteral("片段载体"));
    TreeSheet tree = std::get<TreeSheet>(carrier.sheets.first());
    tree.nodes.clear();
    tree.relations.clear();
    tree.annotations.clear();
    tree.comments.clear();
    tree.name = QStringLiteral("片段");
    tree.kind = QStringLiteral("mindmap");
    tree.layout = QStringLiteral("bilateral");
    tree.rootId = kCarrierRootId;

    TreeNode carrierRoot;
    carrierRoot.id = kCarrierRootId;
    carrierRoot.title = QStringLiteral("片段");
    carrierRoot.contentRevision = 1;
    tree.nodes.insert(kCarrierRootId, carrierRoot);

    // 顶层根挂到合成根下，order按rootIds顺序重排；其余节点保留原parentId/order
    int rootOrder = 0;
    for (const ID& rootId : rootIds) {
        const auto it = nodes.constFind(rootId);
        if (it == nodes.constEnd()) continue;
        TreeNode copy = it.value();
        copy.parentId = kCarrierRootId;
        copy.order = rootOrder++;
        tree.nodes.insert(rootId, copy);
    }
    for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
        if (!tree.nodes.contains(it.key())) tree.nodes.insert(it.key(), it.value());
    }

    tree.relations = relations;
    tree.annotations = annotations;
    tree.comments = comments;

    carrier.sheets.clear();
    carrier.sheets.push_back(tree);
    carrier.activeSheetId = tree.id;
    carrier.tasks = tasks;

    QJsonArray roots;
    for (const ID& rootId : rootIds) roots.append(rootId);

    QJsonObject wrapper;
    wrapper.insert(QStringLiteral("fragmentVersion"), fragmentVersion);
    wrapper.insert(QStringLiteral("roots"), roots);
    wrapper.insert(QStringLiteral("payload"), ProjectJson::toJson(carrier));
    return wrapper;
}

std::variant<Fragment, AppError> Fragment::fromJson(const QJsonObject& object) {
    const QJsonValue versionValue = object.value(QStringLiteral("fragmentVersion"));
    if (!isPlainInteger(versionValue))
        return invalid(QStringLiteral("片段缺少有效的fragmentVersion"));
    const int version = versionValue.toInt();
    if (version < 1) return invalid(QStringLiteral("片段版本号无效：%1").arg(version));
    if (version > kVersion) {
        return AppError{"UNSUPPORTED_SCHEMA",
                        QStringLiteral("片段版本%1高于当前支持的%2").arg(version).arg(kVersion)};
    }

    const QJsonValue rootsValue = object.value(QStringLiteral("roots"));
    if (!rootsValue.isArray() || rootsValue.toArray().isEmpty())
        return invalid(QStringLiteral("片段缺少顶层节点列表"));

    QVector<ID> roots;
    QSet<ID> seen;
    for (const QJsonValue& value : rootsValue.toArray()) {
        if (!value.isString()) return invalid(QStringLiteral("片段顶层节点ID必须是字符串"));
        const ID rootId = value.toString();
        if (rootId.isEmpty()) return invalid(QStringLiteral("片段顶层节点ID不能为空"));
        if (seen.contains(rootId)) return invalid(QStringLiteral("片段顶层节点ID重复：%1").arg(rootId));
        seen.insert(rootId);
        roots.append(rootId);
    }

    const QJsonValue payloadValue = object.value(QStringLiteral("payload"));
    if (!payloadValue.isObject())
        return invalid(QStringLiteral("片段缺少payload对象"));

    const auto parsed = ProjectJson::fromObject(payloadValue.toObject());
    if (const auto* error = std::get_if<AppError>(&parsed)) {
        return AppError{error->code, QStringLiteral("片段内容无效：%1").arg(error->message)};
    }
    const Project& carrier = std::get<Project>(parsed);

    const TreeSheet* tree = nullptr;
    for (const Sheet& sheet : carrier.sheets) {
        if (const auto* candidate = std::get_if<TreeSheet>(&sheet)) {
            if (candidate->rootId == kCarrierRootId) {
                tree = candidate;
                break;
            }
            if (tree == nullptr) tree = candidate;   // 防御：合成根被改写时退化为首张树图
        }
    }
    if (tree == nullptr) return invalid(QStringLiteral("片段载体缺少树图工作表"));

    Fragment fragment;
    fragment.fragmentVersion = version;
    fragment.rootIds = roots;

    for (auto it = tree->nodes.constBegin(); it != tree->nodes.constEnd(); ++it) {
        if (it.key() == kCarrierRootId) continue;   // 合成根不属于片段内容
        fragment.nodes.insert(it.key(), it.value());
    }
    if (fragment.nodes.isEmpty()) return invalid(QStringLiteral("片段不含任何节点"));

    // 顶层根的parentId还原为空；其余节点必须挂回片段内部
    for (const ID& rootId : roots) {
        const auto it = fragment.nodes.find(rootId);
        if (it == fragment.nodes.end())
            return invalid(QStringLiteral("片段顶层节点不存在：%1").arg(rootId));
        it->parentId.reset();
    }
    QVector<ID> orphans;
    for (auto it = fragment.nodes.constBegin(); it != fragment.nodes.constEnd(); ++it) {
        if (!it.value().parentId.has_value()) continue;
        if (!fragment.nodes.contains(*it.value().parentId))
            orphans.append(it.key());
    }
    if (!orphans.isEmpty())
        return invalid(QStringLiteral("片段节点缺少父级：%1").arg(orphans.first()));

    // 只保留仍在片段内的引用（写出的载荷本应已过滤，读取侧再防御一次）
    for (const Relation& relation : tree->relations) {
        if (fragment.nodes.contains(relation.sourceId) &&
            fragment.nodes.contains(relation.targetId))
            fragment.relations.append(relation);
    }
    for (const GroupAnnotation& annotation : tree->annotations) {
        if (!fragment.nodes.contains(annotation.parentId)) continue;
        bool allInside = !annotation.memberIds.isEmpty();
        for (const ID& member : annotation.memberIds)
            if (!fragment.nodes.contains(member)) allInside = false;
        if (allInside) fragment.annotations.append(annotation);
    }
    for (const Comment& comment : tree->comments) {
        if (fragment.nodes.contains(comment.nodeId)) fragment.comments.append(comment);
    }
    for (Task task : carrier.tasks) {
        if (task.source.has_value() && !fragment.nodes.contains(task.source->nodeId))
            task.source.reset();   // 源节点不在片段内时退化为独立任务（规范§5.3）
        fragment.tasks.append(task);
    }

    return fragment;
}

}  // namespace lingya
