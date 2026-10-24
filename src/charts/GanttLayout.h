#pragma once
#include "domain/Domain.h"

#include <QDate>
#include <QVector>

namespace lingya {

// 甘特时间轴布局（AI规范§9.2）：把任务日期换算成时间轴坐标，纯计算、不绘图。
// 输入任务列表，输出每行任务的横向位置与宽度；GanttView 据此绘制，
// 行序与左侧 TaskTableModel 一致（row = 输入下标）。
// 日期只用 QDate 的 daysTo/addDays 运算，禁止用毫秒差除以一天（S01）。
struct GanttBar {
    ID taskId;
    int row = 0;           // 与 TaskTableModel 的行一致
    int startX = 0;        // 相对时间轴起点（像素）
    int width = 0;         // 像素宽度；里程碑为 1 天
    bool milestone = false;
    bool hasDates = false; // false 时该行没有可画的任务条
};

struct GanttLayoutResult {
    QVector<GanttBar> bars;
    int rowCount = 0;   // 行数（= 任务数）
    int totalWidth = 0; // 时间轴总宽（像素）
};

class GanttLayout {
public:
    static constexpr int kDefaultPixelsPerDay = 24;

    explicit GanttLayout(int pixelsPerDay = kDefaultPixelsPerDay);

    // 计算布局：日期范围取所有任务的最小开始/最大结束（支持跨月、跨年）
    GanttLayoutResult compute(const QVector<Task>& tasks) const;

    // 日历日期 → 时间轴 x（相对 rangeStart，按整天）
    int xForDate(const QDate& date, const QDate& rangeStart) const;

    // 时间轴 x → 日历日期（按整天，用于点击/拖动定位；x 预期 >= 0）
    QDate dateForX(int x, const QDate& rangeStart) const;

private:
    int m_pixelsPerDay;
};

}  // namespace lingya
