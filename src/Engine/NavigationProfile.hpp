#pragma once

#include <filesystem>
#include <string>

#include "Engine/Navigation.hpp"

namespace Engine {
    struct NavigationProfile {
        std::string id = "default";
        NavAgentSettings agent;
        NavBuildSettings build;
    };

    struct NavigationProfileLoadResult {
        NavigationProfile profile;
        bool usedFallback = false;
        std::string message;
    };

    NavigationProfile defaultNavigationProfile();
    NavigationProfileLoadResult loadNavigationProfileFromYaml(const std::filesystem::path& path);
}
