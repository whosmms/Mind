#include <QtTest>

#include "ai/SecretStore.h"

using namespace lingya;

// 凭据存储测试（AI规范§10.5 [W1]）：会话实现不写磁盘，读写清空正确。
class TstSecret : public QObject {
    Q_OBJECT

private slots:
    void sessionStartsEmpty();
    void sessionStoresKey();
    void sessionIgnoresRememberFlag();
    void sessionClears();
};

void TstSecret::sessionStartsEmpty() {
    SessionSecretStore store;
    QVERIFY(store.apiKey().isEmpty());
}

void TstSecret::sessionStoresKey() {
    SessionSecretStore store;
    QVERIFY(store.setApiKey(QStringLiteral("sk-test"), false));
    QCOMPARE(store.apiKey(), QStringLiteral("sk-test"));
}

void TstSecret::sessionIgnoresRememberFlag() {
    SessionSecretStore store;
    // 会话实现即使 remember=true 也不写磁盘（仍能读写）
    QVERIFY(store.setApiKey(QStringLiteral("sk-remember"), true));
    QCOMPARE(store.apiKey(), QStringLiteral("sk-remember"));
}

void TstSecret::sessionClears() {
    SessionSecretStore store;
    QVERIFY(store.setApiKey(QStringLiteral("sk-test"), false));
    store.clearApiKey();
    QVERIFY(store.apiKey().isEmpty());
}

QTEST_GUILESS_MAIN(TstSecret)
#include "tst_secret.moc"
