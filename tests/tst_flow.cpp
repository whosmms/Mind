#include <QtTest>

#include <QHash>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

#include "commands/CommandService.h"
#include "commands/FlowCommands.h"
#include "commands/ReviewCommands.h"
#include "commands/SheetCommands.h"
#include "document/ProjectDocument.h"
#include "domain/Domain.h"
#include "domain/ProjectFactory.h"
#include "domain/ProjectValidator.h"
#include "layout/FishboneLayout.h"
#include "layout/FlowLayout.h"

using namespace lingya;

namespace {

FlowSheet& flowOf(Project& project) {
    for (Sheet& sheet : project.sheets)
        if (auto* flow = std::get_if<FlowSheet>(&sheet)) return *flow;
    Q_ASSERT(false);
    return std::get<FlowSheet>(project.sheets.first());
}

const FlowSheet& flowOf(const Project& project) {
    for (const Sheet& sheet : project.sheets)
        if (const auto* flow = std::get_if<FlowSheet>(&sheet)) return *flow;
    Q_ASSERT(false);
    return std::get<FlowSheet>(project.sheets.first());
}

TreeSheet& treeOf(Project& project) {
    return std::get<TreeSheet>(project.sheets.first());
}

const TreeSheet& treeOf(const Project& project) {
    return std::get<TreeSheet>(project.sheets.first());
}

FlowNode rawNode(const ID& id, const QString& role = QStringLiteral("process")) {
    FlowNode node;
    node.id = id;
    node.title = id;
    node.role = role;
    node.contentRevision = 1;
    return node;
}

FlowEdge rawEdge(const ID& id, const ID& source, const ID& target) {
    FlowEdge edge;
    edge.id = id;
    edge.sourceId = source;
    edge.targetId = target;
    edge.style.color = QStringLiteral("#247A62");
    return edge;
}

FlowLayout::NodeSize sizeOf(const ID& id, double width = 120.0, double height = 60.0) {
    return FlowLayout::NodeSize{id, width, height};
}

QHash<ID, QPointF> positionsOf(const FlowLayout::Result& result) {
    QHash<ID, QPointF> map;
    for (const FlowLayout::NodePosition& position : result.positions)
        map.insert(position.id, QPointF(position.x, position.y));
    return map;
}

bool rectsOverlap(const QPointF& a, const FlowLayout::NodeSize& sa, const QPointF& b,
                  const FlowLayout::NodeSize& sb) {
    const double leftA = a.x() - sa.width / 2.0;
    const double rightA = a.x() + sa.width / 2.0;
    const double topA = a.y() - sa.height / 2.0;
    const double bottomA = a.y() + sa.height / 2.0;
    const double leftB = b.x() - sb.width / 2.0;
    const double rightB = b.x() + sb.width / 2.0;
    const double topB = b.y() - sb.height / 2.0;
    const double bottomB = b.y() + sb.height / 2.0;
    const double epsilon = 0.5;   // 允许相邻边贴合
    return rightA - epsilon > leftB && rightB - epsilon > leftA && bottomA - epsilon > topB &&
           bottomB - epsilon > topA;
}

// 任意两节点不得重叠（规范§6.3要求布局结果可校验，不只检查“调用过函数”）
bool anyOverlap(const FlowSheet& sheet, const QVector<FlowLayout::NodeSize>& sizes) {
    QHash<ID, FlowLayout::NodeSize> dims;
    for (const FlowLayout::NodeSize& size : sizes) dims.insert(size.id, size);
    const auto result = FlowLayout::layout(sheet, sizes);
    if (std::holds_alternative<AppError>(result)) return true;
    const FlowLayout::Result& value = std::get<FlowLayout::Result>(result);
    for (int i = 0; i < value.positions.size(); ++i) {
        for (int j = i + 1; j < value.positions.size(); ++j) {
            const auto& left = value.positions.at(i);
            const auto& right = value.positions.at(j);
            if (rectsOverlap(QPointF(left.x, left.y), dims.value(left.id),
                             QPointF(right.x, right.y), dims.value(right.id)))
                return true;
        }
    }
    return false;
}

FishboneLayout::NodeSize fishSize(const ID& id, double width = 120.0, double height = 48.0) {
    return FishboneLayout::NodeSize{id, width, height};
}

QHash<ID, FishboneLayout::NodeSize> fishDims(const QVector<FishboneLayout::NodeSize>& sizes) {
    QHash<ID, FishboneLayout::NodeSize> map;
    for (const FishboneLayout::NodeSize& size : sizes) map.insert(size.id, size);
    return map;
}

// 鱼骨图任意两节点不得重叠（中心点 + 尺寸比较）
bool fishAnyOverlap(const QHash<ID, FishboneLayout::NodeSize>& dims,
                    const QVector<FishboneLayout::NodePosition>& positions) {
    for (int i = 0; i < positions.size(); ++i) {
        for (int j = i + 1; j < positions.size(); ++j) {
            const auto& left = positions.at(i);
            const auto& right = positions.at(j);
            const double leftA = left.x - dims.value(left.id).width / 2.0;
            const double rightA = left.x + dims.value(left.id).width / 2.0;
            const double topA = left.y - dims.value(left.id).height / 2.0;
            const double bottomA = left.y + dims.value(left.id).height / 2.0;
            const double leftB = right.x - dims.value(right.id).width / 2.0;
            const double rightB = right.x + dims.value(right.id).width / 2.0;
            const double topB = right.y - dims.value(right.id).height / 2.0;
            const double bottomB = right.y + dims.value(right.id).height / 2.0;
            if (rightA - 0.5 > leftB && rightB - 0.5 > leftA && bottomA - 0.5 > topB &&
                bottomB - 0.5 > topA)
                return true;
        }
    }
    return false;
}

// 可见子节点（未折叠），按(order, ID)排序——与布局服务的排序规则一致
QVector<QPair<int, ID>> visibleChildrenOf(const TreeSheet& tree, const ID& parentId) {
    QVector<QPair<int, ID>> children;
    for (auto it = tree.nodes.constBegin(); it != tree.nodes.constEnd(); ++it) {
        if (!it.value().parentId.has_value() || *it.value().parentId != parentId) continue;
        children.append({it.value().order, it.key()});
    }
    std::sort(children.begin(), children.end(),
              [](const QPair<int, ID>& left, const QPair<int, ID>& right) {
                  if (left.first != right.first) return left.first < right.first;
                  return left.second < right.second;
              });
    return children;
}

ID addFishReason(TreeSheet& tree, const ID& parentId, int order, const QString& title) {
    TreeNode node;
    node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node.parentId = parentId;
    node.order = order;
    node.title = title;
    node.contentRevision = 1;
    tree.nodes.insert(node.id, node);
    return node.id;
}

struct FlowFixture {
    Project project;
    ID sheetId;
    ID startId;
};

FlowFixture makeFlowFixture() {
    FlowFixture fixture;
    fixture.project = ProjectFactory::createFlowchart(QStringLiteral("流程测试"));
    const FlowSheet& flow = flowOf(fixture.project);
    fixture.sheetId = flow.id;
    fixture.startId = flow.nodes.constBegin().key();
    return fixture;
}

}  // namespace

