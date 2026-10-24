#pragma once
#include <QUndoCommand>
#include "domain/Domain.h"

namespace lingya {


class ProjectDocument;

// 值类型快照命令（规范§5.1）：undo恢复before内容，redo恢复after内容，
// 两者都经ProjectDocument统一发changed信号。初版用值快照保证多对象事务一致；
// 命令保存稳定数据快照，不保存可能失效的图元地址（规范§2.3）。
class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(ProjectDocument* document, Project before, Project after,
                    const QString& label);

    void undo() override;
    void redo() override;

private:
    ProjectDocument* m_document;
    Project m_before;
    Project m_after;
};

}  // namespace lingya
