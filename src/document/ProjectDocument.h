#pragma once
#include <QObject>
#include "domain/Domain.h"

namespace lingya {


class ProjectDocument;

// 唯一正在编辑的工程数据源（规范§3）：ProjectDocument是唯一数据源，所有视图都读它。
// M0范围：持有Project值、单调递增的sessionRevision、脏标记与changed信号。
// M1补充：视口变化单独记录不触发内容历史（§5.1）；setClean的“保存期间无新编辑”修订比对。
class ProjectDocument : public QObject {
    Q_OBJECT
public:
    explicit ProjectDocument(QObject* parent = nullptr);

    const Project& project() const;
    Project snapshot() const;          // 值拷贝，不暴露可写引用（规范§11）
    qint64 sessionRevision() const;    // 每次编辑、撤销、重做或替换工程都递增，不随历史回退（§5.1）
    bool isModified() const;

    // 新建、打开、恢复后整体替换；调用方随后清空撤销历史（§5.1）
    // 注意：只变更数据不发信号，完成后由调用方notifyChanged()统一通知（D-M0-13）
    void replaceProject(Project next);

    // 保存成功后由保存流程调用；失败不能清除脏标记（§5.1）。不发信号，由调用方notifyChanged()
    void setClean();

    // 切换当前工作表（视图定位，不是内容编辑）：只改activeSheetId、置脏并notifyChanged()；
    // 不进撤销历史、不递增sessionRevision——否则Ctrl+Z撤的是“切换工作表”、剪切令牌
    // （按sessionRevision比对）与AI提案的expectedRevision会被切表误判失效（§5.1、D-M1-08）。
    // 表不存在返回false且不改数据；与当前相同则短路（返回true，不通知）。
    bool setActiveSheet(const ID& sheetId);

    // 变更通知唯一出口：在栈操作/保存完成后调用，保证槽内canUndo/canRedo已就绪（D-M0-13）
    void notifyChanged();

signals:
    void changed(qint64 sessionRevision);

private:
    friend class SnapshotCommand;
    // 仅由SnapshotCommand（undo/redo）与replaceProject使用：替换内容并递增会话修订
    void applySnapshot(Project next);

    Project m_project;
    qint64 m_sessionRevision = 0;
    bool m_dirty = false;
};

}  // namespace lingya
