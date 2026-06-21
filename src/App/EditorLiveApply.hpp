#pragma once

#include <string>

#include "App/EditorProjectSettings.hpp"
#include "Engine/OpenWorldStreamingRuntime.hpp"

namespace ManualEngine::App {

    struct EditorLiveApplyHost {
        void* user = nullptr;
        bool (*applyLightweightRuntime)(
            void* user,
            const EditorProjectSettings& settings,
            std::string& message) = nullptr;
        bool (*reloadStreamingRuntime)(
            void* user,
            const EditorProjectSettings& settings,
            std::string& message,
            Engine::OpenWorldStreamingBuildResult& result) = nullptr;
    };

}
