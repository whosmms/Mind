#pragma once
#include "services/RecoveryService.h"
#include <QDialog>
#include <QVector>

class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace lingya
{

// 恢复快照预览对话框（规范§11.3：启动发现快照先预览，由用户决定采用）。
// 选择列表项即加载快照显示摘要；本对话框只负责挑选与预览，采用动作由MainWindow
// 经CommandService事务写入唯一文档（统一规范§7.1固定调用方向），不在此直接改工程。
// 与MainWindow约定：用open()模态显示但不阻塞启动事件流，结果经finished+decision()取回。
class RecoveryDialog : public QDialog
{
    Q_OBJECT
  public:
    enum class Decision
    {
        Cancel,
        Adopt,
        IgnoreAll
    };

    RecoveryDialog(const QString &recoveryRoot,
                   const QVector<RecoveryService::SnapshotInfo> &snapshots,
                   QWidget *parent = nullptr);

    Decision decision() const;
    bool selectedSnapshot(RecoveryService::SnapshotInfo *out) const;
    const QVector<RecoveryService::SnapshotInfo> &snapshots() const;

  public slots:
    // 采用当前选中项：预览已通过完整校验才接受；失败提示并保持打开由用户改选
    void adoptSelected();
    // 全部不采用（放弃未保存副本）：快照清理由MainWindow执行，本对话框只记录决定
    void ignoreAll();

  private:
    void previewCurrentRow();
    QString buildPreviewText(const Project &project,
                             const RecoveryService::SnapshotInfo &info) const;

    QString m_root;
    QVector<RecoveryService::SnapshotInfo> m_snapshots;
    QListWidget *m_list = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_adoptButton = nullptr;
    Decision m_decision = Decision::Cancel;
    Project m_previewProject; // 当前预览已通过校验的工程，采用时复用，不重复读盘
    bool m_previewValid = false;
};

} // namespace lingya
