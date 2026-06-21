#include "Engine/OpenWorldStreaming.hpp"

namespace Engine {
    StreamingHaloPlannerSettings defaultStreamingHaloPlannerSettings()
    {
        StreamingHaloPlannerSettings settings;
        for (StreamingPayloadResidencyPolicy& policy : settings.payloadPolicies) {
            policy = {};
        }
        for (uint32_t payload = 0; payload < StreamingPayloadKindCount; ++payload) {
            for (uint32_t profile = 0; profile < StreamingHaloProfileCount; ++profile) {
                settings.profilePolicies[payload][profile] = settings.payloadPolicies[payload];
            }
        }
        return settings;
    }
}