// 流程图布局（规范§6.3）与流程图/工作表命令（R11、R12、§5.1）的行为级测试。
class TstFlow : public QObject {
    Q_OBJECT

private slots:
    // 布局
    void emptySheetLayoutsToNothing();
    void chainIsLayeredTopDown();
    void chainLayoutIsDeterministic();
    void cycleBecomesRingGroup();
    void diamondDoesNotOverlap();
    void isolatedNodeGetsOwnComponent();
    void missingSizeRejected();
    void nonPositiveSizeRejected();
    void danglingEdgeRejected();
    void sixtyNodesDeterministicAndNoOverlap();
    // 流程命令
    void addNodeDefaultsAndUndoes();
    void addNodeRejectsInvalidRole();
    void addNodeRejectsNonFinitePosition();
    void updateNodeAppliesAndShortCircuits();
    void moveNodeUndoesInOneStep();
    void removeNodeCleansEdgesAndComments();
    void addEdgeAllowsCycleAndSelfLoop();
    void addEdgeRejectsIdenticalConnection();
    void addEdgeAllowsDifferentPorts();
    void updateEdgeValidatesArrow();
    void applyLayoutWritesOnceAndUndoes();
    void applyLayoutRejectsMissingSize();
    void flowCommentLifecycle();
    // 工作表命令
    void createSheetAddsAndActivates();
    void createSheetRejectsUnknownKind();
    void changeKindKeepsNodesAndUndoes();
    void changeKindRejectsFlowSheet();
    void deleteSheetKeepsTasks();
    void deleteSheetRejectsLastSheet();
    void deleteSheetRejectsWhenDraftUsesIt();
    void deleteSheetRejectsWhenCardsExist();
    void deleteSheetRejectsWhenRouteExists();
    // 鱼骨布局
    void fishboneHeadAtOriginAndCategoriesAlternate();
    void fishboneReasonsBranchOutward();
    void fishboneFourLevelsNoOverlap();
    void fishboneCollapsedSubtreeHidden();
    void fishboneRejectsNonFishboneSheet();
};

void TstFlow::emptySheetLayoutsToNothing() {
    FlowSheet sheet;
    sheet.id = QStringLiteral("s");
    sheet.name = QStringLiteral("空");
    const auto result = FlowLayout::layout(sheet, {});
    QVERIFY(std::holds_alternative<FlowLayout::Result>(result));
    const auto& value = std::get<FlowLayout::Result>(result);
    QVERIFY(value.positions.isEmpty());
    QCOMPARE(value.layerCount, 0);
    QCOMPARE(value.componentCount, 0);
}

void TstFlow::chainIsLayeredTopDown() {
    FlowSheet sheet;
    sheet.id = QStringLiteral("s");
    sheet.nodes.insert(QStringLiteral("a"), rawNode(QStringLiteral("a"), QStringLiteral("start")));
    sheet.nodes.insert(QStringLiteral("b"), rawNode(QStringLiteral("b")));
    sheet.nodes.insert(QStringLiteral("c"), rawNode(QStringLiteral("c"), QStringLiteral("end")));
    sheet.edges.append(rawEdge(QStringLiteral("e1"), QStringLiteral("a"), QStringLiteral("b")));
    sheet.edges.append(rawEdge(QStringLiteral("e2"), QStringLiteral("b"), QStringLiteral("c")));

    const QVector<FlowLayout::NodeSize> sizes{sizeOf(QStringLiteral("a")),
                                              sizeOf(QStringLiteral("b")),
                                              sizeOf(QStringLiteral("c"))};
    const auto result = FlowLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FlowLayout::Result>(result));
    const auto& value = std::get<FlowLayout::Result>(result);
    QCOMPARE(value.layerCount, 3);
    QCOMPARE(value.componentCount, 3);
    QVERIFY(value.groups.isEmpty());
    const auto positions = positionsOf(value);
    QVERIFY(positions.value(QStringLiteral("a")).y() < positions.value(QStringLiteral("b")).y());
    QVERIFY(positions.value(QStringLiteral("b")).y() < positions.value(QStringLiteral("c")).y());
    QVERIFY(qAbs(positions.value(QStringLiteral("a")).x()) < 1e-9);
    QVERIFY(qAbs(positions.value(QStringLiteral("c")).x()) < 1e-9);
    QVERIFY(!anyOverlap(sheet, sizes));
}

