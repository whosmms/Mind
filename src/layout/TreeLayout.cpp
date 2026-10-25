#include "layout/TreeLayout.h"
#include "domain/Limits.h"
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

    // 根的可见子节点分侧：side强制left/right，auto按序交替（首子右侧）；
    // layout="right"时全部右侧（TreeSheet.layout枚举：bilateral/right/fishbone，fishbone按bilateral处理）
    const bool rightOnly = sheet.layout == QStringLiteral("right");
    QVector<int> rightChildren, leftChildren;
    int autoIndex = 0;
    for (int child : items.at(rootIndex).children)
    {
        const QString side = sheet.nodes.constFind(items.at(child).id).value().side;
        if (rightOnly || side == QStringLiteral("right"))
            rightChildren.append(child);
        else if (side == QStringLiteral("left"))
            leftChildren.append(child);
        else if (autoIndex % 2 == 0)
            rightChildren.append(child);
        else
            leftChildren.append(child);
        ++autoIndex;
    }

    // 纵向排布（每侧独立）：叶子占自身高度；父中心=首末子中心均值，子树高=子块高与自身高的较大者；
    // 兄弟之间留kSiblingSpacingY。返回子树占据的高度。
    const std::function<double(int, double)> assignY = [&](int idx, double top) -> double
    {
        Item &node = items[idx];
        if (node.collapsed || node.children.isEmpty())
        {
            node.y = top + node.height / 2.0;
            return node.height;
        }
        double cursor = top;
        for (int i = 0; i < node.children.size(); ++i)
        {
            if (i > 0)
                cursor += Limits::kSiblingSpacingY;
            cursor += assignY(node.children.at(i), cursor);
        }
        node.y = (items[node.children.first()].y + items[node.children.last()].y) / 2.0;
        return std::max(cursor - top, node.height);
    };
    // 每侧自上而下顺序铺开：游标依次推进，兄弟之间留kSiblingSpacingY
    const auto layoutSide = [&](const QVector<int> &sideChildren)
    {
        double cursor = 0.0;
        for (int i = 0; i < sideChildren.size(); ++i)
        {
            if (i > 0)
                cursor += Limits::kSiblingSpacingY;
            cursor += assignY(sideChildren.at(i), cursor);
        }
    };
    layoutSide(rightChildren);
    layoutSide(leftChildren);

    // 根的y：有可见子时取全部根子（两侧合并）首末中心均值，否则取自身半高
    if (items[rootIndex].collapsed || items.at(rootIndex).children.isEmpty())
    {
        items[rootIndex].y = items[rootIndex].height / 2.0;
    }
    else
    {
        double firstY = 0.0, lastY = 0.0;
        bool haveVisible = false;
        for (int child : items.at(rootIndex).children)
        {
            if (!items[child].visible)
                continue;
            if (!haveVisible)
            {
                firstY = items[child].y;
                haveVisible = true;
            }
            lastY = items[child].y;
        }
        items[rootIndex].y = haveVisible ? (firstY + lastY) / 2.0 : items[rootIndex].height / 2.0;
    }

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
    const double rootY = items[rootIndex].y;
    result.append(NodePosition{sheet.rootId, 0.0, items[rootIndex].y - rootY});
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
                result.append(NodePosition{node.id, node.x, node.y - rootY});
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
