#include "layout/TreeLayout.h"
#include "domain/Limits.h"
#include <QSet>

#include <algorithm>
#include <functional>

namespace lingya
{

namespace TreeLayout
{

namespace
{

// 内部布局工作项：父子关系解析后的索引与几何（不复制领域数据）
struct Item
{
    ID id;
    double width = 0.0, height = 0.0;
    bool collapsed = false;
    bool visible = false; // 可见=从根可达且未被上层collapsed剪枝
    int depth = 0;
    QVector<int> children; // 同级按(order, ID)稳定排序后的下标
    double x = 0.0, y = 0.0;
};

} // namespace

std::variant<QVector<NodePosition>, AppError> layout(const TreeSheet &sheet,
                                                     const QVector<NodeSize> &sizes)
{
    // 尺寸表：索引化并校验（非正值INVALID_ARGUMENT；缺尺寸按节点查表时NOT_FOUND）
    QHash<ID, NodeSize> sizeById;
    sizeById.reserve(sizes.size());
    for (const NodeSize &size : sizes)
    {
        if (size.width <= 0.0 || size.height <= 0.0)
            return AppError{"INVALID_ARGUMENT",
                            QStringLiteral("节点尺寸必须为正值：%1").arg(size.id)};
        sizeById.insert(size.id, size);
    }

    const auto rootIt = sheet.nodes.constFind(sheet.rootId);
    if (rootIt == sheet.nodes.constEnd() || sheet.rootId.isEmpty())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("根节点不存在：%1").arg(sheet.rootId)};

    // 工作副本：每个节点必须有尺寸
    QHash<ID, int> index;
    QVector<Item> items;
    items.reserve(sheet.nodes.size());
    for (auto nodeIt = sheet.nodes.constBegin(); nodeIt != sheet.nodes.constEnd(); ++nodeIt)
    {
        const auto sizeIt = sizeById.constFind(nodeIt.key());
        if (sizeIt == sizeById.constEnd())
            return AppError{"NOT_FOUND",
                            QStringLiteral("节点缺少尺寸，无法布局：%1").arg(nodeIt.key())};
        Item item;
        item.id = nodeIt.key();
        item.width = sizeIt->width;
        item.height = sizeIt->height;
        item.collapsed = nodeIt->collapsed;
        index.insert(nodeIt.key(), items.size());
        items.append(item);
    }
    const int rootIndex = index.value(sheet.rootId, -1);

    // 父子边：非根节点的parentId必须指向存在的节点（孤儿/悬空引用防御；
    // 正常工程已被ProjectValidator拒绝，此处不静默收养错误数据）
    QVector<int> rootChildren;
    for (int i = 0; i < items.size(); ++i)
    {
        if (i == rootIndex)
            continue;
        const TreeNode &node = sheet.nodes.constFind(items.at(i).id).value();
        if (!node.parentId.has_value() || !sheet.nodes.contains(*node.parentId))
            return AppError{
                "INVALID_ARGUMENT",
                QStringLiteral("节点父级缺失或不存在（孤儿），无法布局：%1").arg(items.at(i).id)};
        const int parent = index.value(*node.parentId, -1);
        if (parent == rootIndex)
            rootChildren.append(i);
        else
            items[parent].children.append(i);
    }

    // 同级排序：(order, ID)稳定排序（规范§13：相同优先级用order、稳定ID打破平局）
    const auto sortSiblings = [&](QVector<int> &siblings)
    {
        std::sort(siblings.begin(), siblings.end(),
                  [&](int a, int b)
                  {
                      const TreeNode &na = sheet.nodes.constFind(items.at(a).id).value();
                      const TreeNode &nb = sheet.nodes.constFind(items.at(b).id).value();
                      if (na.order != nb.order)
                          return na.order < nb.order;
                      return items.at(a).id < items.at(b).id;
                  });
    };
    sortSiblings(rootChildren);
    items[rootIndex].children = rootChildren;
    for (Item &item : items)
        sortSiblings(item.children);

