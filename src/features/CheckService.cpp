#include "features/CheckService.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>

namespace lingya {

namespace {

constexpr int kTitleSoftLimit = 80;    // C02：标题超过 80 字符
constexpr int kDepthSoftLimit = 8;     // C02：层级超过 8 层
constexpr double kMinContrast = 4.5;   // C09：WCAG AA 最低对比度

void addIssue(QVector<Issue>& issues, const char* ruleId, IssueSeverity severity,
              const ID& sheetId, const ID& targetId, const QString& message,
              const QString& suggestion, const char* actionId = nullptr) {
    Issue issue;
    issue.ruleId = QString::fromLatin1(ruleId);
    issue.severity = severity;
    issue.sheetId = sheetId;
    issue.targetId = targetId;
    issue.message = message;
    issue.suggestion = suggestion;
    if (actionId != nullptr) {
        issue.actionId = QString::fromLatin1(actionId);
    }
    issues.push_back(issue);
}

int codePointCount(const QString& text) {
    return static_cast<int>(text.toUcs4().size());
}

// 节点在树中的深度（根为 1）；遇到环或断链即停止，最多上溯 1000 层。
int nodeDepth(const TreeSheet& sheet, const ID& nodeId) {
    int depth = 1;
    QSet<ID> seen;
    ID current = nodeId;
    while (depth < 1000) {
        if (seen.contains(current)) {
            break;
        }
        seen.insert(current);
        const auto it = sheet.nodes.constFind(current);
        if (it == sheet.nodes.constEnd() || !it->parentId.has_value()) {
            break;
        }
        current = *it->parentId;
        ++depth;
    }
    return depth;
}

// ---- C09 对比度：手动解析 #RGB / #RRGGBB，避免让核心库依赖 QtGui ----

bool parseHexColor(const QString& text, int& r, int& g, int& b) {
    QString s = text.trimmed();
    if (!s.startsWith(QLatin1Char('#'))) {
        return false;
    }
    s = s.mid(1);
    if (s.size() == 3) {
        s = QStringLiteral("%1%1%2%2%3%3").arg(s[0]).arg(s[1]).arg(s[2]);
    }
    if (s.size() != 6) {
        return false;
    }
    bool ok = false;
    const uint value = s.toUInt(&ok, 16);
    if (!ok || value > 0xFFFFFF) {
        return false;
    }
    r = static_cast<int>((value >> 16) & 0xFF);
    g = static_cast<int>((value >> 8) & 0xFF);
    b = static_cast<int>(value & 0xFF);
    return true;
}

double relativeLuminance(int r, int g, int b) {
    auto channel = [](int value) {
        const double x = value / 255.0;
        return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

double contrastRatio(int r1, int g1, int b1, int r2, int g2, int b2) {
    const double l1 = relativeLuminance(r1, g1, b1);
    const double l2 = relativeLuminance(r2, g2, b2);
    return (std::max(l1, l2) + 0.05) / (std::min(l1, l2) + 0.05);
}

void checkContrast(const SheetBase& sheet, QVector<Issue>& issues) {
    int fr = 0;
    int fg = 0;
    int fb = 0;
    int br = 0;
    int bgc = 0;
    int bb = 0;
    if (!parseHexColor(sheet.defaultNodeStyle.color, fr, fg, fb) ||
        !parseHexColor(sheet.defaultNodeStyle.fill, br, bgc, bb)) {
        return;   // 颜色非法时交给后续校验，检查助手不误报
    }
    if (contrastRatio(fr, fg, fb, br, bgc, bb) < kMinContrast) {
        addIssue(issues, "C09", IssueSeverity::Suggestion, sheet.id, ID(),
                 QStringLiteral("节点文字与背景对比不足"),
                 QStringLiteral("建议加深文字色或减淡背景色"));
    }
}

void checkTree(const TreeSheet& sheet, QVector<Issue>& issues) {
    // C01 未命名主题
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        if (it.value().title.trimmed().isEmpty()) {
            addIssue(issues, "C01", IssueSeverity::Warning, sheet.id, it.key(),
                     QStringLiteral("节点未命名"), QStringLiteral("请输入主题文字"),
                     "edit-node");
        }
    }

    // C02 标题过长 / 层级过深
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        const TreeNode& node = it.value();
        if (codePointCount(node.title) > kTitleSoftLimit) {
            addIssue(issues, "C02", IssueSeverity::Suggestion, sheet.id, node.id,
                     QStringLiteral("标题超过%1字符").arg(kTitleSoftLimit),
                     QStringLiteral("建议精简标题"));
        }
        if (nodeDepth(sheet, node.id) > kDepthSoftLimit) {
            addIssue(issues, "C02", IssueSeverity::Suggestion, sheet.id, node.id,
                     QStringLiteral("层级超过%1层").arg(kDepthSoftLimit),
                     QStringLiteral("建议折叠或拆分"));
        }
    }

    // C03 多根 / 父级缺失
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        const TreeNode& node = it.value();
        if (node.id != sheet.rootId && !node.parentId.has_value()) {
            addIssue(issues, "C03", IssueSeverity::Error, sheet.id, node.id,
                     QStringLiteral("存在多个根节点"), QStringLiteral("每张树图只能有一个根"));
        } else if (node.parentId.has_value() && !sheet.nodes.contains(*node.parentId)) {
            addIssue(issues, "C03", IssueSeverity::Error, sheet.id, node.id,
                     QStringLiteral("父节点不存在"), QStringLiteral("请修复父级引用"));
        }
    }

