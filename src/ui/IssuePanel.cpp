#include "ui/IssuePanel.h"

#include "features/CheckAssistant.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace lingya {

namespace {

QString severityMark(IssueSeverity severity) {
    switch (severity) {
    case IssueSeverity::Error:
        return QStringLiteral("错误");
    case IssueSeverity::Warning:
        return QStringLiteral("提醒");
    case IssueSeverity::Suggestion:
        return QStringLiteral("建议");
    }
    return QString();
}

}  // namespace

IssuePanel::IssuePanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("issuePanel"));

    m_header = new QLabel(this);
    m_header->setObjectName(QStringLiteral("issueHeader"));

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("issueList"));

    m_actionButton = new QPushButton(QStringLiteral("仅定位"), this);
    m_actionButton->setObjectName(QStringLiteral("issueAction"));
    m_actionButton->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_header);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_actionButton);

    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const int row = m_list->row(item);
        if (row < 0 || row >= m_issues.size()) {
            return;
        }
        const Issue& issue = m_issues.at(row);
        emit locateRequested(issue.sheetId, issue.targetId);
    });
    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int) { refreshActionButton(); });
    connect(m_actionButton, &QPushButton::clicked, this, [this] {
        const Issue* issue = currentIssue();
        if (issue == nullptr) {
            return;
        }
        const CheckAction action = CheckAssistant::actionFor(issue->ruleId);
        if (action.actionId.isEmpty()) {
            return;
        }
        emit actionRequested(issue->sheetId, issue->targetId, action.actionId);
    });

    setIssues({});
}

void IssuePanel::setIssues(const QVector<Issue>& issues) {
    m_issues = issues;
    m_list->clear();
    for (const Issue& issue : m_issues) {
        m_list->addItem(QStringLiteral("[%1] %2：%3")
                            .arg(severityMark(issue.severity),
                                 CheckAssistant::ruleTitle(issue.ruleId), issue.message));
    }
    m_header->setText(QStringLiteral("问题（%1）").arg(m_issues.size()));
    refreshActionButton();
}

const QVector<Issue>& IssuePanel::issues() const {
    return m_issues;
}

const Issue* IssuePanel::currentIssue() const {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_issues.size()) {
        return nullptr;
    }
    return &m_issues.at(row);
}

void IssuePanel::refreshActionButton() {
    const Issue* issue = currentIssue();
    const CheckAction action =
        issue != nullptr ? CheckAssistant::actionFor(issue->ruleId) : CheckAction{};
    m_actionButton->setEnabled(!action.actionId.isEmpty());
    m_actionButton->setText(action.actionId.isEmpty() ? QStringLiteral("仅定位") : action.label);
}

}  // namespace lingya
