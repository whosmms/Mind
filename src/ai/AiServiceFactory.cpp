#include "ai/AiServiceFactory.h"

#include "ai/DeepSeekClient.h"
#include "ai/FakeAiProvider.h"
#include "ai/SecretStore.h"

namespace lingya {

std::unique_ptr<IAiProvider> createAiProvider(SecretStore* secretStore, QObject* parent,
                                              const QString& baseUrl) {
    if (secretStore != nullptr && !secretStore->apiKey().isEmpty()) {
        auto client = std::make_unique<DeepSeekClient>(secretStore->apiKey(), parent);
        if (!baseUrl.isEmpty()) {
            client->setBaseUrl(baseUrl);
        }
        return client;
    }
    return std::make_unique<FakeAiProvider>(parent);
}

}  // namespace lingya
