#include "document/ProjectDocument.h"

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
    m_project = std::move(next);
    ++m_sessionRevision;   // 编辑、撤销、重做、替换工程都递增，拒绝迟到结果的依据（§5.1）
    m_dirty = true;
    emit changed(m_sessionRevision);
}

void ProjectDocument::setClean() {
    // M0简化：由保存流程在保存成功后立即调用（单线程，期间不会有新编辑）。
    // M1补充：记录clean时的sessionRevision，保存期间发生新编辑则不清除脏标记（§5.1）。
    if (m_dirty) {
        m_dirty = false;
        emit changed(m_sessionRevision);
    }
}

}  // namespace lingya
