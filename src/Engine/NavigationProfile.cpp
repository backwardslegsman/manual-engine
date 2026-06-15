#include "Engine/NavigationProfile.hpp"

#include <algorithm>

#include <yaml-cpp/yaml.h>

namespace Engine {
    namespace {
        NavigationProfile readProfile(const YAML::Node& node, std::string id)
        {
            NavigationProfile profile = defaultNavigationProfile();
            profile.id = std::move(id);

            const YAML::Node agent = node["agent"];
            profile.agent.radius = agent["radius"].as<float>(profile.agent.radius);
            profile.agent.height = agent["height"].as<float>(profile.agent.height);
            profile.agent.maxSlopeDegrees = agent["max_slope_degrees"].as<float>(profile.agent.maxSlopeDegrees);
            profile.agent.maxClimb = agent["max_climb"].as<float>(profile.agent.maxClimb);

            const YAML::Node build = node["build"];
            profile.build.cellSize = build["cell_size"].as<float>(profile.build.cellSize);
            profile.build.cellHeight = build["cell_height"].as<float>(profile.build.cellHeight);
            profile.build.tileBorderSize = build["tile_border_size"].as<uint32_t>(profile.build.tileBorderSize);
            profile.build.maxTiles = build["max_tiles"].as<uint32_t>(profile.build.maxTiles);
            profile.build.maxPolysPerTile = build["max_polys_per_tile"].as<uint32_t>(profile.build.maxPolysPerTile);
            profile.build.maxVertsPerPoly = build["max_verts_per_poly"].as<uint32_t>(profile.build.maxVertsPerPoly);
            profile.build.regionMinSize = build["region_min_size"].as<uint32_t>(profile.build.regionMinSize);
            profile.build.regionMergeSize = build["region_merge_size"].as<uint32_t>(profile.build.regionMergeSize);
            profile.build.edgeMaxLen = build["edge_max_len"].as<float>(profile.build.edgeMaxLen);
            profile.build.edgeMaxError = build["edge_max_error"].as<float>(profile.build.edgeMaxError);
            profile.build.detailSampleDist = build["detail_sample_dist"].as<float>(profile.build.detailSampleDist);
            profile.build.detailSampleMaxError = build["detail_sample_max_error"].as<float>(profile.build.detailSampleMaxError);
            profile.navigationResolution = build["navigation_resolution"].as<uint32_t>(profile.navigationResolution);

            profile.agent.radius = std::max(profile.agent.radius, 0.01f);
            profile.agent.height = std::max(profile.agent.height, 0.01f);
            profile.agent.maxSlopeDegrees = std::clamp(profile.agent.maxSlopeDegrees, 0.0f, 89.0f);
            profile.agent.maxClimb = std::max(profile.agent.maxClimb, 0.0f);
            profile.build.cellSize = std::max(profile.build.cellSize, 0.01f);
            profile.build.cellHeight = std::max(profile.build.cellHeight, 0.01f);
            profile.build.maxTiles = std::max(profile.build.maxTiles, 1u);
            profile.build.maxPolysPerTile = std::max(profile.build.maxPolysPerTile, 1u);
            profile.build.maxVertsPerPoly = std::clamp(profile.build.maxVertsPerPoly, 3u, 12u);
            profile.navigationResolution = std::max(profile.navigationResolution, 2u);
            return profile;
        }
    }

    NavigationProfile defaultNavigationProfile()
    {
        NavigationProfile profile;
        profile.id = "human_default";
        profile.agent = {};
        profile.build = {};
        profile.build.cellSize = 0.8f;
        profile.build.cellHeight = 0.25f;
        profile.navigationResolution = 17;
        return profile;
    }

    NavigationProfileLoadResult loadNavigationProfileFromYaml(const std::filesystem::path& path)
    {
        NavigationProfileLoadResult result;
        result.profile = defaultNavigationProfile();
        result.usedFallback = true;
        try {
            const YAML::Node root = YAML::LoadFile(path.string());
            const YAML::Node profilesRoot = root["navigation_profiles"];
            const std::string activeId = profilesRoot["active"].as<std::string>(result.profile.id);
            const YAML::Node profileNode = profilesRoot["profiles"][activeId];
            if (!profileNode || !profileNode.IsMap()) {
                result.message = "Navigation profile '" + activeId + "' was not found; using defaults.";
                return result;
            }

            result.profile = readProfile(profileNode, activeId);
            result.usedFallback = false;
            result.message = "Loaded navigation profile '" + activeId + "'.";
            return result;
        } catch (const std::exception& exception) {
            result.message = std::string{"Failed to load navigation profiles: "} + exception.what();
            return result;
        }
    }
}
