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

QTEST_MAIN(TstAiPanel)
#include "tst_aipanel.moc"
