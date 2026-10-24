#pragma once
#include "ai/IAiProvider.h"

namespace lingya {


// 假AI供应方（规范§10.3、§12 M0“共享类型及假AI响应”）：
// 仅用于单元测试与明显标识的开发模式，不发起任何网络请求，不伪装真实调用。
// 成功模式返回规范§10.4的合法X05样例（DeepSeek完整响应外形）；
// 失败模式模拟常见错误码，供M5之前的校验与状态机开发使用。
class FakeAiProvider : public IAiProvider {
    Q_OBJECT
public:
    enum class FailureMode { Success, Timeout, AuthFailed, EmptyOutput };

    explicit FakeAiProvider(QObject* parent = nullptr);

    void setFailureMode(FailureMode mode);
    void setLatencyMs(int ms);

    void start(const QString& requestId, const QJsonObject& requestBody) override;
    void cancel(const QString& requestId) override;

private:
    void finish();

    FailureMode m_mode = FailureMode::Success;
    int m_latencyMs = 30;
    int m_generation = 0;         // 取消或新请求后旧定时器回调失效
    QString m_activeRequestId;
};

}  // namespace lingya
