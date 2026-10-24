#include <QtTest>

#include "commands/CommandService.h"
#include "domain/Domain.h"
#include "domain/ProjectFactory.h"
#include "document/ProjectDocument.h"

using namespace lingya;

// 命令与历史测试（规范§5.1）：快照事务、撤销重做、失败不入栈、历史上限。
class TstCommands : public QObject {
    Q_OBJECT

private slots:
    void renameThroughService();
    void undoRestoresBefore();
    void redoReapplies();
    void invalidEditLeavesProjectUnchanged();
    void historyLimitedTo50();
    void replaceProjectClearsHistory();

private:
    static QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
    static std::variant<bool, AppError> renameRoot(CommandService& service,
                                                   const ProjectDocument& doc,
                                                   const QString& title) {
        const TreeSheet& sheet = std::get<TreeSheet>(doc.project().sheets.first());
        const ID sheetId = sheet.id;
        const ID nodeId = sheet.rootId;
        return service.applyEdit(QStringLiteral("重命名根节点"),
                                 [sheetId, nodeId, title](Project& project) {
                                     for (Sheet& s : project.sheets) {
                                         if (auto* tree = std::get_if<TreeSheet>(&s);
                                             tree != nullptr && tree->id == sheetId) {
                                             TreeNode& node = tree->nodes[nodeId];
                                             node.title = title;
                                             ++node.contentRevision;
                                         }
                                     }
                                 });
    }
    static QString rootTitle(const ProjectDocument& doc) {
        const TreeSheet& sheet = std::get<TreeSheet>(doc.project().sheets.first());
        return sheet.nodes.value(sheet.rootId).title;
    }
};

void TstCommands::renameThroughService() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("命令测试")));
    CommandService service(&doc);
    const qint64 before = doc.sessionRevision();

    QVERIFY(std::holds_alternative<bool>(renameRoot(service, doc, QStringLiteral("新标题"))));
    QCOMPARE(rootTitle(doc), QStringLiteral("新标题"));
    QVERIFY(doc.isModified());
    QVERIFY(doc.sessionRevision() > before);   // 编辑递增会话修订（规范§5.1）
    QVERIFY(service.canUndo());
}

void TstCommands::undoRestoresBefore() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("撤销测试")));
    CommandService service(&doc);
    QVERIFY(std::holds_alternative<bool>(renameRoot(service, doc, QStringLiteral("改后"))));
    service.undo();
    QCOMPARE(rootTitle(doc), QStringLiteral("中心主题"));   // undo恢复before内容（规范§5.1）
}

void TstCommands::redoReapplies() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("重做测试")));
    CommandService service(&doc);
    QVERIFY(std::holds_alternative<bool>(renameRoot(service, doc, QStringLiteral("改后"))));
    service.undo();
    QVERIFY(service.canRedo());
    service.redo();
    QCOMPARE(rootTitle(doc), QStringLiteral("改后"));
}

void TstCommands::invalidEditLeavesProjectUnchanged() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("失败测试")));
    CommandService service(&doc);
    const qint64 before = doc.sessionRevision();

    // 空标题违反规则8：校验失败不压入命令、不改变原数据（规范§5.1）
    const auto result = renameRoot(service, doc, QString());
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(rootTitle(doc), QStringLiteral("中心主题"));
    QCOMPARE(doc.sessionRevision(), before);
    QVERIFY(!service.canUndo());
}

void TstCommands::historyLimitedTo50() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("历史上限")));
    CommandService service(&doc);
    for (int i = 0; i < 60; ++i)
        QVERIFY(std::holds_alternative<bool>(
            renameRoot(service, doc, QStringLiteral("标题%1").arg(i))));

    int undone = 0;
    while (service.canUndo()) {
        service.undo();
        ++undone;
    }
    QCOMPARE(undone, 50);   // 历史最多50步（规范§5.1）
}

void TstCommands::replaceProjectClearsHistory() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("会话测试")));
    CommandService service(&doc);
    QVERIFY(std::holds_alternative<bool>(renameRoot(service, doc, QStringLiteral("改后"))));
    QVERIFY(service.canUndo());

    // 打开新工程清空撤销历史（规范§5.1）
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("新工程")));
    service.clearHistory();
    QVERIFY(!service.canUndo());
    QVERIFY(!service.canRedo());
}

QTEST_MAIN(TstCommands)
#include "tst_commands.moc"
