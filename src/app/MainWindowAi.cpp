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
#include <QVBoxLayout>
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
                statusBar()->showMessage(QStringLiteral("AI导图已采用；仍可编辑与撤销"), 8000);
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
    auto *drafts = new QPushButton(QStringLiteral("工程文字稿…"), &dialog),
         *close = new QPushButton(QStringLiteral("关闭"), &dialog);
    drafts->setObjectName("openDraftsFromAi");
    row->addWidget(drafts);
    row->addStretch();
    row->addWidget(close);
    column->addLayout(row);
    connect(drafts, &QPushButton::clicked, &dialog, [this] { manageDrafts(); });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(&dialog, &QDialog::finished, panel, &AiPanel::cancelPending);
    dialog.exec();
}

void MainWindow::manageDrafts()
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
    column->addWidget(draftChoice);
    auto *title = new QLineEdit(&dialog);
    title->setObjectName("draftTitle");
    column->addWidget(title);
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
         *erase = new QPushButton(QStringLiteral("删除稿件"), &dialog),
         *close = new QPushButton(QStringLiteral("关闭"), &dialog);
    save->setObjectName("saveDraftText");
    copy->setObjectName("copyDraftText");
    exportButton->setObjectName("exportDraftText");
    erase->setObjectName("deleteDraftText");
    locate->setObjectName("locateDraftSource");
    for (auto *button : {save, copy, exportButton, locate, erase})
        row->addWidget(button);
    row->addStretch();
    row->addWidget(close);
    column->addLayout(row);
    TextDraft editing;
    int sectionIndex = -1;
    qint64 editRevision = -1;
    const auto selectSection = [&](int index)
    {
        sectionIndex = index;
        const QSignalBlocker a(heading), b(text);
        const bool valid = index >= 0 && index < editing.sections.size();
        heading->setEnabled(valid);
        text->setEnabled(valid);
        heading->setText(valid ? editing.sections[index].heading : QString());
        text->setPlainText(valid ? editing.sections[index].text : QString());
        QStringList sourceTitles;
        if (valid)
            for (const auto &id : editing.sections[index].sourceNodeIds)
            {
                QString name = QStringLiteral("已删除来源");
                for (const auto &sheet : m_doc->project().sheets)
                    if (const auto *tree = std::get_if<TreeSheet>(&sheet);
                        tree && tree->id == editing.sourceSheetId && tree->nodes.contains(id))
                        name = tree->nodes.value(id).title;
                sourceTitles.append(name);
            }
        source->setText(valid ? QStringLiteral("%1 · 来源主题：%2")
                                    .arg(editing.sections[index].origin == "supplement"
                                             ? QStringLiteral("AI补充，请核实")
                                             : QStringLiteral("基于来源，编辑后请核对"),
                                         sourceTitles.join(QStringLiteral("、")))
                              : QString());
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
        for (auto *button : {save, copy, exportButton, erase, locate})
            button->setEnabled(valid);
        title->setEnabled(valid);
        QString sourceState = QStringLiteral("旧稿未记录来源指纹，请核对原图；稿件保持原样。");
        if (editing.sourceHash)
            for (const auto &sheet : m_doc->project().sheets)
                if (const auto *tree = std::get_if<TreeSheet>(&sheet);
                    tree && tree->id == editing.sourceSheetId)
                    sourceState =
                        *editing.sourceHash == AiMaterial::treeHash(*tree)
                            ? QStringLiteral("来源导图内容与生成时一致；稿件仍可独立编辑。")
                            : QStringLiteral("源导图已更新，请核对稿件；不会自动重写已有内容。");
        info->setText(
            valid ? sourceState
                  : QStringLiteral("暂无文字稿。请先通过AI创作预览并采用；生成前会发送所选材料。"));
    };
    connect(draftChoice, &QComboBox::currentIndexChanged, &dialog, load);
    connect(sections, &QListWidget::currentRowChanged, &dialog, selectSection);
    connect(heading, &QLineEdit::textChanged, &dialog,
            [&](const QString &value)
            {
                if (sectionIndex >= 0)
                {
                    editing.sections[sectionIndex].heading = value;
                    if (auto *item = sections->item(sectionIndex))
                        item->setText(value);
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
            info->setText(QStringLiteral("文字稿修改已保存到工程，可撤销；来源记录保持原样"));
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
