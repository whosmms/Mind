#include "services/AppearanceService.h"
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
using namespace lingya;
class TstAppearance : public QObject
{
    Q_OBJECT
  private slots:
    void defaultsAndRoundTrip();
    void rejectsDuplicateAndTypingKeys();
    void restoresUnknownSettings();
    void stylesUseBundledResource();
};
void TstAppearance::defaultsAndRoundTrip()
{
    QTemporaryDir dir;
    QSettings settings(dir.filePath("偏好.ini"), QSettings::IniFormat);
    auto p = AppearanceService::read(settings);
    QCOMPARE(p.theme, "mint");
    QVERIFY(p.shortcuts.isEmpty());
    p.theme = "lavender";
    p.shortcuts.insert("newProject", "Ctrl+Alt+N");
    p.shortcuts.insert("openProject", {});
    QVERIFY(std::holds_alternative<bool>(AppearanceService::write(settings, p)));
    QSettings reopened(settings.fileName(), QSettings::IniFormat);
    const auto loaded = AppearanceService::read(reopened);
    QCOMPARE(loaded.theme, p.theme);
    QCOMPARE(loaded.shortcuts, p.shortcuts);
}
void TstAppearance::rejectsDuplicateAndTypingKeys()
{
    QTemporaryDir dir;
    QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat);
    AppearanceService::Preferences p;
    p.shortcuts = {{"first", "Ctrl+N"}, {"second", "Ctrl+N"}};
    QVERIFY(std::holds_alternative<AppError>(AppearanceService::write(settings, p)));
    QVERIFY(!settings.contains("appearance/shortcuts"));
    p.shortcuts = {{"first", "N"}};
    QVERIFY(std::holds_alternative<AppError>(AppearanceService::write(settings, p)));
    p.shortcuts = {{"first", "Alt+F4"}};
    QVERIFY(std::holds_alternative<AppError>(AppearanceService::write(settings, p)));
    p.shortcuts = {{"first", "Ctrl+K, Ctrl+P"}};
    QVERIFY(std::holds_alternative<AppError>(AppearanceService::write(settings, p)));
}
void TstAppearance::restoresUnknownSettings()
{
    QTemporaryDir dir;
    QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat);
    settings.setValue("appearance/theme", "unknown");
    QCOMPARE(AppearanceService::read(settings).theme, "mint");
}
void TstAppearance::stylesUseBundledResource()
{
    const QImage icon(QStringLiteral(":/lingya/icons/app.png"));
    QVERIFY(!icon.isNull());
    QCOMPARE(icon.size(), QSize(512, 512));
    const auto mint = AppearanceService::styleSheet("mint"),
               lavender = AppearanceService::styleSheet("lavender"),
               projection = AppearanceService::styleSheet("projection");
    QVERIFY(!mint.isEmpty());
    QVERIFY(mint.contains("#247A62"));
    QVERIFY(lavender.contains("#675087"));
    QVERIFY(!lavender.contains("#247A62"));
    QVERIFY(projection.contains("#101D16"));
}
QTEST_MAIN(TstAppearance)
#include "tst_appearance.moc"
