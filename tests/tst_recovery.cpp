#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "domain/Domain.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "services/RecoveryService.h"

using namespace lingya;

// 恢复快照服务测试（规范§11.2/§11.3、原设计§8；§19.1错误路径与临时目录隔离）。
// 全部使用QTemporaryDir注入恢复根目录，不触碰真实AppLocalData与用户工程。
class TstRecovery : public QObject
{
    Q_OBJECT

  private slots:
    void defaultRootEndsWithRecovery();
    void writeListLoadRoundtrip();
    void writeSkipsIdenticalContent();
    void pruneKeepsFiveNewestPerProject();
    void listSkipsCorruptAndForeignFiles();
    void loadRejectsCorruptSnapshot();
    void clearRemovesOnlyOwnSnapshots();
    void rejectsUnsafeIdentifiers();
    void listEmptyForUnknownProjectOrRoot();

  private:
    // 生成一个合法工程；suffix拼入标题用于区分内容（内容不同才不会被去重跳过）
    static Project makeProject(const QString &title)
    {
        return ProjectFactory::createMindmap(QStringLiteral("未命名导图") + title);
    }

    static bool writeOk(const QString &root, const Project &project)
    {
        const auto result = RecoveryService::writeSnapshot(root, project);
        return std::holds_alternative<RecoveryService::SnapshotWrite>(result) &&
               std::get<RecoveryService::SnapshotWrite>(result).written;
    }

    // 把工程根主题改名，制造与上一份快照不同的内容（测试夹具直接构造值，非运行时行为）
    static void renameRoot(Project &project, const QString &title)
    {
        for (Sheet &sheet : project.sheets)
        {
            if (auto *tree = std::get_if<TreeSheet>(&sheet))
            {
                tree->nodes[tree->rootId].title = title;
                ++tree->nodes[tree->rootId].contentRevision;
            }
        }
    }
};

void TstRecovery::defaultRootEndsWithRecovery()
{
    const QString root = RecoveryService::defaultRoot();
    QVERIFY(!root.isEmpty());
    QVERIFY(
        root.endsWith(QStringLiteral("recovery"))); // QStandardPaths::AppLocalDataLocation/recovery
}

void TstRecovery::writeListLoadRoundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Project project = makeProject(QStringLiteral("快照往返"));
    const qint64 revision = project.revision;
    const auto written = RecoveryService::writeSnapshot(dir.path(), project);
    QVERIFY(std::holds_alternative<RecoveryService::SnapshotWrite>(written));
    const QString snapshotId = std::get<RecoveryService::SnapshotWrite>(written).snapshotId;
    QVERIFY(!snapshotId.isEmpty());

    const QVector<RecoveryService::SnapshotInfo> snapshots =
        RecoveryService::list(dir.path(), project.id);
    QCOMPARE(snapshots.size(), 1);
    QCOMPARE(snapshots.first().projectId, project.id);
    QCOMPARE(snapshots.first().snapshotId, snapshotId);
    QCOMPARE(snapshots.first().title, QStringLiteral("未命名导图快照往返"));
    QCOMPARE(snapshots.first().revision, revision);
    // 文件名时间戳换算的时间应接近当前时刻（允许10秒时钟容差）
    QVERIFY(qAbs(snapshots.first().savedAt.toMSecsSinceEpoch() -
                 QDateTime::currentMSecsSinceEpoch()) < 10000);

    const auto loaded = RecoveryService::load(dir.path(), project.id, snapshotId);
    QVERIFY(std::holds_alternative<Project>(loaded));
    QCOMPARE(std::get<Project>(loaded).title, QStringLiteral("未命名导图快照往返"));
}

void TstRecovery::writeSkipsIdenticalContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Project project = makeProject(QStringLiteral("去重"));
    QVERIFY(writeOk(dir.path(), project));
    // 内容未变化：第二次写入被跳过（written=false），不产生新文件
    const auto second = RecoveryService::writeSnapshot(dir.path(), project);
    QVERIFY(std::holds_alternative<RecoveryService::SnapshotWrite>(second));
    QVERIFY(!std::get<RecoveryService::SnapshotWrite>(second).written);
    QCOMPARE(RecoveryService::list(dir.path(), project.id).size(), 1);

    renameRoot(project, QStringLiteral("变化后的根主题"));
    QVERIFY(writeOk(dir.path(), project)); // 内容变化：写入第二份
    QCOMPARE(RecoveryService::list(dir.path(), project.id).size(), 2);
}

void TstRecovery::pruneKeepsFiveNewestPerProject()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Project project = makeProject(QStringLiteral("修剪"));
    for (int i = 1; i <= 7; ++i)
    {
        renameRoot(project, QStringLiteral("版本%1").arg(i));
        QVERIFY(writeOk(dir.path(), project));
    }

    const QVector<RecoveryService::SnapshotInfo> snapshots =
        RecoveryService::list(dir.path(), project.id);
    QCOMPARE(snapshots.size(), Limits::kMaxRecoverySnapshots); // 每工程保留最近5份
    const auto loaded = RecoveryService::load(dir.path(), project.id, snapshots.first().snapshotId);
    QVERIFY(std::holds_alternative<Project>(loaded));
    // 最新一份是第7版；更旧的版本已被修剪
    const Project &newest = std::get<Project>(loaded);
    QString newestRootTitle;
    for (const Sheet &sheet : newest.sheets)
        if (const auto *tree = std::get_if<TreeSheet>(&sheet))
            newestRootTitle = tree->nodes.value(tree->rootId).title;
    QCOMPARE(newestRootTitle, QStringLiteral("版本7"));
    QVERIFY(snapshots.first().savedAt >= snapshots.last().savedAt); // 新→旧排序
    QCOMPARE(QDir(dir.path() + QLatin1Char('/') + project.id)
                 .entryList(QStringList{QStringLiteral("*.lingya")}, QDir::Files)
                 .size(),
             Limits::kMaxRecoverySnapshots);
}

