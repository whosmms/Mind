#include "commands/CommandService.h"
#include "commands/SnapshotCommand.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include "domain/ProjectValidator.h"

namespace lingya {


CommandService::CommandService(ProjectDocument* document, QObject* parent)
    : QObject(parent), m_document(document) {
    // 历史最多50步（规范§5.1）；超出由QUndoStack自动丢弃最旧命令
    m_stack.setUndoLimit(Limits::kHistoryMaxSteps);
    // M1补充：快照累计内存64MiB预算检查，压入前超预算则提示并清空旧撤销历史后继续（规范§5.1）
}

std::variant<bool, AppError> CommandService::applyEdit(const QString& label, Edit edit) {
    // 先基于当前Project产生候选副本，不在原文档上直接修改（规范§5.1）
    Project candidate = m_document->project();
    edit(candidate);

    const QVector<AppError> problems = ProjectValidator::validate(candidate);
    if (!problems.isEmpty())
        return problems.first();   // 失败不压入命令，不改变原数据

    // push自动调用第一次redo，由命令把after内容应用到文档（规范§5.1 [Q4]）
    m_stack.push(new SnapshotCommand(m_document, m_document->project(), candidate, label));
    return true;
}

bool CommandService::canUndo() const {
    return m_stack.canUndo();
}

bool CommandService::canRedo() const {
    return m_stack.canRedo();
}

void CommandService::undo() {
    if (m_stack.canUndo()) m_stack.undo();
}

void CommandService::redo() {
    if (m_stack.canRedo()) m_stack.redo();
}

void CommandService::clearHistory() {
    m_stack.clear();
}

}  // namespace lingya
