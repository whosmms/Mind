#include "domain/ProjectValidator.h"
#include "domain/Limits.h"
#include <QHash>
#include <QQueue>
#include <QSet>
#include <functional>

namespace lingya {


namespace {

// 标题、标签等长度按Unicode码点计数（规范§4.2）
int codePointCount(const QString& text) {
    return static_cast<int>(text.toUcs4().size());
}

void checkTree(const TreeSheet& sheet, QVector<AppError>& es) {
    const QString where = QStringLiteral("工作表“%1”").arg(sheet.name);

    if (!sheet.nodes.contains(sheet.rootId)) {
        es << AppError{"TREE_ROOT_MISSING", where + "：根节点不存在"};
        return;
    }
    if (sheet.nodes.value(sheet.rootId).parentId.has_value())
        es << AppError{"TREE_ROOT_PARENT", where + "：根节点不应有父级"};

    // 父级存在性、order非负、标题长度（规则1、规则8）
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        const TreeNode& n = it.value();
        if (n.id != sheet.rootId) {
            if (!n.parentId.has_value()) {
                es << AppError{"TREE_MULTI_ROOT",
                               where + "：节点“" + n.title + "”没有父级，一张图只能有一个根"};
            } else if (!sheet.nodes.contains(*n.parentId)) {
                es << AppError{"TREE_PARENT_MISSING",
                               where + "：节点“" + n.title + "”的父级不存在"};
            }
        }
        if (n.order < 0)
            es << AppError{"TREE_ORDER", where + "：节点“" + n.title + "”的order为负数"};
        const int len = codePointCount(n.title);
        if (len < Limits::kTitleMinChars || len > Limits::kTitleMaxChars)
            es << AppError{"TITLE_LENGTH", where + "：节点标题长度必须在1至2000字符之间"};
        if (n.tags.size() > Limits::kMaxTags)
            es << AppError{"TAG_COUNT", where + "：节点标签最多20个"};
        for (const QString& tag : n.tags)
            if (codePointCount(tag) > Limits::kTagMaxChars)
                es << AppError{"TAG_LENGTH", where + "：单个标签不超过30字符"};
    }

    // 环检测：从每个节点沿父链上溯，步数超过节点数即为环（规则1）
    bool cycleReported = false;
    const int stepLimit = sheet.nodes.size() + 1;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        const TreeNode* cur = &it.value();
        int steps = 0;
        while (cur && cur->id != sheet.rootId) {
            if (++steps > stepLimit) {
                if (!cycleReported) {
                    es << AppError{"TREE_CYCLE", where + "：树的父级引用存在环路"};
                    cycleReported = true;
                }
                break;
            }
            if (!cur->parentId.has_value())
                break;  // 多根或缺父级已单独报错
            const auto nit = sheet.nodes.constFind(*cur->parentId);
            if (nit == sheet.nodes.constEnd())
                break;  // 父级缺失已单独报错
            cur = &nit.value();
        }
    }

    // 可达性：从根BFS，全部节点必须可达（规则1）
    QSet<QString> visited;
    QQueue<QString> queue;
    visited.insert(sheet.rootId);
    queue.enqueue(sheet.rootId);
    while (!queue.isEmpty()) {
        const QString current = queue.dequeue();
        for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it)
            if (it.value().parentId == current && !visited.contains(it.key())) {
                visited.insert(it.key());
                queue.enqueue(it.key());
            }
    }
    if (visited.size() != sheet.nodes.size())
        es << AppError{"TREE_UNREACHABLE", where + "：存在无法从根到达的节点"};

    // 关系线端点必须存在（规则7；删除节点时清理由命令负责）
    for (const Relation& rel : sheet.relations)
        if (!sheet.nodes.contains(rel.sourceId) || !sheet.nodes.contains(rel.targetId))
            es << AppError{"RELATION_ENDPOINT", where + "：关系线“" + rel.label + "”的端点缺失"};

    // 分组注释：父级与成员存在；跨父选区、连续性与成员数由命令保证（规则6、规则7）
    for (const GroupAnnotation& ga : sheet.annotations) {
        if (!sheet.nodes.contains(ga.parentId))
            es << AppError{"ANNOTATION_PARENT", where + "：外框/概要的父节点不存在"};
        for (const ID& member : ga.memberIds)
            if (!sheet.nodes.contains(member)) {
                es << AppError{"ANNOTATION_MEMBER", where + "：外框/概要包含不存在的成员节点"};
                break;
            }
    }

    // 批注的节点必须存在
    for (const Comment& c : sheet.comments)
        if (!sheet.nodes.contains(c.nodeId))
            es << AppError{"COMMENT_NODE", where + "：批注指向不存在的节点"};
}

