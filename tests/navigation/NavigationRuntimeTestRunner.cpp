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
#include <glm/gtc/matrix_transform.hpp>

#include "Engine/CursorTrace.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/NavigationConnectivity.hpp"
#include "Engine/NavigationRuntime.hpp"
#include "Engine/Scene/Scene.hpp"

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

    Engine::NavigationTerrainBuildData flatBuildData(Engine::ChunkCoord coord, float height)
    {
        Engine::NavigationTerrainBuildData data;
        data.coord = coord;
        const glm::vec3 origin{
            static_cast<float>(coord.x) * ChunkSize,
            height,
            static_cast<float>(coord.z) * ChunkSize,
        };
        const float step = ChunkSize / static_cast<float>(Resolution - 1u);
        data.vertices.reserve(static_cast<size_t>(Resolution) * Resolution);
        for (uint32_t z = 0; z < Resolution; ++z) {
            for (uint32_t x = 0; x < Resolution; ++x) {
                data.vertices.push_back({origin.x + static_cast<float>(x) * step, height, origin.z + static_cast<float>(z) * step});
            }
        }
        for (uint32_t z = 0; z + 1 < Resolution; ++z) {
            for (uint32_t x = 0; x + 1 < Resolution; ++x) {
                const uint32_t i0 = z * Resolution + x;
                const uint32_t i1 = i0 + 1u;
                const uint32_t i2 = i0 + Resolution;
                const uint32_t i3 = i2 + 1u;
                data.indices.push_back(i0);
                data.indices.push_back(i2);
                data.indices.push_back(i1);
                data.indices.push_back(i1);
                data.indices.push_back(i2);
                data.indices.push_back(i3);
            }
        }
        data.bounds.min = origin;
        data.bounds.max = {origin.x + ChunkSize, height, origin.z + ChunkSize};
        data.rasterizationBounds = data.bounds;
        return data;
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

    bool addFlatTileAt(
        Engine::NavigationSystem& navigation,
        TestContext& ctx,
        Engine::ChunkCoord coord,
        float height,
        std::vector<Renderer::Aabb> blockers = {})
    {
        Engine::NavigationTerrainBuildData buildData = flatBuildData(coord, height);
        for (const Renderer::Aabb& blocker : blockers) {
            appendAabbBlocker(buildData, blocker);
        }
        const Engine::NavigationTileHandle handle = navigation.buildTerrainTile(buildData, {});
        if (handle.id == UINT32_MAX) {
            ctx.expect(false, "failed to build navigation tile: " + navigation.lastBuildMessage());
            return false;
        }
        return true;
    }

    bool addFlatTile(
        Engine::NavigationSystem& navigation,
        TestContext& ctx,
        std::vector<Renderer::Aabb> blockers = {})
    {
        return addFlatTileAt(navigation, ctx, {0, 0}, 0.0f, std::move(blockers));
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
    {        Engine::NavigationSystem navigation;
        if (!addFlatTile(navigation, ctx)) {
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

    void cursorRayUnprojectsFromScreenCenter(TestContext& ctx)
    {
        const glm::vec3 cameraPosition{8.0f, 20.0f, 8.0f};
        const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3{8.0f, 0.0f, 8.0f}, glm::vec3{0.0f, 0.0f, -1.0f});
        const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);

        const Engine::CursorWorldRay ray =
            Engine::cursorWorldRayFromViewProjection({400.0f, 400.0f}, {800, 800}, projection * view);
        ctx.expect(ray.status == Engine::CursorTraceStatus::Success, "cursor ray failed: " + ray.message);
        ctx.expect(std::abs(ray.direction.x) < 0.01f, "center cursor ray had unexpected x direction");
        ctx.expect(ray.direction.y < -0.99f, "center cursor ray did not point downward");
        ctx.expect(std::abs(ray.direction.z) < 0.01f, "center cursor ray had unexpected z direction");
    }

    void cursorRayProjectsOntoNavigation(TestContext& ctx)
    {
        Engine::NavigationSystem navigation;
        if (!addFlatTile(navigation, ctx)) {
            return;
        }
        Engine::SceneNavigationService service{navigation};

        const glm::vec3 cameraPosition{8.0f, 20.0f, 8.0f};
        const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3{8.0f, 0.0f, 8.0f}, glm::vec3{0.0f, 0.0f, -1.0f});
        const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const Engine::CursorWorldRay ray =
            Engine::cursorWorldRayFromViewProjection({400.0f, 400.0f}, {800, 800}, projection * view);

        Engine::NavigationQueryFilter filter;
        filter.requireLoadedTiles = true;
        const Engine::CursorNavigationProjectionResult result =
            Engine::projectCursorRayToNavigation(service, ray, {}, filter, 64.0f, 0.5f);
        ctx.expect(result.status == Engine::CursorTraceStatus::Success,
            "cursor ray did not project onto navigation: " + result.message);
        ctx.expect(std::abs(result.projection.point.x - 8.0f) < 1.0f &&
                std::abs(result.projection.point.z - 8.0f) < 1.0f,
            "cursor navigation projection was not near the screen-center target");
        ctx.expect(result.distance > 10.0f, "cursor navigation projection accepted a point far above the tile");
    }

    void invalidCursorRayInputsFailCleanly(TestContext& ctx)
    {
        const Engine::CursorWorldRay invalidViewport =
            Engine::cursorWorldRayFromViewProjection({100.0f, 100.0f}, {0, 800}, glm::mat4{1.0f});
        ctx.expect(invalidViewport.status == Engine::CursorTraceStatus::InvalidInput,
            "invalid viewport did not fail cursor ray construction");

        Engine::NavigationSystem navigation;
        Engine::SceneNavigationService service{navigation};
        const Engine::CursorNavigationProjectionResult result =
            Engine::projectCursorRayToNavigation(service, invalidViewport);
        ctx.expect(result.status == Engine::CursorTraceStatus::InvalidInput,
            "invalid cursor ray did not fail navigation projection");
    }

    void localPathReturnsDeterministicPoints(TestContext& ctx)
    {        Engine::NavigationSystem navigation;
        if (!addFlatTile(navigation, ctx)) {
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
        Engine::NavigationSystem navigation;
        if (!addFlatTile(navigation, ctx)) {
            return;
        }
        Engine::SceneNavigationService service{navigation};

        const Engine::NavigationPathResult result = service.findPath({2.0f, 0.0f, 8.0f}, {32.0f, 0.0f, 8.0f});
        ctx.expect(result.status != Engine::NavigationRuntimeStatus::Success || !result.path.complete,
            "unloaded target unexpectedly returned a complete path");
    }

    void reachabilityWrapsPathStatus(TestContext& ctx)
    {        Engine::NavigationSystem navigation;
        if (!addFlatTile(navigation, ctx)) {
            return;
        }
        Engine::SceneNavigationService service{navigation};

        const Engine::NavigationReachabilityResult result = service.reachable({2.0f, 0.0f, 2.0f}, {14.0f, 0.0f, 14.0f});
        ctx.expect(result.status == Engine::NavigationRuntimeStatus::Success, "reachable status was not success");
        ctx.expect(result.reachable, "reachable wrapper did not report reachable");
    }

    void raycastReportsClearAndBlocked(TestContext& ctx)
    {
        {            Engine::NavigationSystem navigation;
            if (!addFlatTile(navigation, ctx)) {
                return;
            }
            Engine::SceneNavigationService service{navigation};
            const Engine::NavigationRaycastResult result = service.raycast({2.0f, 0.0f, 8.0f}, {14.0f, 0.0f, 8.0f});
            ctx.expect(result.status == Engine::NavigationRuntimeStatus::Success, "clear raycast failed: " + result.message);
            ctx.expect(!result.blocked, "clear raycast reported blocked");
        }

        {
            Engine::NavigationSystem navigation;
            if (!addFlatTile(navigation, ctx)) {
                return;
            }
            Engine::SceneNavigationService service{navigation};
            const Engine::NavigationRaycastResult result = service.raycast({2.0f, 0.0f, 8.0f}, {32.0f, 0.0f, 8.0f});
            ctx.expect(result.status != Engine::NavigationRuntimeStatus::Success || result.blocked,
                "out-of-tile raycast unexpectedly reported clear");
        }
    }

    void sceneActorPathUsesWorldTransform(TestContext& ctx)
    {        Engine::NavigationSystem navigation;
        if (!addFlatTile(navigation, ctx)) {
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
    {        Engine::NavigationSystem navigation;
        if (!addFlatTile(navigation, ctx)) {
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

    void crossLoadedTilesUsesDirectPathWhenAvailable(TestContext& ctx)
    {        Engine::NavigationSystem navigation;
        if (!addFlatTileAt(navigation, ctx, {0, 0}, 0.0f) ||
            !addFlatTileAt(navigation, ctx, {1, 0}, 0.0f)) {
            return;
        }
        Engine::NavigationConnectivitySystem connectivity;
        connectivity.rebuild({{0, 0}, {1, 0}}, navigation, Engine::NavAgentSettings{});
        Engine::SceneNavigationService service{navigation, &connectivity};

        const Engine::NavigationPathResult result =
            service.findPathAcrossLoadedTiles({2.0f, 0.0f, 8.0f}, {30.0f, 0.0f, 8.0f});
        ctx.expect(result.status == Engine::NavigationRuntimeStatus::Success,
            "cross-loaded direct path failed: " + result.message);
        ctx.expect(result.path.complete, "cross-loaded direct path was incomplete");
    }

    void crossLoadedTilesUsesTileLocalPathForElevatedSameTile(TestContext& ctx)
    {        Engine::NavigationSystem navigation;
        if (!addFlatTileAt(navigation, ctx, {0, 0}, 0.0f)) {
            return;
        }
        Engine::NavigationConnectivitySystem connectivity;
        connectivity.rebuild({{0, 0}}, navigation, Engine::NavAgentSettings{});
        Engine::SceneNavigationService service{navigation, &connectivity};

        const Engine::NavigationPathResult result =
            service.findPathAcrossLoadedTiles({2.0f, 6.0f, 8.0f}, {14.0f, 6.0f, 8.0f});
        ctx.expect(result.status == Engine::NavigationRuntimeStatus::Success,
            "elevated same-tile path failed: " + result.message);
        ctx.expect(result.path.complete, "elevated same-tile path was incomplete");
    }

    void crossLoadedTilesStitchesPortalRoute(TestContext& ctx)
    {        Engine::NavigationSystem navigation;
        if (!addFlatTileAt(navigation, ctx, {0, 0}, 0.0f) ||
            !addFlatTileAt(navigation, ctx, {2, 0}, 0.0f)) {
            return;
        }

        Engine::ChunkNavConnectivity startConnectivity;
        startConnectivity.coord = {0, 0};
        startConnectivity.biomeId = "test";
        startConnectivity.traversalCost = ChunkSize;
        startConnectivity.partial = true;
        startConnectivity.portalsByEdge[static_cast<uint32_t>(Engine::NavEdgeDirection::East)].push_back({
            Engine::NavEdgeDirection::East,
            {15.0f, 0.0f, 8.0f},
            {2, 0},
            true,
            true,
            {33.0f, 0.0f, 8.0f},
        });
        Engine::ChunkNavConnectivity goalConnectivity;
        goalConnectivity.coord = {2, 0};
        goalConnectivity.biomeId = "test";
        goalConnectivity.traversalCost = ChunkSize;
        goalConnectivity.partial = true;

        Engine::NavigationConnectivitySystem connectivity;
        connectivity.loadCacheData({{startConnectivity, goalConnectivity}});
        Engine::SceneNavigationService service{navigation, &connectivity};

        const Engine::NavigationPathResult direct =
            service.findPath({2.0f, 0.0f, 8.0f}, {46.0f, 0.0f, 8.0f});
        ctx.expect(direct.status != Engine::NavigationRuntimeStatus::Success || !direct.path.complete,
            "test setup unexpectedly had a direct Detour path");

        const Engine::NavigationPathResult stitched =
            service.findPathAcrossLoadedTiles({2.0f, 0.0f, 8.0f}, {46.0f, 0.0f, 8.0f});
        ctx.expect(stitched.status == Engine::NavigationRuntimeStatus::Success,
            "stitched portal route failed: " + stitched.message);
        ctx.expect(stitched.path.complete, "stitched portal route was incomplete");
        ctx.expect(stitched.path.points.size() >= 4, "stitched portal route did not include seam waypoints");
    }

    void crossLoadedTilesSnapsElevatedEndpointsBeforeStitching(TestContext& ctx)
    {        Engine::NavigationSystem navigation;
        if (!addFlatTileAt(navigation, ctx, {0, 0}, 0.0f) ||
            !addFlatTileAt(navigation, ctx, {2, 0}, 0.0f)) {
            return;
        }

        Engine::ChunkNavConnectivity startConnectivity;
        startConnectivity.coord = {0, 0};
        startConnectivity.biomeId = "test";
        startConnectivity.traversalCost = ChunkSize;
        startConnectivity.partial = true;
        startConnectivity.portalsByEdge[static_cast<uint32_t>(Engine::NavEdgeDirection::East)].push_back({
            Engine::NavEdgeDirection::East,
            {15.0f, 0.0f, 8.0f},
            {2, 0},
            true,
            true,
            {33.0f, 0.0f, 8.0f},
        });
        Engine::ChunkNavConnectivity goalConnectivity;
        goalConnectivity.coord = {2, 0};
        goalConnectivity.biomeId = "test";
        goalConnectivity.traversalCost = ChunkSize;
        goalConnectivity.partial = true;

        Engine::NavigationConnectivitySystem connectivity;
        connectivity.loadCacheData({{startConnectivity, goalConnectivity}});
        Engine::SceneNavigationService service{navigation, &connectivity};

        const Engine::NavigationPathResult stitched =
            service.findPathAcrossLoadedTiles({2.0f, 8.0f, 8.0f}, {46.0f, 8.0f, 8.0f});
        ctx.expect(stitched.status == Engine::NavigationRuntimeStatus::Success,
            "elevated endpoint portal route failed: " + stitched.message);
        ctx.expect(stitched.path.complete, "elevated endpoint portal route was incomplete");
    }

    void crossLoadedTilesMissingConnectivityFailsCleanly(TestContext& ctx)
    {        Engine::NavigationSystem navigation;
        if (!addFlatTileAt(navigation, ctx, {0, 0}, 0.0f) ||
            !addFlatTileAt(navigation, ctx, {2, 0}, 0.0f)) {
            return;
        }
        Engine::SceneNavigationService service{navigation};

        const Engine::NavigationPathResult result =
            service.findPathAcrossLoadedTiles({2.0f, 0.0f, 8.0f}, {46.0f, 0.0f, 8.0f});
        ctx.expect(result.status != Engine::NavigationRuntimeStatus::Success,
            "missing connectivity unexpectedly produced a route");
        ctx.expect(!result.message.empty(), "missing connectivity failure had no message");
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
        {"CursorRayUnprojectsFromScreenCenter", cursorRayUnprojectsFromScreenCenter},
        {"CursorRayProjectsOntoNavigation", cursorRayProjectsOntoNavigation},
        {"InvalidCursorRayInputsFailCleanly", invalidCursorRayInputsFailCleanly},
        {"LocalPathReturnsDeterministicPoints", localPathReturnsDeterministicPoints},
        {"BlockedPathFailsCleanly", blockedPathFailsCleanly},
        {"ReachabilityWrapsPathStatus", reachabilityWrapsPathStatus},
        {"RaycastReportsClearAndBlocked", raycastReportsClearAndBlocked},
        {"SceneActorPathUsesWorldTransform", sceneActorPathUsesWorldTransform},
        {"InvalidInputsFailCleanly", invalidInputsFailCleanly},
        {"DiagnosticsAndDebugRequestsAreRecorded", diagnosticsAndDebugRequestsAreRecorded},
        {"CrossLoadedTilesUsesDirectPathWhenAvailable", crossLoadedTilesUsesDirectPathWhenAvailable},
        {"CrossLoadedTilesUsesTileLocalPathForElevatedSameTile", crossLoadedTilesUsesTileLocalPathForElevatedSameTile},
        {"CrossLoadedTilesStitchesPortalRoute", crossLoadedTilesStitchesPortalRoute},
        {"CrossLoadedTilesSnapsElevatedEndpointsBeforeStitching", crossLoadedTilesSnapsElevatedEndpointsBeforeStitching},
        {"CrossLoadedTilesMissingConnectivityFailsCleanly", crossLoadedTilesMissingConnectivityFailsCleanly},
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
