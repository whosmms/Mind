#pragma once
#include <QtGlobal>

namespace lingya {


// 上限与默认值（规范§4.3、§5.1、§5.2、§6.1、§7.4、§10.6）
// 常量集中一处，校验器、命令、界面与AI限制共用，避免各处出现不一致的魔法数字。
// 文本长度按Unicode码点计数；UTF-8字节数只用于文件/响应上限（规范§4.2）。
namespace Limits {

// 文件与工程规模（规范§4.3.9）
constexpr int kSchemaVersion = 1;
constexpr int kMaxNodes = 1000;                              // 工程节点硬上限
constexpr int kAcceptanceNodes = 300;                        // 常规验收单图规模
constexpr int kFirstTestNodes = 100;                         // 首次实现先测规模
constexpr int kMaxSheets = 20;
constexpr qint64 kMaxFileBytes = qint64(10) * 1024 * 1024;   // 10 MiB

// 最近文件（原设计§8 P01；设计规范未定数量，M1登记为项目决定，改动需同步设置服务与测试）
constexpr int kMaxRecentFiles = 10;

// 恢复快照（规范§11.2上限表、§11.3；原设计§8：内容变化时约30秒写一份，每工程保留最近5份）
constexpr int kMaxRecoverySnapshots = 5;
constexpr int kRecoverySnapshotIntervalMs = 30 * 1000;

// 文本长度，按Unicode码点计数（规范§4.3.8）
constexpr int kTitleMinChars = 1;
constexpr int kTitleMaxChars = 2000;
constexpr int kNoteMaxChars = 20000;
constexpr int kMaxTags = 20;
constexpr int kTagMaxChars = 30;
constexpr int kCommentMaxChars = 2000;

// 节点测量与布局间距（规范§6.1）
constexpr double kNodeWidthMin = 120.0;
constexpr double kNodeWidthMax = 280.0;
constexpr double kNodePadding = 16.0;      // 节点与外框内边距
constexpr double kLevelSpacingX = 80.0;    // 父子水平间距
constexpr double kSiblingSpacingY = 24.0;  // 同级竖向最小间距

// 样式（规范§4.3.10、§9）
constexpr int kFontSizeMin = 12;
constexpr int kFontSizeMax = 32;

// 命令与历史（规范§5.1）
constexpr int kHistoryMaxSteps = 50;
constexpr qint64 kHistoryMemoryBudget = qint64(64) * 1024 * 1024;   // 快照累计内存预算

// 视图（规范§5.2）
constexpr double kZoomMin = 0.25;
constexpr double kZoomMax = 2.0;

// AI输入输出限制（规范§10.6，应用首版限制，与模型官方上下文上限不同）
constexpr int kAiTextMaxChars = 12000;
constexpr int kAiRequirementMaxChars = 1000;
constexpr int kAiMapMaxNodes = 50;
constexpr int kAiMapMaxDepth = 4;
constexpr int kAiNodeTitleMaxChars = 80;
constexpr int kAiNoteMaxChars = 500;
constexpr int kAiDraftMaxNodes = 100;
constexpr int kAiDraftMaterialMaxChars = 20000;
constexpr qint64 kAiResponseMaxBytes = 1024 * 1024;   // 输出整体上限1MiB
constexpr int kAiTimeoutMs = 90 * 1000;               // 单次请求90秒截止
constexpr int kAiTotalBudgetMs = 180 * 1000;          // 整个生成180秒总预算

// 检查助手提醒阈值（规范§7.4 C02）
constexpr int kCheckTitleChars = 80;
constexpr int kCheckMaxDepth = 8;

}  // namespace Limits

}  // namespace lingya
