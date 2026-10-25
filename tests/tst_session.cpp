#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "domain/ProjectFactory.h"
#include "services/SessionService.h"
#include "services/UserFileService.h"

using namespace lingya;

// 登录与用户文件区服务测试（D-M2-02）：本地账号（盐哈希/规则/持久）、
// 专有文件夹（按名保存/列表/打开）、归属判定（Own/Others/Public）。
// 全部使用QTemporaryDir注入根目录，不触碰真实AppLocalData。
class TstSession : public QObject
{
    Q_OBJECT

  private slots:
    void createAccountAcceptsAndRejectsDuplicate();
    void createAccountValidatesUserName();
    void createAccountValidatesPassword();
    void loginSucceedsAndRejectsBadCredentials();
    void loginNormalizesUserName();
    void accountFileStoresNoPlaintextPassword();
    void corruptAccountStoreRejected();
    void userDirLayout();
    void saveOwnedWritesAndRoundtrips();
    void saveOwnedRejectsTraversalNames();
    void saveOwnedOverwritesWithBackup();
    void listOwnedNewestFirstAndSkipsForeign();
    void classifyOwnOthersPublic();
    void openOwnedRejectsMissingFile();
    void accountNamesCannotShareDirectoryIdentity();
    void malformedUsersCollectionKeepsAccountStore();
    void duplicateDirectoryIdentityRejectedOnLoad();
    void windowsAccountNamesRejected_data();
    void windowsAccountNamesRejected();
    void ownedNameRejectsStreamBeforeCreatingFolder();

  private:
    static Project makeProject(const QString &title)
    {
        return ProjectFactory::createMindmap(title);
    }
};

void TstSession::createAccountAcceptsAndRejectsDuplicate()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        root.path(), QStringLiteral("甲同学"), QStringLiteral("abc123"))));
    const auto duplicate = SessionService::createAccount(root.path(), QStringLiteral("甲同学"),
                                                         QStringLiteral("other456"));
    QVERIFY(std::holds_alternative<AppError>(duplicate));
    QCOMPARE(std::get<AppError>(duplicate).code, QStringLiteral("CONFLICT"));
}

void TstSession::createAccountValidatesUserName()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const char *code = "INVALID_ARGUMENT";
    QVERIFY(std::get<AppError>(SessionService::createAccount(root.path(), QStringLiteral("甲"),
                                                             QStringLiteral("abc123")))
                .code == QLatin1String(code)); // 1码点过短
    QVERIFY(std::get<AppError>(SessionService::createAccount(root.path(), QStringLiteral("a/b"),
                                                             QStringLiteral("abc123")))
                .code == QLatin1String(code));
    QVERIFY(std::get<AppError>(SessionService::createAccount(root.path(), QStringLiteral(".."),
                                                             QStringLiteral("abc123")))
                .code == QLatin1String(code));
}

void TstSession::createAccountValidatesPassword()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto result = SessionService::createAccount(root.path(), QStringLiteral("甲同学"),
                                                      QStringLiteral("12345"));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_ARGUMENT"));
}

void TstSession::loginSucceedsAndRejectsBadCredentials()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        root.path(), QStringLiteral("甲同学"), QStringLiteral("abc123"))));

    const auto ok =
        SessionService::login(root.path(), QStringLiteral("甲同学"), QStringLiteral("abc123"));
    QVERIFY(std::holds_alternative<SessionService::AccountInfo>(ok));
    QCOMPARE(std::get<SessionService::AccountInfo>(ok).userName, QStringLiteral("甲同学"));

    const auto wrong =
        SessionService::login(root.path(), QStringLiteral("甲同学"), QStringLiteral("wrong!"));
    QVERIFY(std::holds_alternative<AppError>(wrong));
    QCOMPARE(std::get<AppError>(wrong).code, QStringLiteral("AUTH_FAILED"));

    const auto unknown =
        SessionService::login(root.path(), QStringLiteral("乙同学"), QStringLiteral("abc123"));
    QVERIFY(std::holds_alternative<AppError>(unknown));
    QCOMPARE(std::get<AppError>(unknown).code, QStringLiteral("AUTH_FAILED"));
}

void TstSession::loginNormalizesUserName()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        root.path(), QStringLiteral("  甲同学  "), QStringLiteral("abc123"))));
    const auto ok =
        SessionService::login(root.path(), QStringLiteral("甲同学"), QStringLiteral("abc123"));
    QVERIFY(std::holds_alternative<SessionService::AccountInfo>(ok)); // 去空白后一致
}