void TstFlow::chainLayoutIsDeterministic() {
    FlowSheet sheet;
    sheet.id = QStringLiteral("s");
    for (const QString& id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                              QStringLiteral("d")})
        sheet.nodes.insert(id, rawNode(id));
    sheet.edges.append(rawEdge(QStringLiteral("e1"), QStringLiteral("a"), QStringLiteral("b")));
    sheet.edges.append(rawEdge(QStringLiteral("e2"), QStringLiteral("a"), QStringLiteral("c")));
    sheet.edges.append(rawEdge(QStringLiteral("e3"), QStringLiteral("b"), QStringLiteral("d")));
    sheet.edges.append(rawEdge(QStringLiteral("e4"), QStringLiteral("c"), QStringLiteral("d")));
    const QVector<FlowLayout::NodeSize> sizes{
        sizeOf(QStringLiteral("a")), sizeOf(QStringLiteral("b")), sizeOf(QStringLiteral("c")),
        sizeOf(QStringLiteral("d"))};

    const auto first = FlowLayout::layout(sheet, sizes);
    const auto second = FlowLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FlowLayout::Result>(first));
    QVERIFY(std::holds_alternative<FlowLayout::Result>(second));
    const auto& left = std::get<FlowLayout::Result>(first);
    const auto& right = std::get<FlowLayout::Result>(second);
    QCOMPARE(left.positions.size(), right.positions.size());
    for (int i = 0; i < left.positions.size(); ++i) {
        QCOMPARE(left.positions.at(i).id, right.positions.at(i).id);
        QCOMPARE(left.positions.at(i).x, right.positions.at(i).x);
        QCOMPARE(left.positions.at(i).y, right.positions.at(i).y);
    }
}

void TstFlow::cycleBecomesRingGroup() {
    FlowSheet sheet;
    sheet.id = QStringLiteral("s");
    sheet.nodes.insert(QStringLiteral("a"), rawNode(QStringLiteral("a")));
    sheet.nodes.insert(QStringLiteral("b"), rawNode(QStringLiteral("b")));
    sheet.nodes.insert(QStringLiteral("c"), rawNode(QStringLiteral("c")));
    sheet.edges.append(rawEdge(QStringLiteral("e1"), QStringLiteral("a"), QStringLiteral("b")));
    sheet.edges.append(rawEdge(QStringLiteral("e2"), QStringLiteral("b"), QStringLiteral("c")));
    sheet.edges.append(rawEdge(QStringLiteral("e3"), QStringLiteral("c"), QStringLiteral("a")));

    const QVector<FlowLayout::NodeSize> sizes{sizeOf(QStringLiteral("a")),
                                              sizeOf(QStringLiteral("b")),
                                              sizeOf(QStringLiteral("c"))};
    const auto result = FlowLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FlowLayout::Result>(result));
    const auto& value = std::get<FlowLayout::Result>(result);
    // 回路是合法结构（规则3）：整体压缩为一个分量，内部环形排布，不报错
    QCOMPARE(value.componentCount, 1);
    QCOMPARE(value.layerCount, 1);
    QCOMPARE(value.groups.size(), 1);
    QCOMPARE(value.groups.first(), QStringList({QStringLiteral("a"), QStringLiteral("b"),
                                                QStringLiteral("c")}));
    const auto positions = positionsOf(value);
    QVERIFY(positions.value(QStringLiteral("a")) != positions.value(QStringLiteral("b")));
    QVERIFY(positions.value(QStringLiteral("b")) != positions.value(QStringLiteral("c")));
    QVERIFY(!anyOverlap(sheet, sizes));
}

void TstFlow::diamondDoesNotOverlap() {
    FlowSheet sheet;
    sheet.id = QStringLiteral("s");
    for (const QString& id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                              QStringLiteral("d")})
        sheet.nodes.insert(id, rawNode(id));
    sheet.edges.append(rawEdge(QStringLiteral("e1"), QStringLiteral("a"), QStringLiteral("b")));
    sheet.edges.append(rawEdge(QStringLiteral("e2"), QStringLiteral("a"), QStringLiteral("c")));
    sheet.edges.append(rawEdge(QStringLiteral("e3"), QStringLiteral("b"), QStringLiteral("d")));
    sheet.edges.append(rawEdge(QStringLiteral("e4"), QStringLiteral("c"), QStringLiteral("d")));

    const QVector<FlowLayout::NodeSize> sizes{
        sizeOf(QStringLiteral("a"), 160.0, 48.0), sizeOf(QStringLiteral("b"), 200.0, 80.0),
        sizeOf(QStringLiteral("c")), sizeOf(QStringLiteral("d"), 160.0, 48.0)};
    const auto result = FlowLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FlowLayout::Result>(result));
    const auto& value = std::get<FlowLayout::Result>(result);
    QCOMPARE(value.layerCount, 3);
    const auto positions = positionsOf(value);
    // 同层两个分支必须横向分开
    const QPointF b = positions.value(QStringLiteral("b"));
    const QPointF c = positions.value(QStringLiteral("c"));
    QCOMPARE(b.y(), c.y());
    QVERIFY(qAbs(b.x() - c.x()) >= 200.0);
    QVERIFY(!anyOverlap(sheet, sizes));
}

void TstFlow::isolatedNodeGetsOwnComponent() {
    FlowSheet sheet;
    sheet.id = QStringLiteral("s");
    sheet.nodes.insert(QStringLiteral("a"), rawNode(QStringLiteral("a")));
    sheet.nodes.insert(QStringLiteral("b"), rawNode(QStringLiteral("b")));
    sheet.nodes.insert(QStringLiteral("z"), rawNode(QStringLiteral("z")));
    sheet.edges.append(rawEdge(QStringLiteral("e1"), QStringLiteral("a"), QStringLiteral("b")));

    const QVector<FlowLayout::NodeSize> sizes{
        sizeOf(QStringLiteral("a")), sizeOf(QStringLiteral("b")), sizeOf(QStringLiteral("z"))};
    const auto result = FlowLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FlowLayout::Result>(result));
    const auto& value = std::get<FlowLayout::Result>(result);
    QCOMPARE(value.componentCount, 3);   // 孤立节点独占分量，不视为错误
    QCOMPARE(value.positions.size(), 3);
    QVERIFY(!anyOverlap(sheet, sizes));
}

void TstFlow::missingSizeRejected() {
    FlowSheet sheet;
    sheet.id = QStringLiteral("s");
    sheet.nodes.insert(QStringLiteral("a"), rawNode(QStringLiteral("a")));
    const auto result = FlowLayout::layout(sheet, {});
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("NOT_FOUND"));
}

