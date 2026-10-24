#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include "domain/Domain.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "services/FileService.h"
#include "services/SettingsService.h"

using namespace lingya;

// 文件服务与非敏感设置测试（规范§19.1：Domain/JSON往返、文件恢复错误路径、
// 路径中文与含空格必须测试）。全部使用QTemporaryDir，不触碰个人文档与真实磁盘。
class TstFileService : public QObject
{
    Q_OBJECT

  private slots:
    void saveLoadRoundtripWithChineseAndSpaces();
    void loadRejectsMissingFile();
    void loadRejectsOversizedFile();
    void loadRejectsBadBytes();
    void saveCreatesBackupWithPreviousContent();
    void backupFailureKeepsOriginalFile();
    void saveRejectsUnwritablePath();
    void saveRejectsEmptyPath();
    void saveRejectsInvalidProject();

    void recentFilesMostRecentFirstAndDedup();
    void recentFilesTruncateToLimit();
    void recentFilesRemoveEntry();
    void recentFilesToleratesBlankEntries();
    void recentFilesChineseAndSpacesRoundtrip();

  private:
    // 生成一个合法的最小工程（根主题标题可指定）
    static Project makeProject(const QString &title)
    {
        return ProjectFactory::createMindmap(title);
    }

    // 读取文件全部内容；QFile必须显式open，临时对象直接readAll会得到空数组
    static QByteArray readAllBytes(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray();
        return file.readAll();
    }
};

void TstFileService::saveLoadRoundtripWithChineseAndSpaces()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("中文 目录/工程 文件.lingya"));

    Project source = makeProject(QStringLiteral("中文标题 测试"));
    const auto saved = FileService::save(path, source);
    QVERIFY(std::holds_alternative<qint64>(saved));

    const auto loaded = FileService::load(path);
    QVERIFY(std::holds_alternative<Project>(loaded));
    const Project &project = std::get<Project>(loaded);
    QCOMPARE(project.title, QStringLiteral("中文标题 测试"));
    QCOMPARE(project.revision, source.revision);
}

void TstFileService::loadRejectsMissingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto loaded = FileService::load(dir.filePath(QStringLiteral("不存在.lingya")));
    QVERIFY(std::holds_alternative<AppError>(loaded));
    QCOMPARE(std::get<AppError>(loaded).code, QStringLiteral("FILE_NOT_FOUND"));
}

void TstFileService::loadRejectsOversizedFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("超大.lingya"));
    QFile big(path);
    QVERIFY(big.open(QIODevice::WriteOnly));
    big.write(QByteArray(Limits::kMaxFileBytes + 1, 'x'));
    big.close();

    const auto loaded = FileService::load(path);
    QVERIFY(std::holds_alternative<AppError>(loaded));
    QCOMPARE(std::get<AppError>(loaded).code, QStringLiteral("LIMIT_EXCEEDED"));
}

void TstFileService::loadRejectsBadBytes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("坏文件.lingya"));
    QFile bad(path);
    QVERIFY(bad.open(QIODevice::WriteOnly | QIODevice::Text));
    bad.write("{ 这不是合法JSON");
    bad.close();

    // 坏文件返回错误而非崩溃；错误码由ProjectJson契约决定（tst_projectjson覆盖）
    const auto loaded = FileService::load(path);
    QVERIFY(std::holds_alternative<AppError>(loaded));
}

void TstFileService::saveCreatesBackupWithPreviousContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("备份验证.lingya"));

    Project first = makeProject(QStringLiteral("第一版"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, first)));
    const QByteArray firstBytes = readAllBytes(path);
    QVERIFY(!firstBytes.isEmpty());

    Project second = makeProject(QStringLiteral("第二版"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, second)));

    QFile backup(path + QStringLiteral(".bak"));
    QVERIFY(backup.exists()); // 旧文件先备份（规范§11.3）
    QCOMPARE(readAllBytes(backup.fileName()), firstBytes);
    QCOMPARE(readAllBytes(path) == firstBytes, false);
}

void TstFileService::backupFailureKeepsOriginalFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("备份失败.lingya"));
    Project first = makeProject(QStringLiteral("原件内容"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, first)));
    const QByteArray originalBytes = readAllBytes(path);
    QVERIFY(!originalBytes.isEmpty());

    // 用同名目录占住.bak路径，使备份复制失败
    QVERIFY(QDir().mkpath(path + QStringLiteral(".bak")));

    Project second = makeProject(QStringLiteral("不应写入"));
    const auto saved = FileService::save(path, second);
    QVERIFY(std::holds_alternative<AppError>(saved));
    QCOMPARE(std::get<AppError>(saved).code, QStringLiteral("BACKUP_FAILED"));
    // 备份失败时原件保持不变（规范§11.3：不移动或删除唯一原件）
    QCOMPARE(readAllBytes(path), originalBytes);
}

