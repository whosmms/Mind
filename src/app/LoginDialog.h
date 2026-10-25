#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;

namespace lingya
{

// 登录/注册对话框（登录功能，D-M2-02）：自己经SessionService校验凭据，
// 成功以Accepted结束并携带用户名；MainWindow据此建立会话。纯本地账号，无网络。
class LoginDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit LoginDialog(const QString &sessionRoot, QWidget *parent = nullptr);

    // 对话框内已通过SessionService::login校验的用户名（Accepted时有效）
    QString loggedInUserName() const;

  private slots:
    void tryLogin();
    void tryRegisterAndLogin();

  private:
    QString m_root;
    QString m_userName;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLabel *m_errorLabel = nullptr;
};

} // namespace lingya
