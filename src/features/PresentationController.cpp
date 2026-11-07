#include "features/PresentationController.h"
#include <QUuid>
#include <algorithm>

namespace lingya
{
PresentationRoute PresentationController::defaultRoute(const TreeSheet &sheet)
{
    PresentationRoute route;
    route.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    route.sheetId = sheet.id;
    route.title = sheet.name + QStringLiteral("讲解");
    QVector<TreeNode> children;
    for (const auto &node : sheet.nodes)
        if (node.parentId && *node.parentId == sheet.rootId)
            children.append(node);
    std::sort(children.begin(), children.end(), [](const TreeNode &a, const TreeNode &b)
              { return a.order == b.order ? a.id < b.id : a.order < b.order; });
    const auto add = [&route](const ID &id)
    { route.steps.append({QUuid::createUuid().toString(QUuid::WithoutBraces), id, {}}); };
    if (sheet.nodes.contains(sheet.rootId))
        add(sheet.rootId);
    for (const auto &node : children)
        add(node.id);
    return route;
}

std::variant<int, AppError> PresentationController::start(const TreeSheet &sheet,
                                                          const PresentationRoute &route)
{
    m_steps.clear();
    m_skipped = 0;
    m_index = 0;
    m_elapsed = 0;
    m_paused = true;
    if (route.sheetId != sheet.id)
        return AppError{"INVALID_ARGUMENT", QStringLiteral("讲解路线与工作表不匹配")};
    for (const auto &step : route.steps)
    {
        if (sheet.nodes.contains(step.nodeId))
            m_steps.append(step);
        else
            ++m_skipped;
    }
    if (m_steps.isEmpty())
        return AppError{"NOT_FOUND", QStringLiteral("没有可讲解的步骤，请添加仍存在的主题")};
    m_paused = false;
    m_clock.start();
    return int(m_steps.size());
}
bool PresentationController::next()
{
    if (m_index + 1 >= m_steps.size())
        return false;
    ++m_index;
    return true;
}
bool PresentationController::previous()
{
    if (m_index == 0)
        return false;
    --m_index;
    return true;
}
void PresentationController::setPaused(bool value)
{
    if (m_paused == value || m_steps.isEmpty())
        return;
    if (value)
        m_elapsed += m_clock.elapsed();
    else
        m_clock.start();
    m_paused = value;
}
qint64 PresentationController::elapsedMilliseconds() const
{
    return m_elapsed + ((!m_paused && m_clock.isValid()) ? m_clock.elapsed() : 0);
}
PresentationStep PresentationController::current() const
{
    return m_steps.isEmpty() ? PresentationStep{} : m_steps.at(m_index);
}
} // namespace lingya
