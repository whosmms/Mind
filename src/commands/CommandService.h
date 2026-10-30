#pragma once
#include <QObject>
#include <QUndoStack>
#include <functional>
#include <variant>
#include "domain/Domain.h"

namespace lingya {


class ProjectDocument;

// 统一修改入口（规范§5.1）：所有持久化修改经本服务，
// 流程为：基于当前Project产生候选副本 → 执行变更 → ProjectValidator校验 →
// 生成SnapshotCommand压入唯一QUndoStack；QUndoStack::push自动触发第一次redo，
// 因此不能先修改文档再push。校验失败不压入命令、不改变原数据。
// M0提供通用编辑事务支撑纵向闭环；M1起在此之上补充类型化命令：
// addChild、addSibling、renameNode、deleteSubtree、moveSubtree、updateNodeMeta、setStyle、
// addRelation、updateRelation、removeRelation、createAnnotation、updateAnnotation、addComment、
// resolveComment、pasteFragment、addTask、updateTask、linkTask、unlinkTask、applyAIMap、
// M3（乙，2026-10-30）：流程图命令见commands/FlowCommands（节点/连线/自动布局/批注），
// 工作表命令见commands/SheetCommands（新建/改名/换结构/删除）；
// saveTextDraft、changeSheetKind、deleteSheet（规范§5.1清单）。
class CommandService : public QObject {
    Q_OBJECT
public:
    using Edit = std::function<void(Project&)>;

    explicit CommandService(ProjectDocument* document, QObject* parent = nullptr);

    // 成功返回true；校验失败返回AppError，原数据不变
    std::variant<bool, AppError> applyEdit(const QString& label, Edit edit);

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    // 打开新工程时清空撤销历史并建立新会话标识（规范§5.1）
    void clearHistory();

private:
    ProjectDocument* m_document;
    QUndoStack m_stack;
};

}  // namespace lingya
