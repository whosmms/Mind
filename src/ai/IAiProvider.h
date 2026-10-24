#pragma once
#include <QJsonObject>
#include <QObject>
#include "ai/AiTypes.h"

namespace lingya {


// AI供应方抽象（规范§10.3）：假实现与真实实现共用契约。
// Provider负责HTTP、限流重试和脱敏错误，不直接修改Project。
// M0仅定义接口与FakeAiProvider；DeepSeekClient（真实HTTP实现）随M5接入。
class IAiProvider : public QObject {
    Q_OBJECT
public:
    explicit IAiProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~IAiProvider() override = default;

    // 发起一次请求；requestId唯一，同一requestId重复提交不重复发送（规范§10.6）
    virtual void start(const QString& requestId, const QJsonObject& requestBody) = 0;
    virtual void cancel(const QString& requestId) = 0;

signals:
    void completed(const QString& requestId, const QJsonObject& fullResponse);
    void failed(const AiError& error);
};

}  // namespace lingya
