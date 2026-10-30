#include <QtTest>

#include <QSet>
#include <QUuid>

#include "commands/AiCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/Domain.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "domain/ProjectValidator.h"

using namespace lingya;

namespace {

TreeSheet& treeOf(Project& project) {
    return std::get<TreeSheet>(project.sheets.first());
}

const TreeSheet& treeOf(const Project& project) {
    return std::get<TreeSheet>(project.sheets.first());
}

GeneratedNode gen(const QString& title, const QVector<GeneratedNode>& children = {},
                  const QString& note = QString(), const QString& origin = QStringLiteral("source"),
                  const QStringList& paragraphIds = QStringList()) {
    GeneratedNode node;
    node.title = title;
    node.children = children;
    node.note = note;
    node.origin = origin;
    node.sourceParagraphIds = paragraphIds;
    return node;
}

MapProposal proposalOf(const GeneratedNode& root, const QString& title = QString()) {
    MapProposal proposal;
    proposal.title = title;
    proposal.root = root;
    proposal.model = QStringLiteral("deepseek-flash");
    return proposal;
}

DraftSection sectionOf(const QString& heading, const QString& text,
                       const QStringList& sourceNodeIds = QStringList(),
                       const QString& origin = QStringLiteral("source")) {
    DraftSection section;
    section.heading = heading;
    section.text = text;
    section.sourceNodeIds = sourceNodeIds;
    section.origin = origin;
    return section;
}

DraftProposal draftOf(const QString& title, const QVector<DraftSection>& sections) {
    DraftProposal proposal;
    proposal.title = title;
    proposal.sections = sections;
    proposal.model = QStringLiteral("deepseek-flash");
    return proposal;
}

// 按父级取子节点order，验证order连续且从0开始（规范§4.3的排序契约）
QVector<int> childOrders(const TreeSheet& sheet, const ID& parentId) {
    QVector<int> orders;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        if (it.value().parentId.has_value() && *it.value().parentId == parentId)
            orders.append(it.value().order);
    }
    std::sort(orders.begin(), orders.end());
    return orders;
}

const TreeNode* childTitled(const TreeSheet& sheet, const ID& parentId, const QString& title) {
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        if (it.value().parentId.has_value() && *it.value().parentId == parentId &&
            it.value().title == title)
            return &it.value();
    }
    return nullptr;
}

void addPlainChild(Project& project, const ID& parentId, int order, const QString& title) {
    TreeNode node;
    node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node.parentId = parentId;
    node.order = order;
    node.title = title;
    node.contentRevision = 1;
    treeOf(project).nodes.insert(node.id, node);
}

}  // namespace

// AI采用命令（规范§5.1的applyAIMap/saveTextDraft、§10.5/§10.6）的行为级测试：
// 只验证“已验证提案→可编辑数据”的落库结果，不涉及网络、密钥与预览状态。
class TstAiCommands : public QObject {
    Q_OBJECT

private slots:
    // X05采用：新建工作表
    void applyMapAsSheetCreatesEditableTree();
    void applyMapAsSheetFallsBackToProposalTitle();
    void applyMapAsSheetRejectsAtSheetLimit();
    // X05采用：插入分支
    void applyMapAsBranchAppendsAfterExistingChildren();
    void applyMapAsBranchRejectsMissingParent();
    void applyMapAsBranchRejectsNonTreeSheet();
    // 过期与结构校验
    void applyMapRejectsStaleRevision();
    void applyMapRejectsInvalidProposal();
    void applyMapRejectsTooLargeProposal();
    void applyMapRejectsTooDeepProposal();
    // X06采用
    void saveDraftCreatesDraftWithSourceRevision();
    void saveDraftNormalizesPurposeAndTitle();
    void saveDraftRejectsUnknownSourceNode();
    void saveDraftRejectsNonTreeSheet();
    void saveDraftRejectsEmptySections();
};