void TstFlow::nonPositiveSizeRejected() {
    FlowSheet sheet;
    sheet.id = QStringLiteral("s");
    sheet.nodes.insert(QStringLiteral("a"), rawNode(QStringLiteral("a")));
    const auto zero = FlowLayout::layout(sheet, {sizeOf(QStringLiteral("a"), 0.0, 60.0)});
    QVERIFY(std::holds_alternative<AppError>(zero));
    const auto negative =
        FlowLayout::layout(sheet, {sizeOf(QStringLiteral("a"), 120.0, -1.0)});
    QVERIFY(std::holds_alternative<AppError>(negative));
    const auto infinite = FlowLayout::layout(
        sheet, {FlowLayout::NodeSize{QStringLiteral("a"), std::numeric_limits<double>::infinity(),
                                     60.0}});
    QVERIFY(std::holds_alternative<AppError>(infinite));
}

void TstFlow::danglingEdgeRejected() {
    FlowSheet sheet;
    sheet.id = QStringLiteral("s");
    sheet.nodes.insert(QStringLiteral("a"), rawNode(QStringLiteral("a")));
    sheet.edges.append(rawEdge(QStringLiteral("e1"), QStringLiteral("a"), QStringLiteral("nope")));
    const auto result = FlowLayout::layout(sheet, {sizeOf(QStringLiteral("a"))});
    QVERIFY(std::holds_alternative<AppError>(result));
}

void TstFlow::sixtyNodesDeterministicAndNoOverlap() {
    FlowSheet sheet;
    sheet.id = QStringLiteral("s");
    QVector<FlowLayout::NodeSize> sizes;
    for (int i = 0; i < 60; ++i) {
        const ID id = QStringLiteral("n%1").arg(i, 3, 10, QLatin1Char('0'));
        sheet.nodes.insert(id, rawNode(id));
        sizes.append(sizeOf(id, 120.0 + (i % 3) * 20.0, 48.0 + (i % 4) * 12.0));
    }
    for (int i = 0; i + 1 < 60; ++i) {
        const ID from = QStringLiteral("n%1").arg(i, 3, 10, QLatin1Char('0'));
        const ID to = QStringLiteral("n%1").arg(i + 1, 3, 10, QLatin1Char('0'));
        sheet.edges.append(rawEdge(QStringLiteral("e%1").arg(i), from, to));
    }
    for (int i = 0; i < 20; ++i) {
        const ID from = QStringLiteral("n%1").arg(i, 3, 10, QLatin1Char('0'));
        const ID to = QStringLiteral("n%1").arg(i + 5, 3, 10, QLatin1Char('0'));
        sheet.edges.append(rawEdge(QStringLiteral("x%1").arg(i), from, to));
    }

    const auto first = FlowLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FlowLayout::Result>(first));
    const auto& value = std::get<FlowLayout::Result>(first);
    QCOMPARE(value.positions.size(), 60);
    for (const auto& position : value.positions) {
        QVERIFY(std::isfinite(position.x));
        QVERIFY(std::isfinite(position.y));
    }
    QVERIFY(!anyOverlap(sheet, sizes));
    const auto second = FlowLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FlowLayout::Result>(second));
    QCOMPARE(std::get<FlowLayout::Result>(second).positions.first().x,
             value.positions.first().x);
}

void TstFlow::addNodeDefaultsAndUndoes() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const auto added = commands.addNode(fixture.sheetId, QString(),
                                        QStringLiteral("  处理步骤  "), QPointF(40, 60));
    QVERIFY(std::holds_alternative<ID>(added));
    const ID nodeId = std::get<ID>(added);
    const FlowNode node = flowOf(document.project()).nodes.value(nodeId);
    QCOMPARE(node.role, QStringLiteral("process"));   // 空类型默认处理节点
    QCOMPARE(node.title, QStringLiteral("处理步骤"));
    QCOMPARE(node.position, QPointF(40, 60));
    QCOMPARE(node.contentRevision, qint64(1));

    service.undo();
    QVERIFY(!flowOf(document.project()).nodes.contains(nodeId));
    QVERIFY(service.canRedo());
}

void TstFlow::addNodeRejectsInvalidRole() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const auto result = commands.addNode(fixture.sheetId, QStringLiteral("hexagon"),
                                         QStringLiteral("x"), QPointF(0, 0));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_ARGUMENT"));
    QCOMPARE(flowOf(document.project()).nodes.size(), 1);
    QVERIFY(!service.canUndo());
}

void TstFlow::addNodeRejectsNonFinitePosition() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const auto result = commands.addNode(fixture.sheetId, QStringLiteral("process"),
                                         QStringLiteral("x"),
                                         QPointF(std::numeric_limits<double>::quiet_NaN(), 0));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(flowOf(document.project()).nodes.size(), 1);
}

void TstFlow::updateNodeAppliesAndShortCircuits() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    FlowNodePatch patch;
    patch.title = QStringLiteral("  新标题 ");
    patch.role = QStringLiteral("decision");
    patch.position = QPointF(10, 20);
    QVERIFY(std::holds_alternative<bool>(commands.updateNode(fixture.sheetId, fixture.startId,
                                                             patch)));
    FlowNode node = flowOf(document.project()).nodes.value(fixture.startId);
    QCOMPARE(node.title, QStringLiteral("新标题"));
    QCOMPARE(node.role, QStringLiteral("decision"));
    QCOMPARE(node.position, QPointF(10, 20));
    QCOMPARE(node.contentRevision, qint64(2));

    // 同值再次提交：短路，不入历史
    QVERIFY(std::holds_alternative<bool>(commands.updateNode(fixture.sheetId, fixture.startId,
                                                             patch)));
    service.undo();
    node = flowOf(document.project()).nodes.value(fixture.startId);
    QCOMPARE(node.title, QStringLiteral("开始"));
    QCOMPARE(node.role, QStringLiteral("start"));
    QCOMPARE(node.contentRevision, qint64(1));
}

void TstFlow::moveNodeUndoesInOneStep() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    QVERIFY(std::holds_alternative<bool>(commands.moveNode(fixture.sheetId, fixture.startId,
                                                           QPointF(200, 300))));
    QCOMPARE(flowOf(document.project()).nodes.value(fixture.startId).position,
             QPointF(200, 300));
    service.undo();
    QCOMPARE(flowOf(document.project()).nodes.value(fixture.startId).position, QPointF(0, 0));
    QVERIFY(!service.canUndo());
}

