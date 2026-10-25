#include "app/LoginDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "domain/Domain.h"
#include "services/SessionService.h"

namespace lingya
{

LoginDialog::LoginDialog(const QString &sessionRoot, QWidget *parent)
    : QDialog(parent), m_root(sessionRoot)
{
    setWindowTitle(QStringLiteral("登录 灵芽思维"));
    setModal(true);
    resize(380, 180);

    auto *intro =
        new QLabel(QStringLiteral("本地账号：账号信息只保存在本机，不联网。登录后保存只需命名，"
                                  "文件存入你的专有文件夹，并可从“我的文件”打开。"),
                   this);
    intro->setWordWrap(true);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(QStringLiteral("2至32个字符"));
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("至少6个字符"));

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("用户名："), m_nameEdit);
    form->addRow(QStringLiteral("密码："), m_passwordEdit);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color:#A83636;"));
    m_errorLabel->setWordWrap(true);

    auto *loginButton = new QPushButton(QStringLiteral("登录"), this);
    loginButton->setDefault(true);
    auto *registerButton = new QPushButton(QStringLiteral("注册并登录"), this);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), this);
    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(loginButton);
    buttons->addWidget(registerButton);
    buttons->addWidget(cancelButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(m_errorLabel);
    layout->addLayout(buttons);

    connect(loginButton, &QPushButton::clicked, this, &LoginDialog::tryLogin);
    connect(registerButton, &QPushButton::clicked, this, &LoginDialog::tryRegisterAndLogin);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

QString LoginDialog::loggedInUserName() const
{
    return m_userName;
}

void LoginDialog::tryLogin()
{
    const auto result = SessionService::login(m_root, m_nameEdit->text(), m_passwordEdit->text());
    if (std::holds_alternative<SessionService::AccountInfo>(result))
    {
        m_userName = std::get<SessionService::AccountInfo>(result).userName;
        accept();
        return;
    }
    m_errorLabel->setText(std::get<AppError>(result).message);
}

void LoginDialog::tryRegisterAndLogin()
{
    const auto created =
        SessionService::createAccount(m_root, m_nameEdit->text(), m_passwordEdit->text());
    if (std::holds_alternative<AppError>(created))
    {
        m_errorLabel->setText(std::get<AppError>(created).message);
        return;
    }
    tryLogin(); // 注册成功立即登录
}

} // namespace lingya
