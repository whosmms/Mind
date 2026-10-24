#include "domain/ProjectFactory.h"
#include <QUuid>

namespace lingya {


namespace ProjectFactory {

namespace {
// ID统一使用QUuid去花括号形式（规范§4.1）
QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}  // namespace

Project createMindmap(const QString& title) {
    const QDateTime now = QDateTime::currentDateTimeUtc();

    TreeSheet sheet;
    sheet.id = newId();
    sheet.name = QStringLiteral("工作表 1");
    sheet.themeId = "mint";
    sheet.layout = "bilateral";
    // 默认样式采用规范§9设计变量：卡片白、主文字、边框灰绿、主色连线
    sheet.defaultNodeStyle.fill = "#FFFFFF";
    sheet.defaultNodeStyle.color = "#243A34";
    sheet.defaultNodeStyle.borderColor = "#DDE6DF";
    sheet.defaultEdgeStyle.color = "#247A62";

    TreeNode root;
    root.id = newId();
    root.title = QStringLiteral("中心主题");
    root.contentRevision = 1;
    sheet.rootId = root.id;
    sheet.nodes.insert(root.id, root);

    Project project;
    project.schemaVersion = 1;
    project.id = newId();
    project.title = title;
    project.activeSheetId = sheet.id;
    project.revision = 1;
    project.createdAt = now;
    project.updatedAt = now;
    project.sheets.push_back(sheet);
    return project;
}

}  // namespace ProjectFactory

}  // namespace lingya