void TstFlow::removeNodeCleansEdgesAndComments() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const ID decision =
        std::get<ID>(commands.addNode(fixture.sheetId, QStringLiteral("decision"),
                                      QStringLiteral("是否通过"), QPointF(0, 120)));
    const ID end = std::get<ID>(commands.addNode(fixture.sheetId, QStringLiteral("end"),
                                                 QStringLiteral("结束"), QPointF(0, 240)));
    QVERIFY(std::holds_alternative<ID>(commands.addEdge(fixture.sheetId, fixture.startId, decision,
                                                        QStringLiteral(""))));
    QVERIFY(std::holds_alternative<ID>(
        commands.addEdge(fixture.sheetId, decision, end, QStringLiteral("是"))));
    const auto comment = commands.addComment(fixture.sheetId, decision, QStringLiteral("甲"),
                                             QStringLiteral("这里要补充否分支"));
    QVERIFY(std::holds_alternative<ID>(comment));
    QCOMPARE(flowOf(document.project()).edges.size(), 2);
    QCOMPARE(flowOf(document.project()).comments.size(), 1);

    QVERIFY(std::holds_alternative<bool>(commands.removeNode(fixture.sheetId, decision)));
    QCOMPARE(flowOf(document.project()).nodes.size(), 2);
    QVERIFY(flowOf(document.project()).edges.isEmpty());      // 两条端点连线同事务清理
    QVERIFY(flowOf(document.project()).comments.isEmpty());   // 批注同事务清理
    service.undo();
    QCOMPARE(flowOf(document.project()).edges.size(), 2);
    QCOMPARE(flowOf(document.project()).comments.size(), 1);
}

void TstFlow::addEdgeAllowsCycleAndSelfLoop() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const ID decision =
        std::get<ID>(commands.addNode(fixture.sheetId, QStringLiteral("decision"),
                                      QStringLiteral("通过？"), QPointF(0, 120)));
    QVERIFY(std::holds_alternative<ID>(commands.addEdge(fixture.sheetId, fixture.startId, decision,
                                                        QStringLiteral(""))));
    // 回路与自环都是合法结构（R12“回路”）
    QVERIFY(std::holds_alternative<ID>(
        commands.addEdge(fixture.sheetId, decision, fixture.startId, QStringLiteral("否"))));
    QVERIFY(std::holds_alternative<ID>(
        commands.addEdge(fixture.sheetId, decision, decision, QStringLiteral("重试"))));
    QCOMPARE(flowOf(document.project()).edges.size(), 3);
    QVERIFY(ProjectValidator::validate(document.project()).isEmpty());
}

void TstFlow::addEdgeRejectsIdenticalConnection() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const ID decision =
        std::get<ID>(commands.addNode(fixture.sheetId, QStringLiteral("decision"),
                                      QStringLiteral("通过？"), QPointF(0, 120)));
    QVERIFY(std::holds_alternative<ID>(commands.addEdge(fixture.sheetId, fixture.startId, decision,
                                                        QStringLiteral("是"))));
    const auto duplicate = commands.addEdge(fixture.sheetId, fixture.startId, decision,
                                            QStringLiteral("是"));
    QVERIFY(std::holds_alternative<AppError>(duplicate));
    QCOMPARE(std::get<AppError>(duplicate).code, QStringLiteral("DUPLICATE_EDGE"));
    QCOMPARE(flowOf(document.project()).edges.size(), 1);
}

void TstFlow::addEdgeAllowsDifferentPorts() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const ID decision =
        std::get<ID>(commands.addNode(fixture.sheetId, QStringLiteral("decision"),
                                      QStringLiteral("通过？"), QPointF(0, 120)));
    const auto first = commands.addEdge(fixture.sheetId, fixture.startId, decision,
                                        QStringLiteral("是"), QStringLiteral("right"),
                                        QStringLiteral("left"));
    QVERIFY(std::holds_alternative<ID>(first));
    const auto second = commands.addEdge(fixture.sheetId, fixture.startId, decision,
                                         QStringLiteral("是"), QStringLiteral("bottom"),
                                         QStringLiteral("left"));
    QVERIFY(std::holds_alternative<ID>(second));   // 不同端口表达不同含义，允许
    QCOMPARE(flowOf(document.project()).edges.size(), 2);

    const auto badPort = commands.addEdge(fixture.sheetId, fixture.startId, decision,
                                          QStringLiteral(""), QStringLiteral("middle"),
                                          QStringLiteral("left"));
    QVERIFY(std::holds_alternative<AppError>(badPort));
}

void TstFlow::updateEdgeValidatesArrow() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const ID decision =
        std::get<ID>(commands.addNode(fixture.sheetId, QStringLiteral("decision"),
                                      QStringLiteral("通过？"), QPointF(0, 120)));
    const ID edgeId = std::get<ID>(commands.addEdge(fixture.sheetId, fixture.startId, decision,
                                                    QStringLiteral("")));
    FlowEdgePatch patch;
    patch.label = QStringLiteral("  否 ");
    patch.arrow = QStringLiteral("both");
    QVERIFY(std::holds_alternative<bool>(commands.updateEdge(fixture.sheetId, edgeId, patch)));
    const FlowEdge edge = flowOf(document.project()).edges.first();
    QCOMPARE(edge.label, QStringLiteral("否"));
    QCOMPARE(edge.arrow, QStringLiteral("both"));

    patch.arrow = QStringLiteral("double");
    const auto rejected = commands.updateEdge(fixture.sheetId, edgeId, patch);
    QVERIFY(std::holds_alternative<AppError>(rejected));
    QCOMPARE(std::get<AppError>(rejected).code, QStringLiteral("INVALID_ARGUMENT"));

    QVERIFY(std::holds_alternative<bool>(commands.removeEdge(fixture.sheetId, edgeId)));
    QVERIFY(flowOf(document.project()).edges.isEmpty());
    service.undo();
    QCOMPARE(flowOf(document.project()).edges.size(), 1);
}

