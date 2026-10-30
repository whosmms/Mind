#include <QtTest>

#include "ai/AiService.h"
#include "ai/FakeAiProvider.h"
#include "ai/SecretStore.h"
#include "ui/AiPanel.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTextBrowser>

using namespace lingya;

// AI 面板测试（X05/X06）：材料→生成→预览→应用；失败与取消提示。
class TstAiPanel : public QObject {
    Q_OBJECT

private slots:
    void idleDisablesApply();
    void mapGenerationShowsPreviewAndApplies();
    void failureShowsErrorCode();
    void cancelShowsCanceled();
    void testConnectionShowsModel();
    void providerRebuildCancelsBeforeDestroy();
    void providerRebuildShowsAbortedStatus();
};

void TstAiPanel::idleDisablesApply() {
    FakeAiProvider provider;
    SessionSecretStore store;
    AiService service(&provider, &store);
    AiPanel panel(&service);

    auto* applyButton = panel.findChild<QPushButton*>(QStringLiteral("aiApply"));
    QVERIFY(applyButton != nullptr);
    QVERIFY(!applyButton->isEnabled());
}

void TstAiPanel::mapGenerationShowsPreviewAndApplies() {
    FakeAiProvider provider;
    SessionSecretStore store;
    AiService service(&provider, &store);
    AiPanel panel(&service);
    QVERIFY(service.setKey(QStringLiteral("sk-test"), false));

    panel.setSourceText(QStringLiteral("材料内容"));
    QSignalSpy applySpy(&panel, &AiPanel::applyMapRequested);

    auto* mapButton = panel.findChild<QPushButton*>(QStringLiteral("aiGenerateMap"));
    auto* applyButton = panel.findChild<QPushButton*>(QStringLiteral("aiApply"));
    auto* preview = panel.findChild<QTextBrowser*>(QStringLiteral("aiPreview"));
    QVERIFY(mapButton != nullptr);
    QVERIFY(applyButton != nullptr);
    QVERIFY(preview != nullptr);

    mapButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(applyButton->isEnabled(), 3000);
    QVERIFY(preview->toPlainText().contains(QStringLiteral("软件工程")));

    applyButton->click();
    QCOMPARE(applySpy.count(), 1);
}

void TstAiPanel::failureShowsErrorCode() {
    FakeAiProvider provider;
    provider.setFailureMode(FakeAiProvider::FailureMode::AuthFailed);
    SessionSecretStore store;
    AiService service(&provider, &store);
    AiPanel panel(&service);
    QVERIFY(service.setKey(QStringLiteral("bad"), false));

    auto* status = panel.findChild<QLabel*>(QStringLiteral("aiStatus"));
    QVERIFY(status != nullptr);

    panel.findChild<QPushButton*>(QStringLiteral("aiGenerateMap"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("AUTH_FAILED")), 3000);
}

void TstAiPanel::cancelShowsCanceled() {
    FakeAiProvider provider;
    provider.setLatencyMs(3000);   // 让请求挂住，等取消
    SessionSecretStore store;
    AiService service(&provider, &store);
    AiPanel panel(&service);
    QVERIFY(service.setKey(QStringLiteral("sk-test"), false));

    auto* status = panel.findChild<QLabel*>(QStringLiteral("aiStatus"));
    auto* cancelButton = panel.findChild<QPushButton*>(QStringLiteral("aiCancel"));
    QVERIFY(status != nullptr);
    QVERIFY(cancelButton != nullptr);

    panel.findChild<QPushButton*>(QStringLiteral("aiGenerateMap"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(cancelButton->isEnabled(), 1000);
    cancelButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("已取消")), 3000);
}

void TstAiPanel::testConnectionShowsModel() {
    FakeAiProvider provider;
    SessionSecretStore store;
    AiService service(&provider, &store);
    AiPanel panel(&service);
    QVERIFY(service.setKey(QStringLiteral("sk-test"), false));

    auto* status = panel.findChild<QLabel*>(QStringLiteral("aiStatus"));
    auto* testButton = panel.findChild<QPushButton*>(QStringLiteral("aiTestConnection"));
    QVERIFY(status != nullptr);
    QVERIFY(testButton != nullptr);

    testButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("连接成功")), 3000);
}

namespace {

// 记录 start/cancel/析构顺序：只断言"不崩"不够，UB 有时不崩（如去掉在途请求）
class RecordingProvider : public IAiProvider {
public:
    explicit RecordingProvider(QStringList* log, QObject* parent = nullptr)
        : IAiProvider(parent), m_log(log) {}
    ~RecordingProvider() override {
        m_log->append(QStringLiteral("destroyed"));
    }

    void start(const QString&, const QJsonObject&) override {
        m_log->append(QStringLiteral("start"));
    }
    void cancel(const QString&) override {
        m_log->append(QStringLiteral("cancel"));
    }

private:
    QStringList* m_log;
};

}  // namespace

void TstAiPanel::providerRebuildCancelsBeforeDestroy() {
    QStringList log;
    SessionSecretStore store;
    QVERIFY(store.setApiKey(QStringLiteral("k1"), false));

    AiService service(nullptr, &store);
    service.setProviderFactory(
        [&log] { return std::unique_ptr<IAiProvider>(new RecordingProvider(&log)); });

    GenerateMapRequest request;
    request.requestId = QStringLiteral("req-1");
    request.text = QStringLiteral("材料");
    QVERIFY(service.generateMap(request));   // 在途：RecordingProvider 不会完成

    // 密钥相同：不重建（避免打断在途请求）
    QVERIFY(service.setKey(QStringLiteral("k1"), false));
    QCOMPARE(log.count(QStringLiteral("start")), 1);
    QCOMPARE(log.count(QStringLiteral("destroyed")), 0);

    QVERIFY(service.setKey(QStringLiteral("k2"), false));
    const int cancelIndex = log.indexOf(QStringLiteral("cancel"));
    const int destroyedIndex = log.indexOf(QStringLiteral("destroyed"));
    QVERIFY2(cancelIndex >= 0, "旧 provider 的 cancel 未被调用");
    QVERIFY2(destroyedIndex >= 0, "旧 provider 未被析构");
    QVERIFY2(cancelIndex < destroyedIndex, "cancel 在析构之后调用（在已释放对象上）");
}

void TstAiPanel::providerRebuildShowsAbortedStatus() {
    QStringList log;
    SessionSecretStore store;
    QVERIFY(store.setApiKey(QStringLiteral("k1"), false));

    AiService service(nullptr, &store);
    AiPanel panel(&service);
    service.setProviderFactory(
        [&log] { return std::unique_ptr<IAiProvider>(new RecordingProvider(&log)); });

    auto* status = panel.findChild<QLabel*>(QStringLiteral("aiStatus"));
    auto* mapButton = panel.findChild<QPushButton*>(QStringLiteral("aiGenerateMap"));
    QVERIFY(status != nullptr);
    QVERIFY(mapButton != nullptr);

    mapButton->click();   // RecordingProvider 不完成，面板停在"请求中…"
    QVERIFY(status->text().contains(QStringLiteral("请求中")));

    // 换密钥触发重建：在途请求要显式告知用户，不能被随后的"就绪"盖掉
    QVERIFY(service.setKey(QStringLiteral("k2"), false));
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("中止")), 3000);
}

QTEST_MAIN(TstAiPanel)
#include "tst_aipanel.moc"
