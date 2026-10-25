#include "app/UserFilesDialog.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "domain/Domain.h"
#include "services/UserFileService.h"

namespace lingya
{

UserFilesDialog::UserFilesDialog(const QString &sessionRoot, const QString &userName,
                                 QWidget *parent)
    : QDialog(parent), m_root(sessionRoot), m_userName(userName)
{
    setWindowTitle(QStringLiteral("我的文件 - %1").arg(userName));
    setModal(true);
    resize(460, 400);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color:#A83636;"));
    m_errorLabel->setWordWrap(true);

    auto *openButton = new QPushButton(QStringLiteral("打开"), this);
    openButton->setDefault(true);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), this);
    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(openButton);
    buttons->addWidget(cancelButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    layout->addWidget(m_errorLabel);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::itemDoubleClicked, this, &UserFilesDialog::openSelected);
    connect(openButton, &QPushButton::clicked, this, &UserFilesDialog::openSelected);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    refreshList();
}

QString UserFilesDialog::selectedFileName() const
{
    return m_selectedName;
}

void UserFilesDialog::refreshList()
{
    m_list->clear();
    const QVector<UserFileService::OwnedFileInfo> files =
        UserFileService::listOwned(m_root, m_userName);
    if (files.isEmpty())
    {
        auto *empty =
            new QListWidgetItem(QStringLiteral("（暂无文件：登录状态下保存会存入这里）"), m_list);
        empty->setFlags(Qt::NoItemFlags);
        return;
    }
    for (const UserFileService::OwnedFileInfo &info : files)
    {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 · %2").arg(
                info.name, info.modified.toString(QStringLiteral("yyyy-MM-dd HH:mm"))),
            m_list);
        item->setData(Qt::UserRole, info.name);
    }
    m_list->setCurrentRow(0);
}

void UserFilesDialog::openSelected()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_list->count())
        return;
    if (m_list->item(row)->flags() == Qt::NoItemFlags)
        return; // 空态占位行
    // 绑定显示项的文件名，磁盘排序变化或删除不能把当前行换成另一个文件。
    const QString name = m_list->item(row)->data(Qt::UserRole).toString();
    const auto opened = UserFileService::openOwned(m_root, m_userName, name);
    if (std::holds_alternative<AppError>(opened))
    {
        m_errorLabel->setText(std::get<AppError>(opened).message);
        return;
    }
    m_selectedName = name;
    accept();
}

} // namespace lingya
