#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Navigation.hpp"
#include "Engine/NavigationRuntime.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Terrain.hpp"

namespace {
    constexpr float ChunkSize = 16.0f;
    constexpr uint32_t Resolution = 17;

    struct TestFailure {
        std::string testName;
        std::string message;
    };

    struct TestContext {
        std::string name;
        std::vector<TestFailure>& failures;

        void expect(bool condition, std::string message)
        {
            if (!condition) {
                failures.push_back({name, std::move(message)});
            }
        }
    };

    std::vector<float> heights(float value)
    {
        return std::vector<float>(static_cast<size_t>(Resolution) * Resolution, value);
    }

    Engine::TerrainSystem makeTerrain()
    {
        Engine::TerrainSettings settings;
        settings.chunkSize = ChunkSize;
        settings.resolution = Resolution;
        settings.createRendererResources = false;
        return Engine::TerrainSystem(settings);
    }

    void appendAabbBlocker(Engine::NavigationTerrainBuildData& buildData, const Renderer::Aabb& bounds)
    {
        const uint32_t base = static_cast<uint32_t>(buildData.blockingVertices.size());
        buildData.blockingVertices.push_back({bounds.min.x, bounds.min.y, bounds.min.z});
        buildData.blockingVertices.push_back({bounds.max.x, bounds.min.y, bounds.min.z});
        buildData.blockingVertices.push_back({bounds.max.x, bounds.min.y, bounds.max.z});
        buildData.blockingVertices.push_back({bounds.min.x, bounds.min.y, bounds.max.z});
        buildData.blockingVertices.push_back({bounds.min.x, bounds.max.y, bounds.min.z});
        buildData.blockingVertices.push_back({bounds.max.x, bounds.max.y, bounds.min.z});
        buildData.blockingVertices.push_back({bounds.max.x, bounds.max.y, bounds.max.z});
        buildData.blockingVertices.push_back({bounds.min.x, bounds.max.y, bounds.max.z});

        constexpr uint32_t indices[] = {
            0, 1, 2, 0, 2, 3,
            4, 6, 5, 4, 7, 6,
            0, 4, 5, 0, 5, 1,
            1, 5, 6, 1, 6, 2,
            2, 6, 7, 2, 7, 3,
            3, 7, 4, 3, 4, 0,
        };
        for (uint32_t index : indices) {
            buildData.blockingIndices.push_back(base + index);
        }
        buildData.bounds.min = glm::min(buildData.bounds.min, bounds.min);
        buildData.bounds.max = glm::max(buildData.bounds.max, bounds.max);
    }

    bool addFlatTile(
        Engine::TerrainSystem& terrain,
        Engine::NavigationSystem& navigation,
        TestContext& ctx,
        std::vector<Renderer::Aabb> blockers = {})
    {
        const Engine::TerrainTileHandle tile = terrain.createTileFromHeights({0, 0}, heights(0.0f));
        if (tile.id == UINT32_MAX) {
            ctx.expect(false, "failed to create CPU terrain tile");
            return false;
        }
        std::optional<Engine::NavigationTerrainBuildData> buildData = terrain.navigationBuildData(tile);
        if (!buildData) {
            ctx.expect(false, "failed to extract navigation build data");
            return false;
        }
        for (const Renderer::Aabb& blocker : blockers) {
            appendAabbBlocker(*buildData, blocker);
        }
        const Engine::NavigationTileHandle handle = navigation.buildTerrainTile(*buildData, {});
        if (handle.id == UINT32_MAX) {
            ctx.expect(false, "failed to build navigation tile: " + navigation.lastBuildMessage());
            return false;
        }
        return true;
    }