    // 可达性（忽略折叠）：全部节点必须从根到达，否则存在环（孤儿已被上面拒绝）
    {
        QVector<bool> seen(items.size(), false);
        QVector<int> stack{rootIndex};
        seen[rootIndex] = true;
        while (!stack.isEmpty())
        {
            const int current = stack.takeLast();
            for (int child : items.at(current).children)
            {
                if (seen[child])
                    return AppError{"INVALID_ARGUMENT", QStringLiteral("树存在环，无法布局")};
                seen[child] = true;
                stack.append(child);
            }
        }
        for (bool wasSeen : seen)
            if (!wasSeen)
                return AppError{"INVALID_ARGUMENT",
                                QStringLiteral("存在无法从根到达的节点，无法布局")};
    }

    // 可见性与深度：collapsed节点的子树隐藏（§13按可见子树布局）
    {
        QVector<int> stack{rootIndex};
        items[rootIndex].visible = true;
        while (!stack.isEmpty())
        {
            const int current = stack.takeLast();
            if (items.at(current).collapsed)
                continue;
            for (int child : items.at(current).children)
            {
                items[child].visible = true;
                items[child].depth = items.at(current).depth + 1;
                stack.append(child);
            }
        }
    }

    // 每深度最大宽度（两侧合并，bilateral左右镜像对齐；根深度0）
    int maxDepth = 0;
    for (const Item &item : items)
        if (item.visible)
            maxDepth = std::max(maxDepth, item.depth);
    QVector<double> levelMaxWidth(maxDepth + 1, 0.0);
    for (const Item &item : items)
    {
        if (!item.visible)
            continue;
        levelMaxWidth[item.depth] = std::max(levelMaxWidth[item.depth], item.width);
    }
    // 各深度中心x：右向为正，左向镜像取负（父子水平间距kLevelSpacingX）
    QVector<double> levelX(levelMaxWidth.size(), 0.0);
    for (int d = 1; d < levelX.size(); ++d)
        levelX[d] = levelX[d - 1] + levelMaxWidth[d - 1] / 2.0 + Limits::kLevelSpacingX +
                    levelMaxWidth[d] / 2.0;