void TstFlow::applyLayoutWritesOnceAndUndoes() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const ID decision =
        std::get<ID>(commands.addNode(fixture.sheetId, QStringLiteral("decision"),
                                      QStringLiteral("通过？"), QPointF(500, 500)));
    QVERIFY(std::holds_alternative<ID>(commands.addEdge(fixture.sheetId, fixture.startId, decision,
                                                        QStringLiteral(""))));
    const QVector<FlowLayout::NodeSize> sizes{sizeOf(fixture.startId), sizeOf(decision)};

    QVERIFY(std::holds_alternative<bool>(commands.applyLayout(fixture.sheetId, sizes)));
    const QPointF startAfter = flowOf(document.project()).nodes.value(fixture.startId).position;
    QVERIFY(qAbs(startAfter.x()) < 1e-9);   // 第0层以x=0居中
    QVERIFY(flowOf(document.project()).nodes.value(decision).position.y() > startAfter.y());

    // 位置未变时再次布局：短路，一次撤销即可回到最初的位置
    QVERIFY(std::holds_alternative<bool>(commands.applyLayout(fixture.sheetId, sizes)));
    service.undo();
    QCOMPARE(flowOf(document.project()).nodes.value(decision).position, QPointF(500, 500));
}

void TstFlow::applyLayoutRejectsMissingSize() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const auto result = commands.applyLayout(fixture.sheetId, {});
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("NOT_FOUND"));
    QVERIFY(!service.canUndo());
}

void TstFlow::flowCommentLifecycle() {
    FlowFixture fixture = makeFlowFixture();
    ProjectDocument document;
    document.replaceProject(fixture.project);
    CommandService service(&document);
    FlowCommands commands(&service, &document);

    const auto added = commands.addComment(fixture.sheetId, fixture.startId,
                                           QStringLiteral("  乙 "),
                                           QStringLiteral("  起点需要说明  "));
    QVERIFY(std::holds_alternative<ID>(added));
    const ID commentId = std::get<ID>(added);
    const Comment comment = flowOf(document.project()).comments.first();
    QCOMPARE(comment.author, QStringLiteral("乙"));
    QCOMPARE(comment.body, QStringLiteral("起点需要说明"));
    QVERIFY(comment.createdAt.isValid());
    QVERIFY(!comment.resolved);

    QVERIFY(std::holds_alternative<bool>(
        commands.resolveComment(fixture.sheetId, commentId, true)));
    QVERIFY(flowOf(document.project()).comments.first().resolved);
    QVERIFY(std::holds_alternative<bool>(
        commands.resolveComment(fixture.sheetId, commentId, true)));   // 状态相同短路
    service.undo();
    QVERIFY(!flowOf(document.project()).comments.first().resolved);

    // 空白正文拒绝
    const auto empty = commands.addComment(fixture.sheetId, fixture.startId,
                                           QStringLiteral("乙"), QStringLiteral("   "));
    QVERIFY(std::holds_alternative<AppError>(empty));
}

void TstFlow::createSheetAddsAndActivates() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("多表")));
    CommandService service(&document);
    SheetCommands sheets(&service, &document);
    const ID mindmapId = document.project().activeSheetId;

    const auto created = sheets.createSheet(QStringLiteral("flowchart"));
    QVERIFY(std::holds_alternative<ID>(created));
    const ID flowId = std::get<ID>(created);
    QCOMPARE(document.project().sheets.size(), 2);
    QCOMPARE(document.project().activeSheetId, flowId);
    const FlowSheet& flow = flowOf(document.project());
    QCOMPARE(flow.id, flowId);
    QCOMPARE(flow.name, QStringLiteral("工作表 2"));
    QCOMPARE(flow.nodes.size(), 1);
    QCOMPARE(flow.nodes.constBegin().value().role, QStringLiteral("start"));
    QVERIFY(ProjectValidator::validate(document.project()).isEmpty());

    service.undo();
    QCOMPARE(document.project().sheets.size(), 1);
    QCOMPARE(document.project().activeSheetId, mindmapId);
}

void TstFlow::createSheetRejectsUnknownKind() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("多表")));
    CommandService service(&document);
    SheetCommands sheets(&service, &document);

    const auto result = sheets.createSheet(QStringLiteral("pie"));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_ARGUMENT"));
    QCOMPARE(document.project().sheets.size(), 1);
}

void TstFlow::changeKindKeepsNodesAndUndoes() {
    Project mindmap = ProjectFactory::createMindmap(QStringLiteral("换结构"));
    const ID rootId = treeOf(mindmap).rootId;
    TreeNode child;
    child.id = QStringLiteral("child-1");
    child.parentId = rootId;
    child.order = 0;
    child.title = QStringLiteral("原因一");
    child.tags = QStringList{QStringLiteral("现场")};
    child.contentRevision = 1;
    treeOf(mindmap).nodes.insert(child.id, child);

    ProjectDocument document;
    document.replaceProject(mindmap);
    CommandService service(&document);
    SheetCommands sheets(&service, &document);
    const ID sheetId = document.project().activeSheetId;

    QVERIFY(std::holds_alternative<bool>(
        sheets.changeSheetKind(sheetId, QStringLiteral("fishbone"))));
    const TreeSheet& fishbone = treeOf(document.project());
    QCOMPARE(fishbone.kind, QStringLiteral("fishbone"));
    QCOMPARE(fishbone.layout, QStringLiteral("fishbone"));
    QCOMPARE(fishbone.nodes.size(), 2);                        // 数据保留
    QCOMPARE(fishbone.nodes.value(QStringLiteral("child-1")).tags,
             QStringList({QStringLiteral("现场")}));
    QVERIFY(ProjectValidator::validate(document.project()).isEmpty());

    service.undo();
    QCOMPARE(treeOf(document.project()).kind, QStringLiteral("mindmap"));
    QCOMPARE(treeOf(document.project()).layout, QStringLiteral("bilateral"));
}

void TstFlow::changeKindRejectsFlowSheet() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createFlowchart(QStringLiteral("流程图")));
    CommandService service(&document);
    SheetCommands sheets(&service, &document);

    const auto result =
        sheets.changeSheetKind(document.project().activeSheetId, QStringLiteral("mindmap"));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_ARGUMENT"));
}

