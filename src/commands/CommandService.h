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
// M0提供通用编辑事务支撑纵向闭环；M1起在此之上补充类型化命令（规范§5.1清单）：
// NodeCommands：addChild、addSibling、renameNode、deleteSubtree、moveSubtree、updateNodeMeta、
//   setStyle、setSheetStyle（工作表级默认样式）、addRelation/updateRelation/removeRelation、
//   createAnnotation/updateAnnotation/removeAnnotation、addComment、resolveComment、
//   copyFragment、pasteFragment（片段载荷见document/Fragment.h，
//   剪贴板格式application/x-lingya-fragment+json，合成根为保留UUID）；
// TaskCommands（丙）：addTask、updateTask、linkTask、unlinkTask；
// FlowCommands：流程图节点/连线/自动布局/批注；
// SheetCommands：createSheet、renameSheet、changeSheetKind、deleteSheet；
// AiCommands：applyAIMap（applyMapAsSheet/applyMapAsBranch）、saveTextDraft。
// 规范§5.1清单至M5已全部有实现；界面接线与工具栏/菜单入口仍由甲负责。
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