void TstAiCommands::applyMapAsSheetCreatesEditableTree() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("AI采用")));
    CommandService service(&document);
    AiCommands ai(&service, &document);
    const ID baseSheetId = document.project().activeSheetId;

    const GeneratedNode root = gen(QStringLiteral("软件工程"),
                                   {gen(QStringLiteral("需求分析")), gen(QStringLiteral("设计")),
                                    gen(QStringLiteral("测试"))},
                                   QString(), QStringLiteral("source"), {QStringLiteral("p1")});
    const MapProposal proposal = proposalOf(root, QStringLiteral("软件工程流程"));

    AiApplyOptions options;
    options.requestId = QStringLiteral("req-1");
    const auto applied = ai.applyMapAsSheet(proposal, options);
    QVERIFY(std::holds_alternative<ID>(applied));
    const ID sheetId = std::get<ID>(applied);

    QCOMPARE(document.project().sheets.size(), 2);
    QCOMPARE(document.project().activeSheetId, sheetId);
    const TreeSheet& sheet = std::get<TreeSheet>(document.project().sheets.last());
    QCOMPARE(sheet.id, sheetId);
    QCOMPARE(sheet.name, QStringLiteral("软件工程流程"));
    QCOMPARE(sheet.kind, QStringLiteral("mindmap"));
    QCOMPARE(sheet.layout, QStringLiteral("bilateral"));
    QCOMPARE(sheet.nodes.size(), 4);

    // 程序生成根节点：无父级、order为0、标题来自提案
    const TreeNode& rootNode = sheet.nodes.value(sheet.rootId);
    QCOMPARE(rootNode.title, QStringLiteral("软件工程"));
    QVERIFY(!rootNode.parentId.has_value());
    QCOMPARE(rootNode.order, 0);

    // 来源标记写入provenance，手工节点不带该字段
    QVERIFY(rootNode.provenance.has_value());
    QCOMPARE(rootNode.provenance->kind, QStringLiteral("ai"));
    QCOMPARE(rootNode.provenance->requestId, QStringLiteral("req-1"));
    QCOMPARE(rootNode.provenance->model, QStringLiteral("deepseek-flash"));
    QCOMPARE(rootNode.provenance->origin, QStringLiteral("source"));
    QCOMPARE(rootNode.provenance->sourceParagraphIds, QStringList({QStringLiteral("p1")}));

    // 三个孩子按提案顺序连续编号，父级指向根
    QCOMPARE(childOrders(sheet, sheet.rootId), QVector<int>({0, 1, 2}));
    const TreeNode* design = childTitled(sheet, sheet.rootId, QStringLiteral("设计"));
    QVERIFY(design != nullptr);
    QCOMPARE(design->order, 1);
    QVERIFY(design->provenance.has_value());

    // 节点ID全部重新生成且互不相同
    QSet<ID> ids;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        QVERIFY(!it.key().isEmpty());
        QVERIFY(!ids.contains(it.key()));
        ids.insert(it.key());
    }
    QVERIFY(ProjectValidator::validate(document.project()).isEmpty());

    service.undo();
    QCOMPARE(document.project().sheets.size(), 1);
    QCOMPARE(document.project().activeSheetId, baseSheetId);

    service.redo();
    QCOMPARE(document.project().sheets.size(), 2);
    QCOMPARE(document.project().activeSheetId, sheetId);
}

void TstAiCommands::applyMapAsSheetFallsBackToProposalTitle() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("回退")));
    CommandService service(&document);
    AiCommands ai(&service, &document);

    // 提案标题为空时取根节点标题
    const auto applied = ai.applyMapAsSheet(
        proposalOf(gen(QStringLiteral("课程复习")), QString()), {});
    QVERIFY(std::holds_alternative<ID>(applied));
    QCOMPARE(std::get<TreeSheet>(document.project().sheets.last()).name,
             QStringLiteral("课程复习"));

    // 显式指定的工作表名优先于提案标题
    AiApplyOptions named;
    named.sheetName = QStringLiteral("自定名称");
    const auto second = ai.applyMapAsSheet(
        proposalOf(gen(QStringLiteral("另一棵树")), QStringLiteral("提案标题")), named);
    QVERIFY(std::holds_alternative<ID>(second));
    QCOMPARE(std::get<TreeSheet>(document.project().sheets.last()).name,
             QStringLiteral("自定名称"));
}

void TstAiCommands::applyMapAsSheetRejectsAtSheetLimit() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("满表")));
    // 直接构造到上限（命令层限制与界面无关，测试只验证边界）
    Project full = document.project();
    while (full.sheets.size() < Limits::kMaxSheets) {
        const auto built = ProjectFactory::createSheet(QStringLiteral("mindmap"), QStringLiteral("表"));
        full.sheets.push_back(std::get<Sheet>(built));
    }
    document.replaceProject(full);
    CommandService service(&document);
    AiCommands ai(&service, &document);

    const int before = document.project().sheets.size();
    const auto applied = ai.applyMapAsSheet(proposalOf(gen(QStringLiteral("根"))), {});
    QVERIFY(std::holds_alternative<AppError>(applied));
    QCOMPARE(std::get<AppError>(applied).code, QStringLiteral("LIMIT_EXCEEDED"));
    QCOMPARE(document.project().sheets.size(), before);
}

