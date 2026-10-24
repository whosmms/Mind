#include "document/ProjectDocument.h"
#include <QFile>
namespace {
void dbgTrace3(const QString& msg) {
    QFile f(QStringLiteral("D:/mind/app/docs/chain_trace.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append))
        f.write(msg.toUtf8());
}
}  // namespace

namespace lingya {


ProjectDocument::ProjectDocument(QObject* parent) : QObject(parent) {}

const Project& ProjectDocument::project() const {
    return m_project;
}

Project ProjectDocument::snapshot() const {
    return m_project;
}

qint64 ProjectDocument::sessionRevision() const {
    return m_sessionRevision;
}

bool ProjectDocument::isModified() const {
    return m_dirty;
}

void ProjectDocument::replaceProject(Project next) {
    // 打开新工程建立新会话；撤销历史由CommandService::clearHistory清空（§5.1）
    applySnapshot(std::move(next));
}

void ProjectDocument::applySnapshot(Project next) {
    // 只变更数据，不在此处发信号：QUndoStack::push/undo/redo的重放栈内canUndo尚未就绪，
    // changed由CommandService在栈操作完成后经notifyChanged()统一发出（见D-M0-13）
    m_project = std::move(next);
    ++m_sessionRevision;   // 编辑、撤销、重做、替换工程都递增，拒绝迟到结果的依据（§5.1）
    m_dirty = true;
}

void ProjectDocument::notifyChanged() {
    // 统一的变更通知出口：调用时机=栈/保存操作完成后，保证槽内canUndo/canRedo已就绪（D-M0-13）
    emit changed(m_sessionRevision);
}

void ProjectDocument::setClean() {
    // M0简化：由保存流程在保存成功后立即调用（单线程，期间不会有新编辑）。
    // M1补充：记录clean时的sessionRevision，保存期间发生新编辑则不清除脏标记（§5.1）。
    // 不在此处发信号：由调用方在完成状态变更后统一notifyChanged()。
    m_dirty = false;
}

}  // namespace lingya