    std::filesystem::path runtimeHeaderPath()
    {
        const std::filesystem::path sourceRelative = "src/Engine/NavigationRuntime.hpp";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }
        const std::filesystem::path parentRelative = "../src/Engine/NavigationRuntime.hpp";
        if (std::filesystem::exists(parentRelative)) {
            return parentRelative;
        }
        return "../../src/Engine/NavigationRuntime.hpp";
    }

    void projectionSucceedsOnLoadedTile(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        if (!addFlatTile(terrain, navigation, ctx)) {
            return;
        }
        Engine::SceneNavigationService service{navigation};

        const Engine::NavigationProjectionResult result = service.projectPoint({8.0f, 0.5f, 8.0f});
        ctx.expect(result.status == Engine::NavigationRuntimeStatus::Success, "projection failed: " + result.message);
        ctx.expect(std::abs(result.point.x - 8.0f) < 1.0f && std::abs(result.point.z - 8.0f) < 1.0f,
            "projection point was not near the request");
    }

    void projectionReportsMissingTile(TestContext& ctx)
    {
        Engine::NavigationSystem navigation;
        Engine::SceneNavigationService service{navigation};

        const Engine::NavigationProjectionResult result = service.projectPoint({8.0f, 0.0f, 8.0f});
        ctx.expect(result.status == Engine::NavigationRuntimeStatus::NoTile, "empty navigation did not report NoTile");
        ctx.expect(service.diagnostics().missingTileCount == 1, "missing tile diagnostic was not counted");
    }

    void localPathReturnsDeterministicPoints(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        if (!addFlatTile(terrain, navigation, ctx)) {
            return;
        }
        Engine::SceneNavigationService service{navigation};

        const Engine::NavigationPathResult result = service.findPath({2.0f, 0.0f, 2.0f}, {14.0f, 0.0f, 14.0f});
        ctx.expect(result.status == Engine::NavigationRuntimeStatus::Success, "path query failed: " + result.message);
        ctx.expect(result.path.complete, "path was not complete");
        ctx.expect(result.path.points.size() >= 2, "path had too few points");
        ctx.expect(result.length > 0.0f, "path length was not recorded");
    }

    void blockedPathFailsCleanly(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        const Renderer::Aabb wall{{7.4f, -0.2f, 0.0f}, {8.6f, 2.0f, ChunkSize}};
        if (!addFlatTile(terrain, navigation, ctx, {wall})) {
            return;
        }
        Engine::SceneNavigationService service{navigation};

        const Engine::NavigationPathResult result = service.findPath({2.0f, 0.0f, 8.0f}, {14.0f, 0.0f, 8.0f});
        ctx.expect(result.status != Engine::NavigationRuntimeStatus::Success || !result.path.complete,
            "blocked wall unexpectedly returned a complete path");
    }

    void reachabilityWrapsPathStatus(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        if (!addFlatTile(terrain, navigation, ctx)) {
            return;
        }
        Engine::SceneNavigationService service{navigation};

        const Engine::NavigationReachabilityResult result = service.reachable({2.0f, 0.0f, 2.0f}, {14.0f, 0.0f, 14.0f});
        ctx.expect(result.status == Engine::NavigationRuntimeStatus::Success, "reachable status was not success");
        ctx.expect(result.reachable, "reachable wrapper did not report reachable");
    }

    void raycastReportsClearAndBlocked(TestContext& ctx)
    {
        {
            Engine::TerrainSystem terrain = makeTerrain();
            Engine::NavigationSystem navigation;
            if (!addFlatTile(terrain, navigation, ctx)) {
                return;
            }
            Engine::SceneNavigationService service{navigation};
            const Engine::NavigationRaycastResult result = service.raycast({2.0f, 0.0f, 8.0f}, {14.0f, 0.0f, 8.0f});
            ctx.expect(result.status == Engine::NavigationRuntimeStatus::Success, "clear raycast failed: " + result.message);
            ctx.expect(!result.blocked, "clear raycast reported blocked");
        }

        {
            Engine::TerrainSystem terrain = makeTerrain();
            Engine::NavigationSystem navigation;
            const Renderer::Aabb wall{{7.4f, -0.2f, 0.0f}, {8.6f, 2.0f, ChunkSize}};
            if (!addFlatTile(terrain, navigation, ctx, {wall})) {
                return;
            }
            Engine::SceneNavigationService service{navigation};
            const Engine::NavigationRaycastResult result = service.raycast({2.0f, 0.0f, 8.0f}, {14.0f, 0.0f, 8.0f});
            ctx.expect(result.status != Engine::NavigationRuntimeStatus::Success || result.blocked,
                "blocked raycast unexpectedly reported clear");
        }
    }

    void sceneActorPathUsesWorldTransform(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        if (!addFlatTile(terrain, navigation, ctx)) {
            return;
        }
        Engine::Scene scene;
        const Engine::SceneActorHandle actor = scene.createActor();
        Engine::SceneTransform transform;
        transform.translation = {2.0f, 0.0f, 2.0f};
        ctx.expect(scene.setLocalTransform(actor, transform), "failed to set actor transform");
        Engine::SceneNavigationService service{navigation};

        const Engine::NavigationPathResult result = service.findPathFromActor(scene, actor, {14.0f, 0.0f, 14.0f});
        ctx.expect(result.status == Engine::NavigationRuntimeStatus::Success, "actor path query failed: " + result.message);
        ctx.expect(result.path.complete, "actor path was not complete");

        scene.destroyActor(actor);
        const Engine::NavigationPathResult invalid = service.findPathFromActor(scene, actor, {14.0f, 0.0f, 14.0f});
        ctx.expect(invalid.status == Engine::NavigationRuntimeStatus::InvalidActor, "stale actor was not rejected");
    }

    void invalidInputsFailCleanly(TestContext& ctx)
    {
        Engine::NavigationSystem navigation;
        Engine::SceneNavigationService service{navigation};
        Engine::NavigationAgentConfig invalidAgent;
        invalidAgent.radius = -1.0f;

        const Engine::NavigationProjectionResult badAgent = service.projectPoint({0.0f, 0.0f, 0.0f}, invalidAgent);
        ctx.expect(badAgent.status == Engine::NavigationRuntimeStatus::InvalidInput, "invalid agent was not rejected");
        const Engine::NavigationPathResult badPoint = service.findPath(
            {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
            {1.0f, 0.0f, 1.0f});
        ctx.expect(badPoint.status == Engine::NavigationRuntimeStatus::InvalidInput, "NaN path input was not rejected");
    }

    void diagnosticsAndDebugRequestsAreRecorded(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        if (!addFlatTile(terrain, navigation, ctx)) {
            return;
        }
        Engine::SceneNavigationService service{navigation};
        Engine::NavigationQueryFilter filter;
        filter.captureDebug = true;

        const Engine::NavigationProjectionResult projection = service.projectPoint({8.0f, 0.0f, 8.0f}, {}, filter);
        const Engine::NavigationPathResult path = service.findPath({2.0f, 0.0f, 2.0f}, {14.0f, 0.0f, 14.0f}, {}, filter);
        (void)projection;
        (void)path;
        const Engine::NavigationRuntimeDiagnostics diagnostics = service.diagnostics();
        ctx.expect(diagnostics.queryCounts[static_cast<size_t>(Engine::NavigationRuntimeQueryType::Projection)] == 1,
            "projection diagnostic count was wrong");
        ctx.expect(diagnostics.queryCounts[static_cast<size_t>(Engine::NavigationRuntimeQueryType::Path)] == 1,
            "path diagnostic count was wrong");
        ctx.expect(diagnostics.lastPathPointCount >= 2, "last path point count was not recorded");
        ctx.expect(!service.debugRequests().empty(), "debug requests were not captured");

        service.clearDebugRequests();
        ctx.expect(service.debugRequests().empty(), "debug requests were not cleared");
        service.resetDiagnostics();
        ctx.expect(service.diagnostics().lastStatus == Engine::NavigationRuntimeStatus::Unsupported,
            "diagnostics reset did not restore default status");
    }

    void publicHeaderDoesNotExposeRecastDetour(TestContext& ctx)
    {
        const std::filesystem::path path = runtimeHeaderPath();
        std::ifstream file{path};
        ctx.expect(static_cast<bool>(file), "failed to open NavigationRuntime.hpp");
        std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        ctx.expect(contents.find("Detour") == std::string::npos, "NavigationRuntime.hpp mentions Detour");
        ctx.expect(contents.find("Recast") == std::string::npos, "NavigationRuntime.hpp mentions Recast");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"ProjectionSucceedsOnLoadedTile", projectionSucceedsOnLoadedTile},
        {"ProjectionReportsMissingTile", projectionReportsMissingTile},
        {"LocalPathReturnsDeterministicPoints", localPathReturnsDeterministicPoints},
        {"BlockedPathFailsCleanly", blockedPathFailsCleanly},
        {"ReachabilityWrapsPathStatus", reachabilityWrapsPathStatus},
        {"RaycastReportsClearAndBlocked", raycastReportsClearAndBlocked},
        {"SceneActorPathUsesWorldTransform", sceneActorPathUsesWorldTransform},
        {"InvalidInputsFailCleanly", invalidInputsFailCleanly},
        {"DiagnosticsAndDebugRequestsAreRecorded", diagnosticsAndDebugRequestsAreRecorded},
        {"PublicHeaderDoesNotExposeRecastDetour", publicHeaderDoesNotExposeRecastDetour},
    };

    for (const auto& [name, test] : tests) {
        TestContext context{std::string{name}, failures};
        test(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << "[FAIL] " << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "Navigation runtime tests passed (" << tests.size() << ")\n";
    return 0;
}