void TstFileService::saveRejectsUnwritablePath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 父级是普通文件：路径非法，QSaveFile无法打开
    const QString parent = dir.filePath(QStringLiteral("普通文件.txt"));
    QFile file(parent);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();

    const auto saved = FileService::save(parent + QStringLiteral("/子目录/工程.lingya"),
                                         makeProject(QStringLiteral("写入失败")));
    QVERIFY(std::holds_alternative<AppError>(saved));
    QCOMPARE(std::get<AppError>(saved).code, QStringLiteral("FILE_WRITE_FAILED"));
}

void TstFileService::saveRejectsEmptyPath()
{
    const auto saved = FileService::save(QString(), makeProject(QStringLiteral("空路径")));
    QVERIFY(std::holds_alternative<AppError>(saved));
    QCOMPARE(std::get<AppError>(saved).code, QStringLiteral("INVALID_ARGUMENT"));
}

void TstFileService::saveRejectsInvalidProject()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 默认构造工程无标题无工作表，不能被写盘（规范§8：先校验再写出）
    const auto saved = FileService::save(dir.filePath(QStringLiteral("非法.lingya")), Project{});
    QVERIFY(std::holds_alternative<AppError>(saved));
    QCOMPARE(std::get<AppError>(saved).code, QStringLiteral("INVALID_PROJECT"));
}

void TstFileService::recentFilesMostRecentFirstAndDedup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);

    const QString a = dir.filePath(QStringLiteral("工程A.lingya"));
    const QString b = dir.filePath(QStringLiteral("工程B.lingya"));
    SettingsService::addRecentFile(settings, a);
    SettingsService::addRecentFile(settings, b);
    SettingsService::addRecentFile(settings, a); // 重复打开置顶而非新增

    const QVector<QString> recents = SettingsService::recentFiles(settings);
    QCOMPARE(recents.size(), 2);
    QCOMPARE(recents.at(0), QFileInfo(a).absoluteFilePath());
    QCOMPARE(recents.at(1), QFileInfo(b).absoluteFilePath());
}

void TstFileService::recentFilesTruncateToLimit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);

    for (int i = 0; i < Limits::kMaxRecentFiles + 2; ++i)
        SettingsService::addRecentFile(settings,
                                       dir.filePath(QStringLiteral("工程%1.lingya").arg(i)));

    const QVector<QString> recents = SettingsService::recentFiles(settings);
    QCOMPARE(recents.size(), Limits::kMaxRecentFiles);
    // 最旧的（工程0、工程1）被淘汰，最新保留在首位
    QCOMPARE(
        recents.first(),
        QFileInfo(dir.filePath(QStringLiteral("工程%1.lingya").arg(Limits::kMaxRecentFiles + 1)))
            .absoluteFilePath());
}

void TstFileService::recentFilesRemoveEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);

    const QString a = dir.filePath(QStringLiteral("工程A.lingya"));
    const QString b = dir.filePath(QStringLiteral("工程B.lingya"));
    SettingsService::addRecentFile(settings, a);
    SettingsService::addRecentFile(settings, b);
    SettingsService::removeRecentFile(settings, a);

    const QVector<QString> recents = SettingsService::recentFiles(settings);
    QCOMPARE(recents.size(), 1);
    QCOMPARE(recents.first(), QFileInfo(b).absoluteFilePath());
    // 移除不存在的记录是无操作
    SettingsService::removeRecentFile(settings, dir.filePath(QStringLiteral("没有过.lingya")));
    QCOMPARE(SettingsService::recentFiles(settings).size(), 1);
}

void TstFileService::recentFilesToleratesBlankEntries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("recentFiles"),
                      QStringList{QString(), QStringLiteral("  "),
                                  dir.filePath(QStringLiteral("有效.lingya")),
                                  dir.filePath(QStringLiteral("有效.lingya"))});

    const QVector<QString> recents = SettingsService::recentFiles(settings);
    QCOMPARE(recents.size(), 1);
    QCOMPARE(recents.first(),
             QFileInfo(dir.filePath(QStringLiteral("有效.lingya"))).absoluteFilePath());
}

void TstFileService::recentFilesChineseAndSpacesRoundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("设置 文件.ini")), QSettings::IniFormat);

    const QString path = dir.filePath(QStringLiteral("中文 工程 名.lingya"));
    SettingsService::addRecentFile(settings, path);
    settings.sync();

    QSettings reloaded(dir.filePath(QStringLiteral("设置 文件.ini")), QSettings::IniFormat);
    const QVector<QString> recents = SettingsService::recentFiles(reloaded);
    QCOMPARE(recents.size(), 1);
    QCOMPARE(recents.first(), QFileInfo(path).absoluteFilePath());
}

QTEST_MAIN(TstFileService)
#include "tst_fileservice.moc"
