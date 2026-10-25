#pragma once
#include "domain/Domain.h"
#include <QHash>
#include <QString>
#include <QVector>
#include <variant>

namespace lingya
{

// 简化树布局（D-M1-06最小布局接口，M1第一批；完整均衡布局R10按M2推进）。
// 纯数据服务：甲按实际字体测量提供节点尺寸（NodeSize），本服务计算位置（NodePosition），
// 坐标不存为业务数据、不反向修改标题或任务（规范§13）；不依赖任何界面类型。
// 确定性：同样输入得到完全相同的输出序列（§13）；同级排序用(order, ID)打破平局。
// 只做几何排布：collapsed子树隐藏不参与；侧向由TreeNode.side（left/right/auto）与
// TreeSheet.layout（bilateral/right）决定；根固定在原点(0,0)。
namespace TreeLayout
{

// 节点尺寸（甲测量后提供；宽度已含内边距，单位逻辑像素）
struct NodeSize
{
    ID id;
    double width = 0.0;
    double height = 0.0;
};

// 节点位置（中心点坐标，根在原点；右向为正x，下向为正y）
struct NodePosition
{
    ID id;
    double x = 0.0;
    double y = 0.0;
};

// 计算一棵树图的可见节点位置。
// 输入：sheet（父子/order/side/collapsed/layout取自节点数据）+ 全部节点的尺寸表。
// 输出：可见节点（含根，不含collapsed隐藏子树）的位置，顺序固定为
//       根 → 右侧子树DFS（同级按order,ID）→ 左侧子树DFS。
// 失败（原数据不变）：NOT_FOUND=某节点缺少尺寸；INVALID_ARGUMENT=尺寸非正值、
// 根不存在或存在无法从根到达的节点（孤儿/环，防御性检查——正常工程已被ProjectValidator拒绝）。
std::variant<QVector<NodePosition>, AppError> layout(const TreeSheet &sheet,
                                                     const QVector<NodeSize> &sizes);

} // namespace TreeLayout

} // namespace lingya
