#pragma once
#include "domain/Domain.h"

#include <QString>
#include <QVector>
#include <variant>

namespace lingya
{

// 鱼骨图布局（规范§6.2，R11）：问题根节点是鱼头，一级节点是原因类别，二级及以后是原因。
// 主轴水平，鱼头在右、骨向左延伸；类别在上下交替的斜骨上，原因从斜骨向外分支。
// 纯数据服务：甲按实际字体测量提供节点尺寸，本服务只算位置（坐标不写入业务数据，§13）。
// 分支包围盒决定沿主轴占用的长度，类别增多时向左扩展画布而不是压缩字号。
// 确定性：同样输入得到完全相同的输出序列；同级按(order, ID)排序，与QMap遍历顺序无关。
namespace FishboneLayout
{

struct NodeSize
{
    ID id;
    double width = 0.0;
    double height = 0.0;
};

// 节点位置（中心点坐标；鱼头固定在原点）
struct NodePosition
{
    ID id;
    double x = 0.0;
    double y = 0.0;
};

struct Result
{
    QVector<NodePosition> positions;   // 鱼头 → 类别及其原因（同级按order,ID）
    QVector<QVector<ID>> bones;        // 每条主骨：[类别ID, 其可见原因…]；与positions同序生成
};

// 计算鱼骨图可见节点的位置。collapsed的类别/原因不参与布局（其数据保留）。
// 失败（原数据不变）：INVALID_ARGUMENT=不是鱼骨工作表、根不存在、存在无法从根到达的节点；
// NOT_FOUND=某节点缺少尺寸。
std::variant<Result, AppError> layout(const TreeSheet &sheet, const QVector<NodeSize> &sizes);

} // namespace FishboneLayout

} // namespace lingya