void TstAiCommands::applyMapAsBranchAppendsAfterExistingChildren() {
    ProjectDocument document;
    Project project = ProjectFactory::createMindmap(QStringLiteral("插入分支"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    addPlainChild(project, rootId, 0, QStringLiteral("已有一"));
    addPlainChild(project, rootId, 1, QStringLiteral("已有二"));
    document.replaceProject(project);

    CommandService service(&document);
    AiCommands ai(&service, &document);

    const GeneratedNode branch = gen(QStringLiteral("新增分支"),
                                     {gen(QStringLiteral("子项A")), gen(QStringLiteral("子项B"))});
    const auto applied = ai.applyMapAsBranch(proposalOf(branch), sheetId, rootId, {});
    QVERIFY(std::holds_alternative<ID>(applied));
    const ID insertedRootId = std::get<ID>(applied);

    const TreeSheet& sheet = treeOf(document.project());
    QCOMPARE(sheet.nodes.size(), 6);   // 根 + 2已有 + 提案3节点

    // 提案根挂在目标节点下，排到现有子节点之后（“目标下一个新子节点”）
    const TreeNode& inserted = sheet.nodes.value(insertedRootId);
    QCOMPARE(inserted.title, QStringLiteral("新增分支"));
    QVERIFY(inserted.parentId.has_value());
    QCOMPARE(*inserted.parentId, rootId);
    QCOMPARE(inserted.order, 2);
    QCOMPARE(childOrders(sheet, rootId), QVector<int>({0, 1, 2}));

    // 提案内部层级完整保留
    QCOMPARE(childOrders(sheet, insertedRootId), QVector<int>({0, 1}));
    QVERIFY(childTitled(sheet, insertedRootId, QStringLiteral("子项A")) != nullptr);
    QVERIFY(childTitled(sheet, insertedRootId, QStringLiteral("子项B")) != nullptr);
    QVERIFY(inserted.provenance.has_value());
    QVERIFY(ProjectValidator::validate(document.project()).isEmpty());

    service.undo();
    QCOMPARE(treeOf(document.project()).nodes.size(), 3);
    QVERIFY(!treeOf(document.project()).nodes.contains(insertedRootId));
}

void TstAiCommands::applyMapAsBranchRejectsMissingParent() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("父级缺失")));
    CommandService service(&document);
    AiCommands ai(&service, &document);
    const ID sheetId = document.project().activeSheetId;

    const auto applied = ai.applyMapAsBranch(proposalOf(gen(QStringLiteral("分支"))), sheetId,
                                             QStringLiteral("not-a-node"), {});
    QVERIFY(std::holds_alternative<AppError>(applied));
    QCOMPARE(std::get<AppError>(applied).code, QStringLiteral("STALE_SOURCE"));
    QCOMPARE(treeOf(document.project()).nodes.size(), 1);
}

void TstAiCommands::applyMapAsBranchRejectsNonTreeSheet() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createFlowchart(QStringLiteral("流程图")));
    CommandService service(&document);
    AiCommands ai(&service, &document);
    const ID flowSheetId = document.project().activeSheetId;

    const auto applied = ai.applyMapAsBranch(proposalOf(gen(QStringLiteral("分支"))), flowSheetId,
                                             QStringLiteral("whatever"), {});
    QVERIFY(std::holds_alternative<AppError>(applied));
    QCOMPARE(std::get<AppError>(applied).code, QStringLiteral("INVALID_ARGUMENT"));
}

void TstAiCommands::applyMapRejectsStaleRevision() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("过期")));
    CommandService service(&document);
    AiCommands ai(&service, &document);
    const qint64 generatedAt = document.sessionRevision();

    // 生成之后发生一次编辑：会话修订前进，旧结果不得直接采用
    QVERIFY(std::holds_alternative<bool>(
        service.applyEdit(QStringLiteral("空编辑"), [](Project&) {})));
    QVERIFY(document.sessionRevision() != generatedAt);

    AiApplyOptions stale;
    stale.expectedRevision = generatedAt;
    const auto rejected = ai.applyMapAsSheet(proposalOf(gen(QStringLiteral("根"))), stale);
    QVERIFY(std::holds_alternative<AppError>(rejected));
    QCOMPARE(std::get<AppError>(rejected).code, QStringLiteral("STALE_SOURCE"));
    QCOMPARE(document.project().sheets.size(), 1);

    // 用户重新确认后（不传expectedRevision）可以继续采用
    const auto accepted = ai.applyMapAsSheet(proposalOf(gen(QStringLiteral("根"))), {});
    QVERIFY(std::holds_alternative<ID>(accepted));
    QCOMPARE(document.project().sheets.size(), 2);
}