void TstSession::accountFileStoresNoPlaintextPassword()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        root.path(), QStringLiteral("甲同学"), QStringLiteral("秘密口令abc123"))));
    QFile store(root.path() + QStringLiteral("/accounts.json"));
    QVERIFY(store.open(QIODevice::ReadOnly));
    const QByteArray bytes = store.readAll();
    QVERIFY(!bytes.contains("秘密口令abc123")); // 不存明文
    QVERIFY(bytes.contains("salt"));            // 存盐与哈希字段
    QVERIFY(bytes.contains("hash"));
}

void TstSession::corruptAccountStoreRejected()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QFile bad(root.path() + QStringLiteral("/accounts.json"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("{ 坏数据");
    bad.close();
    const auto result =
        SessionService::login(root.path(), QStringLiteral("甲同学"), QStringLiteral("abc123"));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_PROJECT"));
}

void TstSession::userDirLayout()
{
    QTemporaryDir root;
    QCOMPARE(UserFileService::userDir(root.path(), QStringLiteral("甲同学")),
             root.path() + QStringLiteral("/users/甲同学"));
}

void TstSession::saveOwnedWritesAndRoundtrips()
{
    QTemporaryDir root;
    const auto saved = UserFileService::saveOwned(root.path(), QStringLiteral("甲同学"),
                                                  QStringLiteral("我的导图"),
                                                  makeProject(QStringLiteral("内容A")));
    QVERIFY(std::holds_alternative<QString>(saved));
    const QString path = std::get<QString>(saved);
    QVERIFY(path.endsWith(QStringLiteral("/users/甲同学/我的导图.lingya"))); // 自动补后缀
    QVERIFY(QFile::exists(path));

    const auto opened = UserFileService::openOwned(root.path(), QStringLiteral("甲同学"),
                                                   QStringLiteral("我的导图"));
    QVERIFY(std::holds_alternative<Project>(opened));
    QCOMPARE(std::get<Project>(opened).title, QStringLiteral("内容A"));
}

void TstSession::saveOwnedRejectsTraversalNames()
{
    QTemporaryDir root;
    const char *code = "INVALID_ARGUMENT";
    QVERIFY(std::get<AppError>(UserFileService::saveOwned(root.path(), QStringLiteral("甲同学"),
                                                          QStringLiteral("../越界"),
                                                          makeProject(QStringLiteral("x"))))
                .code == QLatin1String(code));
    QVERIFY(std::get<AppError>(UserFileService::saveOwned(root.path(), QStringLiteral("甲同学"),
                                                          QStringLiteral("   "),
                                                          makeProject(QStringLiteral("x"))))
                .code == QLatin1String(code));
    QVERIFY(!QDir(root.filePath(QStringLiteral("users"))).exists(QStringLiteral("越界.lingya")));
}

void TstSession::saveOwnedOverwritesWithBackup()
{
    QTemporaryDir root;
    QVERIFY(std::holds_alternative<QString>(
        UserFileService::saveOwned(root.path(), QStringLiteral("甲同学"), QStringLiteral("版本"),
                                   makeProject(QStringLiteral("第一版")))));
    QVERIFY(std::holds_alternative<QString>(UserFileService::saveOwned(
        root.path(), QStringLiteral("甲同学"), QStringLiteral("版本.lingya"),
        makeProject(QStringLiteral("第二版")))));

    // 覆盖保存有旧内容备份（FileService统一规则，备份留在用户目录内）
    QVERIFY(QFile::exists(root.filePath(QStringLiteral("users/甲同学/灵芽备份/版本.lingya.bak"))));
    const auto opened =
        UserFileService::openOwned(root.path(), QStringLiteral("甲同学"), QStringLiteral("版本"));
    QCOMPARE(std::get<Project>(opened).title, QStringLiteral("第二版"));
}

void TstSession::listOwnedNewestFirstAndSkipsForeign()
{
    QTemporaryDir root;
    QVERIFY(std::holds_alternative<QString>(
        UserFileService::saveOwned(root.path(), QStringLiteral("甲同学"), QStringLiteral("文件一"),
                                   makeProject(QStringLiteral("a")))));
    QVERIFY(std::holds_alternative<QString>(
        UserFileService::saveOwned(root.path(), QStringLiteral("甲同学"), QStringLiteral("文件二"),
                                   makeProject(QStringLiteral("b")))));
    QFile foreign(UserFileService::userDir(root.path(), QStringLiteral("甲同学")) +
                  QStringLiteral("/说明.txt"));
    QVERIFY(foreign.open(QIODevice::WriteOnly));

    const QVector<UserFileService::OwnedFileInfo> files =
        UserFileService::listOwned(root.path(), QStringLiteral("甲同学"));
    QCOMPARE(files.size(), 2);
    QVERIFY(files.first().name == QStringLiteral("文件二") ||
            files.first().modified >= files.last().modified); // 新→旧
    QVERIFY(files.first().name != files.last().name);

    QCOMPARE(UserFileService::listOwned(root.path(), QStringLiteral("乙同学")).size(),
             0); // 无目录=空
}

void TstSession::classifyOwnOthersPublic()
{
    QTemporaryDir root;
    const QString me = QStringLiteral("甲同学");
    QCOMPARE(UserFileService::classify(root.path(), me,
                                       root.filePath(QStringLiteral("users/甲同学/我的.lingya"))),
             UserFileService::Access::Own);
    QCOMPARE(UserFileService::classify(root.path(), me,
                                       root.filePath(QStringLiteral("users/乙同学/她的.lingya"))),
             UserFileService::Access::Others);
    QCOMPARE(UserFileService::classify(root.path(), me,
                                       root.filePath(QStringLiteral("公开目录/共享.lingya"))),
             UserFileService::Access::Public);
    QCOMPARE(UserFileService::classify(root.path(), me, root.filePath(QStringLiteral("users"))),
             UserFileService::Access::Public); // users目录本身不属于任何用户
}

void TstSession::openOwnedRejectsMissingFile()
{
    QTemporaryDir root;
    const auto result =
        UserFileService::openOwned(root.path(), QStringLiteral("甲同学"), QStringLiteral("不存在"));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("FILE_NOT_FOUND"));
}

