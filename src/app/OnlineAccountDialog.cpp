#include "app/OnlineAccountDialog.h"
#include "services/OnlineAuthService.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace lingya
{
OnlineAccountDialog::OnlineAccountDialog(OnlineAuthService *service, QWidget *parent)
    : QDialog(parent), m_service(service)
{
    setObjectName("onlineAccountDialog");
    setWindowTitle(QStringLiteral("联网账号"));
    resize(560, 560);
    auto *layout = new QVBoxLayout(this);
    auto *intro =
        new QLabel(QStringLiteral("账号通过网络验证。导图和个人文件仍只保存在这台电脑，不上传工程内"
                                  "容。旧本机账号请从“文件 → 本机账号（旧）”进入。"),
                   this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *status = new QLabel(this);
    status->setObjectName("onlineAuthStatus");
    status->setWordWrap(true);
    status->setMinimumHeight(44);
    if (service->loggedIn())
    {
        auto *identity = new QLabel(
            QStringLiteral("已登录：%1\n本机文件区：%2/users/%3")
                .arg(service->email(), service->localFilesRoot(), service->localUserId()),
            this);
        identity->setTextFormat(Qt::PlainText);
        identity->setWordWrap(true);
        layout->addWidget(identity);
        layout->addStretch();
        auto *logout = new QPushButton(QStringLiteral("退出联网账号"), this);
        logout->setObjectName("onlineLogout");
        layout->addWidget(logout);
        connect(logout, &QPushButton::clicked, this,
                [this]
                {
                    m_service->logout();
                    accept();
                });
    }
    else
    {
        auto *tabs = new QTabWidget(this);
        tabs->setObjectName("onlineAuthTabs");
        layout->addWidget(tabs, 1);
        QList<QPushButton *> requests;
        const auto page = [tabs](const QString &title)
        {
            auto *widget = new QWidget(tabs);
            auto *form = new QFormLayout(widget);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            tabs->addTab(widget, title);
            return form;
        };
        const auto edit =
            [](QFormLayout *form, const QString &label, const char *name, bool password = false)
        {
            auto *field = new QLineEdit(form->parentWidget());
            field->setObjectName(name);
            field->setMinimumHeight(field->fontMetrics().height() + 16);
            field->setMaxLength(password ? 128 : 254);
            if (password)
                field->setEchoMode(QLineEdit::Password);
            form->addRow(label, field);
            return field;
        };
        const auto button = [&requests](QFormLayout *form, const QString &text, const char *name)
        {
            auto *result = new QPushButton(text, form->parentWidget());
            result->setObjectName(name);
            result->setAutoDefault(false);
            form->addRow(result);
            requests.append(result);
            return result;
        };
        auto *loginForm = page(QStringLiteral("登录"));
        auto *email = edit(loginForm, QStringLiteral("邮箱"), "onlineLoginEmail");
        auto *password = edit(loginForm, QStringLiteral("密码"), "onlineLoginPassword", true);
        auto *remember =
            new QCheckBox(QStringLiteral("记住登录（仅此Windows用户，系统凭据管理器）"), this);
        remember->setObjectName("onlineRemember");
        loginForm->addRow(remember);
        auto *login = button(loginForm, QStringLiteral("登录"), "onlineLogin");
        login->setProperty("primary", true);
        connect(login, &QPushButton::clicked, this,
                [=]
                {
                    service->login(email->text(), password->text(), remember->isChecked());
                    password->clear();
                });
        connect(password, &QLineEdit::returnPressed, login, &QPushButton::click);
        auto *signForm = page(QStringLiteral("注册"));
        auto *signEmail = edit(signForm, QStringLiteral("邮箱"), "onlineSignEmail");
        auto *signPass =
            edit(signForm, QStringLiteral("密码（12至128字符）"), "onlineSignPassword", true);
        auto *signAgain = edit(signForm, QStringLiteral("确认密码"), "onlineSignAgain", true);
        auto *signup = button(signForm, QStringLiteral("注册并发送验证码"), "onlineSignUp");
        connect(signup, &QPushButton::clicked, this,
                [=]
                {
                    if (signPass->text() != signAgain->text())
                    {
                        status->setText(QStringLiteral("两次输入的密码不一致。"));
                        return;
                    }
                    service->signUp(signEmail->text(), signPass->text());
                    signPass->clear();
                    signAgain->clear();
                });
        auto *code = edit(signForm, QStringLiteral("邮件验证码"), "onlineSignCode");
        code->setMaxLength(10);
        auto *confirm = button(signForm, QStringLiteral("验证邮箱并登录"), "onlineConfirm");
        connect(confirm, &QPushButton::clicked, this,
                [=]
                {
                    service->confirmSignUp(signEmail->text(), code->text(), false);
                    code->clear();
                });
        auto *resend = button(signForm, QStringLiteral("重新发送验证码"), "onlineResend");
        connect(resend, &QPushButton::clicked, this,
                [=] { service->resendConfirmation(signEmail->text()); });
        auto *resetForm = page(QStringLiteral("找回密码"));
        auto *resetEmail = edit(resetForm, QStringLiteral("注册邮箱"), "onlineResetEmail");
        auto *send = button(resetForm, QStringLiteral("发送找回密码验证码"), "onlineResetSend");
        connect(send, &QPushButton::clicked, this,
                [=] { service->requestReset(resetEmail->text()); });
        auto *resetCode = edit(resetForm, QStringLiteral("邮件验证码"), "onlineResetCode");
        resetCode->setMaxLength(10);
        auto *resetPass =
            edit(resetForm, QStringLiteral("新密码（12至128字符）"), "onlineResetPassword", true);
        auto *resetAgain = edit(resetForm, QStringLiteral("确认新密码"), "onlineResetAgain", true);
        auto *reset = button(resetForm, QStringLiteral("验证并重设密码"), "onlineReset");
        connect(reset, &QPushButton::clicked, this,
                [=]
                {
                    if (resetPass->text() != resetAgain->text())
                    {
                        status->setText(QStringLiteral("两次输入的新密码不一致。"));
                        return;
                    }
                    service->resetPassword(resetEmail->text(), resetCode->text(),
                                           resetPass->text());
                    resetPass->clear();
                    resetAgain->clear();
                    resetCode->clear();
                });
        auto *settings = page(QStringLiteral("服务配置"));
        auto *help = new QLabel(
            QStringLiteral(
                "由软件维护者配置一次。仅接受Supabase项目地址和publishable公开标识，不能填服务端sec"
                "ret、service_role或数据库密码。配置方法见随包“联网登录部署步骤”。"),
            this);
        help->setWordWrap(true);
        settings->addRow(help);
        auto *url = edit(settings, QStringLiteral("项目URL"), "onlineProjectUrl");
        url->setText(service->config().projectUrl);
        url->setPlaceholderText("https://项目编号.supabase.co");
        auto *key = edit(settings, QStringLiteral("公开标识"), "onlinePublishableKey", true);
        key->setMaxLength(240);
        key->setText(service->config().publishableKey);
        key->setPlaceholderText("sb_publishable_...");
        auto *save = new QPushButton(QStringLiteral("保存服务配置"), this);
        save->setObjectName("onlineSaveConfig");
        save->setAutoDefault(false);
        settings->addRow(save);
        const auto update = [=](bool busy)
        {
            tabs->setEnabled(!busy);
            for (auto *item : requests)
                item->setEnabled(!busy && service->configured());
        };
        connect(save, &QPushButton::clicked, this,
                [=]
                {
                    if (service->configure({url->text().trimmed(), key->text().trimmed()}))
                    {
                        status->setText(QStringLiteral("配置已保存，请注册或登录。"));
                        tabs->setCurrentIndex(0);
                        update(false);
                    }
                });
        connect(service, &OnlineAuthService::busyChanged, this,
                [=](bool busy)
                {
                    update(busy);
                    if (busy)
                        status->setText(
                            QStringLiteral("正在连接账号服务，可关闭窗口取消本次操作……"));
                });
        update(service->busy());
        if (!service->configured())
        {
            tabs->setCurrentIndex(3);
            status->setText(
                QStringLiteral("尚未配置账号服务；本地编辑、保存和旧本机账号仍可使用。"));
        }
        connect(service, &OnlineAuthService::signedIn, this, &QDialog::accept);
    }
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole)->setAutoDefault(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &OnlineAccountDialog::reject);
    connect(service, &OnlineAuthService::failed, status, &QLabel::setText);
    connect(service, &OnlineAuthService::completed, status, &QLabel::setText);
}
void OnlineAccountDialog::reject()
{
    m_service->cancel();
    QDialog::reject();
}
} // namespace lingya
