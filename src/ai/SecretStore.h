#pragma once
#include <QString>
#include <memory>

namespace lingya {

// 凭据存储（AI规范§10.5 [W1]）：会话内存或Windows凭据管理器，绝不回退明文。
// 页面只通过本接口读写密钥，不接触Win32结构。
class SecretStore {
public:
    virtual ~SecretStore() = default;

    // 当前可用密钥：会话优先，其次凭据管理器；都没有返回空串
    virtual QString apiKey() const = 0;

    // 设置密钥：立即写入会话；remember=true 时同时尝试凭据管理器，失败降级为会话
    virtual bool setApiKey(const QString& key, bool remember) = 0;

    // 清空会话并删除凭据管理器中的记录
    virtual void clearApiKey() = 0;
};

// 进程内会话存储：不写磁盘，用于测试与"仅本次使用"
class SessionSecretStore : public SecretStore {
public:
    QString apiKey() const override;
    bool setApiKey(const QString& key, bool remember) override;
    void clearApiKey() override;

private:
    QString m_key;
};

// 生产实现：Windows 凭据管理器（目标名 LingyaMind/DeepSeek），失败回退会话
std::unique_ptr<SecretStore> createSecretStore();

}  // namespace lingya