    // C03 环检测（沿父链上溯）
    QSet<ID> safe;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        if (safe.contains(it.key())) {
            continue;
        }
        QList<ID> path;
        QSet<ID> pathSet;
        ID current = it.key();
        bool cycle = false;
        while (true) {
            if (safe.contains(current)) {
                break;
            }
            if (pathSet.contains(current)) {
                cycle = true;
                break;
            }
            path.append(current);
            pathSet.insert(current);
            const auto nit = sheet.nodes.constFind(current);
            if (nit == sheet.nodes.constEnd() || !nit->parentId.has_value()) {
                break;
            }
            current = *nit->parentId;
        }
        for (const ID& id : path) {
            safe.insert(id);
        }
        if (cycle) {
            addIssue(issues, "C03", IssueSeverity::Error, sheet.id, it.key(),
                     QStringLiteral("父级引用存在循环"), QStringLiteral("请修复树结构"));
        }
    }

    // C03 关系端点缺失
    for (const Relation& rel : sheet.relations) {
        if (!sheet.nodes.contains(rel.sourceId) || !sheet.nodes.contains(rel.targetId)) {
            addIssue(issues, "C03", IssueSeverity::Error, sheet.id, rel.id,
                     QStringLiteral("关系线端点缺失"), QStringLiteral("请清理或重新连接"));
        }
    }

    checkContrast(sheet, issues);
}

void checkFlow(const FlowSheet& sheet, QVector<Issue>& issues) {
    QHash<ID, int> outCount;
    QHash<ID, int> inCount;
    for (const FlowEdge& edge : sheet.edges) {
        ++outCount[edge.sourceId];
        ++inCount[edge.targetId];
    }

    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        const FlowNode& node = it.value();
        const int out = outCount.value(node.id, 0);
        const int in = inCount.value(node.id, 0);

        // C04 孤立节点 / 处理节点无出口
        if (out == 0 && in == 0) {
            addIssue(issues, "C04", IssueSeverity::Warning, sheet.id, node.id,
                     QStringLiteral("流程节点未连接"), QStringLiteral("请连接到流程或删除"));
        } else if (node.role == QStringLiteral("process") && out == 0) {
            addIssue(issues, "C04", IssueSeverity::Warning, sheet.id, node.id,
                     QStringLiteral("处理节点没有出口"), QStringLiteral("请补充后续连线"));
        }

        // C05 判断节点少于两条出边
        if (node.role == QStringLiteral("decision") && out < 2) {
            addIssue(issues, "C05", IssueSeverity::Warning, sheet.id, node.id,
                     QStringLiteral("判断节点少于两条出边"), QStringLiteral("请补充是/否分支"));
        }
    }

    // C05 分支标签缺失
    for (const FlowEdge& edge : sheet.edges) {
        if (edge.label.trimmed().isEmpty()) {
            addIssue(issues, "C05", IssueSeverity::Warning, sheet.id, edge.id,
                     QStringLiteral("流程连线缺少分支标签"),
                     QStringLiteral("请填写是/否等标签"));
        }
    }

    checkContrast(sheet, issues);
}

