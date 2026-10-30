#include <QtTest>

#include "ai/PromptLibrary.h"

using namespace lingya;

// 提示模板测试（X05/X06）：版本号 + 模板可用性 + 注释剔除。
class TstPromptLibrary : public QObject {
    Q_OBJECT

private slots:
    void versionIsPositive();
    void mapPromptIsUsable();
    void draftPromptIsUsable();
    void commentsAreStripped();
};

void TstPromptLibrary::versionIsPositive() {
    QVERIFY(PromptLibrary::kVersion >= 1);
}

void TstPromptLibrary::mapPromptIsUsable() {
    const QString prompt = PromptLibrary::mapPrompt();
    QVERIFY(!prompt.isEmpty());
    QVERIFY(!prompt.startsWith(QLatin1Char('#')));   // 版本注释不发给模型
    QVERIFY(prompt.contains(QStringLiteral("JSON")));
}

void TstPromptLibrary::draftPromptIsUsable() {
    const QString prompt = PromptLibrary::draftPrompt();
    QVERIFY(!prompt.isEmpty());
    QVERIFY(!prompt.startsWith(QLatin1Char('#')));
    QVERIFY(prompt.contains(QStringLiteral("JSON")));
}

void TstPromptLibrary::commentsAreStripped() {
    // 资源不存在时返回空串，由调用方回退内置文本
    QVERIFY(PromptLibrary::fromResource(QStringLiteral(":/lingya/prompts/not-exist.txt")).isEmpty());
}

QTEST_MAIN(TstPromptLibrary)
#include "tst_promptlibrary.moc"
