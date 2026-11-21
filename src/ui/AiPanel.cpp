#include "ui/AiPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include "domain/Limits.h"
#include <functional>

namespace lingya {

namespace {

QString renderMap(const MapProposal& proposal) {
    QStringList lines;
    lines << QStringLiteral("标题：%1").arg(proposal.title);
    std::function<void(const GeneratedNode&, int)> walk =
        [&lines, &walk](const GeneratedNode& node, int depth) {
            lines << QString(depth * 2, QLatin1Char(' ')) + QStringLiteral("- ") + node.title;
            for (const GeneratedNode& child : node.children) {
                walk(child, depth + 1);
            }
        };
    walk(proposal.root, 0);
    for (const QString& warning : proposal.warnings) {
        lines << QStringLiteral("⚠ %1").arg(warning);
    }
    return lines.join(QLatin1Char('\n'));
}

QString renderDraft(const DraftProposal &proposal, const QVector<DraftSourceNode> &source)
{
    QStringList lines;
    lines << QStringLiteral("标题：%1").arg(proposal.title);
    for (const DraftSection& section : proposal.sections) {
        lines << QStringLiteral("## %1").arg(section.heading);
        lines << section.text;
        lines << (section.origin == "supplement" ? QStringLiteral("[AI补充，请核实]")
                                                 : QStringLiteral("[基于来源]"));
        QStringList titles;
        for (const auto &id : section.sourceNodeIds)
            for (const auto &node : source)
                if (node.id == id)
                {
                    titles.append(node.title);
                    break;
                }
        lines << QStringLiteral("来源主题：%1").arg(titles.join(QStringLiteral("、")));
    }
    for (const QString& warning : proposal.warnings) {
        lines << QStringLiteral("⚠ %1").arg(warning);
    }
    return lines.join(QLatin1Char('\n'));
}

QString newRequestId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

}  // namespace

AiPanel::AiPanel(AiService* service, QWidget* parent) : QWidget(parent), m_service(service) {
    setObjectName(QStringLiteral("aiPanel"));

    m_keyEdit = new QLineEdit(this);
    m_keyEdit->setObjectName(QStringLiteral("aiKey"));
    m_keyEdit->setEchoMode(QLineEdit::Password);
    m_keyEdit->setPlaceholderText(QStringLiteral("DeepSeek API Key（仅本次会话/本机凭据）"));
    m_saveKeyButton = new QPushButton(QStringLiteral("保存密钥"), this);
    m_saveKeyButton->setObjectName(QStringLiteral("aiSaveKey"));
    m_testButton = new QPushButton(QStringLiteral("测试连接"), this);
    m_testButton->setObjectName(QStringLiteral("aiTestConnection"));
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("aiStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextFormat(Qt::PlainText);

    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget(m_keyEdit, 1);
    keyRow->addWidget(m_saveKeyButton);
    keyRow->addWidget(m_testButton);

    m_sourceEdit = new QPlainTextEdit(this);
    m_sourceEdit->setObjectName(QStringLiteral("aiSource"));
    m_sourceEdit->setPlaceholderText(QStringLiteral("粘贴材料；也可由主窗口预填选中节点内容"));
    m_sourceEdit->setMinimumHeight(90);
    m_sourceEdit->setMaximumHeight(200);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(QStringLiteral("忠实整理原文"), QStringLiteral("faithful"));
    m_modeCombo->addItem(QStringLiteral("允许补充（需核实）"), QStringLiteral("supplement"));

    m_maxNodesSpin = new QSpinBox(this);
    m_maxNodesSpin->setObjectName("aiMaxNodes");
    m_maxNodesSpin->setRange(1, Limits::kAiMapMaxNodes);
    m_maxNodesSpin->setValue(50);
    m_maxDepthSpin = new QSpinBox(this);
    m_maxDepthSpin->setObjectName("aiMaxDepth");
    m_maxDepthSpin->setRange(1, Limits::kAiMapMaxDepth);
    m_maxDepthSpin->setValue(4);

    auto* paramForm = new QFormLayout;
    auto *mapForm = new QFormLayout;
    auto *draftForm = new QFormLayout;
    paramForm->addRow(QStringLiteral("生成模式"), m_modeCombo);
    mapForm->addRow(QStringLiteral("最多节点"), m_maxNodesSpin);
    mapForm->addRow(QStringLiteral("最大层级"), m_maxDepthSpin);
    m_requirements = new QLineEdit(this);
    m_requirements->setObjectName("aiRequirements");
    m_requirements->setPlaceholderText(QStringLiteral("附加要求，最多1000字"));
    paramForm->addRow(QStringLiteral("附加要求"), m_requirements);
    m_purpose = new QComboBox(this);
    m_purpose->setObjectName("aiDraftPurpose");
    m_purpose->addItem(QStringLiteral("摘要"), "summary");
    m_purpose->addItem(QStringLiteral("说明"), "explanation");
    m_purpose->addItem(QStringLiteral("讲稿"), "speech");
    m_targetChars = new QSpinBox(this);
    m_targetChars->setObjectName("aiDraftLength");
    m_targetChars->setRange(100, 8000);
    m_targetChars->setValue(400);
    draftForm->addRow(QStringLiteral("文稿用途"), m_purpose);
    draftForm->addRow(QStringLiteral("目标字数"), m_targetChars);

    m_mapButton = new QPushButton(QStringLiteral("生成导图"), this);
    m_mapButton->setObjectName(QStringLiteral("aiGenerateMap"));
    m_mapButton->setProperty("primary", true);
    m_draftButton = new QPushButton(QStringLiteral("生成文稿"), this);
    m_draftButton->setObjectName(QStringLiteral("aiGenerateDraft"));
    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_cancelButton->setObjectName(QStringLiteral("aiCancel"));
    m_cancelButton->setEnabled(false);
    m_applyButton = new QPushButton(QStringLiteral("应用"), this);
    m_applyButton->setObjectName(QStringLiteral("aiApply"));
    m_applyButton->setEnabled(false);

    auto* actionRow = new QHBoxLayout;
    actionRow->addWidget(m_mapButton);
    actionRow->addWidget(m_draftButton);
    actionRow->addWidget(m_cancelButton);
    actionRow->addWidget(m_applyButton);

    m_preview = new QTextBrowser(this);
    m_preview->setObjectName(QStringLiteral("aiPreview"));
    m_preview->setOpenExternalLinks(false);
    m_preview->setMinimumHeight(80);
    m_preview->setMaximumHeight(130);
    m_mapTree = new QTreeWidget(this);
    m_mapTree->setObjectName("aiMapTree");
    m_mapTree->setHeaderLabels({QStringLiteral("导图结构"), QStringLiteral("内容来源")});
    m_mapTree->setColumnWidth(0, 250);
    m_mapTree->hide();
    m_draftEditor = new QWidget(this);
    m_draftEditor->setObjectName("aiDraftEditor");
    auto *draftLayout = new QVBoxLayout(m_draftEditor);
    draftLayout->setContentsMargins(0, 0, 0, 0);
    auto *draftCaption =
        new QLabel(QStringLiteral("采用前可逐段编辑；来源记录保持，补充内容请核实"), m_draftEditor);
    draftCaption->setWordWrap(true);
    draftLayout->addWidget(draftCaption);
    m_draftSections = new QListWidget(m_draftEditor);
    m_draftSections->setObjectName("aiDraftSections");
    m_draftSections->setMaximumHeight(85);
    draftLayout->addWidget(m_draftSections);
    m_sectionHeading = new QLineEdit(m_draftEditor);
    m_sectionHeading->setObjectName("aiSectionHeading");
    draftLayout->addWidget(m_sectionHeading);
    m_sectionText = new QPlainTextEdit(m_draftEditor);
    m_sectionText->setObjectName("aiSectionText");
    m_sectionText->setMinimumHeight(120);
    draftLayout->addWidget(m_sectionText, 1);
    m_draftEditor->hide();

    auto* layout = new QVBoxLayout(this);
    auto *configure = new QToolButton(this);
    configure->setText(QStringLiteral("AI服务与密钥设置"));
    configure->setCheckable(true);
    configure->setChecked(!m_service || !m_service->status().configured);
    layout->addWidget(configure, 0, Qt::AlignLeft);
    auto *credentials = new QWidget(this);
    auto *credentialLayout = new QVBoxLayout(credentials);
    credentialLayout->setContentsMargins(0, 0, 0, 0);
    credentialLayout->addLayout(keyRow);
    layout->addWidget(credentials);
    credentials->setVisible(configure->isChecked());
    connect(configure, &QToolButton::toggled, credentials, &QWidget::setVisible);
    m_rememberKey =
        new QCheckBox(QStringLiteral("保存到Windows凭据管理器（默认仅本次使用）"), this);
    m_rememberKey->setObjectName("aiRememberKey");
    credentialLayout->addWidget(m_rememberKey);
    auto *modelRow = new QHBoxLayout;
    m_modelEdit = new QLineEdit(m_service ? m_service->status().model : QString(), credentials);
    m_modelEdit->setObjectName("aiModel");
    m_modelEdit->setPlaceholderText(QStringLiteral("按API服务可用的模型名称填写"));
    m_modelButton = new QPushButton(QStringLiteral("应用模型"), credentials);
    m_modelButton->setObjectName("aiSaveModel");
    modelRow->addWidget(new QLabel(QStringLiteral("模型"), credentials));
    modelRow->addWidget(m_modelEdit, 1);
    modelRow->addWidget(m_modelButton);
    credentialLayout->addLayout(modelRow);
    connect(m_modelButton, &QPushButton::clicked, this,
            [this]
            {
                if (!m_service || !m_service->setModel(m_modelEdit->text()))
                {
                    m_statusLabel->setText(QStringLiteral("模型名称无效或请求尚未结束"));
                    return;
                }
                QSettings settings;
                settings.setValue("ai/model", m_service->status().model);
                settings.sync();
                m_statusLabel->setText(
                    settings.status() == QSettings::NoError
                        ? QStringLiteral("模型已更新，后续生成使用此配置；尚未执行连接测试")
                        : QStringLiteral("模型仅在本次会话更新，本机设置未能保存"));
            });
    auto *clearKey = new QPushButton(QStringLiteral("清除密钥"), this);
    clearKey->setObjectName("aiClearKey");
    keyRow->addWidget(clearKey);
    connect(clearKey, &QPushButton::clicked, this,
            [this]
            {
                if (m_service)
                    m_service->clearKey();
                m_keyEdit->clear();
                clearProposal();
                m_statusLabel->setText(QStringLiteral("已清除密钥"));
            });
    layout->addWidget(m_statusLabel);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setObjectName("aiWorkspaceColumns");
    auto *input = new QWidget(splitter);
    auto *inputLayout = new QVBoxLayout(input);
    inputLayout->setContentsMargins(0, 0, 8, 0);
    m_workflows = new QTabWidget(input);
    m_workflows->setObjectName("aiWorkflowTabs");
    auto *mapPage = new QWidget(m_workflows);
    auto *mapLayout = new QVBoxLayout(mapPage);
    auto *materialLabel = new QLabel(QStringLiteral("将文字整理为可编辑的导图"), mapPage);
    materialLabel->setWordWrap(true);
    mapLayout->addWidget(materialLabel);
    m_sourceEdit->setMaximumHeight(QWIDGETSIZE_MAX);
    mapLayout->addWidget(m_sourceEdit, 1);
    mapLayout->addLayout(mapForm);
    auto *draftPage = new QWidget(m_workflows);
    auto *draftPageLayout = new QVBoxLayout(draftPage);
    auto *draftHint = new QLabel(
        QStringLiteral("从上方选择导图范围。主题与备注会发送给已配置的服务，最多100个主题。"),
        draftPage);
    draftHint->setWordWrap(true);
    draftPageLayout->addWidget(draftHint);
    draftPageLayout->addLayout(draftForm);
    draftPageLayout->addStretch();
    m_workflows->addTab(mapPage, QStringLiteral("文字 → 导图"));
    m_workflows->addTab(draftPage, QStringLiteral("导图 → 文稿"));
    inputLayout->addWidget(m_workflows, 1);
    inputLayout->addLayout(paramForm);
    auto *output = new QWidget(splitter);
    auto *outputLayout = new QVBoxLayout(output);
    outputLayout->setContentsMargins(8, 0, 0, 0);
    outputLayout->addWidget(new QLabel(QStringLiteral("预览与编辑 · 采用前不会更改工程"), output));
    outputLayout->addWidget(m_mapTree, 2);
    outputLayout->addWidget(m_draftEditor, 2);
    outputLayout->addWidget(m_preview, 1);
    splitter->addWidget(input);
    splitter->addWidget(output);
    splitter->setSizes({360, 560});
    layout->addWidget(splitter, 1);
    m_actionBar = new QWidget(this);
    m_actionBar->setObjectName("aiActionBar");
    m_actionBar->setLayout(actionRow);
    layout->addWidget(m_actionBar);
    connect(m_workflows, &QTabWidget::currentChanged, this,
            [this](int index)
            {
                m_mapButton->setVisible(index == 0);
                m_draftButton->setVisible(index == 1);
            });
    m_draftButton->hide();
    connect(m_draftSections, &QListWidget::currentRowChanged, this,
            [this](int index)
            {
                m_editingSection = index;
                const QSignalBlocker a(m_sectionHeading), b(m_sectionText);
                const bool valid = index >= 0 && index < m_draft.sections.size();
                m_sectionHeading->setText(valid ? m_draft.sections[index].heading : QString());
                m_sectionText->setPlainText(valid ? m_draft.sections[index].text : QString());
            });
    connect(m_sectionHeading, &QLineEdit::textChanged, this,
            [this](const QString &text)
            {
                if (m_editingSection >= 0 && m_editingSection < m_draft.sections.size())
                {
                    m_draft.sections[m_editingSection].heading = text;
                    if (auto *item = m_draftSections->item(m_editingSection))
                        item->setText(text);
                }
            });
    connect(m_sectionText, &QPlainTextEdit::textChanged, this,
            [this]
            {
                if (m_editingSection >= 0 && m_editingSection < m_draft.sections.size())
                    m_draft.sections[m_editingSection].text = m_sectionText->toPlainText();
            });

    connect(m_saveKeyButton, &QPushButton::clicked, this, &AiPanel::onSaveKey);
    connect(m_testButton, &QPushButton::clicked, this, &AiPanel::onTestConnection);
    connect(m_mapButton, &QPushButton::clicked, this, &AiPanel::onGenerateMap);
    connect(m_draftButton, &QPushButton::clicked, this, &AiPanel::onGenerateDraft);
    connect(m_cancelButton, &QPushButton::clicked, this, &AiPanel::onCancel);
    connect(m_applyButton, &QPushButton::clicked, this, &AiPanel::onApply);

    if (m_service != nullptr) {
        connect(m_service, &AiService::stateChanged, this, &AiPanel::onStateChanged);
        connect(m_service, &AiService::mapReady, this, &AiPanel::onMapReady);
        connect(m_service, &AiService::draftReady, this, &AiPanel::onDraftReady);
        connect(m_service, &AiService::failed, this, &AiPanel::onFailed);
        connect(m_service, &AiService::canceled, this, &AiPanel::onCanceled);
        connect(m_service, &AiService::connectionReady, this, &AiPanel::onConnectionReady);
    }
    onStateChanged(QString(), AiService::State::Idle);
}

void AiPanel::setSourceText(const QString& text) {
    m_sourceEdit->setPlainText(text);
}

void AiPanel::setTargetSheet(const ID& sheetId) {
    m_targetSheet = sheetId;
}

void AiPanel::onSaveKey() {
    if (m_service == nullptr) {
        return;
    }
    if (m_service->setKey(m_keyEdit->text().trimmed(), m_rememberKey->isChecked()))
    {
        m_keyEdit->clear();
        m_statusLabel->setText(
            QStringLiteral("密钥已应用，不写入工程；仅在点击生成或连接测试时请求服务"));
    }
    else
    {
        m_statusLabel->setText(QStringLiteral("密钥无效或保存失败"));
    }
}

void AiPanel::onTestConnection() {
    if (m_service == nullptr) {
        return;
    }
    m_currentRequestId = newRequestId();
    m_service->testConnection(m_currentRequestId);
}

void AiPanel::onGenerateMap() {
    if (m_service == nullptr) {
        return;
    }
    clearProposal();
    m_workflows->setCurrentIndex(0);
    emit contextRequested(false);
    GenerateMapRequest request;
    request.requestId = newRequestId();
    request.text = m_sourceEdit->toPlainText();
    request.requirements = m_requirements->text();
    request.mode = m_modeCombo->currentData().toString();
    request.maxNodes = m_maxNodesSpin->value();
    request.maxDepth = m_maxDepthSpin->value();
    m_currentRequestId = request.requestId;
    const QString before = m_statusLabel->text();
    if (!m_service->generateMap(request)) {
        // AiService 已同步 emit failed 时不覆盖它的具体提示；BUSY 不发 failed，需要兜底
        if (m_statusLabel->text() == before) {
            m_statusLabel->setText(QStringLiteral("正在请求中，请稍候"));
        }
    }
}

void AiPanel::onGenerateDraft() {
    if (m_service == nullptr) {
        return;
    }
    clearProposal();
    m_workflows->setCurrentIndex(1);
    emit contextRequested(true);
    if (!m_draftSupported || !m_contextError.isEmpty())
    {
        m_statusLabel->setText(m_contextError.isEmpty() ? QStringLiteral("当前类型不支持文字稿")
                                                        : m_contextError);
        return;
    }
    GenerateDraftRequest request = m_draftContext;
    request.requestId = newRequestId();
    request.sheetId = m_targetSheet;
    request.mode = m_modeCombo->currentData().toString();
    request.requirements = m_requirements->text();
    request.purpose = m_purpose->currentData().toString();
    request.targetChars = m_targetChars->value();
    m_generatedDraftPurpose = request.purpose;
    m_currentRequestId = request.requestId;
    const QString before = m_statusLabel->text();
    if (!m_service->generateDraft(request)) {
        if (m_statusLabel->text() == before) {
            m_statusLabel->setText(QStringLiteral("正在请求中，请稍候"));
        }
    }
}

void AiPanel::onCancel() {
    if (m_service == nullptr || m_currentRequestId.isEmpty()) {
        return;
    }
    m_service->cancel(m_currentRequestId);
}

void AiPanel::onApply() {
    if (m_hasMap) {
        emit applyMapRequested(m_map);
    }
    if (m_hasDraft) {
        emit applyDraftRequested(m_draft);
    }
}

void AiPanel::onStateChanged(const QString& requestId, AiService::State state) {
    setBusy(state == AiService::State::Validating || state == AiService::State::Requesting ||
            state == AiService::State::ValidatingResponse);
    switch (state) {
    case AiService::State::Preview:
        m_statusLabel->setText(QStringLiteral("已生成，可预览后应用"));
        break;
    case AiService::State::Error:
        m_statusLabel->setText(QStringLiteral("请求失败"));
        break;
    case AiService::State::Canceled:
        m_statusLabel->setText(QStringLiteral("已取消"));
        break;
    case AiService::State::Done:
        m_statusLabel->setText(QStringLiteral("完成"));
        break;
    case AiService::State::Idle:
        m_statusLabel->setText(QStringLiteral("就绪"));
        break;
    default:
        m_statusLabel->setText(QStringLiteral("请求中…"));
        break;
    }
}

void AiPanel::onConnectionReady(const QString& requestId, const QString& model) {
    Q_UNUSED(requestId)
    m_statusLabel->setText(QStringLiteral("连接成功（模型 %1）").arg(model));
}

void AiPanel::onMapReady(const QString& requestId, const MapProposal& proposal) {
    if (!m_currentRequestId.isEmpty() && requestId != m_currentRequestId)
        return;
    m_proposalRequestId = requestId;
    m_map = proposal;
    m_hasMap = true;
    m_hasDraft = false;
    m_mapTree->clear();
    m_mapTree->show();
    std::function<void(const GeneratedNode &, QTreeWidgetItem *)> add =
        [&](const GeneratedNode &node, QTreeWidgetItem *parent)
    {
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_mapTree);
        item->setText(0, node.title);
        item->setToolTip(0, QStringLiteral("<span>%1</span>").arg(node.note.toHtmlEscaped()));
        item->setText(1, node.origin == "supplement" ? QStringLiteral("AI补充")
                                                     : QStringLiteral("基于材料"));
        for (const auto &child : node.children)
            add(child, item);
    };
    add(proposal.root, nullptr);
    m_mapTree->expandAll();
    showPreview(renderMap(proposal));
    m_applyButton->setEnabled(true);
}

void AiPanel::onDraftReady(const QString& requestId, const DraftProposal& proposal) {
    if (!m_currentRequestId.isEmpty() && requestId != m_currentRequestId)
        return;
    m_proposalRequestId = requestId;
    m_draft = proposal;
    m_hasDraft = true;
    m_hasMap = false;
    m_mapTree->hide();
    m_editingSection = -1;
    m_draftSections->clear();
    for (const auto &section : m_draft.sections)
        m_draftSections->addItem(section.heading);
    m_draftEditor->setEnabled(true);
    m_draftEditor->show();
    m_draftSections->setCurrentRow(0);
    showPreview(renderDraft(proposal, m_draftContext.source));
    m_applyButton->setEnabled(true);
}

void AiPanel::onFailed(const AiError& error) {
    clearProposal();
    m_statusLabel->setText(QStringLiteral("失败：%1（%2）").arg(error.message, error.code));
    m_applyButton->setEnabled(false);
}

void AiPanel::onCanceled(const QString& requestId) {
    Q_UNUSED(requestId)
    clearProposal();
    m_statusLabel->setText(QStringLiteral("已取消"));
    m_applyButton->setEnabled(false);
}

void AiPanel::showPreview(const QString& text) {
    m_preview->setPlainText(text);
}

void AiPanel::setBusy(bool busy) {
    m_modelButton->setEnabled(!busy);
    m_modelEdit->setEnabled(!busy);
    m_mapButton->setEnabled(!busy);
    m_draftButton->setEnabled(!busy && m_draftSupported);
    m_testButton->setEnabled(!busy);
    m_cancelButton->setEnabled(busy);
    if (busy) {
        m_applyButton->setEnabled(false);
    }
    else
    {
        m_applyButton->setEnabled(m_hasMap || m_hasDraft);
    }
}

void AiPanel::setDraftContext(const GenerateDraftRequest &request, const QString &error)
{
    m_draftContext = request;
    m_contextError = error;
    m_targetSheet = request.sheetId;
}
void AiPanel::setDraftSupported(bool supported)
{
    m_draftSupported = supported;
    m_draftButton->setEnabled(supported && m_service && !m_service->status().busy);
    m_workflows->setTabEnabled(1, supported);
}
void AiPanel::clearProposal()
{
    m_hasMap = false;
    m_hasDraft = false;
    m_proposalRequestId.clear();
    m_applyButton->setEnabled(false);
    m_draftEditor->hide();
    m_mapTree->hide();
}
void AiPanel::reportApplyResult(const QString &error)
{
    if (!error.isEmpty())
    {
        m_statusLabel->setText(QStringLiteral("未采用：") + error);
        return;
    }
    clearProposal();
    m_statusLabel->setText(
        QStringLiteral("已采用，可在编辑器撤销；继续编辑文字稿请打开工程文字稿"));
}
QString AiPanel::currentRequestId() const
{
    return m_proposalRequestId.isEmpty() ? m_currentRequestId : m_proposalRequestId;
}
QString AiPanel::draftPurpose() const
{
    return m_generatedDraftPurpose;
}
void AiPanel::cancelPending()
{
    if (m_service && m_service->status().busy)
        onCancel();
    clearProposal();
}

}  // namespace lingya
