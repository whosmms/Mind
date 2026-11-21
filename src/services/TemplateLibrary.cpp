#include "services/TemplateLibrary.h"
#include "domain/ProjectFactory.h"
#include <QUuid>
#include <algorithm>
namespace lingya::TemplateLibrary
{
namespace
{
ID uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace
QVector<Entry> entries()
{
    return {{"map-bilateral", "双向导图", "mindmap", "中心主题向两侧展开"},
            {"map-right", "向右逻辑图", "mindmap", "从左到右梳理多级要点"},
            {"map-left", "向左逻辑图", "mindmap", "从右到左展开"},
            {"map-down", "向下组织结构", "mindmap", "上级与下级纵向层次"},
            {"map-up", "向上树形图", "mindmap", "由下向上汇聚层次"},
            {"map-study", "课程知识树", "mindmap", "概念、方法、练习、复盘"},
            {"map-brainstorm", "头脑风暴", "mindmap", "问题、想法、资源与行动"},
            {"map-goal", "目标拆解", "mindmap", "目标、指标、阶段与行动"},
            {"fish-quality", "质量根因分析", "fishbone", "人员、设备、材料、方法、测量、环境"},
            {"fish-study", "学习问题分析", "fishbone", "基础、方法、练习、反馈、时间、环境"},
            {"fish-retro", "项目复盘鱼骨", "fishbone", "目标、流程、协作、资源、风险、反馈"},
            {"fish-project", "交付延期分析", "fishbone", "需求、计划、技术、人员、依赖、变更"},
            {"flow-sequence", "顺序流程", "flowchart", "开始、处理、检查、结束"},
            {"flow-decision", "条件分支", "flowchart", "是/否两条路径"},
            {"flow-cycle", "循环与重试", "flowchart", "判断后返回处理步骤"},
            {"flow-approval", "审批流程", "flowchart", "提交、审核、批准或修改"},
            {"flow-timeline", "横向时间轴", "flowchart", "里程碑沿横轴排列"},
            {"flow-timeline-v", "纵向时间轴", "flowchart", "里程碑从上到下排列"},
            {"flow-org", "层级组织流程", "flowchart", "负责人分派到多个团队"},
            {"flow-swimlane", "跨角色协作", "flowchart", "按颜色区分发起、处理和复核角色"},
            {"gantt-project", "项目实施计划", "gantt", "启动、开发、测试、交付"},
            {"gantt-thesis", "论文写作计划", "gantt", "调研、方案、实验、写作、修改"},
            {"gantt-study", "课程复习计划", "gantt", "分日复习与阶段总结"},
            {"gantt-release", "版本发布计划", "gantt", "功能冻结、回归、打包、交付"}};
}
std::variant<Project, AppError> create(const QString &id)
{
    Entry entry;
    for (const auto &e : entries())
        if (e.id == id)
            entry = e;
    if (entry.id.isEmpty())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("未知模板")};
    if (entry.kind == "mindmap")
    {
        auto p = ProjectFactory::createMindmap(entry.title);
        auto &tree = std::get<TreeSheet>(p.sheets.first());
        tree.name = entry.title;
        if (id == "map-right")
            tree.layout = "right";
        else if (id == "map-left")
            tree.layout = "left";
        else if (id == "map-down")
            tree.layout = "down";
        else if (id == "map-up")
            tree.layout = "up";
        QStringList headings{QStringLiteral("目标"), QStringLiteral("关键要点"),
                             QStringLiteral("行动"), QStringLiteral("检查")};
        if (id == "map-study")
            headings = {QStringLiteral("概念"), QStringLiteral("方法"), QStringLiteral("练习"),
                        QStringLiteral("复盘")};
        if (id == "map-brainstorm")
            headings = {QStringLiteral("问题"), QStringLiteral("想法"), QStringLiteral("资源"),
                        QStringLiteral("下一步")};
        if (id == "map-down" || id == "map-up")
            headings = {QStringLiteral("团队一"), QStringLiteral("团队二"),
                        QStringLiteral("团队三")};
        for (int i = 0; i < headings.size(); ++i)
        {
            TreeNode node;
            node.id = uuid();
            node.title = headings[i];
            node.parentId = tree.rootId;
            node.order = i;
            node.contentRevision = 1;
            node.style.fill = "#DDF2E7";
            tree.nodes.insert(node.id, node);
            for (int j = 0; j < 2; ++j)
            {
                TreeNode child;
                child.id = uuid();
                child.parentId = node.id;
                child.order = j;
                child.title = QStringLiteral("%1·要点%2").arg(headings[i]).arg(j + 1);
                child.contentRevision = 1;
                tree.nodes.insert(child.id, child);
            }
        }
        return p;
    }
    if (entry.kind == "fishbone")
    {
        auto p = ProjectFactory::createFishbone(entry.title);
        auto &tree = std::get<TreeSheet>(p.sheets.first());
        tree.name = entry.title;
        QStringList names;
        if (id == "fish-study")
            names = {"基础", "方法", "练习", "反馈", "时间", "环境"};
        else if (id == "fish-retro")
            names = {"目标", "流程", "协作", "资源", "风险", "反馈"};
        else if (id == "fish-project")
            names = {"需求", "计划", "技术", "人员", "依赖", "变更"};
        QVector<ID> ids;
        for (const auto &n : tree.nodes)
            if (n.parentId)
                ids.append(n.id);
        std::sort(ids.begin(), ids.end(), [&](const ID &a, const ID &b)
                  { return tree.nodes[a].order < tree.nodes[b].order; });
        for (int i = 0; i < ids.size(); ++i)
        {
            if (!names.isEmpty())
                tree.nodes[ids[i]].title = names[i];
            for (int j = 0; j < 2; ++j)
            {
                TreeNode cause;
                cause.id = uuid();
                cause.parentId = ids[i];
                cause.order = j;
                cause.title = QStringLiteral("待核实原因%1").arg(j + 1);
                cause.contentRevision = 1;
                tree.nodes.insert(cause.id, cause);
            }
        }
        return p;
    }
    if (entry.kind == "flowchart")
    {
        auto p = ProjectFactory::createFlowchart(entry.title);
        auto &flow = std::get<FlowSheet>(p.sheets.first());
        flow.name = entry.title;
        flow.nodes.clear();
        const auto node = [&](const QString &title, const QString &role, QPointF point)
        {
            FlowNode n;
            n.id = uuid();
            n.title = title;
            n.role = role;
            n.position = point;
            n.contentRevision = 1;
            flow.nodes.insert(n.id, n);
            return n.id;
        };
        const auto edge = [&](const ID &a, const ID &b, const QString &label = QString(),
                              const QString &from = QString(), const QString &to = QString())
        {
            FlowEdge e;
            e.id = uuid();
            e.sourceId = a;
            e.targetId = b;
            e.label = label;
            e.sourcePort = from;
            e.targetPort = to;
            e.style = flow.defaultEdgeStyle;
            flow.edges.append(e);
        };
        if (id == "flow-timeline" || id == "flow-timeline-v")
        {
            const bool vertical = id.endsWith("-v");
            ID previous;
            for (int i = 0; i < 5; ++i)
            {
                const ID current = node(QStringLiteral("阶段%1\n里程碑").arg(i + 1), "process",
                                        vertical ? QPointF(0, i * 160) : QPointF(i * 260, 0));
                if (!previous.isEmpty())
                    edge(previous, current, {}, vertical ? "bottom" : "right",
                         vertical ? "top" : "left");
                previous = current;
            }
        }
        else if (id == "flow-org")
        {
            const ID root = node("负责人", "start", {0, 0});
            for (int i = 0; i < 3; ++i)
            {
                const ID group =
                    node(QStringLiteral("团队%1").arg(i + 1), "process", {(i - 1) * 270.0, 180});
                edge(root, group);
                const ID task =
                    node(QStringLiteral("工作项%1").arg(i + 1), "process", {(i - 1) * 270.0, 360});
                edge(group, task);
            }
        }
        else
        {
            const ID start = node("开始", "start", {0, 0}),
                     process =
                         node(id == "flow-approval" ? "提交申请" : "处理步骤", "process", {0, 150});
            edge(start, process);
            const ID decision =
                node(id == "flow-approval" ? "审核通过？"
                                           : (id == "flow-sequence" ? "检查结果" : "是否完成？"),
                     id == "flow-sequence" ? "process" : "decision", {0, 330});
            edge(process, decision);
            const ID end = node("结束", "end", {0, 550});
            edge(decision, end, id == "flow-sequence" ? QString() : QString("是"));
            if (id == "flow-cycle" || id == "flow-approval")
                edge(decision, process, "否，修改后重试", "right", "right");
            else if (id == "flow-decision")
            {
                const ID alternate = node("替代处理", "process", {320, 330});
                edge(decision, alternate, "否", "right", "left");
                edge(alternate, end);
            }
            if (id == "flow-swimlane")
            {
                flow.nodes[start].style.fill = "#DDF2E7";
                flow.nodes[process].style.fill = "#FFF0D0";
                flow.nodes[decision].style.fill = "#EEE8FA";
                flow.nodes[process].title = "处理角色·执行";
                flow.nodes[decision].title = "复核角色·确认";
            }
        }
        return p;
    }
    auto p = ProjectFactory::createGantt(entry.title);
    auto &sheet = std::get<GanttSheet>(p.sheets.first());
    sheet.name = entry.title;
    QStringList tasks{"启动规划", "功能开发", "回归检查", "交付"};
    if (id == "gantt-thesis")
        tasks = {"文献调研", "研究方案", "实验验证", "论文写作", "修改定稿"};
    else if (id == "gantt-study")
        tasks = {"概念梳理", "例题练习", "专题训练", "错题整理", "阶段总结"};
    else if (id == "gantt-release")
        tasks = {"功能冻结", "全量回归", "安装打包", "交付确认"};
    const auto day = QDate::currentDate();
    ID previous;
    for (int i = 0; i < tasks.size(); ++i)
    {
        Task task;
        task.id = uuid();
        task.title = tasks[i];
        task.startDate = day.addDays(i * 4);
        task.endDate = day.addDays(i * 4 + 2);
        if (!previous.isEmpty())
            task.predecessorIds.append(previous);
        if (i == tasks.size() - 1)
        {
            task.milestone = true;
            task.endDate = task.startDate;
        }
        p.tasks.append(task);
        previous = task.id;
    }
    return p;
}
} // namespace lingya::TemplateLibrary
