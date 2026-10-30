#include <QtTest>

#include "ai/PromptLibrary.h"

#include <QFile>

using namespace lingya;

// 提示模板测试（X05/X06）：版本号 + 模板可用性 + 注释剔除。
class TstPromptLibrary : public QObject {
    Q_OBJECT

private slots:
    void versionIsPositive();
    void mapPromptIsUsable();
    void draftPromptIsUsable();
    void commentsAreStripped();
    void fallbackMatchesResourceFile();
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

namespace {

// 与 PromptLibrary 内部剔除规则独立实现，用于交叉核对
QString filePromptWithoutComments(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QStringList kept;
    for (const QString& line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        if (!line.trimmed().startsWith(QLatin1Char('#'))) {
            kept.append(line);
        }
    }
    return kept.join(QLatin1Char('\n')).trimmed();
}

}  // namespace

void TstPromptLibrary::fallbackMatchesResourceFile() {
    // appcore 不链接 qrc，测试走内置回退；一旦有人只改 .txt 不改回退（或反之），
    // 应用发出的提示与测试覆盖的提示就会分叉，这里直接比对两份文本。
    const QString sourceDir = QStringLiteral(LINGYA_SOURCE_DIR);
    const QString mapFile = filePromptWithoutComments(sourceDir + QStringLiteral("/resources/prompts/map-v1.txt"));
    const QString draftFile = filePromptWithoutComments(sourceDir + QStringLiteral("/resources/prompts/draft-v1.txt"));
    QVERIFY2(!mapFile.isEmpty(), "读不到 map-v1.txt，检查 LINGYA_SOURCE_DIR 定义");
    QVERIFY2(!draftFile.isEmpty(), "读不到 draft-v1.txt，检查 LINGYA_SOURCE_DIR 定义");
    QCOMPARE(PromptLibrary::mapPrompt(), mapFile);
    QCOMPARE(PromptLibrary::draftPrompt(), draftFile);

    // 版本注释行必须与 kVersion 一致，避免“改了内容忘了升版本”
    QFile mapSource(sourceDir + QStringLiteral("/resources/prompts/map-v1.txt"));
    QVERIFY(mapSource.open(QIODevice::ReadOnly));
    const QString firstLine = QString::fromUtf8(mapSource.readLine());
    QVERIFY2(firstLine.contains(QStringLiteral("版本 %1").arg(PromptLibrary::kVersion)),
             qPrintable(firstLine));
}

QTEST_MAIN(TstPromptLibrary)
#include "tst_promptlibrary.moc"
