#include <QtTest>

#include <QHash>
#include <QVector>
#include <cmath>
#include <optional>

#include "domain/Domain.h"
#include "domain/Limits.h"
#include "layout/TreeLayout.h"

using namespace lingya;

// 简化树布局测试（D-M1-06最小布局接口、规范§13布局规则）：
// 确定性、同级(order,ID)排序、兄弟间距、父居中、双侧/右侧布局、collapsed隐藏、
// 错误路径（缺尺寸/非正尺寸/孤儿/环）与100节点规模（M1验收先100节点）。
class TstLayout : public QObject
{
    Q_OBJECT

  private slots:
    void singleRootAtOrigin();
    void chainGrowsRightByLevelSpacing();
    void rightLayoutPutsAllChildrenRight();
    void bilateralAlternatesAndSideForces();
    void siblingsSortedByOrderThenId();
    void siblingsKeepVerticalGap();
    void parentCenteredOnChildrenMean();
    void tallerParentKeepsSubtreeHeight();
    void collapsedSubtreeHidden();
    void collapsedRootShowsOnlyRoot();
    void deterministicSameOutputTwice();
    void missingSizeRejected();
    void nonPositiveSizeRejected();
    void orphanParentRejected();
    void cycleRejected();
    void hundredNodesNoOverlapAndFinite();

  private:
    static TreeNode makeNode(const QString &id, std::optional<ID> parentId, int order = 0,
                             const QString &side = QStringLiteral("auto"), bool collapsed = false)
    {
        TreeNode node;
        node.id = id;
        node.title = id;
        node.parentId = std::move(parentId);
        node.order = order;
        node.side = side;
        node.collapsed = collapsed;
        return node;
    }

    // 生成一棵根为"root"的树图（bilateral，可用layoutOverride覆盖）
    static TreeSheet makeSheet(const QString &layout = QStringLiteral("bilateral"))
    {
        TreeSheet sheet;
        sheet.id = QStringLiteral("sheet-1");
        sheet.rootId = QStringLiteral("root");
        sheet.layout = layout;
        sheet.nodes.insert(QStringLiteral("root"), makeNode(QStringLiteral("root"), std::nullopt));
        return sheet;
    }

    static void addNode(TreeSheet &sheet, const QString &id, const QString &parentId, int order = 0,
                        const QString &side = QStringLiteral("auto"), bool collapsed = false)
    {
        sheet.nodes.insert(id, makeNode(id, parentId, order, side, collapsed));
    }

    // 全部节点统一尺寸（w=120在kNodeWidthMin..Max内，h=40）
    static QVector<TreeLayout::NodeSize> uniformSizes(const TreeSheet &sheet, double w = 120.0,
                                                      double h = 40.0)
    {
        QVector<TreeLayout::NodeSize> sizes;
        for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it)
            sizes.append(TreeLayout::NodeSize{it.key(), w, h});
        return sizes;
    }

    // 从结果取某节点位置
    static TreeLayout::NodePosition posOf(const QVector<TreeLayout::NodePosition> &positions,
                                          const QString &id)
    {
        for (const auto &p : positions)
            if (p.id == id)
                return p;
        return TreeLayout::NodePosition{};
    }

    static bool hasPosition(const QVector<TreeLayout::NodePosition> &positions, const QString &id)
    {
        for (const auto &p : positions)
            if (p.id == id)
                return true;
        return false;
    }

    // 造一棵100节点宽树：root + 99个子（编号00..98，order=i）
    static TreeSheet makeHundredNodeSheet()
    {
        TreeSheet sheet = makeSheet(QStringLiteral("right"));
        for (int i = 0; i < 99; ++i)
            addNode(sheet, QStringLiteral("n%1").arg(i, 2, 10, QLatin1Char('0')),
                    QStringLiteral("root"), i);
        return sheet;
    }
};

void TstLayout::singleRootAtOrigin()
{
    const TreeSheet sheet = makeSheet();
    const auto result = TreeLayout::layout(sheet, uniformSizes(sheet));
    QVERIFY(std::holds_alternative<QVector<TreeLayout::NodePosition>>(result));
    const auto positions = std::get<QVector<TreeLayout::NodePosition>>(result);
    QCOMPARE(positions.size(), 1);
    QCOMPARE(positions.first().id, QStringLiteral("root"));
    QCOMPARE(positions.first().x, 0.0);
    QCOMPARE(positions.first().y, 0.0);
}

void TstLayout::chainGrowsRightByLevelSpacing()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    addNode(sheet, "c1", "root", 0);
    addNode(sheet, "c2", "c1", 0);
    const auto result = TreeLayout::layout(sheet, uniformSizes(sheet));
    QVERIFY(std::holds_alternative<QVector<TreeLayout::NodePosition>>(result));
    const auto positions = std::get<QVector<TreeLayout::NodePosition>>(result);
    QCOMPARE(positions.size(), 3);
    // 等宽链：相邻深度中心x差 = 宽/2 + 80 + 宽/2 = 120+80=200（kLevelSpacingX）
    QCOMPARE(posOf(positions, "root").x, 0.0);
    QCOMPARE(posOf(positions, "c1").x, 200.0);
    QCOMPARE(posOf(positions, "c2").x, 400.0);
    // 单链：父中心=子中心，全部同一水平线
    QCOMPARE(posOf(positions, "root").y, 0.0);
    QCOMPARE(posOf(positions, "c1").y, 0.0);
    QCOMPARE(posOf(positions, "c2").y, 0.0);
}