void TstAiCommands::applyMapRejectsInvalidProposal() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("非法提案")));
    CommandService service(&document);
    AiCommands ai(&service, &document);

    // 空标题（规范§10.6“禁止空标题”）
    const auto emptyTitle = ai.applyMapAsSheet(proposalOf(gen(QStringLiteral("   "))), {});
    QVERIFY(std::holds_alternative<AppError>(emptyTitle));
    QCOMPARE(std::get<AppError>(emptyTitle).code, QStringLiteral("SCHEMA_INVALID"));

    // 非法origin
    const auto badOrigin = ai.applyMapAsSheet(
        proposalOf(gen(QStringLiteral("根"), {gen(QStringLiteral("子"), {}, QString(),
                                              QStringLiteral("model"))})),
        {});
    QVERIFY(std::holds_alternative<AppError>(badOrigin));
    QCOMPARE(std::get<AppError>(badOrigin).code, QStringLiteral("SCHEMA_INVALID"));

    // 标题超过80码点
    const auto longTitle = ai.applyMapAsSheet(proposalOf(gen(QString(81, QChar(0x957F)))), {});
    QVERIFY(std::holds_alternative<AppError>(longTitle));
    QCOMPARE(std::get<AppError>(longTitle).code, QStringLiteral("SCHEMA_INVALID"));

    // 备注超过500码点
    const auto longNote = ai.applyMapAsSheet(
        proposalOf(gen(QStringLiteral("根"), {}, QString(501, QChar(0x6CE8)))), {});
    QVERIFY(std::holds_alternative<AppError>(longNote));
    QCOMPARE(std::get<AppError>(longNote).code, QStringLiteral("SCHEMA_INVALID"));

    // 失败都不改数据
    QCOMPARE(document.project().sheets.size(), 1);
    QCOMPARE(treeOf(document.project()).nodes.size(), 1);
}

void TstAiCommands::applyMapRejectsTooLargeProposal() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("超量")));
    CommandService service(&document);
    AiCommands ai(&service, &document);

    QVector<GeneratedNode> children;
    for (int i = 0; i <= Limits::kAiMapMaxNodes; ++i)   // 根 + 51 = 52，超过50
        children.append(gen(QStringLiteral("节点%1").arg(i)));
    const auto applied = ai.applyMapAsSheet(proposalOf(gen(QStringLiteral("根"), children)), {});
    QVERIFY(std::holds_alternative<AppError>(applied));
    QCOMPARE(std::get<AppError>(applied).code, QStringLiteral("LIMIT_EXCEEDED"));
    QCOMPARE(document.project().sheets.size(), 1);
}

void TstAiCommands::applyMapRejectsTooDeepProposal() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("超深")));
    CommandService service(&document);
    AiCommands ai(&service, &document);

    // 第5层（根算第1层）超出kAiMapMaxDepth=4
    GeneratedNode node = gen(QStringLiteral("第5层"));
    for (int level = 4; level >= 1; --level)
        node = gen(QStringLiteral("第%1层").arg(level), {node});

    const auto applied = ai.applyMapAsSheet(proposalOf(node), {});
    QVERIFY(std::holds_alternative<AppError>(applied));
    QCOMPARE(std::get<AppError>(applied).code, QStringLiteral("SCHEMA_INVALID"));
    QCOMPARE(document.project().sheets.size(), 1);
}

