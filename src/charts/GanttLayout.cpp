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
    result.rangeStart = minDate;

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

GanttDateRange shiftTaskDates(const QDate& start, const QDate& end, int deltaDays,
                              GanttDragMode mode) {
    if (!start.isValid() || !end.isValid()) {
        return GanttDateRange{start, end};
    }
    switch (mode) {
    case GanttDragMode::Move:
        return GanttDateRange{start.addDays(deltaDays), end.addDays(deltaDays)};
    case GanttDragMode::ResizeStart: {
        const QDate shifted = start.addDays(deltaDays);
        return GanttDateRange{shifted > end ? end : shifted, end};
    }
    case GanttDragMode::ResizeEnd: {
        const QDate shifted = end.addDays(deltaDays);
        return GanttDateRange{start, shifted < start ? start : shifted};
    }
    }
    return GanttDateRange{start, end};
}

int freeRouteX(const QVector<GanttBar>& bars, int preferredX, int rowFrom, int rowTo,
               int pixelsPerDay) {
    if (pixelsPerDay <= 0) {
        return preferredX;
    }
    const int lo = qMin(rowFrom, rowTo);
    const int hi = qMax(rowFrom, rowTo);
    const auto blocked = [&bars, lo, hi](int x) {
        for (const GanttBar& bar : bars) {
            if (!bar.hasDates || bar.row < lo || bar.row > hi) {
                continue;
            }
            if (x >= bar.startX && x <= bar.startX + bar.width) {
                return true;
            }
        }
        return false;
    };
    int x = preferredX;
    for (int attempt = 0; attempt < 12 && blocked(x); ++attempt) {
        x += pixelsPerDay;   // 向右挪一天，直到不再压住中间行的任务条
    }
    return x;
}

}  // namespace lingya