void TstLayout::rightLayoutPutsAllChildrenRight()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    addNode(sheet, "a", "root", 0);
    addNode(sheet, "b", "root", 1);
    addNode(sheet, "c", "root", 2);
    const auto result = TreeLayout::layout(sheet, uniformSizes(sheet));
    const auto positions = std::get<QVector<TreeLayout::NodePosition>>(result);
    QVERIFY(posOf(positions, "a").x > 0.0);
    QVERIFY(posOf(positions, "b").x > 0.0);
    QVERIFY(posOf(positions, "c").x > 0.0);
}

void TstLayout::bilateralAlternatesAndSideForces()
{
    TreeSheet sheet = makeSheet(); // bilateral
    addNode(sheet, "auto0", "root", 0);
    addNode(sheet, "auto1", "root", 1);
    addNode(sheet, "auto2", "root", 2);
    addNode(sheet, "forceL", "root", 3, QStringLiteral("left"));
    const auto result = TreeLayout::layout(sheet, uniformSizes(sheet));
    const auto positions = std::get<QVector<TreeLayout::NodePosition>>(result);
    // auto按序交替：首个右侧，第二个左侧，第三个右侧
    QVERIFY(posOf(positions, "auto0").x > 0.0);
    QVERIFY(posOf(positions, "auto1").x < 0.0);
    QVERIFY(posOf(positions, "auto2").x > 0.0);
    // side="left"强制左侧（不参与交替）
    QVERIFY(posOf(positions, "forceL").x < 0.0);
}

void TstLayout::siblingsSortedByOrderThenId()
{
    {
        TreeSheet sheet = makeSheet(QStringLiteral("right"));
        addNode(sheet, "later", "root", 2);
        addNode(sheet, "earlier", "root", 1);
        const auto positions = std::get<QVector<TreeLayout::NodePosition>>(
            TreeLayout::layout(sheet, uniformSizes(sheet)));
        QVERIFY(posOf(positions, "earlier").y < posOf(positions, "later").y); // order小者在上
    }
    {
        TreeSheet sheet = makeSheet(QStringLiteral("right"));
        addNode(sheet, "bbb", "root", 5);
        addNode(sheet, "aaa", "root", 5); // 同order：按ID稳定平局打破（§13）
        const auto positions = std::get<QVector<TreeLayout::NodePosition>>(
            TreeLayout::layout(sheet, uniformSizes(sheet)));
        QVERIFY(posOf(positions, "aaa").y < posOf(positions, "bbb").y);
    }
}

void TstLayout::siblingsKeepVerticalGap()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    addNode(sheet, "s0", "root", 0);
    addNode(sheet, "s1", "root", 1);
    const auto positions =
        std::get<QVector<TreeLayout::NodePosition>>(TreeLayout::layout(sheet, uniformSizes(sheet)));
    // 两叶h=40、间距24：中心差恰为40+24=64（kSiblingSpacingY）
    QCOMPARE(std::fabs(posOf(positions, "s1").y - posOf(positions, "s0").y), 64.0);
}

void TstLayout::parentCenteredOnChildrenMean()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    addNode(sheet, "s0", "root", 0);
    addNode(sheet, "s1", "root", 1);
    const auto positions =
        std::get<QVector<TreeLayout::NodePosition>>(TreeLayout::layout(sheet, uniformSizes(sheet)));
    // 根居中于首末子中心均值；归一化后子对称为-32/+32（h=40、gap=24）
    QCOMPARE(posOf(positions, "root").y, 0.0);
    QCOMPARE(posOf(positions, "s0").y, -32.0);
    QCOMPARE(posOf(positions, "s1").y, 32.0);
}

void TstLayout::tallerParentKeepsSubtreeHeight()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    addNode(sheet, "tall", "root", 0); // 高100、带两个h=20的子（子块高64<100）
    addNode(sheet, "leafA", "tall", 0);
    addNode(sheet, "leafB", "tall", 1);
    addNode(sheet, "sibling", "root", 1); // h=40
    const QVector<TreeLayout::NodeSize> sizes{
        TreeLayout::NodeSize{QStringLiteral("root"), 120.0, 40.0},
        TreeLayout::NodeSize{QStringLiteral("tall"), 120.0, 100.0},
        TreeLayout::NodeSize{QStringLiteral("leafA"), 120.0, 20.0},
        TreeLayout::NodeSize{QStringLiteral("leafB"), 120.0, 20.0},
        TreeLayout::NodeSize{QStringLiteral("sibling"), 120.0, 40.0},
    };
    const auto positions =
        std::get<QVector<TreeLayout::NodePosition>>(TreeLayout::layout(sheet, sizes));
    // tall子树高=max(64,100)=100；sibling与tall中心差 ≥ 100/2+40/2+24=94
    QVERIFY(std::fabs(posOf(positions, "sibling").y - posOf(positions, "tall").y) >= 94.0);
    // tall自身仍居中于其子
    QVERIFY(posOf(positions, "tall").y < posOf(positions, "leafB").y);
    QVERIFY(posOf(positions, "tall").y > posOf(positions, "leafA").y);
}

