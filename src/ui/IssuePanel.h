#pragma once
#include "features/CheckService.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace lingya {

// 问题侧栏（X04）：列出检查结果，双击定位、按钮派发修复动作。
// 纯展示与交互，不修改 Project；定位与修复由调用方接信号后执行。
class IssuePanel : public QWidget {
    Q_OBJECT
public:
    explicit IssuePanel(QWidget* parent = nullptr);

    void setIssues(const QVector<Issue>& issues);
    const QVector<Issue>& issues() const;

signals:
    // 定位：跳到工作表 + 目标（节点/任务/关系/卡片）
    void locateRequested(const ID& sheetId, const ID& targetId);
    // 修复：派发动作（edit-node / edit-task / fix-source / fix-contrast）
    void actionRequested(const ID& sheetId, const ID& targetId, const QString& actionId);

private:
    void refreshActionButton();
    const Issue* currentIssue() const;

    QVector<Issue> m_issues;
    QLabel* m_header = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_actionButton = nullptr;
};

}  // namespace lingya