void TstFlow::deleteSheetKeepsTasks() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("删表"));
    const ID firstId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;

    Task task;
    task.id = QStringLiteral("task-1");
    task.title = QStringLiteral("中心主题");
    task.source = TaskSource{firstId, rootId};
    project.tasks.append(task);

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    SheetCommands sheets(&service, &document);

    const auto created = sheets.createSheet(QStringLiteral("mindmap"), QStringLiteral("第二张"));
    QVERIFY(std::holds_alternative<ID>(created));
    const ID secondId = std::get<ID>(created);

    QVERIFY(std::holds_alternative<bool>(sheets.deleteSheet(firstId)));
    QCOMPARE(document.project().sheets.size(), 1);
    QCOMPARE(document.project().activeSheetId, secondId);           // 删除当前表后回退到剩余表
    QCOMPARE(document.project().tasks.size(), 1);                    // 任务保留
    QVERIFY(!document.project().tasks.first().source.has_value());   // 但不再是工作表任务

    service.undo();
    QCOMPARE(document.project().sheets.size(), 2);
    QVERIFY(document.project().tasks.first().source.has_value());
}

// 甲复核 PR28（BUG-020）：以前删表会把表上的复习卡片一起删掉。规范§7.1要求“源节点删除保留
// 卡片快照并标missing，提供移除或转独立卡片”——学习状态（status/lastReviewedAt/custom）
// 是用户攒出来的，不能因为删了一张表就静默销毁。卡片sheetId必须指向存在的表（CARD_SHEET），
// 放宽该校验要先三方确认，所以本轮按影响确认阻断：有卡片就SHEET_IN_USE。
void TstFlow::deleteSheetRejectsWhenCardsExist() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("带卡片"));
    const ID firstId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;

    ReviewCard card;
    card.id = QStringLiteral("card-1");
    card.sheetId = firstId;
    card.nodeId = rootId;
    card.front = QStringLiteral("问");
    card.back = QStringLiteral("答");
    card.status = QStringLiteral("mastered");
    card.lastReviewedAt = QDateTime::currentDateTimeUtc();
    project.cards.append(card);

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    SheetCommands sheets(&service, &document);
    QVERIFY(std::holds_alternative<ID>(sheets.createSheet(QStringLiteral("mindmap"))));

    const qint64 revisionBefore = document.sessionRevision();
    const auto result = sheets.deleteSheet(firstId);
    QVERIFY(std::holds_alternative<AppError>(result));
    const AppError error = std::get<AppError>(result);
    QCOMPARE(error.code, QStringLiteral("SHEET_IN_USE"));
    QVERIFY(error.message.contains(QStringLiteral("复习卡片")));

    // 阻断式失败：表、卡片、历史全都不动
    QCOMPARE(document.project().sheets.size(), 2);
    QCOMPARE(document.project().cards.size(), 1);
    QCOMPARE(document.project().cards.first().status, QStringLiteral("mastered"));
    QVERIFY(document.project().cards.first().lastReviewedAt.has_value());
    QCOMPARE(document.sessionRevision(), revisionBefore);   // 不产生快照、不入历史

    // 提示是可操作的，不是死路：按提示在处理路径上移除卡片后，删表即可通过
    ReviewCommands reviews(&service, &document);
    QVERIFY(std::holds_alternative<bool>(reviews.removeCard(QStringLiteral("card-1"))));
    QVERIFY(std::holds_alternative<bool>(sheets.deleteSheet(firstId)));
    QCOMPARE(document.project().sheets.size(), 1);
    QVERIFY(document.project().cards.isEmpty());
}

// 讲解路线的步骤带用户写的讲解备注，同理不静默销毁（规范§7.2）
void TstFlow::deleteSheetRejectsWhenRouteExists() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("带路线"));
    const ID firstId = treeOf(project).id;

    PresentationRoute route;
    route.id = QStringLiteral("route-1");
    route.sheetId = firstId;
    route.title = QStringLiteral("讲解");
    PresentationStep step;
    step.id = QStringLiteral("step-1");
    step.nodeId = treeOf(project).rootId;
    step.speakerNote = QStringLiteral("开场先讲这个");
    route.steps.append(step);
    project.routes.append(route);

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    SheetCommands sheets(&service, &document);
    QVERIFY(std::holds_alternative<ID>(sheets.createSheet(QStringLiteral("mindmap"))));

    const auto result = sheets.deleteSheet(firstId);
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("SHEET_IN_USE"));
    QVERIFY(std::get<AppError>(result).message.contains(QStringLiteral("讲解路线")));
    QCOMPARE(document.project().sheets.size(), 2);
    QCOMPARE(document.project().routes.size(), 1);
    QCOMPARE(document.project().routes.first().steps.first().speakerNote,
             QStringLiteral("开场先讲这个"));
}

void TstFlow::deleteSheetRejectsLastSheet() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("单表")));
    CommandService service(&document);
    SheetCommands sheets(&service, &document);

    const auto result = sheets.deleteSheet(document.project().activeSheetId);
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_ARGUMENT"));
    QCOMPARE(document.project().sheets.size(), 1);
}

void TstFlow::deleteSheetRejectsWhenDraftUsesIt() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("文稿"));
    TextDraft draft;
    draft.id = QStringLiteral("draft-1");
    draft.sourceSheetId = treeOf(project).id;
    draft.title = QStringLiteral("总结草稿");
    project.drafts.append(draft);

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    SheetCommands sheets(&service, &document);
    const ID firstId = document.project().activeSheetId;
    QVERIFY(std::holds_alternative<ID>(sheets.createSheet(QStringLiteral("mindmap"))));

    const auto result = sheets.deleteSheet(firstId);
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("SHEET_IN_USE"));
    QCOMPARE(document.project().sheets.size(), 2);
    QCOMPARE(document.project().drafts.size(), 1);
}


