#include "app/MainWindow.h"
#include "document/ProjectDocument.h"
#include "features/SearchService.h"
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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
    dialog.setWindowTitle(QStringLiteral("搜索主题与任务"));
    dialog.resize(680, 480);
    auto *column = new QVBoxLayout(&dialog);
    auto *query = new QLineEdit(&dialog);
    query->setObjectName("searchQuery");
    query->setPlaceholderText(QStringLiteral("输入标题、备注或任务名称，搜索整个工程"));
    column->addWidget(query);
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
    row->addWidget(locate);
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
    connect(query, &QLineEdit::textChanged, &dialog, refresh);
    connect(m_doc, &ProjectDocument::changed, &dialog, refresh);
    connect(query, &QLineEdit::returnPressed, &dialog, activate);
    connect(list, &QListWidget::itemActivated, &dialog, activate);
    connect(locate, &QPushButton::clicked, &dialog, activate);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    refresh();
    query->setFocus();
    dialog.exec();
}
} // namespace lingya