void TstLayout::collapsedSubtreeHidden()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    addNode(sheet, "folded", "root", 0, QStringLiteral("auto"), true); // 折叠节点
    addNode(sheet, "hidden", "folded", 0);
    addNode(sheet, "hidden2", "folded", 1);
    addNode(sheet, "normal", "root", 1);
    const auto result = TreeLayout::layout(sheet, uniformSizes(sheet));
    const auto positions = std::get<QVector<TreeLayout::NodePosition>>(result);
    QCOMPARE(positions.size(), 3); // 根+folded+normal；隐藏子树不输出
    QVERIFY(hasPosition(positions, "folded"));
    QVERIFY(!hasPosition(positions, "hidden"));
    QVERIFY(!hasPosition(positions, "hidden2"));
}

void TstLayout::collapsedRootShowsOnlyRoot()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    sheet.nodes["root"].collapsed = true;
    addNode(sheet, "c", "root", 0);
    const auto positions =
        std::get<QVector<TreeLayout::NodePosition>>(TreeLayout::layout(sheet, uniformSizes(sheet)));
    QCOMPARE(positions.size(), 1);
    QCOMPARE(positions.first().id, QStringLiteral("root"));
}

void TstLayout::deterministicSameOutputTwice()
{
    const TreeSheet sheet = makeHundredNodeSheet();
    const auto first = TreeLayout::layout(sheet, uniformSizes(sheet));
    const auto second = TreeLayout::layout(sheet, uniformSizes(sheet));
    QVERIFY(std::holds_alternative<QVector<TreeLayout::NodePosition>>(first));
    const auto a = std::get<QVector<TreeLayout::NodePosition>>(first);
    const auto b = std::get<QVector<TreeLayout::NodePosition>>(second);
    QCOMPARE(a.size(), b.size());
    for (int i = 0; i < a.size(); ++i)
    {
        QCOMPARE(a.at(i).id, b.at(i).id);
        QCOMPARE(a.at(i).x, b.at(i).x);
        QCOMPARE(a.at(i).y, b.at(i).y);
    }
}

void TstLayout::missingSizeRejected()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    addNode(sheet, "c", "root", 0);
    QVector<TreeLayout::NodeSize> sizes = uniformSizes(sheet);
    sizes.removeFirst(); // 挖掉一个节点的尺寸
    const auto result = TreeLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("NOT_FOUND"));
}

void TstLayout::nonPositiveSizeRejected()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    addNode(sheet, "c", "root", 0);
    QVector<TreeLayout::NodeSize> sizes = uniformSizes(sheet);
    sizes.last().width = 0.0;
    const auto result = TreeLayout::layout(sheet, sizes);
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_ARGUMENT"));
}

void TstLayout::orphanParentRejected()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    addNode(sheet, "c", "root", 0);
    addNode(sheet, "orphan", "ghost", 0); // 父节点不存在
    const auto result = TreeLayout::layout(sheet, uniformSizes(sheet));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_ARGUMENT"));
}

void TstLayout::cycleRejected()
{
    TreeSheet sheet = makeSheet(QStringLiteral("right"));
    addNode(sheet, "a", "b", 0);
    addNode(sheet, "b", "a", 0); // 互相为父：从根不可达
    const auto result = TreeLayout::layout(sheet, uniformSizes(sheet));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_ARGUMENT"));
}

void TstLayout::hundredNodesNoOverlapAndFinite()
{
    const TreeSheet sheet = makeHundredNodeSheet();
    const auto positions =
        std::get<QVector<TreeLayout::NodePosition>>(TreeLayout::layout(sheet, uniformSizes(sheet)));
    QCOMPARE(positions.size(), 100);
    // 全部坐标有限
    for (const auto &p : positions)
    {
        QVERIFY(std::isfinite(p.x));
        QVERIFY(std::isfinite(p.y));
    }
    // 同级（root的99个子，全右侧）相邻两两不重叠：中心差 ≥ 40+24=64（order序）
    double previousY = posOf(positions, "n00").y;
    for (int i = 1; i < 99; ++i)
    {
        const double y = posOf(positions, QStringLiteral("n%1").arg(i, 2, 10, QLatin1Char('0'))).y;
        QVERIFY(y - previousY >= 64.0);
        previousY = y;
    }
    // 根居中于首末子
    QVERIFY(posOf(positions, "root").y > posOf(positions, "n00").y);
    QVERIFY(posOf(positions, "root").y < posOf(positions, "n98").y);
}

QTEST_MAIN(TstLayout)
#include "tst_layout.moc"
