#include "ai/FakeAiProvider.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

namespace lingya {


FakeAiProvider::FakeAiProvider(QObject* parent) : IAiProvider(parent) {
    // 用QTimer模拟异步完成；真实网络路径见规范§10（M5实现DeepSeekClient）
}

void FakeAiProvider::setFailureMode(FailureMode mode) {
    m_mode = mode;
}

void FakeAiProvider::setLatencyMs(int ms) {
    m_latencyMs = qMax(0, ms);
}

void FakeAiProvider::start(const QString& requestId, const QJsonObject& requestBody) {
    Q_UNUSED(requestBody);
    if (m_activeRequestId == requestId)
        return;   // 同一requestId重复提交不重复发送（规范§10.6）
    m_activeRequestId = requestId;
    const int generation = ++m_generation;
    QTimer::singleShot(m_latencyMs, this, [this, generation] {
        if (generation == m_generation) finish();
    });
}

void FakeAiProvider::cancel(const QString& requestId) {
    if (m_activeRequestId != requestId) return;
    // 静默停止；canceled终态语义由AiService解释（M5，规范§10.3信号契约）
    m_activeRequestId.clear();
    ++m_generation;
}

void FakeAiProvider::finish() {
    if (m_activeRequestId.isEmpty()) return;   // 已取消
    const QString requestId = m_activeRequestId;
    m_activeRequestId.clear();

    switch (m_mode) {
    case FailureMode::Success: {
        // 规范§10.4的X05返回示例；content为JSON字符串，外形与真实接口一致
        const auto child = [](const char* title) {
            return QJsonObject{{"title", QLatin1String(title)},
                               {"note", QString()},
                               {"sourceParagraphIds", QJsonArray{"p1"}},
                               {"origin", "source"},
                               {"children", QJsonArray()}};
        };
        const QJsonObject proposal{
            {"title", QStringLiteral("软件工程流程")},
            {"root",
             QJsonObject{{"title", QStringLiteral("软件工程")},
                         {"note", QString()},
                         {"sourceParagraphIds", QJsonArray{"p1"}},
                         {"origin", "source"},
                         {"children",
                          QJsonArray{child("需求分析"), child("设计"), child("编码"), child("测试")}}}},
            {"warnings", QJsonArray()}};
        const QString content =
            QString::fromUtf8(QJsonDocument(proposal).toJson(QJsonDocument::Compact));
        const QJsonObject response{
            {"model", "deepseek-flash"},
            {"choices",
             QJsonArray{QJsonObject{
                 {"finish_reason", "stop"},
                 {"message", QJsonObject{{"role", "assistant"}, {"content", content}}}}}},
            {"usage", QJsonObject{{"prompt_tokens", 111},
                                  {"completion_tokens", 222},
                                  {"total_tokens", 333}}}};
        emit completed(requestId, response);
        break;
    }
    case FailureMode::Timeout:
        emit failed(AiError{"TIMEOUT", "假供应方：模拟请求超时", requestId, false});
        break;
    case FailureMode::AuthFailed:
        emit failed(AiError{"AUTH_FAILED", "假供应方：模拟密钥无效", requestId, false});
        break;
    case FailureMode::EmptyOutput:
        emit failed(AiError{"EMPTY_OUTPUT", "假供应方：模拟空返回", requestId, true});
        break;
    }
}

}  // namespace lingya
