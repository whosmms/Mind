#include "ai/SecretStore.h"

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

namespace lingya {

namespace {
// 固定目标名，不随电脑漫游（规范§10.5）
constexpr char kTargetName[] = "LingyaMind/DeepSeek";
}  // namespace

QString SessionSecretStore::apiKey() const {
    return m_key;
}

bool SessionSecretStore::setApiKey(const QString& key, bool remember) {
    Q_UNUSED(remember);  // 会话实现不写磁盘
    m_key = key;
    return true;
}

void SessionSecretStore::clearApiKey() {
    m_key.clear();
}

#ifdef _WIN32

namespace {

class WindowsSecretStore : public SecretStore {
public:
    QString apiKey() const override {
        if (!m_session.isEmpty()) {
            return m_session;
        }
        PCREDENTIALW credential = nullptr;
        const std::wstring target = QString::fromLatin1(kTargetName).toStdWString();
        if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) == FALSE) {
            return QString();  // 不存在或读取失败，回退会话（已为空）
        }
        const QString key =
            QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
                              static_cast<int>(credential->CredentialBlobSize));
        CredFree(credential);
        return key;
    }

    bool setApiKey(const QString& key, bool remember) override {
        m_session = key;
        if (!remember) {
            return true;  // 仅本次使用
        }
        return writeCredential(key);
    }

    void clearApiKey() override {
        m_session.clear();
        deleteCredential();
    }

private:
    bool writeCredential(const QString& key) {
        const QByteArray utf8 = key.toUtf8();
        const std::wstring target = QString::fromLatin1(kTargetName).toStdWString();

        CREDENTIALW credential = {};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(target.c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(utf8.size());
        credential.CredentialBlob =
            reinterpret_cast<LPBYTE>(const_cast<char*>(utf8.constData()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        return CredWriteW(&credential, 0) != FALSE;
    }

    void deleteCredential() {
        const std::wstring target = QString::fromLatin1(kTargetName).toStdWString();
        CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
    }

    mutable QString m_session;  // 进程内副本；apiKey() 会先查它
};

}  // namespace

std::unique_ptr<SecretStore> createSecretStore() {
    return std::make_unique<WindowsSecretStore>();
}

#else

std::unique_ptr<SecretStore> createSecretStore() {
    return std::make_unique<SessionSecretStore>();
}

#endif

}  // namespace lingya
