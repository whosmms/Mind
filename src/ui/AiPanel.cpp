#include "ui/AiPanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBrowser>
#include <QUuid>
#include <QVBoxLayout>

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

QString renderDraft(const DraftProposal& proposal) {
    QStringList lines;
    lines << QStringLiteral("标题：%1").arg(proposal.title);
    for (const DraftSection& section : proposal.sections) {
        lines << QStringLiteral("## %1").arg(section.heading);
        lines << section.text;
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

    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget(m_keyEdit, 1);
    keyRow->addWidget(m_saveKeyButton);
    keyRow->addWidget(m_testButton);

    m_sourceEdit = new QPlainTextEdit(this);
    m_sourceEdit->setObjectName(QStringLiteral("aiSource"));
    m_sourceEdit->setPlaceholderText(QStringLiteral("粘贴材料；也可由主窗口预填选中节点内容"));

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(QStringLiteral("faithful（只整理原文）"), QStringLiteral("faithful"));
    m_modeCombo->addItem(QStringLiteral("supplement（允许补充）"), QStringLiteral("supplement"));

    m_maxNodesSpin = new QSpinBox(this);
    m_maxNodesSpin->setRange(1, 200);
    m_maxNodesSpin->setValue(50);
    m_maxDepthSpin = new QSpinBox(this);
    m_maxDepthSpin->setRange(1, 6);
    m_maxDepthSpin->setValue(4);

    auto* paramForm = new QFormLayout;
    paramForm->addRow(QStringLiteral("模式"), m_modeCombo);
    paramForm->addRow(QStringLiteral("最多节点"), m_maxNodesSpin);
    paramForm->addRow(QStringLiteral("最大层级"), m_maxDepthSpin);

    m_mapButton = new QPushButton(QStringLiteral("生成导图"), this);
    m_mapButton->setObjectName(QStringLiteral("aiGenerateMap"));
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

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(keyRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_sourceEdit, 1);
    layout->addLayout(paramForm);
    layout->addLayout(actionRow);
    layout->addWidget(m_preview, 2);

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
    if (m_service->setKey(m_keyEdit->text().trimmed(), false)) {
        m_statusLabel->setText(QStringLiteral("密钥已保存（不写入工程）"));
    } else {
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
    GenerateMapRequest request;
    request.requestId = newRequestId();
    request.text = m_sourceEdit->toPlainText();
    request.mode = m_modeCombo->currentData().toString();
    request.maxNodes = m_maxNodesSpin->value();
    request.maxDepth = m_maxDepthSpin->value();
    m_currentRequestId = request.requestId;
    m_service->generateMap(request);   // 失败时 AiService 已同步 emit failed，不再覆盖其提示
}

void AiPanel::onGenerateDraft() {
    if (m_service == nullptr) {
        return;
    }
    GenerateDraftRequest request;
    request.requestId = newRequestId();
    request.sheetId = m_targetSheet;
    request.mode = m_modeCombo->currentData().toString();
    m_currentRequestId = request.requestId;
    m_service->generateDraft(request);   // 同上：不覆盖 AiService 的错误提示
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
    Q_UNUSED(requestId)
    m_map = proposal;
    m_hasMap = true;
    m_hasDraft = false;
    showPreview(renderMap(proposal));
    m_applyButton->setEnabled(true);
}

void AiPanel::onDraftReady(const QString& requestId, const DraftProposal& proposal) {
    Q_UNUSED(requestId)
    m_draft = proposal;
    m_hasDraft = true;
    m_hasMap = false;
    showPreview(renderDraft(proposal));
    m_applyButton->setEnabled(true);
}

void AiPanel::onFailed(const AiError& error) {
    m_statusLabel->setText(QStringLiteral("失败：%1（%2）").arg(error.message, error.code));
    m_applyButton->setEnabled(false);
}

void AiPanel::onCanceled(const QString& requestId) {
    Q_UNUSED(requestId)
    m_statusLabel->setText(QStringLiteral("已取消"));
    m_applyButton->setEnabled(false);
}

void AiPanel::showPreview(const QString& text) {
    m_preview->setPlainText(text);
}

void AiPanel::setBusy(bool busy) {
    m_mapButton->setEnabled(!busy);
    m_draftButton->setEnabled(!busy);
    m_testButton->setEnabled(!busy);
    m_cancelButton->setEnabled(busy);
    if (busy) {
        m_applyButton->setEnabled(false);
    }
}

}  // namespace lingya
