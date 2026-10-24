#pragma once
#include "domain/Domain.h"

#include <QString>
#include <optional>

namespace lingya {

// 复习卡片服务（X01，AI规范§7.1）：由节点提取正反面、计算来源哈希、检测来源变化。
// 纯逻辑（QtCore），不依赖界面；不声明未实现的间隔重复算法。
class ReviewService {
public:
    // 由节点生成新卡片：正面=节点标题，背面=备注或直属子节点标题；
    // 无可提取内容时返回 nullopt（不生成空卡）。
    static std::optional<ReviewCard> createCard(const Project& project, const ID& sheetId,
                                                const ID& nodeId);

    // 来源哈希：标题 + 备注 + 用于答案的直属子内容，规范化后 SHA-256。
    static QString sourceHash(const TreeNode& node, const TreeSheet& sheet);

    // 来源状态：ok / changed / missing（打开复习页或重开时重新计算）。
    static QString detectSourceState(const ReviewCard& card, const Project& project);

    // 用户选择“更新卡片”时：按当前源重算正反面与哈希，保留卡片 ID 与学习状态。
    static std::optional<ReviewCard> refreshCard(const ReviewCard& card, const Project& project);

    // 源变化后用户选择“保留自定义”：不再跟随源，mode 改为 custom。
    static void keepCustom(ReviewCard& card);

    // 源删除后用户选择“转独立卡片”：保留卡片与来源记录，标记为独立（custom + missing）。
    static void makeStandalone(ReviewCard& card);
};

}  // namespace lingya
