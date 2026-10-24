# 丙交付的 UI 集成接口说明

> 用途：说明丙已完成的 4 个逻辑层模块（TaskCommands、TaskTableModel、CheckService、ReviewService）
> 如何接入甲/乙的界面。丙负责逻辑与模型，甲负责界面布局与绘制，乙负责数据/命令基础。
> 依据：AI 规范 §6.4/§7.1/§7.4/§9.2；三人规范 §2.2 文件归属。

## 1 已完成模块与集成归属

| 模块 | 文件 | 所在库 | 需要谁接 | 界面位置 |
|---|---|---|---|---|
| TaskCommands | `src/commands/TaskCommands.*` | lingya_appcore | 甲 | 任务面板/按钮 |
| TaskTableModel | `src/charts/TaskTableModel.*` | lingya_appcore | 甲 | 甘特页左侧 QTableView |
| CheckService | `src/features/CheckService.*` | lingya_core | 甲 | 检查侧栏（P06） |
| ReviewService | `src/features/ReviewService.*` | lingya_core | 甲 | 复习中心（P04） |

**构建无需改动**：以上已编入 `lingya_core` / `lingya_appcore`，甲/乙直接包含头文件即可。

## 2 TaskTableModel → QTableView（甘特任务表）

**谁接**：甲。**位置**：甘特图页面（P03）左侧。

```cpp
#include "charts/TaskTableModel.h"

// 构造
auto* model = new TaskTableModel(this);
auto* view = new QTableView(this);
view->setModel(model);
view->setSelectionBehavior(QAbstractItemView::SelectRows);
view->horizontalHeader()->setStretchLastSection(true);

// 数据刷新（文档改变后）
model->setTasks(document->project().tasks);

// 编辑回传：用户改单元格 → 交给 TaskCommands 提交（可撤销）
connect(model, &TaskTableModel::editRequested, this,
        [this](const ID& taskId, int column, const QVariant& value) {
            // column: TaskTableModel::Column
            // 按列构造 updateTask 并调用 TaskCommands（见 §3）
            Q_UNUSED(taskId); Q_UNUSED(column); Q_UNUSED(value);
        });
```

**列**：`TitleColumn`=0 名称、`OwnerColumn`=1 负责人、`StartColumn`=2 开始、
`EndColumn`=3 结束、`ProgressColumn`=4 进度（显示为 `xx%`，右对齐）。

**注意**：模型只展示 + 发编辑信号；**实际写入必须经 TaskCommands**，不要直接改模型内部 `m_tasks`。

## 3 TaskCommands → 任务面板/按钮

**谁接**：甲。构造时传入 `CommandService` 与 `ProjectDocument`：

```cpp
#include "commands/TaskCommands.h"

TaskCommands tasks(&commandService, &document);

// 新增（返回 ID 或 AppError）
auto created = tasks.addTask(QStringLiteral("新任务"));
// 修改（返回 bool 或 AppError）
tasks.updateTask(updatedTask);
// 删除（同时清理其他任务对它的前置依赖）
tasks.removeTask(taskId);
// 设为任务（X03 联动：source 指向节点，标题取节点当前标题）
tasks.linkTask(sheetId, nodeId);
// 解除关联（保留标题，清除 source）
tasks.unlinkTask(taskId);
```

**失败处理**：全部返回 `std::variant<ID/bool, AppError>`；失败时原数据不变、不入撤销栈。
用 `std::get_if<AppError>(&r)` 取错误，弹中文提示。

**约束**：全部经 CommandService 事务，自动可撤销；不要绕过它直接改 `Project.tasks`。

## 4 CheckService → 问题侧栏

**谁接**：甲。**位置**：检查侧栏（P06）。

```cpp
#include "features/CheckService.h"

const QVector<Issue> issues = CheckService::checkProject(document->project());
for (const Issue& issue : issues) {
    // issue.ruleId      C01..C09
    // issue.severity    IssueSeverity::Error / Warning / Suggestion
    // issue.sheetId     定位：切换到该工作表
    // issue.targetId    定位：选中/高亮该节点或任务
    // issue.message     中文问题描述
    // issue.suggestion  中文建议
    // issue.actionId    可选：可修复标识
}
```

**行为**：纯函数，返回已按严重度排序（错误→提醒→建议）。
建议在按钮点击或编辑后 500ms 防抖调用；点击问题时按 `sheetId` 切表、`targetId` 定位高亮。

## 5 ReviewService → 复习页

**谁接**：甲。**位置**：复习中心（P04）。

```cpp
#include "features/ReviewService.h"

// 由节点生成卡片（无内容返回空，不生成空卡）
auto card = ReviewService::createCard(document->project(), sheetId, nodeId);
if (card.has_value()) {
    // 加入 Project.cards（经命令/文档持久化，不要直接改 vector）
}

// 打开复习页时重新检测来源状态
for (auto& c : cards) {
    c.sourceState = ReviewService::detectSourceState(c, document->project());
}

// 用户在“来源变化/删除”时的选择：
ReviewService::refreshCard(card, document->project());   // 更新卡片（保留 ID/状态）
ReviewService::keepCustom(card);                         // 保留自定义 → mode=custom
ReviewService::makeStandalone(card);                     // 转独立卡片 → custom + missing
```

**注意**：卡片数据存在 `Project.cards`，持久化由乙的 ProjectJson/Command 负责；ReviewService 只算内容与状态。

## 6 边界（避免越界）

| 丙负责 | 甲负责 | 乙负责 |
|---|---|---|
| 模型/服务/规则/哈希 | 页面布局、QSS、拖拽、侧栏/页面装配 | 数据持久化、文件、命令基础、布局算法 |
| 接口契约与测试 | MainWindow 集成接线 | 构建、部署 |

**丙不写界面像素**。GanttView（时间轴自绘）是丙下一阶段的活，但需要甲/乙先确认在 `MainWindow`
中给它一个 `QGraphicsView` 位置；在此之前不阻塞其他工作。

## 7 待确认的接口问题

1. 甘特页面由甲创建：丙提供 `TaskTableModel`，甲挂到 `QTableView`；时间轴区域留给 `GanttView`。
2. 问题侧栏：点击 `Issue` 的定位方式（切表 + 选中 `targetId`）由甲实现。
3. 复习卡片存储：`Project.cards` 的新增/更新走哪个命令（乙的 CommandService 清单）？
4. `GanttView` 入口：甲/乙在 MainWindow 预留 `QGraphicsView` 后，丙实现自绘。
5. 以上任一接口有变化，先登记 IF 记录（三人规范 §2.3）再改。
