#pragma once

#include <QString>
#include <initializer_list>

namespace lingya::NodeShapes
{
inline constexpr const char *kCapsule = "capsule";
inline constexpr const char *kHexagon = "hexagon";
inline constexpr const char *kCutRectangle = "cutRect";
inline constexpr std::initializer_list<const char *> kIds{
    "roundRect", "rect", "ellipse", "diamond", "parallelogram", kCapsule, kHexagon, kCutRectangle};

inline bool isSupported(const QString &shape)
{
    for (const char *id : kIds)
    {
        if (shape == QLatin1StringView(id))
        {
            return true;
        }
    }
    return false;
}

// 流程图早期会存储但不显示普通节点的形状字段，新增选项才表示主动覆盖角色形状。
inline bool isFlowBorder(const QString &shape)
{
    return shape == QLatin1StringView(kCapsule) || shape == QLatin1StringView(kHexagon) ||
           shape == QLatin1StringView(kCutRectangle);
}
} // namespace lingya::NodeShapes