    // ---- R10均衡布局（§6.1）：按子树实际高度分侧与紧凑排布 ----
    // 子树布局结果：offsets为子树内各节点y相对子树根中心的偏移；top/bottom为每深度轮廓。
    // H(n)=max(节点高, ΣH(子)+24×(子数−1))在轮廓恒为满矩形时退化为该公式；
    // 高父超出子块等情形由逐深度轮廓直接涵盖，无需§6.1的事后冲突扩张循环。
    struct SubtreeResult
    {
        QVector<QPair<ID, double>> offsets;
        QVector<double> top;
        QVector<double> bottom;
    };
    // 把子树轮廓并入累计轮廓（center为子树根中心相对累计原点的偏移）
    const auto mergeInto = [&](QVector<double> &accTop, QVector<double> &accBottom, double center,
                               const QVector<double> &childTop, const QVector<double> &childBottom)
    {
        const qsizetype depth = qMax(accTop.size(), childTop.size());
        QVector<double> nextTop(depth), nextBottom(depth);
        for (int d = 0; d < depth; ++d)
        {
            const bool hasAcc = d < accTop.size();
            const bool hasChild = d < childTop.size();
            if (hasAcc && hasChild)
            {
                nextTop[d] = std::min(accTop.at(d), center + childTop.at(d));
                nextBottom[d] = std::max(accBottom.at(d), center + childBottom.at(d));
            }
            else if (hasChild)
            {
                nextTop[d] = center + childTop.at(d);
                nextBottom[d] = center + childBottom.at(d);
            }
            else
            {
                nextTop[d] = accTop.at(d);
                nextBottom[d] = accBottom.at(d);
            }
        }
        accTop = nextTop;
        accBottom = nextBottom;
    };
    // 递归构建子树：兄弟子树按每深度轮廓紧凑堆放（最小满足top(d)≥累计bottom(d)+间距），
    // 父居中于首末子中心均值（保持M1语义），最后平移使子树根中心为0
    const std::function<SubtreeResult(int)> buildSubtree = [&](int idx) -> SubtreeResult
    {
        const Item &node = items.at(idx);
        if (node.collapsed || node.children.isEmpty())
        {
            SubtreeResult leaf;
            leaf.offsets.append({node.id, 0.0});
            leaf.top.append(-node.height / 2.0);
            leaf.bottom.append(node.height / 2.0);
            return leaf;
        }
        QVector<double> accTop, accBottom;
        QHash<int, double> centers;
        QHash<int, SubtreeResult> childResults;
        for (int child : node.children)
        {
            SubtreeResult sub = buildSubtree(child);
            double center = 0.0;
            const qsizetype shared = qMin(accBottom.size(), sub.top.size());
            for (int d = 0; d < shared; ++d)
                center =
                    std::max(center, accBottom.at(d) + Limits::kSiblingSpacingY - sub.top.at(d));
            centers.insert(child, center);
            childResults.insert(child, sub);
            mergeInto(accTop, accBottom, center, sub.top, sub.bottom);
        }
        const double parentShift =
            -(centers.value(node.children.first()) + centers.value(node.children.last())) / 2.0;
        SubtreeResult result;
        result.offsets.append({node.id, 0.0});
        int deepest = 0;
        for (int child : node.children)
            deepest = std::max(deepest, static_cast<int>(childResults.value(child).top.size()));
        result.top.resize(deepest + 1);
        result.bottom.resize(deepest + 1);
        result.top[0] = -node.height / 2.0;
        result.bottom[0] = node.height / 2.0;
        for (int d = 1; d <= deepest; ++d)
        {
            double topAt = 0.0, bottomAt = 0.0;
            bool any = false;
            for (int child : node.children)
            {
                const SubtreeResult &sub = childResults.value(child);
                if (d - 1 >= sub.top.size())
                    continue;
                const double offset = centers.value(child) + parentShift;
                const double t = offset + sub.top.at(d - 1);
                const double b = offset + sub.bottom.at(d - 1);
                if (!any)
                {
                    topAt = t;
                    bottomAt = b;
                    any = true;
                }
                else
                {
                    topAt = std::min(topAt, t);
                    bottomAt = std::max(bottomAt, b);
                }
            }
            if (any)
            {
                result.top[d] = topAt;
                result.bottom[d] = bottomAt;
            }
        }
        for (int child : node.children)
        {
            const SubtreeResult &sub = childResults.value(child);
            const double offset = centers.value(child) + parentShift;
            for (const auto &pair : sub.offsets)
                result.offsets.append({pair.first, pair.second + offset});
        }
        return result;
    };

    // R10根直属分支分侧（§6.1）：已明确side的先固定；auto按树顺序依次放入累计子树高度
    // 较小的一侧，累计相等放右侧（确定性平局，保持"首分支在右"惯例）；
    // layout="right"时全部右侧（枚举：bilateral/right/fishbone，fishbone按bilateral处理）。
    // 分侧完成后各侧仍按(order, ID)原序排布。
    const bool rightOnly = sheet.layout == QStringLiteral("right");
    QVector<int> rightChildren, leftChildren;
    QHash<int, SubtreeResult> subtreeOf;
    if (rightOnly)
    {
        rightChildren = items.at(rootIndex).children;
    }
    else
    {
        for (int child : items.at(rootIndex).children)
            subtreeOf.insert(child, buildSubtree(child));
        const auto subtreeHeight = [&](int child)
        {
            const SubtreeResult &sub = subtreeOf.value(child);
            double top = 0.0, bottom = 0.0;
            for (int d = 0; d < sub.top.size(); ++d)
            {
                top = std::min(top, sub.top.at(d));
                bottom = std::max(bottom, sub.bottom.at(d));
            }
            return bottom - top;
        };
        double accRight = 0.0, accLeft = 0.0;
        QSet<int> rightSet, leftSet;
        for (int child : items.at(rootIndex).children)
        {
            const QString side = sheet.nodes.constFind(items.at(child).id).value().side;
            if (side == QStringLiteral("left"))
            {
                leftSet.insert(child);
                accLeft += subtreeHeight(child);
            }
            else if (side == QStringLiteral("right"))
            {
                rightSet.insert(child);
                accRight += subtreeHeight(child);
            }
        }
        for (int child : items.at(rootIndex).children)
        {
            const QString side = sheet.nodes.constFind(items.at(child).id).value().side;
            if (side == QStringLiteral("left") || side == QStringLiteral("right"))
                continue;
            if (accLeft < accRight)
            {
                leftSet.insert(child);
                accLeft += subtreeHeight(child);
            }
            else
            {
                rightSet.insert(child);
                accRight += subtreeHeight(child);
            }
        }
        for (int child : items.at(rootIndex).children)
        {
            if (rightSet.contains(child))
                rightChildren.append(child);
            else if (leftSet.contains(child))
                leftChildren.append(child);
        }
    }

