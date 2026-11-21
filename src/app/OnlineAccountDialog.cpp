#include "app/OnlineAccountDialog.h"
#include "services/OnlineAuthService.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace lingya
{
namespace
{
QFormLayout *accountForm(QWidget *page)
{
    auto *form = new QFormLayout(page);
    form->setContentsMargins(16, 14, 16, 14);
    form->setVerticalSpacing(9);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    return form;
}

QLineEdit *addAccountField(QFormLayout *form, const QString &label, const char *name,
                           bool password = false)
{
    auto *field = new QLineEdit(form->parentWidget());
    field->setObjectName(name);
    field->setMinimumHeight(field->fontMetrics().height() + 16);
    field->setMaxLength(password ? 128 : 254);
    if (password)
    {
        field->setEchoMode(QLineEdit::Password);
        field->setPlaceholderText(QStringLiteral("输入密码"));
    }
    form->addRow(label, field);
    return field;
}
} // namespace

OnlineAccountDialog::OnlineAccountDialog(OnlineAuthService *service, QWidget *parent, Entry entry,
                                         bool localSessionActive)
    : QDialog(parent), m_service(service)
{
    setObjectName("onlineAccountDialog");
    const bool settings = entry == Entry::ServiceSettings;
    setWindowTitle(settings ? QStringLiteral("账号服务设置") : QStringLiteral("灵芽思维账号"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 16);
    layout->setSpacing(10);
    auto *title = new QLabel(settings              ? QStringLiteral("账号服务设置")
                             : service->loggedIn() ? QStringLiteral("我的账号")
                                                   : QStringLiteral("登录灵芽思维"),
                             this);
    title->setObjectName("onlineAuthTitle");
    layout->addWidget(title);
    auto *intro =
        new QLabel(settings ? QStringLiteral("供维护者设置已有的账号服务适配器。")
                            : QStringLiteral("使用邮箱登录、注册或找回密码。工程文件保存在本机。"),
                   this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *status = new QLabel(this);
    status->setObjectName("onlineAuthStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    status->hide();
    layout->addWidget(status);
    const auto showStatus = [status](const QString &message)
    {
        status->setText(message);
        status->setVisible(!message.isEmpty());
    };
    connect(service, &OnlineAuthService::failed, this, showStatus);
    connect(service, &OnlineAuthService::completed, this, showStatus);
    connect(service, &OnlineAuthService::busyChanged, this,
            [showStatus](bool busy)
            {
                if (busy)
                    showStatus(QStringLiteral("正在连接账号服务，可关闭窗口取消本次操作。"));
            });
    if (service->busy())
        showStatus(QStringLiteral("正在连接账号服务，可关闭窗口取消本次操作。"));

    if (settings)
    {
        auto *panel = new QWidget(this);
        panel->setObjectName("onlineAuthForm");
        auto *form = accountForm(panel);
        auto *help =
            new QLabel(QStringLiteral("已有适配器使用 Supabase。填写项目地址和公开标识。"), panel);
        help->setWordWrap(true);
        form->addRow(help);
        auto *url = addAccountField(form, QStringLiteral("项目地址"), "onlineProjectUrl");
        url->setText(service->config().projectUrl);
        url->setPlaceholderText("https://项目编号.supabase.co");
        auto *key = addAccountField(form, QStringLiteral("公开标识"), "onlinePublishableKey", true);
        key->setMaxLength(240);
        key->setText(service->config().publishableKey);
        key->setPlaceholderText("sb_publishable_...");
        auto *save = new QPushButton(QStringLiteral("保存服务设置"), panel);
        save->setObjectName("onlineSaveConfig");
        save->setProperty("primary", true);
        save->setAutoDefault(false);
        form->addRow(save);
        panel->setEnabled(!service->busy());
        connect(service, &OnlineAuthService::busyChanged, panel,
                [panel](bool busy) { panel->setEnabled(!busy); });
        connect(service, &OnlineAuthService::signedIn, this, &QDialog::accept);
        connect(save, &QPushButton::clicked, this,
                [=]
                {
                    if (service->configure({url->text().trimmed(), key->text().trimmed()}))
                        accept();
                });
        layout->addWidget(panel);
        layout->addStretch();
    }
    else if (service->loggedIn())
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
            auto *scroll = new QScrollArea(tabs);
            scroll->setWidgetResizable(true);
            auto *widget = new QWidget;
            widget->setObjectName("onlineAuthForm");
            auto *form = accountForm(widget);
            scroll->setWidget(widget);
            tabs->addTab(scroll, title);
            return form;
        };
        const auto button = [&requests](QFormLayout *form, const QString &text, const char *name)
        {
            auto *result = new QPushButton(text, form->parentWidget());
            result->setObjectName(name);
            result->setAutoDefault(false);
            requests.append(result);
            return result;
        };
        auto *loginForm = page(QStringLiteral("登录"));
        loginForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
        auto *email = addAccountField(loginForm, QStringLiteral("邮箱"), "onlineLoginEmail");
        email->setPlaceholderText(QStringLiteral("name@example.com"));
        auto *password =
            addAccountField(loginForm, QStringLiteral("密码"), "onlineLoginPassword", true);
        auto *remember =
            new QCheckBox(QStringLiteral("在此设备记住登录"), loginForm->parentWidget());
        remember->setObjectName("onlineRemember");
        remember->setToolTip(QStringLiteral("仅对当前 Windows 用户生效，使用系统凭据管理器。"));
        loginForm->addRow(remember);
        auto *login = button(loginForm, QStringLiteral("登录"), "onlineLogin");
        login->setProperty("primary", true);
        loginForm->addRow(login);
        connect(login, &QPushButton::clicked, this,
                [=]
                {
                    service->login(email->text(), password->text(), remember->isChecked());
                    password->clear();
                });
        connect(password, &QLineEdit::returnPressed, login, &QPushButton::click);
        auto *signForm = page(QStringLiteral("注册"));
        auto *signEmail = addAccountField(signForm, QStringLiteral("邮箱"), "onlineSignEmail");
        signEmail->setPlaceholderText(QStringLiteral("name@example.com"));
        auto *signPass = addAccountField(signForm, QStringLiteral("密码（12至128字符）"),
                                         "onlineSignPassword", true);
        auto *signAgain =
            addAccountField(signForm, QStringLiteral("确认密码"), "onlineSignAgain", true);
        auto *signup = button(signForm, QStringLiteral("注册并发送验证码"), "onlineSignUp");
        signup->setProperty("primary", true);
        signForm->addRow(signup);
        connect(signup, &QPushButton::clicked, this,
                [=]
                {
                    if (signPass->text() != signAgain->text())
                    {
                        showStatus(QStringLiteral("两次输入的密码不一致。"));
                        return;
                    }
                    service->signUp(signEmail->text(), signPass->text());
                    signPass->clear();
                    signAgain->clear();
                });
        auto *code = addAccountField(signForm, QStringLiteral("邮件验证码"), "onlineSignCode");
        code->setMaxLength(10);
        code->setPlaceholderText(QStringLiteral("输入邮件中的验证码"));
        auto *confirm = button(signForm, QStringLiteral("验证邮箱并登录"), "onlineConfirm");
        connect(confirm, &QPushButton::clicked, this,
                [=]
                {
                    service->confirmSignUp(signEmail->text(), code->text(), false);
                    code->clear();
                });
        auto *resend = button(signForm, QStringLiteral("重发验证码"), "onlineResend");
        connect(resend, &QPushButton::clicked, this,
                [=] { service->resendConfirmation(signEmail->text()); });
        auto *verifyActions = new QHBoxLayout;
        verifyActions->addWidget(confirm, 1);
        verifyActions->addWidget(resend);
        signForm->addRow(verifyActions);
        auto *resetForm = page(QStringLiteral("找回密码"));
        auto *resetEmail =
            addAccountField(resetForm, QStringLiteral("注册邮箱"), "onlineResetEmail");
        resetEmail->setPlaceholderText(QStringLiteral("name@example.com"));
        auto *send = button(resetForm, QStringLiteral("发送找回密码验证码"), "onlineResetSend");
        resetForm->addRow(send);
        connect(send, &QPushButton::clicked, this,
                [=] { service->requestReset(resetEmail->text()); });
        auto *resetCode =
            addAccountField(resetForm, QStringLiteral("邮件验证码"), "onlineResetCode");
        resetCode->setMaxLength(10);
        resetCode->setPlaceholderText(QStringLiteral("输入邮件中的验证码"));
        auto *resetPass = addAccountField(resetForm, QStringLiteral("新密码（12至128字符）"),
                                          "onlineResetPassword", true);
        auto *resetAgain =
            addAccountField(resetForm, QStringLiteral("确认新密码"), "onlineResetAgain", true);
        auto *reset = button(resetForm, QStringLiteral("验证并重设密码"), "onlineReset");
        reset->setProperty("primary", true);
        resetForm->addRow(reset);
        connect(reset, &QPushButton::clicked, this,
                [=]
                {
                    if (resetPass->text() != resetAgain->text())
                    {
                        showStatus(QStringLiteral("两次输入的新密码不一致。"));
                        return;
                    }
                    service->resetPassword(resetEmail->text(), resetCode->text(),
                                           resetPass->text());
                    resetPass->clear();
                    resetAgain->clear();
                    resetCode->clear();
                });
        const auto fields = tabs->findChildren<QLineEdit *>();
        const auto update = [=](bool busy)
        {
            const bool available = service->configured() && !localSessionActive;
            tabs->setEnabled(!busy);
            remember->setEnabled(available && !busy);
            for (auto *field : fields)
                field->setReadOnly(!available);
            for (auto *item : requests)
                item->setEnabled(!busy && available);
        };
        connect(service, &OnlineAuthService::busyChanged, this, update);
        update(service->busy());
        if (!service->configured())
            showStatus(QStringLiteral("账号服务待接入。当前可预览登录、注册和找回密码界面。"));
        else if (localSessionActive)
            showStatus(QStringLiteral("当前正在使用旧本机账号。退出后可登录联网账号。"));
        connect(service, &OnlineAuthService::signedIn, this, &QDialog::accept);
    }
    auto *buttons = new QDialogButtonBox(this);
    buttons
        ->addButton(settings || service->loggedIn() ? QStringLiteral("关闭")
                                                    : QStringLiteral("暂不登录"),
                    QDialogButtonBox::RejectRole)
        ->setAutoDefault(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &OnlineAccountDialog::reject);
    const auto screenSize = screen()->availableGeometry().size();
    const int height = settings ? 380 : service->loggedIn() ? 320 : 540;
    resize(qMin(480, screenSize.width() - 32), qMin(height, screenSize.height() - 48));
}
void OnlineAccountDialog::reject()
{
    m_service->cancel();
    QDialog::reject();
}
} // namespace lingya