void checkFlow(const FlowSheet& sheet, QVector<AppError>& es) {
    // 流程图允许回路和暂时未连接的节点，不按树约束强行校验（规则3）
    const QString where = QStringLiteral("工作表“%1”").arg(sheet.name);
    for (const FlowEdge& edge : sheet.edges)
        if (!sheet.nodes.contains(edge.sourceId) || !sheet.nodes.contains(edge.targetId))
            es << AppError{"FLOW_EDGE_ENDPOINT", where + "：流程连线端点缺失"};
    for (const Comment& c : sheet.comments)
        if (!sheet.nodes.contains(c.nodeId))
            es << AppError{"COMMENT_NODE", where + "：批注指向不存在的节点"};
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        const int len = codePointCount(it.value().title);
        if (len < Limits::kTitleMinChars || len > Limits::kTitleMaxChars)
            es << AppError{"TITLE_LENGTH", where + "：节点标题长度必须在1至2000字符之间"};
    }
}

}  // namespace

namespace ProjectValidator {

QVector<AppError> validate(const Project& project) {
    QVector<AppError> es;

    if (project.schemaVersion != Limits::kSchemaVersion)
        es << AppError{"SCHEMA_VERSION",
                       QString("schemaVersion必须为%1").arg(Limits::kSchemaVersion)};
    if (codePointCount(project.title) > Limits::kTitleMaxChars)
        es << AppError{"TITLE_LENGTH", "工程标题超过2000字符"};

    // 工程内ID唯一（规则1）：跨工作表、任务、卡片、路线、文稿统一登记
    QSet<QString> used;
    auto claim = [&es, &used](const QString& id, const QString& what) {
        if (id.isEmpty()) {
            es << AppError{"EMPTY_ID", what + "的ID为空"};
            return;
        }
        if (used.contains(id))
            es << AppError{"DUPLICATE_ID", what + "的ID与其他记录重复：" + id};
        used.insert(id);
    };
    claim(project.id, "工程");

    // 第一遍：登记全部ID，检查各工作表内部规则
    QSet<QString> sheetIds;
    QHash<QString, const TreeSheet*> treeSheets;
    for (const Sheet& sheet : project.sheets) {
        if (const TreeSheet* tree = std::get_if<TreeSheet>(&sheet)) {
            claim(tree->id, "工作表");
            sheetIds.insert(tree->id);
            treeSheets.insert(tree->id, tree);
            // kind与layout配对（规则2）：mindmap只用bilateral/right，fishbone只用fishbone
            const bool layoutOk =
                (tree->kind == "mindmap" && (tree->layout == "bilateral" || tree->layout == "right")) ||
                (tree->kind == "fishbone" && tree->layout == "fishbone");
            if (!layoutOk)
                es << AppError{"LAYOUT_KIND",
                               QStringLiteral("工作表“%1”：kind与layout不匹配").arg(tree->name)};
            for (auto it = tree->nodes.constBegin(); it != tree->nodes.constEnd(); ++it)
                claim(it.key(), "节点");
            for (const Relation& rel : tree->relations) claim(rel.id, "关系线");
            for (const GroupAnnotation& ga : tree->annotations) claim(ga.id, "分组注释");
            for (const Comment& c : tree->comments) claim(c.id, "批注");
            checkTree(*tree, es);
        } else if (const FlowSheet* flow = std::get_if<FlowSheet>(&sheet)) {
            claim(flow->id, "工作表");
            sheetIds.insert(flow->id);
            for (auto it = flow->nodes.constBegin(); it != flow->nodes.constEnd(); ++it)
                claim(it.key(), "节点");
            for (const FlowEdge& edge : flow->edges) claim(edge.id, "流程连线");
            for (const Comment& c : flow->comments) claim(c.id, "批注");
            checkFlow(*flow, es);
        } else if (const GanttSheet* gantt = std::get_if<GanttSheet>(&sheet)) {
            claim(gantt->id, "工作表");
            sheetIds.insert(gantt->id);
        }
    }
    if (!sheetIds.contains(project.activeSheetId))
        es << AppError{"MISSING_SHEET", "活动工作表不存在"};

    // 第二遍：跨工作表引用（甘特筛选来源可能指向后声明的工作表，故分两遍）
    for (const Sheet& sheet : project.sheets)
        if (const GanttSheet* gantt = std::get_if<GanttSheet>(&sheet))
            if (gantt->filter.sourceSheetId.has_value() &&
                !sheetIds.contains(*gantt->filter.sourceSheetId))
                es << AppError{"GANTT_FILTER_SHEET",
                               QStringLiteral("工作表“%1”：甘特筛选来源工作表不存在").arg(gantt->name)};

    // 任务：进度、排期、里程碑、关联来源、依赖（规则5；依赖图必须无环）
    // 先整体登记ID再逐条校验：前置依赖的合法性不能依赖任务在数组中的先后顺序
    QHash<QString, const Task*> tasksById;
    for (const Task& task : project.tasks) {
        claim(task.id, "任务");
        tasksById.insert(task.id, &task);
    }
    for (const Task& task : project.tasks) {
        if (task.progress < 0 || task.progress > 100)
            es << AppError{"TASK_PROGRESS", "任务“" + task.title + "”进度必须在0至100之间"};
        if (codePointCount(task.title) < Limits::kTitleMinChars ||
            codePointCount(task.title) > Limits::kTitleMaxChars)
            es << AppError{"TITLE_LENGTH", "任务标题长度必须在1至2000字符之间"};
        if (task.source.has_value()) {
            const auto it = treeSheets.find(task.source->sheetId);
            if (it == treeSheets.end())
                es << AppError{"TASK_SOURCE", "任务“" + task.title + "”关联的工作表不存在或不是树图"};
            else if (!it.value()->nodes.contains(task.source->nodeId))
                es << AppError{"TASK_SOURCE", "任务“" + task.title + "”关联的节点不存在"};
        }
        if (task.startDate.has_value() != task.endDate.has_value())
            es << AppError{"TASK_DATE", "任务“" + task.title + "”的起止日期必须成对设置或同时为空"};
        if (task.startDate.has_value() && task.endDate.has_value()) {
            if (*task.startDate > *task.endDate)
                es << AppError{"TASK_DATE", "任务“" + task.title + "”结束日期早于开始日期"};
            if (task.milestone && *task.startDate != *task.endDate)
                es << AppError{"TASK_MILESTONE", "里程碑任务“" + task.title + "”必须同日起止"};
        }
        for (const ID& pred : task.predecessorIds)
            if (!tasksById.contains(pred))
                es << AppError{"TASK_PREDECESSOR", "任务“" + task.title + "”依赖的前驱任务不存在"};
    }

    // 任务依赖无环：三色标记深度优先（规则5）
    enum class Mark { None, Active, Done };
    QHash<QString, int> mark;
    std::function<bool(const QString&)> visit = [&](const QString& id) -> bool {
        const int m = mark.value(id, static_cast<int>(Mark::None));
        if (m == static_cast<int>(Mark::Done)) return false;
        if (m == static_cast<int>(Mark::Active)) return true;  // 回边：发现环
        mark[id] = static_cast<int>(Mark::Active);
        const Task* task = tasksById.value(id, nullptr);
        if (task)
            for (const QString& pred : task->predecessorIds)
                if (tasksById.contains(pred) && visit(pred))
                    return true;
        mark[id] = static_cast<int>(Mark::Done);
        return false;
    };
    bool dependencyCycle = false;
    for (const Task& task : project.tasks)
        if (visit(task.id)) {
            dependencyCycle = true;
            break;
        }
    if (dependencyCycle)
        es << AppError{"TASK_DEPENDENCY_CYCLE", "任务依赖存在环路"};

    // 复习卡片：工作表必须存在；源节点允许缺失（sourceState=missing，§7.1运行时处理）
    for (const ReviewCard& card : project.cards) {
        claim(card.id, "复习卡片");
        if (!sheetIds.contains(card.sheetId))
            es << AppError{"CARD_SHEET", "复习卡片“" + card.front + "”指向的工作表不存在"};
    }

    // 讲解路线：基于树图建立（§7.2）；步骤节点缺失由运行时重校验提示跳过
    for (const PresentationRoute& route : project.routes) {
        claim(route.id, "讲解路线");
        if (!treeSheets.contains(route.sheetId))
            es << AppError{"ROUTE_SHEET", "讲解路线“" + route.title + "”必须基于树图工作表"};
        for (const PresentationStep& step : route.steps) claim(step.id, "讲解步骤");
    }

    // 文稿：来源工作表必须存在
    for (const TextDraft& draft : project.drafts) {
        claim(draft.id, "文稿");
        if (!sheetIds.contains(draft.sourceSheetId))
            es << AppError{"DRAFT_SHEET", "文稿“" + draft.title + "”的来源工作表不存在"};
    }

    return es;    return es;
}

}  // namespace ProjectValidator

}  // namespace lingya
