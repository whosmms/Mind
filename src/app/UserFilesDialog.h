#pragma once
#include "services/UserFileService.h"
#include <QDialog>
#include <QVector>

class QListWidget;
class QLabel;

namespace lingya
{

// 我的文件对话框（登录功能，D-M2-02）：列出当前用户专有文件夹内的工程，
// 选择后经UserFileService::openOwned校验；成功以Accepted结束并携带文件名，
// 由MainWindow完成打开（不在此直接改工程）。
class UserFilesDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit UserFilesDialog(const QString &sessionRoot, const QString &userName,
                             QWidget *parent = nullptr);

    // 已通过校验、可打开的文件名（不含.lingya；Accepted时有效）
    QString selectedFileName() const;

  private slots:
    void openSelected();

  private:
    void refreshList();

    QString m_root;
    QString m_userName;
    QString m_selectedName;
    QListWidget *m_list = nullptr;
    QLabel *m_errorLabel = nullptr;
};

} // namespace lingya
