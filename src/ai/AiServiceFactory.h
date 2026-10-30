#pragma once
#include "ai/IAiProvider.h"

#include <memory>

namespace lingya {

class SecretStore;

// 供应方装配入口（M5）：有密钥时用 DeepSeekClient（真实 HTTP），无密钥时用 FakeAiProvider（不联网）。
// 调用方拿到 provider 后构造 AiService；provider 生命周期由调用方持有（parent 或 unique_ptr）。
std::unique_ptr<IAiProvider> createAiProvider(SecretStore* secretStore, QObject* parent = nullptr);

}  // namespace lingya
