#include "ai/AiServiceFactory.h"

#include "ai/DeepSeekClient.h"
#include "ai/FakeAiProvider.h"
#include "ai/SecretStore.h"

namespace lingya {

std::unique_ptr<IAiProvider> createAiProvider(SecretStore* secretStore, QObject* parent) {
    if (secretStore != nullptr && !secretStore->apiKey().isEmpty()) {
        return std::make_unique<DeepSeekClient>(secretStore->apiKey(), parent);
    }
    return std::make_unique<FakeAiProvider>(parent);
}

}  // namespace lingya
