#include "ai/AiMaterial.h"
#include "ai/AiService.h"
#include "ai/AiServiceFactory.h"
#include "ai/SecretStore.h"
#include "app/MainWindow.h"
#include "commands/AiCommands.h"
#include "commands/DraftCommands.h"
#include "document/ProjectDocument.h"
#include "services/ExportService.h"
#include "ui/AiPanel.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QVBoxLayout>
#include <functional>
#include <memory>

namespace lingya
{
namespace
{
class AiRuntime : public QObject
{
  public:
    explicit AiRuntime(QObject *parent) : QObject(parent), secret(createSecretStore())
    {
        service = new AiService(nullptr, secret.get(), this);
        QSettings settings;
        service->setModel(settings.value("ai/model", service->status().model).toString());
        SecretStore *store = secret.get();
        service->setProviderFactory([store] { return createAiProvider(store); });
    }
    ~AiRuntime() override
    {
        delete service;
    }
    std::unique_ptr<SecretStore> secret;
    AiService *service = nullptr;
};
QString draftText(const TextDraft &draft)
{
    QString text = "# " + draft.title + "\n\n";
    for (const auto &section : draft.sections)
    {
        text += "## " + section.heading + "\n\n" + section.text + "\n\n";
        text += section.origin == "supplement" ? QStringLiteral("[AI补充，请核实]\n")
                                               : QStringLiteral("[基于来源]\n");
        if (!section.sourceNodeIds.isEmpty())
            text += QStringLiteral("来源主题ID：") + section.sourceNodeIds.join(", ") + "\n\n";
    }
    return text;
}

enum class DraftSourceState
{
    Unchanged,
    Changed,
    Unrecorded
};

QString draftSectionLabel(const DraftSection &section, DraftSourceState state)
{
    if (state == DraftSourceState::Changed)
        return QStringLiteral("来源变更 · %1").arg(section.heading);
    if (state == DraftSourceState::Unrecorded)
        return QStringLiteral("来源待核对 · %1").arg(section.heading);
    return section.heading;
}

const DraftSourceText *topicText(const DraftSourceSnapshot &snapshot, const ID &id)
{
    const auto it = snapshot.constFind(id);
    return it == snapshot.cend() ? nullptr : &it.value();
}

QString sourceChanges(const DraftSourceText *previous, const DraftSourceText *current,
                      bool recorded)
{
    if (!recorded)
        return QStringLiteral("旧来源未记录，只能核对当前内容。");
    if (!current)
        return previous ? QStringLiteral("原主题已删除。") : QStringLiteral("原主题仍然缺失。");
    if (!previous)
        return QStringLiteral("原主题已恢复。");
    QStringList changes;
    if (previous->title != current->title)
        changes.append(QStringLiteral("标题已修改"));
    if (previous->note != current->note)
        changes.append(QStringLiteral("备注已修改"));
    return changes.isEmpty() ? QStringLiteral("标题和备注一致。")
                             : changes.join(QStringLiteral(" · "));
}

using ParagraphEdit = QPair<QString, QString>;

bool compareDraftSources(
    QWidget *parent, const DraftSection &section, const DraftSourceSnapshot &latest,
    const std::function<std::optional<AppError>(const std::optional<ParagraphEdit> &)> &commit)
{
    QDialog dialog(parent);
    dialog.setObjectName("draftSourceComparison");
    dialog.setWindowTitle(QStringLiteral("段落来源对照"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(
        QStringLiteral(
            "确认会记录本段所有来源的当前内容。保留正文只确认来源，应用本段会保存下方的编辑。"),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *topics = new QComboBox(&dialog);
    topics->setObjectName("draftComparisonTopic");
    topics->setAccessibleName(QStringLiteral("选择要对照的原主题"));
    topics->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    topics->setMinimumContentsLength(20);
    layout->addWidget(topics);

    const auto previous = section.sourceSnapshot.value_or(DraftSourceSnapshot{});
    QSet<ID> added;
    int firstChanged = -1;
    for (const auto &id : section.sourceNodeIds)
    {
        if (added.contains(id))
            continue;
        added.insert(id);
        const auto *old = topicText(previous, id);
        const auto *now = topicText(latest, id);
        const bool changed = section.sourceSnapshot && ((old == nullptr) != (now == nullptr) ||
                                                        (old && now && !(*old == *now)));
        if (changed && firstChanged < 0)
            firstChanged = topics->count();
        const auto name = now   ? now->title
                          : old ? old->title
                                : QStringLiteral("缺失主题 %1").arg(topics->count() + 1);
        topics->addItem(changed ? QStringLiteral("已变更 · %1").arg(name) : name, id);
    }

    auto *changes = new QLabel(&dialog);
    changes->setObjectName("draftComparisonChanges");
    changes->setWordWrap(true);
    layout->addWidget(changes);
    auto *columns = new QHBoxLayout;
    const auto addColumn = [&](const QString &label, const char *name)
    {
        auto *column = new QVBoxLayout;
        column->addWidget(new QLabel(label, &dialog));
        auto *text = new QPlainTextEdit(&dialog);
        text->setObjectName(name);
        text->setAccessibleName(label);
        text->setReadOnly(true);
        column->addWidget(text, 1);
        columns->addLayout(column, 1);
        return text;
    };
    auto *oldText = addColumn(QStringLiteral("上次记录的原主题"), "draftPreviousSource");
    auto *newText = addColumn(QStringLiteral("当前导图中的主题"), "draftCurrentSource");
    layout->addLayout(columns, 1);

    auto *form = new QFormLayout;
    auto *heading = new QLineEdit(section.heading, &dialog);
    heading->setObjectName("draftComparisonHeading");
    form->addRow(QStringLiteral("段落标题"), heading);
    layout->addLayout(form);
    auto *editRow = new QHBoxLayout;
    editRow->addWidget(new QLabel(QStringLiteral("本段正文预览"), &dialog));
    editRow->addStretch();
    auto *insert = new QPushButton(QStringLiteral("插入当前备注"), &dialog);
    insert->setObjectName("insertDraftSourceNote");
    insert->setToolTip(QStringLiteral("把所选主题的备注插入下方预览，确认应用后才写入稿件"));
    editRow->addWidget(insert);
    layout->addLayout(editRow);
    auto *body = new QPlainTextEdit(section.text, &dialog);
    body->setObjectName("draftComparisonText");
    body->setAccessibleName(QStringLiteral("本段正文预览"));
    layout->addWidget(body, 1);
    const auto displayTopic = [](const DraftSourceText &value)
    {
        return QStringLiteral("标题\n%1\n\n备注\n%2")
            .arg(value.title, value.note.isEmpty() ? QStringLiteral("无备注") : value.note);
    };
    const auto showTopic = [&]
    {
        const ID id = topics->currentData().toString();
        const auto *old = topicText(previous, id);
        const auto *now = topicText(latest, id);
        changes->setText(sourceChanges(old, now, section.sourceSnapshot.has_value()));
        oldText->setPlainText(old                      ? displayTopic(*old)
                              : section.sourceSnapshot ? QStringLiteral("上次记录时主题缺失")
                                                       : QStringLiteral("旧来源未记录"));
        newText->setPlainText(now ? displayTopic(*now) : QStringLiteral("原主题已删除或缺失"));
        insert->setEnabled(now && !now->note.trimmed().isEmpty());
    };
    QObject::connect(topics, &QComboBox::currentIndexChanged, &dialog, showTopic);
    topics->setCurrentIndex(qMax(firstChanged, 0));
    showTopic();
    QObject::connect(insert, &QPushButton::clicked, &dialog,
                     [&]
                     {
                         if (const auto *now = topicText(latest, topics->currentData().toString()))
                         {
                             auto cursor = body->textCursor();
                             cursor.clearSelection();
                             cursor.movePosition(QTextCursor::End);
                             cursor.insertText((body->toPlainText().isEmpty()
                                                    ? QString()
                                                    : QStringLiteral("\n\n")) +
                                               now->note);
                             body->setTextCursor(cursor);
                             body->setFocus();
                         }
                     });

    auto *error = new QLabel(&dialog);
    error->setObjectName("draftComparisonError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setText(QStringLiteral("暂不处理"));
    cancel->setDefault(true);
    auto *keep = buttons->addButton(QStringLiteral("保留正文并确认"), QDialogButtonBox::ActionRole);
    keep->setObjectName("confirmDraftSourcesOnly");
    keep->setAutoDefault(false);
    keep->setEnabled(!section.sourceSnapshot || *section.sourceSnapshot != latest);
    auto *apply = buttons->addButton(QStringLiteral("应用本段修改"), QDialogButtonBox::ActionRole);
    apply->setObjectName("applyDraftSourceUpdate");
    apply->setAutoDefault(false);
    layout->addWidget(buttons);
    const auto finish = [&](const std::optional<ParagraphEdit> &edit)
    {
        if (const auto problem = commit(edit))
            error->setText(problem->message);
        else
            dialog.accept();
    };
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(keep, &QPushButton::clicked, &dialog, [&] { finish(std::nullopt); });
    QObject::connect(apply, &QPushButton::clicked, &dialog,
                     [&] { finish(ParagraphEdit{heading->text(), body->toPlainText()}); });
    const auto available = parent->screen()->availableGeometry();
    dialog.resize(qMin(880, available.width() - 48), qMin(700, available.height() - 70));
    return dialog.exec() == QDialog::Accepted;
}
} // namespace
void MainWindow::setAiServiceForTest(AiService *service)
{
    m_aiService = service;
}

void MainWindow::showAiWorkspace()
{
    if (m_homeVisible)
        return;
    if (!m_aiService)
        m_aiService = (new AiRuntime(this))->service;
    auto *service = m_aiService.data();
    QDialog dialog(this);
    dialog.setObjectName("aiWorkspace");
    dialog.setWindowTitle(QStringLiteral("AI 创作"));
    dialog.resize(qMin(1120, screen()->availableGeometry().width() - 48),
                  qMin(820, screen()->availableGeometry().height() - 60));
    auto *column = new QVBoxLayout(&dialog);
    auto *notice = new QLabel(QStringLiteral("先预览再采用。生成与连接测试会发送所选材料并使用已配"
                                             "置的API；关闭窗口会取消在途请求。"),
                              &dialog);
    notice->setWordWrap(true);
    column->addWidget(notice);
    auto *options = new QHBoxLayout;
    auto *scope = new QComboBox(&dialog);
    scope->setObjectName("aiSourceScope");
    scope->addItems({QStringLiteral("所选主题完整子树（含折叠）"), QStringLiteral("整张工作表")});
    auto *target = new QComboBox(&dialog);
    target->setObjectName("aiMapTarget");
    target->addItems(
        {QStringLiteral("导图采用到新工作表"), QStringLiteral("导图作为所选主题的子树")});
    target->setEnabled(activeTreeSheet() != nullptr);
    options->addWidget(scope, 1);
    options->addWidget(target, 1);
    column->addLayout(options);
    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *panel = new AiPanel(service, scroll);
    panel->setObjectName("hostAiPanel");
    scroll->setWidget(panel);
    column->addWidget(scroll, 1);
    column->addWidget(panel->actionBar());
    panel->actionBar()->show();
    const auto *tree = activeTreeSheet();
    if (tree && tree->nodes.contains(m_selectedNodeId))
    {
        const auto node = tree->nodes.value(m_selectedNodeId);
        panel->setSourceText(node.title + "\n" + node.note);
    }
    panel->setDraftSupported(tree != nullptr);
    auto *workflows = panel->findChild<QTabWidget *>("aiWorkflowTabs");
    const auto updateScope = [this, scope, target, workflows]
    {
        scope->setEnabled(activeTreeSheet() && workflows->currentIndex() == 1);
        target->setEnabled(activeTreeSheet() && workflows->currentIndex() == 0);
    };
    scope->setToolTip(QStringLiteral("生成文字稿时使用的来源范围"));
    connect(workflows, &QTabWidget::currentChanged, &dialog, updateScope);
    connect(m_doc, &ProjectDocument::changed, &dialog, updateScope);
    updateScope();
    ID sourceSheet, sourceNode;
    qint64 sourceRevision = -1;
    connect(panel, &AiPanel::contextRequested, &dialog,
            [&](bool draft)
            {
                sourceSheet = m_doc->project().activeSheetId;
                sourceNode = m_selectedNodeId;
                sourceRevision = m_doc->sessionRevision();
                if (draft)
                {
                    const auto context =
                        AiMaterial::fromTree(m_doc->project(), sourceSheet,
                                             scope->currentIndex() == 0 ? sourceNode : ID{});
                    if (const auto *error = std::get_if<AppError>(&context))
                        panel->setDraftContext({}, error->message);
                    else
                    {
                        auto request = std::get<GenerateDraftRequest>(context);
                        request.projectRevision = sourceRevision;
                        panel->setDraftContext(request);
                    }
                }
            });
    connect(panel, &AiPanel::applyMapRequested, &dialog,
            [&](const MapProposal &proposal)
            {
                AiApplyOptions options;
                options.expectedRevision = sourceRevision;
                options.requestId = panel->currentRequestId();
                const auto result =
                    target->currentIndex() == 1
                        ? AiCommands(m_commands, m_doc)
                              .applyMapAsBranch(proposal, sourceSheet, sourceNode, options)
                        : AiCommands(m_commands, m_doc).applyMapAsSheet(proposal, options);
                if (const auto *error = std::get_if<AppError>(&result))
                {
                    panel->reportApplyResult(error->message);
                    return;
                }
                panel->reportApplyResult({});
                statusBar()->showMessage(QStringLiteral("AI导图已采用；仍可编辑与撤回"), 8000);
            });
    connect(panel, &AiPanel::applyDraftRequested, &dialog,
            [&](const DraftProposal &proposal)
            {
                AiApplyOptions options;
                options.expectedRevision = sourceRevision;
                options.requestId = panel->currentRequestId();
                const auto result = AiCommands(m_commands, m_doc)
                                        .saveDraft(proposal, sourceSheet, panel->draftPurpose(),
                                                   sourceRevision, options);
                if (const auto *error = std::get_if<AppError>(&result))
                {
                    panel->reportApplyResult(error->message);
                    return;
                }
                panel->reportApplyResult({});
                statusBar()->showMessage(
                    QStringLiteral("文字稿已存入工程，可在文字稿管理中继续编辑、复制和导出"), 8000);
            });
    auto *row = new QHBoxLayout;
    auto *drafts = new QPushButton(QStringLiteral("管理文字稿"), &dialog),
         *close = new QPushButton(QStringLiteral("关闭"), &dialog);
    drafts->setObjectName("openDraftsFromAi");
    drafts->setProperty("secondaryAction", true);
    row->addWidget(drafts);
    row->addStretch();
    row->addWidget(close);
    column->addLayout(row);
    connect(drafts, &QPushButton::clicked, &dialog, [this] { manageDrafts(); });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(&dialog, &QDialog::finished, panel, &AiPanel::cancelPending);
    dialog.exec();
}

void MainWindow::createDraftFromBranch(const ID &rootId)
{
    const auto *tree = activeTreeSheet();
    if (!tree)
        return;
    const auto sheetId = tree->id;
    const auto revision = m_doc->sessionRevision();
    const auto material = AiMaterial::fromTree(m_doc->project(), sheetId, rootId);
    if (const auto *failure = std::get_if<AppError>(&material))
    {
        statusBar()->showMessage(failure->message, 8000);
        return;
    }
    const auto &topics = std::get<GenerateDraftRequest>(material).source;
    DraftProposal proposal;
    proposal.title = topics.front().title;
    QStringList previewLines;
    for (const auto &topic : topics)
    {
        const auto body =
            topic.note.trimmed().isEmpty() ? QStringLiteral("待补充正文。") : topic.note;
        proposal.sections.append({topic.title, body, {topic.id}, QStringLiteral("source")});
        previewLines.append(QStringLiteral("%1\n%2").arg(topic.title, body));
    }

    QDialog dialog(this);
    dialog.setObjectName("branchDraftPreview");
    dialog.setWindowTitle(QStringLiteral("从分支整理文字稿"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(
        QStringLiteral("按导图顺序整理 %1 个主题。每个主题对应一段，备注作为正文，空段标为待补充。")
            .arg(topics.size()),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *title = new QLineEdit(proposal.title, &dialog);
    title->setObjectName("branchDraftTitle");
    title->setAccessibleName(QStringLiteral("文字稿标题"));
    layout->addWidget(title);
    auto *preview = new QPlainTextEdit(previewLines.join(QStringLiteral("\n\n")), &dialog);
    preview->setReadOnly(true);
    preview->setObjectName("branchDraftText");
    preview->setAccessibleName(QStringLiteral("分支文字稿预览"));
    layout->addWidget(preview, 1);
    auto *notice = new QLabel(&dialog);
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto *save = buttons->addButton(QStringLiteral("保存为文字稿"), QDialogButtonBox::AcceptRole);
    save->setObjectName("saveBranchDraft");
    save->setProperty("primary", true);
    layout->addWidget(buttons);
    ID created;
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [&]
            {
                proposal.title = title->text();
                AiApplyOptions options;
                options.expectedRevision = revision;
                const auto result = AiCommands(m_commands, m_doc)
                                        .saveDraft(proposal, sheetId, "summary", revision, options);
                if (const auto *error = std::get_if<AppError>(&result))
                {
                    notice->setText(error->message);
                    return;
                }
                created = std::get<ID>(result);
                dialog.accept();
            });
    const auto available = screen()->availableGeometry();
    dialog.resize(qMin(690, available.width() - 48), qMin(540, available.height() - 72));
    if (dialog.exec() == QDialog::Accepted && !created.isEmpty())
        openDraftManager(created);
}

void MainWindow::manageDrafts()
{
    openDraftManager({});
}

void MainWindow::openDraftManager(const ID &draftId)
{
    if (m_homeVisible)
        return;
    QDialog dialog(this);
    dialog.setObjectName("draftManager");
    dialog.setWindowTitle(QStringLiteral("工程文字稿"));
    dialog.resize(900, 700);
    auto *column = new QVBoxLayout(&dialog);
    auto *draftChoice = new QComboBox(&dialog);
    draftChoice->setObjectName("draftChoice");
    for (const auto &draft : m_doc->project().drafts)
        draftChoice->addItem(draft.title, draft.id);
    if (const int index = draftChoice->findData(draftId); index >= 0)
        draftChoice->setCurrentIndex(index);
    column->addWidget(draftChoice);
    auto *title = new QLineEdit(&dialog);
    title->setObjectName("draftTitle");
    column->addWidget(title);
    auto *sourceStatus = new QLabel(&dialog);
    sourceStatus->setObjectName("draftSourceStatus");
    sourceStatus->setTextFormat(Qt::PlainText);
    sourceStatus->setWordWrap(true);
    column->addWidget(sourceStatus);
    auto *sections = new QListWidget(&dialog);
    sections->setObjectName("draftSections");
    sections->setMaximumHeight(130);
    column->addWidget(sections);
    auto *heading = new QLineEdit(&dialog);
    heading->setObjectName("draftHeading");
    column->addWidget(heading);
    auto *text = new QPlainTextEdit(&dialog);
    text->setObjectName("draftText");
    column->addWidget(text, 1);
    auto *source = new QLabel(&dialog);
    source->setObjectName("draftSource");
    source->setWordWrap(true);
    source->setTextFormat(Qt::PlainText);
    column->addWidget(source);
    auto *info = new QLabel(&dialog);
    info->setObjectName("draftInfo");
    info->setTextFormat(Qt::PlainText);
    info->setWordWrap(true);
    column->addWidget(info);
    auto *row = new QHBoxLayout;
    auto *save = new QPushButton(QStringLiteral("保存修改"), &dialog),
         *copy = new QPushButton(QStringLiteral("复制全文"), &dialog),
         *exportButton = new QPushButton(QStringLiteral("导出文字…"), &dialog),
         *locate = new QPushButton(QStringLiteral("定位来源"), &dialog),
         *compare = new QPushButton(QStringLiteral("对照来源…"), &dialog),
         *reviewSources = new QPushButton(QStringLiteral("把来源加入复习…"), &dialog),
         *erase = new QPushButton(QStringLiteral("删除稿件"), &dialog),
         *close = new QPushButton(QStringLiteral("关闭"), &dialog);
    save->setObjectName("saveDraftText");
    copy->setObjectName("copyDraftText");
    exportButton->setObjectName("exportDraftText");
    erase->setObjectName("deleteDraftText");
    locate->setObjectName("locateDraftSource");
    compare->setObjectName("compareDraftSource");
    compare->setToolTip(QStringLiteral("也可以双击段落，查看上次记录和当前原图"));
    reviewSources->setObjectName("reviewDraftSources");
    reviewSources->setToolTip(QStringLiteral("从本篇稿件关联的原主题选题，答案取自原导图"));
    auto *sourceActions = new QHBoxLayout;
    sourceActions->addWidget(compare);
    sourceActions->addWidget(locate);
    sourceActions->addWidget(reviewSources);
    sourceActions->addStretch();
    column->insertLayout(column->indexOf(info), sourceActions);
    for (auto *button : {save, copy, exportButton, erase})
        row->addWidget(button);
    row->addStretch();
    row->addWidget(close);
    column->addLayout(row);
    TextDraft editing;
    QSet<ID> topicIds;
    int sectionIndex = -1;
    qint64 editRevision = -1;
    const auto selectedSourceDetails = [&]
    {
        const bool valid = sectionIndex >= 0 && sectionIndex < editing.sections.size();
        compare->setEnabled(valid && !editing.sections[sectionIndex].sourceNodeIds.isEmpty());
        if (!valid)
        {
            source->clear();
            locate->setEnabled(false);
            return;
        }
        const auto &section = editing.sections.at(sectionIndex);
        const auto *tree = AiMaterial::sourceTree(m_doc->project(), editing.sourceSheetId);
        QStringList titles;
        bool canLocate = false;
        for (const auto &id : section.sourceNodeIds)
        {
            if (tree && tree->nodes.contains(id))
            {
                titles.append(tree->nodes.value(id).title);
                canLocate = true;
            }
            else
                titles.append(QStringLiteral("已删除来源"));
        }
        locate->setEnabled(canLocate);
        source->setText(
            QStringLiteral("%1 · 来源主题：%2")
                .arg(section.origin == "supplement" ? QStringLiteral("AI补充，请核实")
                                                    : QStringLiteral("基于来源"),
                     titles.isEmpty() ? QStringLiteral("无") : titles.join(QStringLiteral("、"))));
    };
    const auto refreshSourceBadges = [&]
    {
        const auto *tree = AiMaterial::sourceTree(m_doc->project(), editing.sourceSheetId);
        int changed = 0, unknown = 0;
        for (int i = 0; i < sections->count(); ++i)
        {
            const auto &section = editing.sections.at(i);
            auto state = DraftSourceState::Unchanged;
            if (!section.sourceNodeIds.isEmpty())
            {
                const auto latest =
                    tree ? AiMaterial::draftSourceSnapshot(*tree, section.sourceNodeIds)
                         : DraftSourceSnapshot{};
                if (!section.sourceSnapshot)
                {
                    state = DraftSourceState::Unrecorded;
                    ++unknown;
                }
                else if (*section.sourceSnapshot != latest)
                {
                    state = DraftSourceState::Changed;
                    ++changed;
                }
            }
            auto *row = sections->item(i);
            row->setData(Qt::UserRole, static_cast<int>(state));
            row->setText(draftSectionLabel(section, state));
        }
        QStringList summary;
        if (changed > 0)
            summary.append(QStringLiteral("%1 段来源有变更").arg(changed));
        if (unknown > 0)
            summary.append(QStringLiteral("%1 段旧来源待核对").arg(unknown));
        if (summary.isEmpty() && !editing.id.isEmpty())
            summary.append(topicIds.isEmpty() ? QStringLiteral("本篇稿件没有关联来源主题。")
                                              : QStringLiteral("已记录的段落来源与原图一致。"));
        sourceStatus->setText(summary.join(QStringLiteral(" · ")));
        sourceStatus->setVisible(!summary.isEmpty());
        selectedSourceDetails();
    };
    const auto selectSection = [&](int index)
    {
        sectionIndex = index;
        const QSignalBlocker a(heading), b(text);
        const bool valid = index >= 0 && index < editing.sections.size();
        heading->setEnabled(valid);
        text->setEnabled(valid);
        heading->setText(valid ? editing.sections[index].heading : QString());
        text->setPlainText(valid ? editing.sections[index].text : QString());
        selectedSourceDetails();
    };
    const auto load = [&]
    {
        editing = {};
        const ID id = draftChoice->currentData().toString();
        for (const auto &draft : m_doc->project().drafts)
            if (draft.id == id)
                editing = draft;
        editRevision = m_doc->sessionRevision();
        title->setText(editing.title);
        const QSignalBlocker blocker(sections);
        sections->clear();
        for (const auto &section : editing.sections)
            sections->addItem(section.heading);
        sections->setCurrentRow(editing.sections.isEmpty() ? -1 : 0);
        selectSection(sections->currentRow());
        const bool valid = !editing.id.isEmpty();
        for (auto *button : {save, copy, exportButton, erase})
            button->setEnabled(valid);
        topicIds.clear();
        for (const auto &section : editing.sections)
            for (const auto &id : section.sourceNodeIds)
                topicIds.insert(id);
        reviewSources->setEnabled(valid && !topicIds.isEmpty());
        title->setEnabled(valid);
        refreshSourceBadges();
        info->setText(
            valid ? QString()
                  : QStringLiteral(
                        "暂无文字稿。可在主题右键把分支整理为文字稿，也可从 AI 创作采用。"));
    };
    connect(draftChoice, &QComboBox::currentIndexChanged, &dialog, load);
    connect(sections, &QListWidget::currentRowChanged, &dialog, selectSection);
    connect(m_doc, &ProjectDocument::changed, &dialog, refreshSourceBadges);
    connect(sections, &QListWidget::itemDoubleClicked, &dialog,
            [compare](QListWidgetItem *) { compare->click(); });
    connect(heading, &QLineEdit::textChanged, &dialog,
            [&](const QString &value)
            {
                if (sectionIndex >= 0)
                {
                    editing.sections[sectionIndex].heading = value;
                    if (auto *item = sections->item(sectionIndex))
                        item->setText(draftSectionLabel(
                            editing.sections.at(sectionIndex),
                            static_cast<DraftSourceState>(item->data(Qt::UserRole).toInt())));
                }
            });
    connect(text, &QPlainTextEdit::textChanged, &dialog,
            [&]
            {
                if (sectionIndex >= 0)
                    editing.sections[sectionIndex].text = text->toPlainText();
            });
    const auto current = [&]
    {
        auto result = editing;
        result.title = title->text();
        return result;
    };
    connect(
        save, &QPushButton::clicked, &dialog,
        [&]
        {
            if (m_doc->sessionRevision() != editRevision)
            {
                info->setText(
                    QStringLiteral("工程在打开编辑器后已改变，请关闭重开稿件，再核对修改"));
                return;
            }
            QVector<QPair<QString, QString>> paragraphs;
            for (const auto &section : editing.sections)
                paragraphs.append({section.heading, section.text});
            const auto result =
                DraftCommands(m_commands, m_doc).updateText(editing.id, title->text(), paragraphs);
            if (const auto *error = std::get_if<AppError>(&result))
            {
                info->setText(error->message);
                return;
            }
            draftChoice->setItemText(draftChoice->currentIndex(), title->text());
            editing.title = title->text();
            editRevision = m_doc->sessionRevision();
            info->setText(QStringLiteral("文字稿修改已保存到工程，可撤回；来源记录保持原样"));
        });
    connect(copy, &QPushButton::clicked, &dialog,
            [&]
            {
                QApplication::clipboard()->setText(draftText(current()));
                info->setText(QStringLiteral("已复制当前编辑内容（含来源标记）"));
            });
    connect(exportButton, &QPushButton::clicked, &dialog,
            [&]
            {
                const QString path =
                    QFileDialog::getSaveFileName(&dialog, QStringLiteral("导出文字稿"), "文字稿.md",
                                                 QStringLiteral("Markdown (*.md);;文本 (*.txt)"));
                if (path.isEmpty())
                    return;
                const auto result = ExportService::writeText(draftText(current()), path);
                info->setText(std::holds_alternative<AppError>(result)
                                  ? std::get<AppError>(result).message
                                  : QStringLiteral("文字稿已导出"));
            });
    connect(locate, &QPushButton::clicked, &dialog,
            [&]
            {
                if (sectionIndex < 0)
                    return;
                const auto &ids = editing.sections[sectionIndex].sourceNodeIds;
                bool found = false;
                for (const auto &sheet : m_doc->project().sheets)
                    if (const auto *tree = std::get_if<TreeSheet>(&sheet);
                        tree && tree->id == editing.sourceSheetId)
                        for (const auto &id : ids)
                            if (tree->nodes.contains(id))
                            {
                                m_doc->setActiveSheet(tree->id);
                                selectNode(id);
                                found = true;
                                break;
                            }
                info->setText(found
                                  ? QStringLiteral("已在编辑器定位第一个仍存在的来源主题")
                                  : QStringLiteral("本段没有仍存在的来源主题；保留当前稿件文本。"));
            });
    connect(
        compare, &QPushButton::clicked, &dialog,
        [&]
        {
            if (sectionIndex < 0 || sectionIndex >= editing.sections.size())
                return;
            const auto index = sectionIndex;
            const auto id = editing.id;
            const auto section = editing.sections.at(index);
            const auto *tree = AiMaterial::sourceTree(m_doc->project(), editing.sourceSheetId);
            const auto latest = tree ? AiMaterial::draftSourceSnapshot(*tree, section.sourceNodeIds)
                                     : DraftSourceSnapshot{};
            const auto revision = m_doc->sessionRevision();
            bool textApplied = false;
            const auto commit =
                [&](const std::optional<ParagraphEdit> &replacement) -> std::optional<AppError>
            {
                if (editRevision != revision || editing.id != id)
                    return AppError{"STALE_SOURCE",
                                    QStringLiteral("稿件窗口已过期，请重新打开稿件后核对。")};
                const auto result = DraftCommands(m_commands, m_doc)
                                        .reconcileSources(id, index, revision, replacement);
                if (const auto *error = std::get_if<AppError>(&result))
                    return *error;
                auto &edited = editing.sections[index];
                edited.sourceSnapshot = latest;
                if (replacement)
                {
                    edited.heading = replacement->first;
                    edited.text = replacement->second;
                    textApplied = true;
                }
                editRevision = m_doc->sessionRevision();
                return std::nullopt;
            };
            if (compareDraftSources(&dialog, section, latest, commit))
            {
                if (textApplied)
                    selectSection(index);
                refreshSourceBadges();
                info->setText(textApplied ? QStringLiteral("本段修改及来源核对已保存，可撤回。")
                                          : QStringLiteral("已确认本段来源，原稿编辑内容保留。"));
            }
        });
    connect(reviewSources, &QPushButton::clicked, &dialog,
            [&]
            {
                const auto before = m_doc->sessionRevision();
                const auto result = addReviewTopics(
                    &dialog, ReviewScope{editing.sourceSheetId, topicIds, editing.title});
                if (const auto *error = std::get_if<AppError>(&result))
                {
                    info->setText(error->message);
                    return;
                }
                const auto count = std::get<QVector<ID>>(result).size();
                if (count == 0)
                    return;
                // 建卡只改复习数据，保留本窗口尚未保存的文字稿修改。
                if (editRevision == before && m_doc->sessionRevision() == before + 1)
                    editRevision = m_doc->sessionRevision();
                info->setText(QStringLiteral("已把 %1 个原主题加入复习。").arg(count));
            });
    connect(erase, &QPushButton::clicked, &dialog,
            [&]
            {
                const auto result = DraftCommands(m_commands, m_doc).remove(editing.id);
                if (const auto *error = std::get_if<AppError>(&result))
                {
                    info->setText(error->message);
                    return;
                }
                const int index = draftChoice->currentIndex();
                {
                    const QSignalBlocker blocker(draftChoice);
                    draftChoice->removeItem(index);
                }
                load();
            });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    load();
    dialog.exec();
}
} // namespace lingya
