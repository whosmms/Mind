#include "charts/GanttLayout.h"

namespace lingya {

GanttLayout::GanttLayout(int pixelsPerDay)
    : m_pixelsPerDay(pixelsPerDay > 0 ? pixelsPerDay : kDefaultPixelsPerDay) {
}

GanttLayoutResult GanttLayout::compute(const QVector<Task>& tasks) const {
    GanttLayoutResult result;
    result.rowCount = tasks.size();

    QDate minDate;
    QDate maxDate;
    for (const Task& task : tasks) {
        if (task.startDate.has_value() && task.startDate->isValid() &&
            (!minDate.isValid() || *task.startDate < minDate)) {
            minDate = *task.startDate;
        }
        if (task.endDate.has_value() && task.endDate->isValid() &&
            (!maxDate.isValid() || *task.endDate > maxDate)) {
            maxDate = *task.endDate;
        }
    }

    result.bars.reserve(tasks.size());

    // 没有任何有效日期：仍返回行数，便于表格与时间轴行对齐；每条 bar 的 hasDates=false
    if (!minDate.isValid() || !maxDate.isValid()) {
        for (int row = 0; row < tasks.size(); ++row) {
            GanttBar bar;
            bar.taskId = tasks.at(row).id;
            bar.row = row;
            result.bars.append(bar);
        }
        return result;
    }

    // 含首尾两天：1 月 1 日到 1 月 1 日也算 1 天
    const int totalDays = static_cast<int>(minDate.daysTo(maxDate)) + 1;
    result.totalWidth = totalDays * m_pixelsPerDay;

    for (int row = 0; row < tasks.size(); ++row) {
        const Task& task = tasks.at(row);
        GanttBar bar;
        bar.taskId = task.id;
        bar.row = row;
        if (task.startDate.has_value() && task.startDate->isValid() &&
            task.endDate.has_value() && task.endDate->isValid()) {
            bar.hasDates = true;
            bar.startX = xForDate(*task.startDate, minDate);
            const int days = static_cast<int>(task.startDate->daysTo(*task.endDate)) + 1;
            bar.width = days * m_pixelsPerDay;
            bar.milestone = task.milestone;
        }
        result.bars.append(bar);
    }
    return result;
}

int GanttLayout::xForDate(const QDate& date, const QDate& rangeStart) const {
    return static_cast<int>(rangeStart.daysTo(date)) * m_pixelsPerDay;
}

QDate GanttLayout::dateForX(int x, const QDate& rangeStart) const {
    // 向下取整到天；x 为负（范围左侧）时也按整天回退
    const int days = x >= 0 ? x / m_pixelsPerDay
                            : -((-x + m_pixelsPerDay - 1) / m_pixelsPerDay);
    return rangeStart.addDays(days);
}

}  // namespace lingya
