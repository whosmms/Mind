#include "app/RecoveryDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "domain/Domain.h"

namespace lingya
{

RecoveryDialog::RecoveryDialog(const QString &recoveryRoot,
                               const QVector<RecoveryService::SnapshotInfo> &snapshots,
                               QWidget *parent)
    : QDialog(parent), m_root(recoveryRoot), m_snapshots(snapshots)
{
    setWindowTitle(QStringLiteral("恢复快照"));
    setModal(true);
    resize(620, 440);

    auto *intro = new QLabel(
        QStringLiteral(
            "检测到程序异常退出前留下的恢复快照（编辑期间约30秒自动保存，每工程保留最近5份）。\n"
            "恢复快照与正式保存是不同状态，采用不会覆盖磁盘上的任何正式文件。选择一条进行预览："),
        this);
    intro->setWordWrap(true);

    m_list = new QListWidget(this);
    for (const RecoveryService::SnapshotInfo &info : m_snapshots)
    {
        m_list->addItem(
            QStringLiteral("%1 · %2 · 修订 %3")
                .arg(info.title,
                     info.savedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                     QString::number(info.revision)));
    }

    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setPlaceholderText(QStringLiteral("（选择上方快照后在此显示预览）"));

    m_adoptButton = new QPushButton(QStringLiteral("采用(&A)"), this);
    m_adoptButton->setDefault(true);
    m_adoptButton->setEnabled(false); // 选中且预览通过校验后才可用
    auto *ignoreButton = new QPushButton(QStringLiteral("全部不采用(&D)"), this);
    auto *laterButton = new QPushButton(QStringLiteral("稍后再问(&L)"), this);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(m_adoptButton);
    buttons->addWidget(ignoreButton);
    buttons->addWidget(laterButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_list, 2);
    layout->addWidget(m_preview, 3);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { previewCurrentRow(); });
    connect(m_adoptButton, &QPushButton::clicked, this, &RecoveryDialog::adoptSelected);
    connect(ignoreButton, &QPushButton::clicked, this, &RecoveryDialog::ignoreAll);
    connect(laterButton, &QPushButton::clicked, this, &QDialog::reject);
}

RecoveryDialog::Decision RecoveryDialog::decision() const
{
    return m_decision;
}

bool RecoveryDialog::selectedSnapshot(RecoveryService::SnapshotInfo *out) const
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_snapshots.size())
        return false;
    if (out != nullptr)
        *out = m_snapshots.at(row);
    return true;
}

const QVector<RecoveryService::SnapshotInfo> &RecoveryDialog::snapshots() const
{
    return m_snapshots;
}

void RecoveryDialog::adoptSelected()
{
    if (!m_previewValid)
    {
        QMessageBox::warning(this, QStringLiteral("无法采用快照"),
                             QStringLiteral("请先选择一份可读取的快照再采用。"));
        return;
    }
    m_decision = Decision::Adopt;
    accept();
}

void RecoveryDialog::ignoreAll()
{
    m_decision = Decision::IgnoreAll;
    accept();
}

void RecoveryDialog::previewCurrentRow()
{
    m_previewValid = false;
    m_previewProject = Project{};
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_snapshots.size())
    {
        m_preview->setPlainText(QStringLiteral("（未选择快照）"));
        m_adoptButton->setEnabled(false);
        return;
    }
    const RecoveryService::SnapshotInfo &info = m_snapshots.at(row);
    const auto loaded = RecoveryService::load(m_root, info.projectId, info.snapshotId);
    if (std::holds_alternative<AppError>(loaded))
    {
        m_preview->setPlainText(
            QStringLiteral("快照无法读取：%1").arg(std::get<AppError>(loaded).message));
        m_adoptButton->setEnabled(false);
        return;
    }
    m_previewProject = std::get<Project>(loaded);
    m_previewValid = true;
    m_preview->setPlainText(buildPreviewText(m_previewProject, info));
    m_adoptButton->setEnabled(true);
}

QString RecoveryDialog::buildPreviewText(const Project &project,
                                         const RecoveryService::SnapshotInfo &info) const
{
    QString rootTitle;
    int treeNodes = 0;
    int flowNodes = 0;
    int trees = 0, flows = 0, gantts = 0;
    for (const Sheet &sheet : project.sheets)
    {
        if (const auto *tree = std::get_if<TreeSheet>(&sheet))
        {
            ++trees;
            treeNodes += tree->nodes.size();
            if (rootTitle.isEmpty())
                rootTitle = tree->nodes.value(tree->rootId).title;
        }
        else if (const auto *flow = std::get_if<FlowSheet>(&sheet))
        {
            ++flows;
            flowNodes += flow->nodes.size();
        }
        else if (std::get_if<GanttSheet>(&sheet) != nullptr)
        {
            ++gantts;
        }
    }
    QString sheets;
    if (trees > 0)
        sheets += QStringLiteral("思维导图%1张").arg(trees);
    if (flows > 0)
        sheets += (sheets.isEmpty() ? QString() : QStringLiteral("、")) +
                  QStringLiteral("流程图%1张").arg(flows);
    if (gantts > 0)
        sheets += (sheets.isEmpty() ? QString() : QStringLiteral("、")) +
                  QStringLiteral("甘特%1张").arg(gantts);

    return QStringLiteral(
               "工程标题：%1\n根主题：%2\n工作表：%3（节点%4个）\n任务：%5个\n修订：%6\n"
               "快照时间：%7\n\n"
               "采用后将作为一条可撤销历史写入当前工程；快照不是正式保存，首次保存请使用“另存为”。")
        .arg(project.title, rootTitle.isEmpty() ? QStringLiteral("（无）") : rootTitle,
             sheets.isEmpty() ? QStringLiteral("无") : sheets,
             QString::number(treeNodes + flowNodes), QString::number(project.tasks.size()),
             QString::number(info.revision),
             info.savedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

} // namespace lingya
