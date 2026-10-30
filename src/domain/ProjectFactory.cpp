#include "domain/ProjectFactory.h"
#include <QUuid>

namespace lingya {


namespace ProjectFactory {

namespace {
// ID统一使用QUuid去花括号形式（规范§4.1）
QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// 默认样式采用规范§9设计变量：卡片白、主文字、边框灰绿、主色连线
void applyDefaultStyles(SheetBase& sheet) {
    sheet.themeId = "mint";
    sheet.defaultNodeStyle.fill = "#FFFFFF";
    sheet.defaultNodeStyle.color = "#243A34";
    sheet.defaultNodeStyle.borderColor = "#DDE6DF";
    sheet.defaultEdgeStyle.color = "#247A62";
}

TreeSheet makeTreeSheet(const QString& name) {
    TreeSheet sheet;
    sheet.id = newId();
    sheet.name = name;
    applyDefaultStyles(sheet);
    sheet.layout = "bilateral";
    TreeNode root;
    root.id = newId();
    root.title = QStringLiteral("中心主题");
    root.contentRevision = 1;
    sheet.rootId = root.id;
    sheet.nodes.insert(root.id, root);
    return sheet;
}

Project finishProject(const QString& title, const Sheet& sheet) {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    Project project;
    project.schemaVersion = 1;
    project.id = newId();
    project.title = title;
    project.activeSheetId = std::visit([](const auto& s) { return s.id; }, sheet);
    project.revision = 1;
    project.createdAt = now;
    project.updatedAt = now;
    project.sheets.push_back(sheet);
    return project;
}
}  // namespace

Project createMindmap(const QString& title) {
    return finishProject(title, makeTreeSheet(QStringLiteral("工作表 1")));
}

std::variant<Sheet, AppError> createSheet(const QString& kind, const QString& name) {
    if (kind == QStringLiteral("mindmap")) {
        return Sheet{makeTreeSheet(name)};
    }
    if (kind == QStringLiteral("fishbone")) {
        TreeSheet sheet = makeTreeSheet(name);
        sheet.kind = QStringLiteral("fishbone");
        sheet.layout = QStringLiteral("fishbone");
        sheet.nodes[sheet.rootId].title = QStringLiteral("问题");
        // R11：一级节点是原因类别，默认给出6类（人员/机器/材料/方法/测量/环境）
        const QStringList categories{QStringLiteral("人员"), QStringLiteral("机器"),
                                     QStringLiteral("材料"), QStringLiteral("方法"),
                                     QStringLiteral("测量"), QStringLiteral("环境")};
        for (int i = 0; i < categories.size(); ++i) {
            TreeNode category;
            category.id = newId();
            category.parentId = sheet.rootId;
            category.order = i;
            category.title = categories.at(i);
            category.contentRevision = 1;
            sheet.nodes.insert(category.id, category);
        }
        return Sheet{sheet};
    }
    if (kind == QStringLiteral("flowchart")) {
        FlowSheet sheet;
        sheet.id = newId();
        sheet.name = name;
        applyDefaultStyles(sheet);
        FlowNode start;
        start.id = newId();
        start.title = QStringLiteral("开始");
        start.role = QStringLiteral("start");
        start.position = QPointF(0.0, 0.0);
        start.contentRevision = 1;
        sheet.nodes.insert(start.id, start);
        return Sheet{sheet};
    }
    if (kind == QStringLiteral("gantt")) {
        GanttSheet sheet;
        sheet.id = newId();
        sheet.name = name;
        applyDefaultStyles(sheet);
        return Sheet{sheet};
    }
    return AppError{"INVALID_ARGUMENT",
                    QStringLiteral("图表类型只允许mindmap/fishbone/flowchart/gantt")};
}

Project createFishbone(const QString& title) {
    const auto sheet = createSheet(QStringLiteral("fishbone"), QStringLiteral("工作表 1"));
    return finishProject(title, std::get<Sheet>(sheet));
}

Project createFlowchart(const QString& title) {
    const auto sheet = createSheet(QStringLiteral("flowchart"), QStringLiteral("工作表 1"));
    return finishProject(title, std::get<Sheet>(sheet));
}

Project createGantt(const QString& title) {
    const auto sheet = createSheet(QStringLiteral("gantt"), QStringLiteral("工作表 1"));
    return finishProject(title, std::get<Sheet>(sheet));
}

}  // namespace ProjectFactory

}  // namespace lingya