    // 每侧自上而下轮廓紧凑堆放（同子树内部规则），整块按累计轮廓跨度居中于根轴
    // （§6.1"左右各自将子树按累计高度居中排布，避免只按节点个数平均"）；根固定在y=0
    QHash<ID, double> yById;
    yById.insert(sheet.rootId, 0.0);
    const auto layoutSide = [&](const QVector<int> &sideChildren)
    {
        if (sideChildren.isEmpty())
            return;
        QVector<double> accTop, accBottom;
        QHash<int, double> centers;
        for (int child : sideChildren)
        {
            if (!subtreeOf.contains(child))
                subtreeOf.insert(child, buildSubtree(child));
            const SubtreeResult &sub = subtreeOf.value(child);
            double center = 0.0;
            const qsizetype shared = qMin(accBottom.size(), sub.top.size());
            for (int d = 0; d < shared; ++d)
                center =
                    std::max(center, accBottom.at(d) + Limits::kSiblingSpacingY - sub.top.at(d));
            centers.insert(child, center);
            mergeInto(accTop, accBottom, center, sub.top, sub.bottom);
        }
        double top = 0.0, bottom = 0.0;
        for (int d = 0; d < accTop.size(); ++d)
        {
            top = std::min(top, accTop.at(d));
            bottom = std::max(bottom, accBottom.at(d));
        }
        const double shift = -(top + bottom) / 2.0;
        for (int child : sideChildren)
        {
            const double finalY = centers.value(child) + shift;
            for (const auto &pair : subtreeOf.value(child).offsets)
                yById.insert(pair.first, pair.second + finalY);
        }
    };
    layoutSide(rightChildren);
    layoutSide(leftChildren);

    // 侧向落位：右侧子树全右（正x），左侧子树全左（负x镜像）
    const auto markSide = [&](const QVector<int> &sideChildren, bool isRight)
    {
        QVector<int> stack = sideChildren;
        while (!stack.isEmpty())
        {
            const int current = stack.takeLast();
            Item &node = items[current];
            node.x = isRight ? levelX[node.depth] : -levelX[node.depth];
            for (int child : node.children)
                stack.append(child);
        }
    };
    markSide(rightChildren, true);
    markSide(leftChildren, false);

    // 输出（确定性顺序）：根 → 右侧子树前序DFS → 左侧子树前序DFS；只含可见节点
    QVector<NodePosition> result;
    int visibleCount = 0;
    for (const Item &item : items)
        if (item.visible)
            ++visibleCount;
    result.reserve(visibleCount);
    result.append(NodePosition{sheet.rootId, 0.0, 0.0});
    const auto appendSide = [&](const QVector<int> &sideChildren)
    {
        QVector<int> stack;
        for (int i = sideChildren.size() - 1; i >= 0; --i)
            stack.append(sideChildren.at(i));
        while (!stack.isEmpty())
        {
            const int current = stack.takeLast();
            const Item &node = items.at(current);
            if (node.visible)
                result.append(NodePosition{node.id, node.x, yById.value(node.id, 0.0)});
            for (int i = node.children.size() - 1; i >= 0; --i)
                stack.append(node.children.at(i));
        }
    };
    appendSide(rightChildren);
    appendSide(leftChildren);
    return result;
}

} // namespace TreeLayout

} // namespace lingya