void TstSession::accountNamesCannotShareDirectoryIdentity()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        root.path(), QStringLiteral("CaseOwner"), QStringLiteral("test-only-one"))));
    const auto alias = SessionService::createAccount(root.path(), QStringLiteral("caseowner"),
                                                     QStringLiteral("test-only-two"));
    QVERIFY(std::holds_alternative<AppError>(alias));
    QCOMPARE(std::get<AppError>(alias).code, QStringLiteral("CONFLICT"));
}

void TstSession::malformedUsersCollectionKeepsAccountStore()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray broken = R"({"schemaVersion":1,"users":42})";
    QFile file(root.filePath(QStringLiteral("accounts.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(broken), broken.size());
    file.close();
    const auto result = SessionService::createAccount(root.path(), QStringLiteral("新同学"),
                                                      QStringLiteral("test-only-one"));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_PROJECT"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), broken);
}

void TstSession::duplicateDirectoryIdentityRejectedOnLoad()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        root.path(), QStringLiteral("CaseOwner"), QStringLiteral("test-only-one"))));
    QFile file(root.filePath(QStringLiteral("accounts.json")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto object = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    auto users = object.value(QStringLiteral("users")).toArray();
    auto second = users.first().toObject();
    second[QStringLiteral("name")] = QStringLiteral("caseowner");
    users.append(second);
    object[QStringLiteral("users")] = users;
    const auto broken = QJsonDocument(object).toJson();
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(broken), broken.size());
    file.close();
    const auto result = SessionService::login(root.path(), QStringLiteral("CaseOwner"),
                                              QStringLiteral("test-only-one"));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_PROJECT"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), broken);
}

void TstSession::windowsAccountNamesRejected_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("尾点目录别名") << QStringLiteral("同学.");
    QTest::newRow("保留设备名") << QStringLiteral("NUL");
    QTest::newRow("设备名带扩展") << QStringLiteral("con.notes");
    QTest::newRow("串行设备名") << QStringLiteral("COM1");
    QTest::newRow("上标设备名") << QStringLiteral("LPT²");
    QTest::newRow("冒号流名") << QStringLiteral("同学:附加");
}

void TstSession::windowsAccountNamesRejected()
{
    QFETCH(QString, name);
    // 纯校验，不尝试打开Windows设备。
    QVERIFY(!SessionService::validUserName(name));
}

void TstSession::ownedNameRejectsStreamBeforeCreatingFolder()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString user = QStringLiteral("复核同学");
    const auto result = UserFileService::saveOwned(root.path(), user, QStringLiteral("提纲:附加"),
                                                   makeProject(QStringLiteral("测试工程")));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_ARGUMENT"));
    QVERIFY(!QDir(UserFileService::userDir(root.path(), user)).exists());
}

QTEST_MAIN(TstSession)
#include "tst_session.moc"
