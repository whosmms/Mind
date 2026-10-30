#include "layout/FlowLayout.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>

namespace lingya
{
namespace FlowLayout
{

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kLayerGap = 80.0;   // 相邻层的竖向净间距（规范§6.3分层结果按包围盒隔开）
constexpr double kNodeGap = 48.0;    // 同层相邻节点/分量的水平净间距
constexpr double kRingGap = 32.0;    // 回路环上相邻节点的净间距
constexpr int kBarycenterSweeps = 3; // 重心排序往返次数（固定次数保证确定性）

struct Extent
{
    double width = 0.0;
    double height = 0.0;
};

bool isPositiveFinite(double value)
{
    return std::isfinite(value) && value > 0.0;
}

// Tarjan求强连通分量（递归实现；工程节点上限1000，栈深度可接受）。
// 输出comp[v]=分量编号，编号按完成顺序递增（即压缩图的逆拓扑序）。
void strongConnect(int v, const QVector<QVector<int>> &adj, QVector<int> &index, QVector<int> &low,
                   QVector<int> &stack, QVector<bool> &onStack, int &nextIndex, QVector<int> &comp,
                   int &compCount)
{
    index[v] = low[v] = nextIndex++;
    stack.push_back(v);
    onStack[v] = true;
    for (const int w : adj.at(v)) {
        if (index.at(w) < 0) {
            strongConnect(w, adj, index, low, stack, onStack, nextIndex, comp, compCount);
            low[v] = std::min(low[v], low[w]);
        } else if (onStack.at(w)) {
            low[v] = std::min(low[v], index[w]);
        }
    }
    if (low[v] == index[v]) {
        while (true) {
            const int w = stack.takeLast();
            onStack[w] = false;
            comp[w] = compCount;
            if (w == v)
                break;
        }
        ++compCount;
    }
}

// 环半径：使环上相邻两节点的中心距不小于两者外接圆半径之和加净间距。
double ringRadius(const QVector<Extent> &members)
{
    const int k = members.size();
    if (k < 2)
        return 0.0;
    double maxHalfDiagonal = 0.0;
    for (const Extent &extent : members)
        maxHalfDiagonal = std::max(maxHalfDiagonal, std::hypot(extent.width, extent.height) / 2.0);
    const double needed = 2.0 * maxHalfDiagonal + kRingGap;
    return std::max(needed / (2.0 * std::sin(kPi / k)), maxHalfDiagonal);
}

} // namespace

std::variant<Result, AppError> layout(const FlowSheet &sheet, const QVector<NodeSize> &sizes)
{
    QHash<ID, Extent> dims;
    dims.reserve(sizes.size());
    for (const NodeSize &size : sizes)
        dims.insert(size.id, Extent{size.width, size.height});

    // 节点按ID升序遍历（QMap按键升序），保证后续所有排序都有稳定的起点
    QVector<ID> ids;
    QVector<Extent> extents;
    ids.reserve(sheet.nodes.size());
    extents.reserve(sheet.nodes.size());
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        const auto found = dims.constFind(it.key());
        if (found == dims.constEnd())
            return AppError{"NOT_FOUND", QStringLiteral("节点缺少尺寸：%1").arg(it.key())};
        if (!isPositiveFinite(found->width) || !isPositiveFinite(found->height))
            return AppError{"INVALID_ARGUMENT",
                            QStringLiteral("节点尺寸必须为有限正数：%1").arg(it.key())};
        ids.append(it.key());
        extents.append(*found);
    }

    Result out;
    if (ids.isEmpty())
        return out;

    QHash<ID, int> ordinal;
    ordinal.reserve(ids.size());
    for (int i = 0; i < ids.size(); ++i)
        ordinal.insert(ids.at(i), i);

