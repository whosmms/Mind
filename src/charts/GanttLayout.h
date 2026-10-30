#pragma once
#include "domain/Domain.h"

#include <QDate>
#include <QVector>

namespace lingya {

// 甘特时间轴布局（AI规范§6.4）：把任务日期换算成时间轴坐标，纯计算、不绘图。
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
    QDate rangeStart;   // 时间轴起点日期；无有效日期时无效，可用于画刻度
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

// 拖拽改期（R13 编辑）：整体平移或只改一端
enum class GanttDragMode {
    Move,         // 整体平移
    ResizeStart,  // 只改开始日
    ResizeEnd     // 只改结束日
};

struct GanttDateRange {
    QDate start;
    QDate end;
};

// 按天偏移计算拖拽后的起止日期：Move 平移；ResizeStart/ResizeEnd 只改一端并保持 start<=end。
// 输入日期无效时原样返回，调用方可据此跳过。
GanttDateRange shiftTaskDates(const QDate& start, const QDate& end, int deltaDays,
                              GanttDragMode mode);

}  // namespace lingya