void checkTasks(const Project& project, QVector<Issue>& issues) {
    QHash<ID, const Task*> byId;
    for (const Task& task : project.tasks) {
        byId.insert(task.id, &task);
    }

    for (const Task& task : project.tasks) {
        // C06 日期
        if (task.startDate.has_value() != task.endDate.has_value()) {
            addIssue(issues, "C06", IssueSeverity::Error, ID(), task.id,
                     QStringLiteral("任务“%1”日期缺一项").arg(task.title),
                     QStringLiteral("请同时填写开始与结束日期"));
        } else if (task.startDate.has_value() && *task.startDate > *task.endDate) {
            addIssue(issues, "C06", IssueSeverity::Error, ID(), task.id,
                     QStringLiteral("任务“%1”结束早于开始").arg(task.title),
                     QStringLiteral("请调整日期"));
        }
        if (task.milestone && task.startDate.has_value() && *task.startDate != *task.endDate) {
            addIssue(issues, "C06", IssueSeverity::Error, ID(), task.id,
                     QStringLiteral("里程碑“%1”跨日").arg(task.title),
                     QStringLiteral("里程碑应同日起止"));
        }

        // C07 前驱存在 / FS 日期冲突
        for (const ID& predId : task.predecessorIds) {
            const auto it = byId.constFind(predId);
            if (it == byId.constEnd()) {
                addIssue(issues, "C07", IssueSeverity::Error, ID(), task.id,
                         QStringLiteral("任务“%1”的前驱不存在").arg(task.title),
                         QStringLiteral("请移除或修正依赖"));
                continue;
            }
            const Task& pred = *it.value();
            if (task.startDate.has_value() && pred.endDate.has_value() &&
                task.startDate.value() <= pred.endDate.value()) {
                addIssue(issues, "C07", IssueSeverity::Error, ID(), task.id,
                         QStringLiteral("任务“%1”存在FS日期冲突").arg(task.title),
                         QStringLiteral("后继开始日应晚于前驱结束日"));
            }
        }
    }

    // C07 依赖环（三色标记）
    enum class Mark { None, Active, Done };
    QHash<ID, int> mark;
    std::function<bool(const ID&)> visit = [&](const ID& id) -> bool {
        const int m = mark.value(id, static_cast<int>(Mark::None));
        if (m == static_cast<int>(Mark::Done)) {
            return false;
        }
        if (m == static_cast<int>(Mark::Active)) {
            return true;
        }
        mark[id] = static_cast<int>(Mark::Active);
        const auto it = byId.constFind(id);
        if (it != byId.constEnd()) {
            for (const ID& pred : it.value()->predecessorIds) {
                if (byId.contains(pred) && visit(pred)) {
                    return true;
                }
            }
        }
        mark[id] = static_cast<int>(Mark::Done);
        return false;
    };
    for (const Task& task : project.tasks) {
        if (visit(task.id)) {
            addIssue(issues, "C07", IssueSeverity::Error, ID(), task.id,
                     QStringLiteral("任务依赖存在循环"), QStringLiteral("请拆开循环依赖"));
            break;
        }
    }
}

void checkCardsAndRoutes(const Project& project, QVector<Issue>& issues) {
    // C01 空白答案卡片 + C08 源变化/删除
    for (const ReviewCard& card : project.cards) {
        if (card.back.trimmed().isEmpty()) {
            addIssue(issues, "C01", IssueSeverity::Warning, card.sheetId, card.id,
                     QStringLiteral("复习卡片答案为空"), QStringLiteral("请补充答案"));
        }
        if (card.sourceState == QStringLiteral("changed") ||
            card.sourceState == QStringLiteral("missing")) {
            const QString state = card.sourceState == QStringLiteral("changed")
                                      ? QStringLiteral("已变化")
                                      : QStringLiteral("已删除");
            addIssue(issues, "C08", IssueSeverity::Warning, card.sheetId, card.id,
                     QStringLiteral("复习卡片来源%1").arg(state),
                     QStringLiteral("请更新卡片或转为独立卡片"));
        }
    }

    // C08 路线节点已删除
    QSet<ID> allNodes;
    for (const Sheet& sheet : project.sheets) {
        if (const auto* tree = std::get_if<TreeSheet>(&sheet)) {
            for (auto it = tree->nodes.constBegin(); it != tree->nodes.constEnd(); ++it) {
                allNodes.insert(it.key());
            }
        }
    }
    for (const PresentationRoute& route : project.routes) {
        for (const PresentationStep& step : route.steps) {
            if (!allNodes.contains(step.nodeId)) {
                addIssue(issues, "C08", IssueSeverity::Warning, route.sheetId, step.id,
                         QStringLiteral("讲解路线的节点已删除"), QStringLiteral("请移除该步骤"));
            }
        }
    }
}

}  // namespace

QVector<Issue> CheckService::checkProject(const Project& project) {
    QVector<Issue> issues;

    for (const Sheet& sheet : project.sheets) {
        if (const auto* tree = std::get_if<TreeSheet>(&sheet)) {
            checkTree(*tree, issues);
        } else if (const auto* flow = std::get_if<FlowSheet>(&sheet)) {
            checkFlow(*flow, issues);
        } else if (const auto* gantt = std::get_if<GanttSheet>(&sheet)) {
            checkContrast(*gantt, issues);
        }
    }

    checkTasks(project, issues);
    checkCardsAndRoutes(project, issues);

    // 侧栏按严重度排序：错误 → 提醒 → 建议（同级保持发现顺序）
    std::stable_sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b) {
        return static_cast<int>(a.severity) < static_cast<int>(b.severity);
    });
    return issues;
}

}  // namespace lingya
