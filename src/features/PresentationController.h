#pragma once
#include "domain/Domain.h"
#include <QElapsedTimer>
#include <variant>

namespace lingya
{
// 离线讲解会话：只持有步骤副本，计时和导航不写入工程。
class PresentationController
{
  public:
    static PresentationRoute defaultRoute(const TreeSheet &sheet);
    std::variant<int, AppError> start(const TreeSheet &sheet, const PresentationRoute &route);
    bool next();
    bool previous();
    void setPaused(bool paused);
    bool paused() const
    {
        return m_paused;
    }
    qint64 elapsedMilliseconds() const;
    int index() const
    {
        return m_index;
    }
    int count() const
    {
        return m_steps.size();
    }
    int skipped() const
    {
        return m_skipped;
    }
    PresentationStep current() const;

  private:
    QVector<PresentationStep> m_steps;
    int m_index = 0, m_skipped = 0;
    qint64 m_elapsed = 0;
    bool m_paused = true;
    QElapsedTimer m_clock;
};
} // namespace lingya
