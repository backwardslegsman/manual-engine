#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/Scene/Scene.hpp"

namespace Engine {
    class NavigationConnectivitySystem;

    enum class NavigationRuntimeStatus {
        Success,
        NotInitialized,
        NoTile,
        NoNearestPoly,
        NoPath,
        PartialPath,
        Blocked,
        InvalidInput,
        InvalidActor,
        Unsupported,
    };

    enum class NavigationRuntimeQueryType {
        Projection,
        Path,
        Reachability,
        Raycast,
        Count,
    };

    enum class NavigationDebugRequestType {
        Path,
        Projection,
        Raycast,
        NavmeshEdges,
        Portal,
        FailedQuery,
    };

    struct NavigationAgentConfig {
        float radius = 0.45f;
        float height = 1.8f;
        float maxSlopeDegrees = 45.0f;
        float maxClimb = 0.45f;
        glm::vec3 queryExtents{0.9f, 1.8f, 0.9f};
        uint32_t maxPathPoints = 256;
    };

    struct NavigationQueryFilter {
        bool requireLoadedTiles = true;
        bool allowPartialPath = false;
        bool captureDebug = false;
        uint16_t includeFlags = 1;
        uint16_t excludeFlags = 0;
    };

    struct NavigationProjectionResult {
        NavigationRuntimeStatus status = NavigationRuntimeStatus::Unsupported;
        glm::vec3 requestedPoint{};
        glm::vec3 point{};
        std::string message;
    };

    struct NavigationPathResult {
        NavigationRuntimeStatus status = NavigationRuntimeStatus::Unsupported;
        glm::vec3 requestedStart{};
        glm::vec3 requestedEnd{};
        NavPath path;
        float length = 0.0f;
        std::string message;
    };

    struct NavigationReachabilityResult {
        NavigationRuntimeStatus status = NavigationRuntimeStatus::Unsupported;
        bool reachable = false;
        NavigationPathResult path;
        std::string message;
    };

    struct NavigationRaycastResult {
        NavigationRuntimeStatus status = NavigationRuntimeStatus::Unsupported;
        glm::vec3 start{};
        glm::vec3 end{};
        glm::vec3 hitPoint{};
        bool blocked = false;
        float pathDeviation = 0.0f;
        std::string message;
    };

    struct NavigationDebugRequest {
        NavigationDebugRequestType type = NavigationDebugRequestType::FailedQuery;
        NavigationRuntimeStatus status = NavigationRuntimeStatus::Unsupported;
        std::vector<glm::vec3> points;
        std::string message;
    };

    struct NavigationRuntimeDiagnostics {
        std::array<uint32_t, static_cast<size_t>(NavigationRuntimeQueryType::Count)> queryCounts{};
        std::array<uint32_t, 10> statusCounts{};
        NavigationRuntimeQueryType lastQueryType = NavigationRuntimeQueryType::Projection;
        NavigationRuntimeStatus lastStatus = NavigationRuntimeStatus::Unsupported;
        std::string lastMessage = "No navigation runtime queries have run.";
        uint64_t lastElapsedMicroseconds = 0;
        uint32_t lastPathPointCount = 0;
        float lastPathLength = 0.0f;
        uint32_t missingTileCount = 0;
        uint32_t invalidActorCount = 0;
        uint32_t debugRequestCount = 0;
    };

    [[nodiscard]] NavAgentSettings toNavAgentSettings(const NavigationAgentConfig& config);
    [[nodiscard]] NavigationRuntimeStatus toNavigationRuntimeStatus(NavQueryStatus status, bool complete = true);
    [[nodiscard]] float navigationPathLength(const NavPath& path);
    [[nodiscard]] const char* navigationRuntimeStatusName(NavigationRuntimeStatus status);

    class SceneNavigationService {
    public:
        explicit SceneNavigationService(
            const NavigationSystem& navigation,
            const NavigationConnectivitySystem* connectivity = nullptr);

        [[nodiscard]] NavigationProjectionResult projectPoint(
            glm::vec3 point,
            const NavigationAgentConfig& agent = {},
            const NavigationQueryFilter& filter = {});
        [[nodiscard]] NavigationProjectionResult projectPointInTile(
            ChunkCoord coord,
            glm::vec3 point,
            const NavigationAgentConfig& agent = {},
            const NavigationQueryFilter& filter = {});
        [[nodiscard]] NavigationProjectionResult projectPointInLoadedTiles(
            glm::vec3 point,
            const NavigationAgentConfig& agent = {},
            const NavigationQueryFilter& filter = {});
        [[nodiscard]] NavigationPathResult findPath(
            glm::vec3 start,
            glm::vec3 end,
            const NavigationAgentConfig& agent = {},
            const NavigationQueryFilter& filter = {});
        [[nodiscard]] NavigationPathResult findPathAcrossLoadedTiles(
            glm::vec3 start,
            glm::vec3 end,
            const NavigationAgentConfig& agent = {},
            const NavigationQueryFilter& filter = {});
        [[nodiscard]] NavigationPathResult findPathFromActor(
            Scene& scene,
            SceneActorHandle actor,
            glm::vec3 end,
            const NavigationAgentConfig& agent = {},
            const NavigationQueryFilter& filter = {});
        [[nodiscard]] NavigationReachabilityResult reachable(
            glm::vec3 start,
            glm::vec3 end,
            const NavigationAgentConfig& agent = {},
            const NavigationQueryFilter& filter = {});
        [[nodiscard]] NavigationRaycastResult raycast(
            glm::vec3 start,
            glm::vec3 end,
            const NavigationAgentConfig& agent = {},
            const NavigationQueryFilter& filter = {});

        [[nodiscard]] std::vector<NavigationDebugRequest> debugRequests() const;
        void clearDebugRequests();
        [[nodiscard]] NavigationRuntimeDiagnostics diagnostics() const;
        void resetDiagnostics();

    private:
        [[nodiscard]] bool validAgent(const NavigationAgentConfig& agent) const;
        [[nodiscard]] bool validPoint(glm::vec3 point) const;
        void record(
            NavigationRuntimeQueryType type,
            NavigationRuntimeStatus status,
            std::string message,
            uint64_t elapsedMicroseconds,
            uint32_t pathPointCount = 0,
            float pathLength = 0.0f);
        void appendDebugRequest(
            const NavigationQueryFilter& filter,
            NavigationDebugRequestType type,
            NavigationRuntimeStatus status,
            std::vector<glm::vec3> points,
            std::string message);

        const NavigationSystem& navigation_;
        const NavigationConnectivitySystem* connectivity_ = nullptr;
        NavigationRuntimeDiagnostics diagnostics_;
        std::vector<NavigationDebugRequest> debugRequests_;
    };
}
