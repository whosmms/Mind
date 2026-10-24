#include "commands/SnapshotCommand.h"
#include "document/ProjectDocument.h"

namespace lingya {


SnapshotCommand::SnapshotCommand(ProjectDocument* document, Project before, Project after,
                                 const QString& label)
    : QUndoCommand(label),
      m_document(document),
      m_before(std::move(before)),
      m_after(std::move(after)) {}

// QUndoStack::push会自动调用第一次redo，因此构造时不修改文档（规范§5.1）
void SnapshotCommand::redo() {
    m_document->applySnapshot(m_after);
}

void SnapshotCommand::undo() {
    m_document->applySnapshot(m_before);
}

}  // namespace lingya