void TstRecovery::listSkipsCorruptAndForeignFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Project project = makeProject(QStringLiteral("损坏跳过"));
    QVERIFY(writeOk(dir.path(), project));

    // 同目录放一个损坏快照（数字命名）与一个非本服务命名的文件
    const QString projectDir = dir.path() + QLatin1Char('/') + project.id;
    QVERIFY(QFile(projectDir + QStringLiteral("/12345.lingya")).open(QIODevice::WriteOnly));
    QFile foreign(projectDir + QStringLiteral("/说明.txt"));
    QVERIFY(foreign.open(QIODevice::WriteOnly));
    foreign.write("用户自己的文件");
    foreign.close();

    const QVector<RecoveryService::SnapshotInfo> snapshots =
        RecoveryService::list(dir.path(), project.id);
    QCOMPARE(snapshots.size(), 1); // 损坏快照跳过，成功快照仍列出
    QVERIFY(QFileInfo::exists(projectDir + QStringLiteral("/说明.txt")));     // 外来文件不动
    QVERIFY(QFileInfo::exists(projectDir + QStringLiteral("/12345.lingya"))); // 损坏文件不擅自删除
}

void TstRecovery::loadRejectsCorruptSnapshot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString projectDir = dir.path() + QLatin1Char('/') + QStringLiteral("工程");
    QVERIFY(QDir().mkpath(projectDir));
    QFile bad(projectDir + QStringLiteral("/999.lingya"));
    QVERIFY(bad.open(QIODevice::WriteOnly | QIODevice::Text));
    bad.write("{ 不是合法工程JSON");
    bad.close();

    const auto loaded =
        RecoveryService::load(dir.path(), QStringLiteral("工程"), QStringLiteral("999"));
    QVERIFY(std::holds_alternative<AppError>(loaded));
}

void TstRecovery::clearRemovesOnlyOwnSnapshots()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Project a = makeProject(QStringLiteral("工程甲"));
    Project b = makeProject(QStringLiteral("工程乙"));
    QVERIFY(writeOk(dir.path(), a));
    renameRoot(a, QStringLiteral("第二版"));
    QVERIFY(writeOk(dir.path(), a));
    QVERIFY(writeOk(dir.path(), b));

    // 在甲的快照目录放一个外来文件，清理必须保留
    const QString aDir = dir.path() + QLatin1Char('/') + a.id;
    QFile foreign(aDir + QStringLiteral("/备注.txt"));
    QVERIFY(foreign.open(QIODevice::WriteOnly));
    foreign.write("保留我");
    foreign.close();

    RecoveryService::clear(dir.path(), a.id);
    QCOMPARE(RecoveryService::list(dir.path(), a.id).size(), 0);    // 自建快照已清
    QVERIFY(QFileInfo::exists(aDir + QStringLiteral("/备注.txt"))); // 外来文件保留
    QCOMPARE(RecoveryService::list(dir.path(), b.id).size(), 1);    // 其他工程不受影响
}

void TstRecovery::rejectsUnsafeIdentifiers()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 工程ID含路径分隔符：拒绝写快照，也不产生目录
    Project evil = makeProject(QStringLiteral(" evil "));
    evil.id = QStringLiteral("../escape");
    QVERIFY(std::holds_alternative<AppError>(RecoveryService::writeSnapshot(dir.path(), evil)));
    QCOMPARE(std::get<AppError>(RecoveryService::writeSnapshot(dir.path(), evil)).code,
             QStringLiteral("INVALID_ARGUMENT"));
    QVERIFY(!QFileInfo::exists(dir.path() + QStringLiteral("/escape")));

    // 快照基名含分隔符：load拒绝
    const auto loaded =
        RecoveryService::load(dir.path(), QStringLiteral("工程"), QStringLiteral("../999"));
    QVERIFY(std::holds_alternative<AppError>(loaded));
    QCOMPARE(std::get<AppError>(loaded).code, QStringLiteral("INVALID_ARGUMENT"));

    // clear对非法ID为无操作
    RecoveryService::clear(dir.path(), QStringLiteral("../escape"));
    QVERIFY(QDir(dir.path()).isEmpty());

    // 根目录为空：写快照拒绝
    QVERIFY(std::holds_alternative<AppError>(
        RecoveryService::writeSnapshot(QString(), makeProject(QStringLiteral("无根目录")))));
}

void TstRecovery::listEmptyForUnknownProjectOrRoot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(RecoveryService::list(dir.path(), QStringLiteral("从未写过的工程")).isEmpty());
    QVERIFY(RecoveryService::listAll(dir.path()).isEmpty());
    QVERIFY(RecoveryService::listAll(dir.filePath(QStringLiteral("不存在"))).isEmpty());
}

QTEST_MAIN(TstRecovery)
#include "tst_recovery.moc"
