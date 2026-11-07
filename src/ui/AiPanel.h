#pragma once
#include "ai/AiService.h"
#include "ai/AiTypes.h"
#include "domain/Domain.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTextBrowser;
class QCheckBox;
class QListWidget;
class QTreeWidget;
class QTabWidget;

namespace lingya {

// AI 面板（X05/X06，规范§7.2/§10.4）：材料输入 → 生成导图/文稿 → 预览 → 应用。
// 只负责交互与展示；请求经 AiService，应用由调用方接 applyMapRequested/applyDraftRequested
// 后走命令层（AiCommands），本类不直接修改 Project。
class AiPanel : public QWidget {
    Q_OBJECT
public:
    explicit AiPanel(AiService* service, QWidget* parent = nullptr);

    // 把当前选中节点内容作为材料预填
    void setSourceText(const QString& text);
    // 文稿/导图的目标工作表（空的导图请求不填）
    void setTargetSheet(const ID& sheetId);
    void setDraftContext(const GenerateDraftRequest &request, const QString &error = {});
    void setDraftSupported(bool supported);
    void reportApplyResult(const QString &error);
    QString currentRequestId() const;
    QString draftPurpose() const;
    void cancelPending();
    QWidget *actionBar() const
    {
        return m_actionBar;
    }

  signals:
    void contextRequested(bool draft);
    void applyMapRequested(const MapProposal& proposal);
    void applyDraftRequested(const DraftProposal& proposal);

private:
    void onGenerateMap();
    void onGenerateDraft();
    void onApply();
    void onCancel();
    void onSaveKey();
    void onTestConnection();
    void onStateChanged(const QString& requestId, AiService::State state);
    void onMapReady(const QString& requestId, const MapProposal& proposal);
    void onDraftReady(const QString& requestId, const DraftProposal& proposal);
    void onFailed(const AiError& error);
    void onCanceled(const QString& requestId);
    void onConnectionReady(const QString& requestId, const QString& model);
    void showPreview(const QString& text);
    void setBusy(bool busy);
    void clearProposal();

    AiService* m_service = nullptr;
    ID m_targetSheet;

    QLineEdit* m_keyEdit = nullptr;
    QPushButton* m_saveKeyButton = nullptr;
    QPushButton* m_testButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPlainTextEdit* m_sourceEdit = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QSpinBox* m_maxNodesSpin = nullptr;
    QSpinBox* m_maxDepthSpin = nullptr;
    QPushButton* m_mapButton = nullptr;
    QPushButton* m_draftButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_applyButton = nullptr;
    QTextBrowser* m_preview = nullptr;
    QWidget *m_actionBar = nullptr;
    QTreeWidget *m_mapTree = nullptr;
    QTabWidget *m_workflows = nullptr;
    QCheckBox *m_rememberKey = nullptr;
    QLineEdit *m_requirements = nullptr;
    QComboBox *m_purpose = nullptr;
    QSpinBox *m_targetChars = nullptr;
    QWidget *m_draftEditor = nullptr;
    QListWidget *m_draftSections = nullptr;
    QLineEdit *m_sectionHeading = nullptr;
    QPlainTextEdit *m_sectionText = nullptr;
    int m_editingSection = -1;
    bool m_draftSupported = true;
    GenerateDraftRequest m_draftContext;
    QString m_contextError, m_proposalRequestId;
    QString m_generatedDraftPurpose = "summary";

    MapProposal m_map;
    DraftProposal m_draft;
    bool m_hasMap = false;
    bool m_hasDraft = false;
    QString m_currentRequestId;   // 取消时要用真实 requestId
};

}  // namespace lingya
