#include "app/MainWindow.h"
#include "commands/SearchReplaceCommands.h"
#include "document/ProjectDocument.h"
#include "features/SearchService.h"
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
namespace lingya
{
void MainWindow::showSearch()
{
    if (m_homeVisible)
        return;
    QDialog dialog(this);
    dialog.setObjectName("projectSearch");
    dialog.setWindowTitle(QStringLiteral("查找和替换"));
    dialog.resize(680, 480);
    auto *column = new QVBoxLayout(&dialog);
    auto *query = new QLineEdit(&dialog);
    query->setObjectName("searchQuery");
    query->setPlaceholderText(QStringLiteral("输入标题、备注或任务名称，搜索整个工程"));
    query->setAccessibleName(QStringLiteral("查找"));
    query->setClearButtonEnabled(true);
    column->addWidget(query);
    auto *replacement = new QLineEdit(&dialog);
    replacement->setObjectName("searchReplacement");
    replacement->setAccessibleName(QStringLiteral("替换为"));
    replacement->setPlaceholderText(QStringLiteral("替换为（留空可移除查找文字）"));
    replacement->setClearButtonEnabled(true);
    column->addWidget(replacement);
    auto *info = new QLabel(&dialog);
    info->setObjectName("searchResultCount");
    info->setWordWrap(true);
    info->setTextFormat(Qt::PlainText);
    column->addWidget(info);
    auto *list = new QListWidget(&dialog);
    list->setObjectName("searchResults");
    list->setWordWrap(true);
    column->addWidget(list, 1);
    auto *row = new QHBoxLayout;
    auto *locate = new QPushButton(QStringLiteral("定位所选"), &dialog),
         *close = new QPushButton(QStringLiteral("关闭"), &dialog);
    locate->setObjectName("locateSearchResult");
    auto *replaceSelected = new QPushButton(QStringLiteral("替换所选"), &dialog);
    replaceSelected->setObjectName("replaceSearchSelection");
    auto *replaceAll = new QPushButton(QStringLiteral("全部替换…"), &dialog);
    replaceAll->setObjectName("replaceAllSearchResults");
    row->addWidget(locate);
    row->addWidget(replaceSelected);
    row->addWidget(replaceAll);
    row->addStretch();
    row->addWidget(close);
    column->addLayout(row);
    SearchService::Results results;
    const auto refresh = [&]
    {
        results = SearchService::find(m_doc->project(), query->text());
        list->clear();
        for (const auto &match : results.matches)
        {
            const QString text = match.context + QStringLiteral(" · ") + match.title +
                                 (match.excerpt.isEmpty() ? QString() : "\n" + match.excerpt);
            auto *item = new QListWidgetItem(text, list);
            item->setSizeHint(QSize(0, match.excerpt.isEmpty() ? 42 : 66));
        }
        info->setText(query->text().trimmed().isEmpty()
                          ? QStringLiteral("输入关键字后查找；折叠主题和未排期任务也会参与。")
                      : results.total > results.matches.size()
                          ? QStringLiteral("共%1条，先显示前%2条；请缩小关键字。")
                                .arg(results.total)
                                .arg(results.matches.size())
                          : QStringLiteral("找到%1条结果").arg(results.total));
        if (list->count())
            list->setCurrentRow(0);
        locate->setEnabled(list->count() > 0);
        replaceSelected->setEnabled(list->currentRow() >= 0);
        replaceAll->setEnabled(results.total > 0);
    };
    const auto activate = [&]
    {
        const int index = list->currentRow();
        if (index < 0 || index >= results.matches.size())
            return;
        const auto match = results.matches[index];
        dialog.accept();
        locateIssue(match.sheetId, match.targetId);
    };
    SearchReplaceCommands command(m_commands, m_doc);
    const auto replace = [&](bool all)
    {
        const int index = list->currentRow();
        if (!all && (index < 0 || index >= results.matches.size()))
            return;
        const auto preview = command.preview(query->text(), replacement->text(),
                                             all ? nullptr : &results.matches[index]);
        if (const auto *error = std::get_if<AppError>(&preview))
        {
            info->setText(error->message);
            return;
        }
        const auto &plan = std::get<SearchReplaceCommands::Plan>(preview);
        if (!plan.topics() && !plan.tasks())
        {
            info->setText(QStringLiteral("替换后文字没有变化。"));
            return;
        }
        const QString count =
            QStringLiteral("%1 个主题，%2 个任务").arg(plan.topics()).arg(plan.tasks());
        if (all)
        {
            QMessageBox confirm(QMessageBox::Question, QStringLiteral("确认全部替换"),
                                QStringLiteral("将修改 %1。可以一次撤销这批修改。").arg(count),
                                QMessageBox::NoButton, &dialog);
            confirm.setObjectName("searchReplaceConfirmation");
            confirm.setTextFormat(Qt::PlainText);
            confirm.setDetailedText(plan.examples().join('\n'));
            auto *submit = confirm.addButton(QStringLiteral("全部替换"), QMessageBox::AcceptRole);
            auto *cancel = confirm.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
            confirm.setDefaultButton(cancel);
            confirm.setEscapeButton(cancel);
            confirm.exec();
            if (confirm.clickedButton() != submit)
                return;
        }
        const auto result = command.apply(plan);
        if (const auto *error = std::get_if<AppError>(&result))
            info->setText(error->message);
        else
            info->setText(QStringLiteral("已替换 %1。当前还有 %2 条查找结果。")
                              .arg(count)
                              .arg(results.total));
    };
    connect(query, &QLineEdit::textChanged, &dialog, refresh);
    connect(m_doc, &ProjectDocument::changed, &dialog, refresh);
    connect(query, &QLineEdit::returnPressed, &dialog, activate);
    connect(list, &QListWidget::itemActivated, &dialog, activate);
    connect(list, &QListWidget::currentRowChanged, &dialog,
            [locate, replaceSelected](int index)
            {
                locate->setEnabled(index >= 0);
                replaceSelected->setEnabled(index >= 0);
            });
    connect(locate, &QPushButton::clicked, &dialog, activate);
    connect(replaceSelected, &QPushButton::clicked, &dialog, [&] { replace(false); });
    connect(replaceAll, &QPushButton::clicked, &dialog, [&] { replace(true); });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    refresh();
    query->setFocus();
    dialog.exec();
}
} // namespace lingya