void TstFlow::fishboneHeadAtOriginAndCategoriesAlternate() {
    Project project = ProjectFactory::createFishbone(QStringLiteral("鱼骨"));
    const TreeSheet& sheet = treeOf(project);
    QCOMPARE(sheet.kind, QStringLiteral("fishbone"));
    QCOMPARE(sheet.nodes.size(), 7);   // 问题 + 6类原因

    QVector<FishboneLayout::NodeSize> sizes;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it)
        sizes.append(fishSize(it.key()));

    const auto result = FishboneLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FishboneLayout::Result>(result));
    const auto& value = std::get<FishboneLayout::Result>(result);
    QCOMPARE(value.positions.size(), 7);
    QCOMPARE(value.bones.size(), 6);

    QHash<ID, QPointF> positions;
    for (const auto& position : value.positions)
        positions.insert(position.id, QPointF(position.x, position.y));
    // 鱼头固定在原点，其余节点都在左侧
    QCOMPARE(positions.value(sheet.rootId), QPointF(0, 0));
    const QVector<QPair<int, ID>> categories = visibleChildrenOf(sheet, sheet.rootId);
    QCOMPARE(categories.size(), 6);
    for (int i = 0; i < categories.size(); ++i) {
        const QPointF center = positions.value(categories.at(i).second);
        QVERIFY(center.x() < 0);
        // 上下交替：偶数在上（负y），奇数在下（正y）
        if (i % 2 == 0)
            QVERIFY(center.y() < 0);
        else
            QVERIFY(center.y() > 0);
    }
    QVERIFY(!fishAnyOverlap(fishDims(sizes), value.positions));

    // 确定性
    const auto again = FishboneLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FishboneLayout::Result>(again));
    QCOMPARE(std::get<FishboneLayout::Result>(again).positions.first().x,
             value.positions.first().x);
}

void TstFlow::fishboneReasonsBranchOutward() {
    Project project = ProjectFactory::createFishbone(QStringLiteral("鱼骨"));
    TreeSheet& sheet = treeOf(project);
    const QVector<QPair<int, ID>> categories = visibleChildrenOf(sheet, sheet.rootId);
    const ID below = categories.at(1).second;   // 下方类别
    const ID first = addFishReason(sheet, below, 0, QStringLiteral("原因一"));
    addFishReason(sheet, below, 1, QStringLiteral("原因二"));
    addFishReason(sheet, below, 2, QStringLiteral("原因三"));

    QVector<FishboneLayout::NodeSize> sizes;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it)
        sizes.append(fishSize(it.key()));
    const auto result = FishboneLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FishboneLayout::Result>(result));
    const auto& value = std::get<FishboneLayout::Result>(result);
    QCOMPARE(value.positions.size(), 10);
    QCOMPARE(value.bones.size(), 6);
    QCOMPARE(value.bones.at(1).size(), 4);   // 类别 + 3个原因
    QCOMPARE(value.bones.at(1).first(), below);

    QHash<ID, QPointF> positions;
    for (const auto& position : value.positions)
        positions.insert(position.id, QPointF(position.x, position.y));
    // 原因在类别左侧并向外（下方）依次排开
    QVERIFY(positions.value(first).x() < positions.value(below).x());
    QVERIFY(positions.value(first).y() > positions.value(below).y());
    QVERIFY(!fishAnyOverlap(fishDims(sizes), value.positions));
}

void TstFlow::fishboneFourLevelsNoOverlap() {
    Project project = ProjectFactory::createFishbone(QStringLiteral("鱼骨"));
    TreeSheet& sheet = treeOf(project);
    const ID category = visibleChildrenOf(sheet, sheet.rootId).at(0).second;
    const ID level2 = addFishReason(sheet, category, 0, QStringLiteral("二级原因"));
    const ID level3a = addFishReason(sheet, level2, 0, QStringLiteral("三级原因A"));
    const ID level3b = addFishReason(sheet, level2, 1, QStringLiteral("三级原因B"));
    addFishReason(sheet, level3a, 0, QStringLiteral("四级原因"));
    addFishReason(sheet, level3b, 0, QStringLiteral("四级原因2"));

    QVector<FishboneLayout::NodeSize> sizes;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it)
        sizes.append(fishSize(it.key(), 120.0 + (it.value().title.size() % 3) * 30.0,
                              48.0 + (it.value().title.size() % 2) * 16.0));
    const auto result = FishboneLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FishboneLayout::Result>(result));
    const auto& value = std::get<FishboneLayout::Result>(result);
    QCOMPARE(value.positions.size(), sheet.nodes.size());
    QCOMPARE(value.bones.first().size(), 6);   // 类别 + 2级 + 3级×2 + 4级×2
    QVERIFY(!fishAnyOverlap(fishDims(sizes), value.positions));
}

void TstFlow::fishboneCollapsedSubtreeHidden() {
    Project project = ProjectFactory::createFishbone(QStringLiteral("鱼骨"));
    TreeSheet& sheet = treeOf(project);
    const ID category = visibleChildrenOf(sheet, sheet.rootId).at(2).second;
    addFishReason(sheet, category, 0, QStringLiteral("隐藏原因"));
    const auto reason = addFishReason(sheet, category, 1, QStringLiteral("隐藏原因2"));
    sheet.nodes[category].collapsed = true;

    QVector<FishboneLayout::NodeSize> sizes;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it)
        sizes.append(fishSize(it.key()));
    const auto result = FishboneLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<FishboneLayout::Result>(result));
    const auto& value = std::get<FishboneLayout::Result>(result);
    QCOMPARE(value.positions.size(), 7);   // 根 + 6类，折叠子树不参与
    for (const auto& position : value.positions)
        QVERIFY(position.id != reason);
    QCOMPARE(value.bones.at(2).size(), 1);   // 折叠类别只保留自身
}

void TstFlow::fishboneRejectsNonFishboneSheet() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("导图"));
    const TreeSheet& sheet = treeOf(project);
    const auto result =
        FishboneLayout::layout(sheet, {fishSize(sheet.rootId)});
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_ARGUMENT"));

    Project fish = ProjectFactory::createFishbone(QStringLiteral("鱼骨"));
    const TreeSheet& fishSheet = treeOf(fish);
    const auto missing = FishboneLayout::layout(fishSheet, {});
    QVERIFY(std::holds_alternative<AppError>(missing));
    QCOMPARE(std::get<AppError>(missing).code, QStringLiteral("NOT_FOUND"));
}

QTEST_GUILESS_MAIN(TstFlow)
#include "tst_flow.moc"
