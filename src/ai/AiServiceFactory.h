#pragma once
#include "ai/IAiProvider.h"

#include <memory>

namespace lingya {

class SecretStore;

// 供应方装配入口（M5）：有密钥时用 DeepSeekClient（真实 HTTP），无密钥时用 FakeAiProvider（不联网）。
// baseUrl 仅测试用（指向本机桩）；生产留空走官方端点。
// 配合 AiService::setProviderFactory 使用：存/清密钥后由服务自动重建，不把换供应方的责任推给 UI。
std::unique_ptr<IAiProvider> createAiProvider(SecretStore* secretStore, QObject* parent = nullptr,
                                              const QString& baseUrl = QString());

}  // namespace lingya