    QVector<QVector<int>> adj(ids.size());
    for (const FlowEdge &edge : sheet.edges) {
        const auto source = ordinal.constFind(edge.sourceId);
        const auto target = ordinal.constFind(edge.targetId);
        if (source == ordinal.constEnd() || target == ordinal.constEnd())
            return AppError{"INVALID_ARGUMENT",
                            QStringLiteral("流程连线端点不存在：%1").arg(edge.id)};
        adj[*source].append(*target);
    }
    for (auto &list : adj) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }

    // 强连通分量：回路整体作为一个分量，内部环形排布（规范§6.3）
    QVector<int> index(ids.size(), -1);
    QVector<int> low(ids.size(), 0);
    QVector<int> comp(ids.size(), -1);
    QVector<bool> onStack(ids.size(), false);
    QVector<int> stack;
    int nextIndex = 0;
    int compCount = 0;
    for (int v = 0; v < ids.size(); ++v)
        if (index.at(v) < 0)
            strongConnect(v, adj, index, low, stack, onStack, nextIndex, comp, compCount);

    QVector<QVector<int>> members(compCount);
    for (int v = 0; v < ids.size(); ++v)
        members[comp.at(v)].append(v);   // 追加顺序即节点下标升序（=ID升序）

    QVector<Extent> compExtent(compCount);
    QVector<double> compRadius(compCount, 0.0);
    for (int c = 0; c < compCount; ++c) {
        double maxWidth = 0.0;
        double maxHeight = 0.0;
        QVector<Extent> memberExtents;
        for (const int v : members.at(c)) {
            maxWidth = std::max(maxWidth, extents.at(v).width);
            maxHeight = std::max(maxHeight, extents.at(v).height);
            memberExtents.append(extents.at(v));
        }
        const double radius = ringRadius(memberExtents);
        compRadius[c] = radius;
        compExtent[c] = Extent{2.0 * radius + maxWidth, 2.0 * radius + maxHeight};
    }

    // 压缩为无环图：去重、按分量编号升序，便于确定性拓扑排序
    QVector<QSet<int>> condAdjSet(compCount);
    for (int v = 0; v < ids.size(); ++v)
        for (const int w : adj.at(v))
            if (comp.at(v) != comp.at(w))
                condAdjSet[comp.at(v)].insert(comp.at(w));

    QVector<QVector<int>> condAdj(compCount);
    QVector<QVector<int>> revAdj(compCount);
    QVector<int> indegree(compCount, 0);
    for (int c = 0; c < compCount; ++c) {
        QVector<int> targets(condAdjSet.at(c).begin(), condAdjSet.at(c).end());
        std::sort(targets.begin(), targets.end());
        condAdj[c] = targets;
        for (const int w : targets) {
            ++indegree[w];
            revAdj[w].append(c);
        }
    }
    for (auto &list : revAdj)
        std::sort(list.begin(), list.end());

    // 拓扑顺序（最小分量编号优先），同时按最长路径定层级
    std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
    for (int c = 0; c < compCount; ++c)
        if (indegree.at(c) == 0)
            ready.push(c);

    QVector<int> topo;
    topo.reserve(compCount);
    QVector<int> layer(compCount, 0);
    while (!ready.empty()) {
        const int c = ready.top();
        ready.pop();
        topo.append(c);
        for (const int w : condAdj.at(c)) {
            layer[w] = std::max(layer.at(w), layer.at(c) + 1);
            if (--indegree[w] == 0)
                ready.push(w);
        }
    }
    if (topo.size() != compCount)
        return AppError{"INVALID_ARGUMENT", QStringLiteral("流程图分层失败：压缩图仍有环")};

    int maxLayer = 0;
    for (const int value : layer)
        maxLayer = std::max(maxLayer, value);

    QVector<QVector<int>> layers(maxLayer + 1);
    for (int c = 0; c < compCount; ++c)
        layers[layer.at(c)].append(c);
    for (auto &list : layers) {
        std::sort(list.begin(), list.end(), [&members](int left, int right) {
            return members.at(left).first() < members.at(right).first();
        });
    }

    // 重心排序减少交叉：以相邻层已定位置的邻居平均序号为键，往返固定次数
    const auto sweep = [&layers, &condAdj, &revAdj](bool downward) {
        const int layerTotal = layers.size();
        for (int step = 0; step < layerTotal; ++step) {
            const int li = downward ? step : layerTotal - 1 - step;
            const int ref = downward ? li - 1 : li + 1;
            if (ref < 0 || ref >= layerTotal)
                continue;
            QHash<int, int> position;
            const QVector<int> &reference = layers.at(ref);
            for (int i = 0; i < reference.size(); ++i)
                position.insert(reference.at(i), i);

            const QVector<QVector<int>> &neighbours = downward ? revAdj : condAdj;
            QVector<int> &current = layers[li];
            QVector<QPair<double, int>> keys;
            keys.reserve(current.size());
            for (int i = 0; i < current.size(); ++i) {
                const int c = current.at(i);
                double sum = 0.0;
                int count = 0;
                for (const int n : neighbours.at(c)) {
                    const auto found = position.constFind(n);
                    if (found != position.constEnd()) {
                        sum += *found;
                        ++count;
                    }
                }
                // 无邻居时保持当前位置，避免无关分量被推到层首
                keys.append({count > 0 ? sum / count : static_cast<double>(i), c});
            }
            std::stable_sort(keys.begin(), keys.end(),
                             [](const QPair<double, int> &left, const QPair<double, int> &right) {
                                 if (left.first != right.first)
                                     return left.first < right.first;
                                 return left.second < right.second;
                             });
            current.clear();
            current.reserve(keys.size());
            for (const auto &key : keys)
                current.append(key.second);
        }
    };
    for (int pass = 0; pass < kBarycenterSweeps; ++pass) {
        sweep(true);
        sweep(false);
    }

    // 层高与层心y：按每层最大高度累加，层间留固定净间距
    QVector<double> layerHeight(layers.size(), 0.0);
    for (int li = 0; li < layers.size(); ++li)
        for (const int c : layers.at(li))
            layerHeight[li] = std::max(layerHeight.at(li), compExtent.at(c).height);

    QVector<double> layerCenterY(layers.size(), 0.0);
    double cursorY = 0.0;
    for (int li = 0; li < layers.size(); ++li) {
        layerCenterY[li] = cursorY + layerHeight.at(li) / 2.0;
        cursorY += layerHeight.at(li) + kLayerGap;
    }

    // 层内水平排布：整层以x=0居中
    QVector<QPointF> centers(compCount);
    for (int li = 0; li < layers.size(); ++li) {
        const QVector<int> &current = layers.at(li);
        double total = 0.0;
        for (const int c : current)
            total += compExtent.at(c).width;
        total += kNodeGap * std::max(0, static_cast<int>(current.size()) - 1);
        double cursorX = -total / 2.0;
        for (const int c : current) {
            centers[c] = QPointF(cursorX + compExtent.at(c).width / 2.0, layerCenterY.at(li));
            cursorX += compExtent.at(c).width + kNodeGap;
        }
    }

    // 分量内：单节点取分量中心；回路成员按环形均布（自环视作单节点）
    QVector<NodePosition> positions(ids.size());
    for (int c = 0; c < compCount; ++c) {
        const QVector<int> &group = members.at(c);
        if (group.size() == 1) {
            const int v = group.first();
            positions[v] = NodePosition{ids.at(v), centers.at(c).x(), centers.at(c).y()};
            continue;
        }
        const double radius = compRadius.at(c);
        const double count = group.size();
        for (int i = 0; i < group.size(); ++i) {
            const double angle = -kPi / 2.0 + 2.0 * kPi * i / count;
            const int v = group.at(i);
            positions[v] = NodePosition{ids.at(v), centers.at(c).x() + radius * std::cos(angle),
                                        centers.at(c).y() + radius * std::sin(angle)};
        }
    }

    for (int c = 0; c < compCount; ++c) {
        if (members.at(c).size() < 2)
            continue;
        QVector<ID> group;
        for (const int v : members.at(c))
            group.append(ids.at(v));
        out.groups.append(group);
    }
    std::sort(out.groups.begin(), out.groups.end(),
              [](const QVector<ID> &left, const QVector<ID> &right) {
                  return left.first() < right.first();
              });

    out.positions = positions;
    out.componentCount = compCount;
    out.layerCount = layers.size();
    return out;
}

} // namespace FlowLayout
} // namespace lingya
