#include "layout/FishboneLayout.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace lingya
{
namespace FishboneLayout
{

namespace
{

constexpr double kHeadGap = 48.0;    // 鱼头与第一根主骨的水平间距
constexpr double kSlotGap = 40.0;    // 相邻主骨沿主轴的最小净间距
constexpr double kSpineGap = 72.0;   // 主轴到类别节点的竖向距离（斜骨长度）
constexpr double kReasonGap = 20.0;  // 原因之间的竖向净间距
constexpr double kDepthShift = 56.0; // 每深入一层向左偏移，形成向外分支

struct Extent
{
    double width = 0.0;
    double height = 0.0;
};

bool isPositiveFinite(double value)
{
    return std::isfinite(value) && value > 0.0;
}

// 可见子节点：collapsed只隐藏“子节点”，节点自身仍显示，因此这里不过滤collapsed
QVector<QPair<int, ID>> childList(const TreeSheet &sheet, const ID &nodeId)
{
    QVector<QPair<int, ID>> children;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        if (!it.value().parentId.has_value() || *it.value().parentId != nodeId)
            continue;
        children.append({it.value().order, it.key()});
    }
    std::sort(children.begin(), children.end(),
              [](const QPair<int, ID> &left, const QPair<int, ID> &right) {
                  if (left.first != right.first)
                      return left.first < right.first;
                  return left.second < right.second;
              });
    return children;
}

bool isCollapsed(const TreeSheet &sheet, const ID &nodeId)
{
    const auto it = sheet.nodes.constFind(nodeId);
    return it != sheet.nodes.constEnd() && it->collapsed;
}

// 子树沿“向外轴”占用的跨度：自身高度 + 每个子块的跨度 + 每个间隙。
// 节点位于自身块的内侧边缘、子块依次向外排列，因此兄弟块之间不会重叠。
double span(const TreeSheet &sheet, const ID &nodeId, const QHash<ID, Extent> &dims)
{
    const double selfHeight = dims.value(nodeId).height;
    if (isCollapsed(sheet, nodeId))
        return selfHeight;
    const QVector<QPair<int, ID>> children = childList(sheet, nodeId);
    if (children.isEmpty())
        return selfHeight;
    double total = 0.0;
    for (const auto &child : children)
        total += span(sheet, child.second, dims);
    total += kReasonGap * children.size();   // 父到首子 + 兄弟之间
    return selfHeight + total;
}

// 各层最大宽度：决定该主骨沿主轴需要占用的长度（分支包围盒决定骨间距）
void measure(const TreeSheet &sheet, const ID &nodeId, const QHash<ID, Extent> &dims, int depth,
             QVector<double> &widthByDepth)
{
    if (widthByDepth.size() <= depth)
        widthByDepth.resize(depth + 1, 0.0);
    widthByDepth[depth] = std::max(widthByDepth.at(depth), dims.value(nodeId).width);
    if (isCollapsed(sheet, nodeId))
        return;
    for (const auto &child : childList(sheet, nodeId))
        measure(sheet, child.second, dims, depth + 1, widthByDepth);
}

void placeSubtree(const TreeSheet &sheet, const ID &nodeId, const QPointF &center, int side,
                  const QHash<ID, Extent> &dims, QVector<NodePosition> &out, QVector<ID> &bone)
{
    out.append(NodePosition{nodeId, center.x(), center.y()});
    bone.append(nodeId);
    if (isCollapsed(sheet, nodeId))
        return;

    const double selfHeight = dims.value(nodeId).height;
    double outer = center.y() + side * (selfHeight / 2.0);
    for (const auto &child : childList(sheet, nodeId)) {
        outer += side * kReasonGap;
        const double childSpan = span(sheet, child.second, dims);
        const QPointF childCenter(center.x() - kDepthShift,
                                  outer + side * (dims.value(child.second).height / 2.0));
        placeSubtree(sheet, child.second, childCenter, side, dims, out, bone);
        outer += side * childSpan;
    }
}

} // namespace

std::variant<Result, AppError> layout(const TreeSheet &sheet, const QVector<NodeSize> &sizes)
{
    if (sheet.kind != QStringLiteral("fishbone"))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("该工作表不是鱼骨图：%1").arg(sheet.name)};
    if (!sheet.nodes.contains(sheet.rootId))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("鱼骨图根节点不存在")};

    QHash<ID, Extent> dims;
    dims.reserve(sizes.size());
    for (const NodeSize &size : sizes) dims.insert(size.id, Extent{size.width, size.height});

    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        const auto found = dims.constFind(it.key());
        if (found == dims.constEnd())
            return AppError{"NOT_FOUND", QStringLiteral("节点缺少尺寸：%1").arg(it.key())};
        if (!isPositiveFinite(found->width) || !isPositiveFinite(found->height))
            return AppError{"INVALID_ARGUMENT",
                            QStringLiteral("节点尺寸必须为有限正数：%1").arg(it.key())};
    }

    // 可达性检查：孤儿或环（正常工程已被ProjectValidator拒绝，这里只做防御）
    QHash<ID, int> reachable;
    QVector<ID> queue{sheet.rootId};
    reachable.insert(sheet.rootId, 1);
    while (!queue.isEmpty()) {
        const ID current = queue.takeLast();
        for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
            if (!it.value().parentId.has_value() || *it.value().parentId != current)
                continue;
            if (reachable.contains(it.key()))
                continue;
            reachable.insert(it.key(), 1);
            queue.append(it.key());
        }
    }
    if (reachable.size() != sheet.nodes.size())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("鱼骨图存在无法从根到达的节点")};

    Result result;
    const Extent headExtent = dims.value(sheet.rootId);
    result.positions.append(NodePosition{sheet.rootId, 0.0, 0.0});

    // 鱼头固定在原点，主轴向左延伸；类别上下交替
    double cursorX = -(headExtent.width / 2.0 + kHeadGap);
    int index = 0;
    for (const auto &category : childList(sheet, sheet.rootId)) {
        const ID categoryId = category.second;
        QVector<double> widthByDepth;
        measure(sheet, categoryId, dims, 0, widthByDepth);
        double slotWidth = 0.0;
        for (int depth = 0; depth < widthByDepth.size(); ++depth)
            slotWidth += widthByDepth.at(depth) + kDepthShift;
        slotWidth = std::max(slotWidth, dims.value(categoryId).width + kSlotGap) + kSlotGap;

        const double centerX = cursorX - slotWidth / 2.0;
        cursorX -= slotWidth;

        const int side = (index % 2 == 0) ? -1 : 1;   // 偶数在上（负y），奇数在下（正y）
        const double centerY = side * (kSpineGap + dims.value(categoryId).height / 2.0);

        QVector<NodePosition> subtreePositions;
        QVector<ID> bone;
        placeSubtree(sheet, categoryId, QPointF(centerX, centerY), side, dims, subtreePositions,
                     bone);
        for (const NodePosition &position : subtreePositions)
            result.positions.append(position);
        result.bones.append(bone);
        ++index;
    }
    return result;
}

} // namespace FishboneLayout
} // namespace lingya
