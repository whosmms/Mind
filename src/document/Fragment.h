#pragma once
// R03 复制粘贴的片段载荷（规范§5.3）：自包含的节点子树与内部引用，
// 不含图元指针、QWidget或任何界面类型，可安全跨工作表、跨工程传递。
// 剪贴板格式 application/x-lingya-fragment+json 的载体：toJson/fromJson。
// 语义约束（规范§5.3）：只带内部关系、内部外框/概要、挂在片段节点上的批注与任务副本；
// 复习记录与讲解路线不随节点复制；粘贴由命令层重新生成全部ID并重映射内部引用。
#include "domain/Domain.h"

#include <QJsonObject>
#include <variant>

namespace lingya {

struct Fragment {
    static constexpr int kVersion = 1;
    // 系统剪贴板格式标识（规范§5.3）
    static constexpr const char* kMimeType = "application/x-lingya-fragment+json";

    int fragmentVersion = kVersion;
    QVector<ID> rootIds;                    // 顶层根，多选时已折叠为最高层，按树顺序
    QMap<ID, TreeNode> nodes;               // 根与全部后代；根的parentId为空
    QVector<Relation> relations;            // 仅两端都在片段内的关系线
    QVector<GroupAnnotation> annotations;   // 仅父级与成员都在片段内的外框/概要
    QVector<Comment> comments;              // 仅指向片段节点的批注
    QVector<Task> tasks;                    // 关联任务副本，source已指向片段内节点

    bool isEmpty() const;

    // 剪贴板载荷。内部以“单张树图 + 合成根”的载体工程复用ProjectJson的严格序列化，
    // 合成根与载体工程字段不会出现在片段数据中。
    QJsonObject toJson() const;
    static std::variant<Fragment, AppError> fromJson(const QJsonObject& object);
};

}  // namespace lingya