void TstAiCommands::saveDraftCreatesDraftWithSourceRevision() {
    ProjectDocument document;
    Project project = ProjectFactory::createMindmap(QStringLiteral("文字稿"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    addPlainChild(project, rootId, 0, QStringLiteral("需求分析"));
    document.replaceProject(project);
    CommandService service(&document);
    AiCommands ai(&service, &document);
    const qint64 generatedAt = document.sessionRevision();

    QVector<DraftSection> sections;
    sections.append(sectionOf(QStringLiteral("概述"), QStringLiteral("本文概述需求分析。"),
                              {rootId}, QStringLiteral("source")));
    sections.append(sectionOf(QStringLiteral("补充"), QStringLiteral("可结合课程项目理解。"),
                              QStringList(), QStringLiteral("supplement")));
    const auto saved = ai.saveDraft(draftOf(QStringLiteral("复习摘要"), sections), sheetId,
                                    QStringLiteral("summary"), generatedAt);
    QVERIFY(std::holds_alternative<ID>(saved));
    const ID draftId = std::get<ID>(saved);

    QCOMPARE(document.project().drafts.size(), 1);
    const TextDraft& draft = document.project().drafts.first();
    QCOMPARE(draft.id, draftId);
    QCOMPARE(draft.title, QStringLiteral("复习摘要"));
    QCOMPARE(draft.sourceSheetId, sheetId);
    QCOMPARE(draft.sourceRevision, generatedAt);
    QCOMPARE(draft.purpose, QStringLiteral("summary"));
    QCOMPARE(draft.sections.size(), 2);
    QCOMPARE(draft.sections.at(0).sourceNodeIds, QStringList({rootId}));
    QCOMPARE(draft.sections.at(0).origin, QStringLiteral("source"));
    QCOMPARE(draft.sections.at(1).origin, QStringLiteral("supplement"));
    QVERIFY(draft.sections.at(1).sourceNodeIds.isEmpty());
    QVERIFY(draft.createdAt.isValid());
    QVERIFY(ProjectValidator::validate(document.project()).isEmpty());

    service.undo();
    QVERIFY(document.project().drafts.isEmpty());
}

void TstAiCommands::saveDraftNormalizesPurposeAndTitle() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("默认值")));
    CommandService service(&document);
    AiCommands ai(&service, &document);
    const ID sheetId = document.project().activeSheetId;

    const auto saved = ai.saveDraft(
        draftOf(QString(), {sectionOf(QStringLiteral("段"), QStringLiteral("正文。"))}), sheetId,
        QString(), -1);
    QVERIFY(std::holds_alternative<ID>(saved));
    const TextDraft& draft = document.project().drafts.first();
    QCOMPARE(draft.purpose, QStringLiteral("summary"));       // 空用途按摘要
    QCOMPARE(draft.title, QStringLiteral("默认值"));           // 空标题取来源工作表名
    QCOMPARE(draft.sourceRevision, document.sessionRevision() - 1);   // 未提供时记录当前修订
}

void TstAiCommands::saveDraftRejectsUnknownSourceNode() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("来源校验")));
    CommandService service(&document);
    AiCommands ai(&service, &document);
    const ID sheetId = document.project().activeSheetId;

    const auto saved = ai.saveDraft(
        draftOf(QStringLiteral("稿"), {sectionOf(QStringLiteral("段"), QStringLiteral("正文。"),
                                                {QStringLiteral("not-a-node")})}),
        sheetId, QStringLiteral("summary"), -1);
    QVERIFY(std::holds_alternative<AppError>(saved));
    QCOMPARE(std::get<AppError>(saved).code, QStringLiteral("INVALID_ARGUMENT"));
    QVERIFY(document.project().drafts.isEmpty());
}

void TstAiCommands::saveDraftRejectsNonTreeSheet() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createGantt(QStringLiteral("甘特")));
    CommandService service(&document);
    AiCommands ai(&service, &document);
    const ID sheetId = document.project().activeSheetId;

    const auto saved = ai.saveDraft(
        draftOf(QStringLiteral("稿"), {sectionOf(QStringLiteral("段"), QStringLiteral("正文。"))}),
        sheetId, QStringLiteral("summary"), -1);
    QVERIFY(std::holds_alternative<AppError>(saved));
    QCOMPARE(std::get<AppError>(saved).code, QStringLiteral("INVALID_ARGUMENT"));
}

void TstAiCommands::saveDraftRejectsEmptySections() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("空稿")));
    CommandService service(&document);
    AiCommands ai(&service, &document);
    const ID sheetId = document.project().activeSheetId;

    const auto empty = ai.saveDraft(draftOf(QStringLiteral("稿"), {}), sheetId,
                                    QStringLiteral("summary"), -1);
    QVERIFY(std::holds_alternative<AppError>(empty));
    QCOMPARE(std::get<AppError>(empty).code, QStringLiteral("SCHEMA_INVALID"));

    // 有段落但正文为空同样拒绝
    const auto blank = ai.saveDraft(
        draftOf(QStringLiteral("稿"), {sectionOf(QStringLiteral("段"), QStringLiteral("   "))}),
        sheetId, QStringLiteral("summary"), -1);
    QVERIFY(std::holds_alternative<AppError>(blank));
    QCOMPARE(std::get<AppError>(blank).code, QStringLiteral("SCHEMA_INVALID"));
    QVERIFY(document.project().drafts.isEmpty());
}

QTEST_GUILESS_MAIN(TstAiCommands)
#include "tst_aicommands.moc"
