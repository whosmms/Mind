#pragma once
#include "domain/Domain.h"

#include <QString>
#include <QVector>
#include <variant>

namespace lingya
{

// 流程图自动布局（规范§6.3）：确定性分层算法，纯数据服务，不依赖界面类型。
// 步骤：Tarjan求强连通分量 → 分量压缩为无环图 → 拓扑顺序+最长路径定层级 →
// 相邻层重心排序减少交叉 → 分量内部环形排布 → 按层包围盒隔开。
// 坐标系：层自上而下（y向下为正），每层整体以x=0居中；节点位置为中心点。
// 手写position属于用户数据，本服务只产出计算结果，由命令层一次写入（可撤销）。
// 流程图允许回路与孤立节点（规则3）：回路不视为错误，孤立节点独占一个分量、置于第0层。
// 确定性：输入相同则输出完全相同——节点按ID升序、邻接去重排序、分层与排序均带ID打破平局。
namespace FlowLayout
{

// 节点尺寸（甲按实际字体测量后提供，单位逻辑像素）
struct NodeSize
{
    ID id;
    double width = 0.0;
    double height = 0.0;
};

// 节点位置（中心点坐标）
struct NodePosition
{
    ID id;
    double x = 0.0;
    double y = 0.0;
};

// 布局结果
struct Result
{
    QVector<NodePosition> positions;   // 按节点ID升序，与输入节点集合一一对应
    QVector<QVector<ID>> groups;       // 节点数≥2的回路分组，组内按ID升序，组间按首元素升序
    int componentCount = 0;            // 压缩后的分量个数（=分层图中的弱连通块数）
    int layerCount = 0;                // 层数（空图为0）
};

// 计算整张流程图的节点位置。
// 失败（原数据不变）：NOT_FOUND=某节点缺少尺寸；INVALID_ARGUMENT=尺寸非正值/非有限、
// 连线端点不存在（防御性检查，正常工程已被ProjectValidator拒绝）。
std::variant<Result, AppError> layout(const FlowSheet &sheet, const QVector<NodeSize> &sizes);

} // namespace FlowLayout

} // namespace lingya
