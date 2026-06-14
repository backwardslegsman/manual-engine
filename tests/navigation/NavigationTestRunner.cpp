#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ActorController.hpp"
#include "Engine/BlockingCollision.hpp"
#include "Engine/EventQueue.hpp"
#include "Engine/NavigationConnectivity.hpp"
#include "Engine/SpatialRegistry.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/World.hpp"
#include "Engine/WorldNavigationGraph.hpp"

namespace {
    constexpr float ChunkSize = 16.0f;
    constexpr uint32_t Resolution = 17;
    constexpr float FixedDt = 1.0f / 60.0f;

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

    struct ScenarioResult {
        Engine::ActorPathStatus pathStatus = Engine::ActorPathStatus::Idle;
        Engine::ActorRouteStatus routeStatus = Engine::ActorRouteStatus::None;
        Engine::NavQueryStatus lastQueryStatus = Engine::NavQueryStatus::Unsupported;
        std::string lastQueryMessage;
        std::string lastRouteMessage;
        uint32_t pathPointCount = 0;
        uint32_t currentCorner = 0;
        uint32_t blockedTicks = 0;
        uint32_t collisionHitCount = 0;
        Engine::WorldObjectHandle firstBlockingObject;
        glm::vec3 finalPosition{};
        float distanceToDestination = 0.0f;
        std::vector<glm::vec3> trace;
    };

    const char* pathStatusName(Engine::ActorPathStatus status)
    {
        switch (status) {
            case Engine::ActorPathStatus::Idle:
                return "idle";
            case Engine::ActorPathStatus::Pathing:
                return "pathing";
            case Engine::ActorPathStatus::Moving:
                return "moving";
            case Engine::ActorPathStatus::Blocked:
                return "blocked";
            case Engine::ActorPathStatus::Repathing:
                return "repathing";
            case Engine::ActorPathStatus::Arrived:
                return "arrived";
            case Engine::ActorPathStatus::Failed:
                return "failed";
            case Engine::ActorPathStatus::Cancelled:
                return "cancelled";
        }
        return "unknown";
    }

    const char* routeStatusName(Engine::ActorRouteStatus status)
    {
        switch (status) {
            case Engine::ActorRouteStatus::None:
                return "none";
            case Engine::ActorRouteStatus::Planning:
                return "planning";
            case Engine::ActorRouteStatus::MovingToWaypoint:
                return "moving_to_waypoint";
            case Engine::ActorRouteStatus::WaitingForLocalTile:
                return "waiting_for_local_tile";
            case Engine::ActorRouteStatus::Arrived:
                return "arrived";
            case Engine::ActorRouteStatus::Failed:
                return "failed";
            case Engine::ActorRouteStatus::Cancelled:
                return "cancelled";
        }
        return "unknown";
    }

    const char* navStatusName(Engine::NavQueryStatus status)
    {
        switch (status) {
            case Engine::NavQueryStatus::Success:
                return "success";
            case Engine::NavQueryStatus::NotInitialized:
                return "not_initialized";
            case Engine::NavQueryStatus::NoTile:
                return "no_tile";
            case Engine::NavQueryStatus::NoNearestPoly:
                return "no_nearest_poly";
            case Engine::NavQueryStatus::NoPath:
                return "no_path";
            case Engine::NavQueryStatus::InvalidInput:
                return "invalid_input";
            case Engine::NavQueryStatus::Unsupported:
                return "unsupported";
        }
        return "unknown";
    }

    float xzDistance(const glm::vec3& a, const glm::vec3& b)
    {
        const glm::vec2 delta{a.x - b.x, a.z - b.z};
        return std::sqrt(glm::dot(delta, delta));
    }

    bool terminalPathStatus(Engine::ActorPathStatus status)
    {
        return status == Engine::ActorPathStatus::Arrived ||
            status == Engine::ActorPathStatus::Failed ||
            status == Engine::ActorPathStatus::Cancelled;
    }

    Engine::TerrainSystem makeTerrain()
    {
        Engine::TerrainSettings settings;
        settings.chunkSize = ChunkSize;
        settings.resolution = Resolution;
        settings.createRendererResources = false;
        return Engine::TerrainSystem(settings);
    }

    std::vector<float> heights(float value)
    {
        return std::vector<float>(static_cast<size_t>(Resolution) * Resolution, value);
    }

    std::vector<float> rampHeights(float startHeight, float endHeight)
    {
        std::vector<float> result;
        result.reserve(static_cast<size_t>(Resolution) * Resolution);
        for (uint32_t z = 0; z < Resolution; ++z) {
            (void)z;
            for (uint32_t x = 0; x < Resolution; ++x) {
                const float t = static_cast<float>(x) / static_cast<float>(Resolution - 1);
                result.push_back(startHeight + (endHeight - startHeight) * t);
            }
        }
        return result;
    }

    std::vector<float> stepHeights(float lowHeight, float highHeight, uint32_t stepColumn = Resolution / 2)
    {
        std::vector<float> result;
        result.reserve(static_cast<size_t>(Resolution) * Resolution);
        for (uint32_t z = 0; z < Resolution; ++z) {
            (void)z;
            for (uint32_t x = 0; x < Resolution; ++x) {
                result.push_back(x < stepColumn ? lowHeight : highHeight);
            }
        }
        return result;
    }

    float riseForSlopeDegrees(float degrees, float run)
    {
        return std::tan(glm::radians(degrees)) * run;
    }

    std::vector<float> rampForSlopeDegrees(float degrees, float startHeight = 0.0f)
    {
        return rampHeights(startHeight, startHeight + riseForSlopeDegrees(degrees, ChunkSize));
    }

    std::string pathSummary(const Engine::NavQueryResult& path)
    {
        std::ostringstream message;
        message << "status=" << navStatusName(path.status)
                << " complete=" << (path.path.complete ? "true" : "false")
                << " points=" << path.path.points.size()
                << " message='" << path.message << "'";
        return message.str();
    }

    void expectCompletePath(
        TestContext& ctx,
        const Engine::NavQueryResult& path,
        const glm::vec3& destination,
        std::string_view label)
    {
        ctx.expect(path.status == Engine::NavQueryStatus::Success, std::string{label} + " query failed: " + pathSummary(path));
        ctx.expect(path.path.complete, std::string{label} + " path was incomplete: " + pathSummary(path));
        ctx.expect(!path.path.points.empty() && xzDistance(path.path.points.back(), destination) <= 1.0f,
            std::string{label} + " path did not end near destination");
    }

    void expectIncompletePath(TestContext& ctx, const Engine::NavQueryResult& path, std::string_view label)
    {
        ctx.expect(path.status != Engine::NavQueryStatus::Success || !path.path.complete,
            std::string{label} + " unexpectedly returned a complete path: " + pathSummary(path));
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

    struct TileSpec {
        Engine::ChunkCoord coord;
        std::vector<float> heights;
        std::vector<Renderer::Aabb> blockers;
    };

    bool addTiles(
        Engine::TerrainSystem& terrain,
        Engine::NavigationSystem& navigation,
        const std::vector<TileSpec>& specs,
        const Engine::NavAgentSettings& agent,
        TestContext& ctx,
        bool requireBuildSuccess = true)
    {
        bool ok = true;
        for (const TileSpec& spec : specs) {
            const Engine::TerrainTileHandle tile = terrain.createTileFromHeights(spec.coord, spec.heights);
            if (tile.id == UINT32_MAX) {
                ctx.expect(false, "failed to create CPU terrain tile");
                ok = false;
                continue;
            }
            std::optional<Engine::NavigationTerrainBuildData> buildData = terrain.navigationBuildData(tile);
            if (!buildData) {
                ctx.expect(false, "failed to extract navigation build data");
                ok = false;
                continue;
            }
            for (const Renderer::Aabb& blocker : spec.blockers) {
                appendAabbBlocker(*buildData, blocker);
            }
            const Engine::NavigationTileHandle handle = navigation.buildTerrainTile(*buildData, agent);
            if (handle.id == UINT32_MAX) {
                if (requireBuildSuccess) {
                    std::ostringstream message;
                    message << "failed to build nav tile " << spec.coord.x << "," << spec.coord.z
                            << ": " << navigation.lastBuildMessage();
                    ctx.expect(false, message.str());
                }
                ok = false;
            }
        }
        return ok;
    }

    Engine::ActorHandle createActorAt(
        Engine::ActorController& actors,
        Engine::World& world,
        Engine::TerrainSystem& terrain,
        const glm::vec3& position,
        Engine::ActorControllerSettings settings = {})
    {
        const Engine::WorldObjectHandle object = world.createObject();
        Engine::ActorHandle actor = actors.createActor(object, settings);
        actors.setPosition(actor, position, &terrain, &world);
        return actor;
    }

    ScenarioResult captureResult(
        const Engine::ActorController& actors,
        Engine::ActorHandle actor,
        const Engine::World& world,
        const glm::vec3& destination,
        std::vector<glm::vec3> trace)
    {
        ScenarioResult result;
        result.trace = std::move(trace);
        if (const std::optional<Engine::ActorState> state = actors.state(actor)) {
            result.pathStatus = state->path.status;
            result.routeStatus = state->route.status;
            result.lastQueryStatus = state->path.lastQueryStatus;
            result.lastQueryMessage = state->path.lastQueryMessage;
            result.lastRouteMessage = state->route.lastRouteMessage;
            result.pathPointCount = static_cast<uint32_t>(state->path.path.points.size());
            result.currentCorner = state->path.currentCorner;
            result.blockedTicks = state->path.blockedTicks;
            result.collisionHitCount = state->collisionHitCount;
            result.firstBlockingObject = state->firstBlockingObject;
        }
        result.finalPosition = actors.position(actor, world).value_or(glm::vec3{});
        result.distanceToDestination = xzDistance(result.finalPosition, destination);
        return result;
    }

    ScenarioResult simulate(
        Engine::ActorController& actors,
        Engine::ActorHandle actor,
        Engine::TerrainSystem& terrain,
        Engine::World& world,
        const glm::vec3& destination,
        uint32_t maxTicks,
        const Engine::SpatialRegistry* spatialRegistry = nullptr,
        const Engine::BlockingCollisionSystem* collision = nullptr,
        const Engine::NavigationSystem* navigation = nullptr,
        const Engine::NavAgentSettings* agent = nullptr)
    {
        std::vector<glm::vec3> trace;
        Engine::EventQueue events;
        for (uint32_t tick = 0; tick < maxTicks; ++tick) {
            if (spatialRegistry && collision && navigation && agent) {
                actors.fixedUpdate(actor, events, terrain, world, *spatialRegistry, *collision, *navigation, *agent, FixedDt);
            } else if (spatialRegistry && collision) {
                actors.fixedUpdate(actor, events, terrain, world, *spatialRegistry, *collision, FixedDt);
            } else {
                actors.fixedUpdate(actor, events, terrain, world, FixedDt);
            }
            if (tick % 15 == 0) {
                trace.push_back(actors.position(actor, world).value_or(glm::vec3{}));
            }
            const std::optional<Engine::ActorState> state = actors.state(actor);
            if (state && terminalPathStatus(state->path.status) &&
                (state->route.status == Engine::ActorRouteStatus::None ||
                    state->route.status == Engine::ActorRouteStatus::Arrived ||
                    state->route.status == Engine::ActorRouteStatus::Failed ||
                    state->route.status == Engine::ActorRouteStatus::Cancelled ||
                    state->route.status == Engine::ActorRouteStatus::WaitingForLocalTile)) {
                break;
            }
            events.clear();
        }
        return captureResult(actors, actor, world, destination, std::move(trace));
    }

    void expectArrived(TestContext& ctx, const ScenarioResult& result, float maxDistance)
    {
        std::ostringstream message;
        message << "expected arrival, got path=" << pathStatusName(result.pathStatus)
                << " route=" << routeStatusName(result.routeStatus)
                << " query=" << navStatusName(result.lastQueryStatus)
                << " dist=" << result.distanceToDestination
                << " msg='" << result.lastQueryMessage << "'";
        ctx.expect(result.pathStatus == Engine::ActorPathStatus::Arrived, message.str());
        ctx.expect(result.distanceToDestination <= maxDistance, "actor stopped too far from destination");
    }

    void directFlatPath(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        Engine::World world;
        Engine::ActorController actors;
        Engine::ActorControllerSettings actorSettings;
        actorSettings.collisionEnabled = false;
        actorSettings.groundOffset = 0.0f;
        const glm::vec3 start{2.0f, 0.0f, 2.0f};
        const glm::vec3 destination{14.0f, 0.0f, 14.0f};
        const Engine::ActorHandle actor = createActorAt(actors, world, terrain, start, actorSettings);
        const Engine::NavQueryResult path = actors.setPathDestination(actor, destination, navigation, agent, world);
        ctx.expect(path.status == Engine::NavQueryStatus::Success, "direct flat path query failed: " + path.message);
        ctx.expect(path.path.points.size() >= 2, "direct flat path returned fewer than two points");
        expectArrived(ctx, simulate(actors, actor, terrain, world, destination, 360), 0.5f);
    }

    void crossChunkSeam(TestContext& ctx, Engine::ChunkCoord left, Engine::ChunkCoord right)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{left, heights(0.0f), {}}, {right, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        const float startX = static_cast<float>(left.x) * ChunkSize + ChunkSize * 0.5f;
        const float endX = static_cast<float>(right.x) * ChunkSize + ChunkSize * 0.5f;
        const float z = static_cast<float>(left.z) * ChunkSize + ChunkSize * 0.5f;
        const glm::vec3 start{startX, 0.0f, z};
        const glm::vec3 destination{endX, 0.0f, z};
        const Engine::NavQueryResult path = navigation.findPath(start, destination, agent);
        ctx.expect(path.status == Engine::NavQueryStatus::Success, "cross-chunk path query failed: " + path.message);
        ctx.expect(path.path.complete, "cross-chunk path was not complete");
        ctx.expect(!path.path.points.empty() && xzDistance(path.path.points.back(), destination) <= 1.0f,
            "cross-chunk path did not end near destination");
    }

    void blockedDestination(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        const Renderer::Aabb wall{{7.4f, -0.2f, 0.0f}, {8.6f, 2.0f, ChunkSize}};
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {wall}}}, agent, ctx)) {
            return;
        }

        const Engine::NavQueryResult path = navigation.findPath({2.0f, 0.0f, 8.0f}, {14.0f, 0.0f, 8.0f}, agent);
        ctx.expect(path.status != Engine::NavQueryStatus::Success || !path.path.complete,
            "blocked destination unexpectedly returned a complete path");
    }

    void narrowCorridor(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        const Renderer::Aabb northWall{{0.0f, -0.2f, 10.0f}, {ChunkSize, 2.0f, ChunkSize}};
        const Renderer::Aabb southWall{{0.0f, -0.2f, 0.0f}, {ChunkSize, 2.0f, 6.0f}};
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {northWall, southWall}}}, agent, ctx)) {
            return;
        }

        const Engine::NavQueryResult path = navigation.findPath({2.0f, 0.0f, 8.0f}, {14.0f, 0.0f, 8.0f}, agent);
        ctx.expect(path.status == Engine::NavQueryStatus::Success, "narrow corridor path failed: " + path.message);
        ctx.expect(path.path.complete, "narrow corridor path was incomplete");
    }

    void slopeThreshold(TestContext& ctx)
    {
        Engine::NavAgentSettings agent;
        agent.maxSlopeDegrees = 30.0f;

        {
            Engine::TerrainSystem terrain = makeTerrain();
            Engine::NavigationSystem navigation;
            if (!addTiles(terrain, navigation, {{{0, 0}, rampHeights(0.0f, 6.0f), {}}}, agent, ctx)) {
                return;
            }
            const Engine::NavQueryResult path = navigation.findPath({2.0f, 0.8f, 8.0f}, {14.0f, 5.2f, 8.0f}, agent);
            ctx.expect(path.status == Engine::NavQueryStatus::Success, "below-threshold slope failed: " + path.message);
        }

        {
            Engine::TerrainSystem terrain = makeTerrain();
            Engine::NavigationSystem navigation;
            const Engine::TerrainTileHandle tile = terrain.createTileFromHeights({0, 0}, rampHeights(0.0f, 12.0f));
            if (tile.id == UINT32_MAX) {
                ctx.expect(false, "failed to create above-threshold slope tile");
                return;
            }
            const std::optional<Engine::NavigationTerrainBuildData> buildData = terrain.navigationBuildData(tile);
            if (!buildData) {
                ctx.expect(false, "failed to extract above-threshold slope build data");
                return;
            }
            const Engine::NavigationTileHandle handle = navigation.buildTerrainTile(*buildData, agent);
            if (handle.id != UINT32_MAX) {
                const Engine::NavQueryResult path = navigation.findPath({2.0f, 1.5f, 8.0f}, {14.0f, 10.5f, 8.0f}, agent);
                ctx.expect(path.status != Engine::NavQueryStatus::Success || !path.path.complete,
                    "above-threshold slope unexpectedly returned a complete path");
            }
        }
    }

    void stepHeightWithinChunk(TestContext& ctx)
    {
        struct StepCase {
            const char* label;
            float stepHeight;
            bool expectComplete;
        };
        constexpr StepCase cases[] = {
            {"below max climb", 0.4f, true},
            {"at max climb", 0.6f, true},
            {"above max climb", 1.2f, false},
        };

        for (const StepCase& stepCase : cases) {
            Engine::TerrainSystem terrain = makeTerrain();
            Engine::NavigationSystem navigation;
            Engine::NavAgentSettings agent;
            agent.maxClimb = 0.65f;
            if (!addTiles(terrain, navigation, {{{0, 0}, stepHeights(0.0f, stepCase.stepHeight), {}}}, agent, ctx, stepCase.expectComplete) &&
                stepCase.expectComplete) {
                continue;
            }

            const glm::vec3 start{2.0f, 0.0f, 8.0f};
            const glm::vec3 destination{14.0f, stepCase.stepHeight, 8.0f};
            const Engine::NavQueryResult path = navigation.findPath(start, destination, agent);
            if (stepCase.expectComplete) {
                expectCompletePath(ctx, path, destination, std::string{"within-chunk step "} + stepCase.label);
            } else {
                expectIncompletePath(ctx, path, std::string{"within-chunk step "} + stepCase.label);
            }
        }
    }

    void stepHeightAcrossChunks(TestContext& ctx)
    {
        struct StepCase {
            const char* label;
            float stepHeight;
            bool expectComplete;
        };
        constexpr StepCase cases[] = {
            {"below max climb", 0.4f, true},
            {"at max climb", 0.6f, true},
            {"above max climb", 1.2f, false},
        };

        for (const StepCase& stepCase : cases) {
            Engine::TerrainSystem terrain = makeTerrain();
            Engine::NavigationSystem navigation;
            Engine::NavAgentSettings agent;
            agent.maxClimb = 0.65f;
            if (!addTiles(
                    terrain,
                    navigation,
                    {{{0, 0}, heights(0.0f), {}}, {{1, 0}, heights(stepCase.stepHeight), {}}},
                    agent,
                    ctx,
                    stepCase.expectComplete) &&
                stepCase.expectComplete) {
                continue;
            }

            const glm::vec3 start{8.0f, 0.0f, 8.0f};
            const glm::vec3 destination{24.0f, stepCase.stepHeight, 8.0f};
            const Engine::NavQueryResult path = navigation.findPath(start, destination, agent);
            if (stepCase.expectComplete) {
                expectCompletePath(ctx, path, destination, std::string{"cross-chunk step "} + stepCase.label);
            } else {
                expectIncompletePath(ctx, path, std::string{"cross-chunk step "} + stepCase.label);
            }
        }
    }

    void stepHeightSameChunkDetour(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        agent.maxClimb = 0.65f;

        const Renderer::Aabb highStepBarrier{{7.0f, -0.2f, 0.0f}, {9.0f, 2.0f, ChunkSize}};
        if (!addTiles(
                terrain,
                navigation,
                {
                    {{0, 0}, heights(0.0f), {highStepBarrier}},
                    {{0, 1}, heights(0.0f), {}},
                    {{0, -1}, heights(0.0f), {}},
                },
                agent,
                ctx)) {
            return;
        }

        const glm::vec3 start{2.0f, 0.0f, 8.0f};
        const glm::vec3 destination{14.0f, 0.0f, 8.0f};
        const Engine::NavQueryResult directPath = navigation.findPath(start, destination, agent);
        expectIncompletePath(ctx, directPath, "same-chunk step detour direct local path");

        Engine::NavigationConnectivitySystem connectivity;
        connectivity.rebuild({{0, 0}, {0, 1}, {0, -1}}, navigation, terrain, agent);
        Engine::WorldNavigationGraph graph({4, ChunkSize});
        graph.rebuild({0, 0}, terrain, connectivity);
        const Engine::WorldNavRoute route = graph.findRouteAllowingSameChunkDetour(start, destination);
        ctx.expect(route.status == Engine::WorldNavRouteStatus::Success,
            "same-chunk step detour did not find a coarse route: " + route.message);
        ctx.expect(route.chunkSequence.size() >= 3, "same-chunk step detour route did not leave and re-enter the chunk");
    }

    void slopeSettingsWithinChunk(TestContext& ctx)
    {
        struct SlopeSettingCase {
            float maxSlope;
            float belowSlope;
            float atSlope;
            float aboveSlope;
        };
        constexpr SlopeSettingCase cases[] = {
            {20.0f, 15.0f, 20.0f, 25.0f},
            {35.0f, 30.0f, 35.0f, 40.0f},
            {50.0f, 45.0f, 50.0f, 55.0f},
        };

        for (const SlopeSettingCase& slopeCase : cases) {
            const auto runCase = [&](float slopeDegrees, bool expectComplete, const char* label) {
                Engine::TerrainSystem terrain = makeTerrain();
                Engine::NavigationSystem navigation;
                Engine::NavAgentSettings agent;
                agent.maxSlopeDegrees = slopeCase.maxSlope;
                agent.maxClimb = 3.0f;
                if (!addTiles(terrain, navigation, {{{0, 0}, rampForSlopeDegrees(slopeDegrees), {}}}, agent, ctx, expectComplete) &&
                    expectComplete) {
                    return;
                }
                const float destinationHeight = riseForSlopeDegrees(slopeDegrees, 14.0f);
                const glm::vec3 destination{14.0f, destinationHeight, 8.0f};
                const Engine::NavQueryResult path = navigation.findPath({2.0f, riseForSlopeDegrees(slopeDegrees, 2.0f), 8.0f}, destination, agent);
                std::ostringstream caseLabel;
                caseLabel << "within-chunk slope max=" << slopeCase.maxSlope << " " << label;
                if (expectComplete) {
                    expectCompletePath(ctx, path, destination, caseLabel.str());
                } else {
                    expectIncompletePath(ctx, path, caseLabel.str());
                }
            };

            runCase(slopeCase.belowSlope, true, "below");
            runCase(slopeCase.atSlope, true, "at");
            runCase(slopeCase.aboveSlope, false, "above");
        }
    }

    void slopeSettingsAcrossChunks(TestContext& ctx)
    {
        struct SlopeSettingCase {
            float maxSlope;
            float belowSlope;
            float atSlope;
            float aboveSlope;
        };
        constexpr SlopeSettingCase cases[] = {
            {20.0f, 15.0f, 20.0f, 25.0f},
            {35.0f, 30.0f, 35.0f, 40.0f},
        };

        for (const SlopeSettingCase& slopeCase : cases) {
            const auto runCase = [&](float slopeDegrees, bool expectComplete, const char* label) {
                const float tileRise = riseForSlopeDegrees(slopeDegrees, ChunkSize);
                Engine::TerrainSystem terrain = makeTerrain();
                Engine::NavigationSystem navigation;
                Engine::NavAgentSettings agent;
                agent.maxSlopeDegrees = slopeCase.maxSlope;
                agent.maxClimb = 3.0f;
                if (!addTiles(
                        terrain,
                        navigation,
                        {
                            {{0, 0}, rampHeights(0.0f, tileRise), {}},
                            {{1, 0}, rampHeights(tileRise, tileRise * 2.0f), {}},
                        },
                        agent,
                        ctx,
                        expectComplete) &&
                    expectComplete) {
                    return;
                }
                const glm::vec3 start{8.0f, riseForSlopeDegrees(slopeDegrees, 8.0f), 8.0f};
                const glm::vec3 destination{24.0f, riseForSlopeDegrees(slopeDegrees, 24.0f), 8.0f};
                const Engine::NavQueryResult path = navigation.findPath(start, destination, agent);
                std::ostringstream caseLabel;
                caseLabel << "cross-chunk slope max=" << slopeCase.maxSlope << " " << label;
                if (expectComplete) {
                    expectCompletePath(ctx, path, destination, caseLabel.str());
                } else {
                    expectIncompletePath(ctx, path, caseLabel.str());
                }
            };

            runCase(slopeCase.belowSlope, true, "below");
            runCase(slopeCase.atSlope, true, "at");
            runCase(slopeCase.aboveSlope, false, "above");
        }
    }

    void slopeSameChunkDetour(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        agent.maxSlopeDegrees = 30.0f;
        agent.maxClimb = 3.0f;

        const Renderer::Aabb steepSlopeBarrier{{7.0f, -0.2f, 0.0f}, {9.0f, 2.0f, ChunkSize}};
        if (!addTiles(
                terrain,
                navigation,
                {
                    {{0, 0}, heights(0.0f), {steepSlopeBarrier}},
                    {{0, 1}, rampForSlopeDegrees(20.0f), {}},
                    {{0, -1}, rampForSlopeDegrees(20.0f), {}},
                },
                agent,
                ctx)) {
            return;
        }

        const glm::vec3 start{2.0f, 0.0f, 8.0f};
        const glm::vec3 destination{14.0f, 0.0f, 8.0f};
        const Engine::NavQueryResult directPath = navigation.findPath(start, destination, agent);
        expectIncompletePath(ctx, directPath, "same-chunk slope detour direct local path");

        Engine::NavigationConnectivitySystem connectivity;
        connectivity.rebuild({{0, 0}, {0, 1}, {0, -1}}, navigation, terrain, agent);
        Engine::WorldNavigationGraph graph({4, ChunkSize});
        graph.rebuild({0, 0}, terrain, connectivity);
        const Engine::WorldNavRoute route = graph.findRouteAllowingSameChunkDetour(start, destination);
        ctx.expect(route.status == Engine::WorldNavRouteStatus::Success,
            "same-chunk slope detour did not find a coarse route: " + route.message);
        ctx.expect(route.chunkSequence.size() >= 3, "same-chunk slope detour route did not leave and re-enter the chunk");
    }

    void staticBlockerDetour(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        const Renderer::Aabb blocker{{6.0f, -0.2f, 4.0f}, {10.0f, 2.0f, 12.0f}};
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {blocker}}}, agent, ctx)) {
            return;
        }

        const Engine::NavQueryResult path = navigation.findPath({2.0f, 0.0f, 8.0f}, {14.0f, 0.0f, 8.0f}, agent);
        ctx.expect(path.status == Engine::NavQueryStatus::Success, "static blocker detour path failed: " + path.message);
        ctx.expect(path.path.complete, "static blocker detour path was incomplete");
        ctx.expect(path.path.points.size() > 2, "static blocker path did not expose a detour corner");
    }

    void hierarchicalRoute(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        std::vector<TileSpec> specs;
        for (int32_t x = 0; x <= 2; ++x) {
            specs.push_back({{x, 0}, heights(0.0f), {}});
        }
        if (!addTiles(terrain, navigation, specs, agent, ctx)) {
            return;
        }

        Engine::NavigationConnectivitySystem connectivity;
        connectivity.rebuild({{0, 0}, {1, 0}, {2, 0}}, navigation, terrain, agent);
        Engine::WorldNavigationGraph graph({4, ChunkSize});
        graph.rebuild({1, 0}, terrain, connectivity);
        const Engine::WorldNavRoute route = graph.findRoute({2.0f, 0.0f, 8.0f}, {42.0f, 0.0f, 8.0f});
        ctx.expect(route.status == Engine::WorldNavRouteStatus::Success, "world graph failed to plan route: " + route.message);
        ctx.expect(route.chunkSequence.size() >= 3, "world graph route did not cross multiple chunks");
        ctx.expect(route.portalWaypoints.size() >= 4, "world graph route did not include portal waypoint pairs");

        Engine::World world;
        Engine::ActorController actors;
        Engine::ActorControllerSettings actorSettings;
        actorSettings.collisionEnabled = false;
        actorSettings.groundOffset = 0.0f;
        const Engine::ActorHandle actor = createActorAt(actors, world, terrain, {2.0f, 0.0f, 8.0f}, actorSettings);
        const Engine::WorldNavRoute actorRoute =
            actors.setRouteDestination(actor, {42.0f, 0.0f, 8.0f}, graph, navigation, agent, world);
        ctx.expect(actorRoute.status == Engine::WorldNavRouteStatus::Success,
            "actor route command failed: " + actorRoute.message);
        Engine::SpatialRegistry registry;
        Engine::BlockingCollisionSystem collision;
        expectArrived(ctx, simulate(actors, actor, terrain, world, {42.0f, 0.0f, 8.0f}, 720, &registry, &collision, &navigation, &agent), 0.75f);
    }

    void missingLocalTile(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        Engine::NavigationConnectivitySystem connectivity;
        Engine::WorldNavigationGraph graph({4, ChunkSize});
        graph.rebuild({0, 0}, terrain, connectivity);

        Engine::World world;
        Engine::ActorController actors;
        Engine::ActorControllerSettings actorSettings;
        actorSettings.collisionEnabled = false;
        actorSettings.groundOffset = 0.0f;
        const Engine::ActorHandle actor = createActorAt(actors, world, terrain, {2.0f, 0.0f, 8.0f}, actorSettings);
        const glm::vec3 destination{42.0f, 0.0f, 8.0f};
        const Engine::WorldNavRoute route = actors.setRouteDestination(actor, destination, graph, navigation, agent, world);
        ctx.expect(route.status == Engine::WorldNavRouteStatus::Success, "coarse route should exist over generated graph");

        Engine::SpatialRegistry registry;
        Engine::BlockingCollisionSystem collision;
        const ScenarioResult result = simulate(actors, actor, terrain, world, destination, 180, &registry, &collision, &navigation, &agent);
        ctx.expect(
            result.routeStatus == Engine::ActorRouteStatus::WaitingForLocalTile ||
                result.routeStatus == Engine::ActorRouteStatus::Failed,
            "missing local tile did not produce waiting/failure route state");
    }

    void collisionReaction(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        Engine::World world;
        Engine::SpatialRegistry registry;
        Engine::BlockingCollisionSystem collision;
        const Engine::WorldObjectHandle blocker = world.createObject();
        world.setPosition(blocker, {8.0f, 0.0f, 8.0f});
        world.setLocalBounds(blocker, {{-0.75f, -1.0f, -2.0f}, {0.75f, 2.0f, 2.0f}});
        world.setCollisionEnabled(blocker, true);
        registry.insert(blocker, {8.0f, 0.0f, 8.0f});

        Engine::ActorController actors;
        Engine::ActorControllerSettings actorSettings;
        actorSettings.groundOffset = 0.0f;
        actorSettings.path.blockedTickLimit = 4;
        actorSettings.path.repathAttempts = 0;
        const Engine::ActorHandle actor = createActorAt(actors, world, terrain, {2.0f, 0.0f, 8.0f}, actorSettings);
        const glm::vec3 destination{14.0f, 0.0f, 8.0f};
        const Engine::NavQueryResult path = actors.setPathDestination(actor, destination, navigation, agent, world);
        ctx.expect(path.status == Engine::NavQueryStatus::Success, "collision reaction setup path failed: " + path.message);
        const ScenarioResult result = simulate(actors, actor, terrain, world, destination, 240, &registry, &collision, &navigation, &agent);
        ctx.expect(
            result.pathStatus == Engine::ActorPathStatus::Failed ||
                result.pathStatus == Engine::ActorPathStatus::Blocked,
            "collision reaction did not fail/block active path");
        ctx.expect(result.blockedTicks > 0 || result.collisionHitCount > 0, "collision reaction did not record blocking evidence");
    }

    void manualInputCancelsPath(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        Engine::World world;
        Engine::ActorController actors;
        Engine::ActorControllerSettings actorSettings;
        actorSettings.collisionEnabled = false;
        actorSettings.groundOffset = 0.0f;
        const Engine::ActorHandle actor = createActorAt(actors, world, terrain, {2.0f, 0.0f, 2.0f}, actorSettings);
        const Engine::NavQueryResult path = actors.setPathDestination(actor, {14.0f, 0.0f, 14.0f}, navigation, agent, world);
        ctx.expect(path.status == Engine::NavQueryStatus::Success, "manual cancel setup path failed: " + path.message);

        Engine::EventQueue events;
        events.publish(Engine::InputActionEvent{
            "player.move",
            Engine::InputActionPhase::Held,
            Engine::InputActionPayloadType::Axis2,
            Engine::InputActionSource::Keyboard,
            false,
            {1.0f, 0.0f},
            0.0f,
        });
        actors.fixedUpdate(actor, events, terrain, world, FixedDt);
        const std::optional<Engine::ActorPathState> state = actors.pathState(actor);
        ctx.expect(state && state->status == Engine::ActorPathStatus::Cancelled, "manual input did not cancel path");
    }

    void tileDiagnosticsAfterLiveBuild(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        const std::optional<Engine::NavigationTileDiagnostics> diagnostics = navigation.tileDiagnostics({0, 0});
        ctx.expect(diagnostics.has_value(), "missing tile diagnostics after live build");
        if (!diagnostics) {
            return;
        }
        ctx.expect(diagnostics->source == Engine::NavigationTileSource::LiveBuild, "tile diagnostics did not report live build source");
        ctx.expect(diagnostics->status == Engine::NavQueryStatus::Success, "tile diagnostics did not report success");
        ctx.expect(diagnostics->terrainTriangleCount > 0, "tile diagnostics missing terrain triangle count");
        ctx.expect(diagnostics->walkableTerrainTriangleCount > 0, "tile diagnostics missing walkable triangle count");
        ctx.expect(diagnostics->heightfieldWidth > 0 && diagnostics->heightfieldHeight > 0, "tile diagnostics missing heightfield size");
        ctx.expect(diagnostics->navPolygonCount > 0, "tile diagnostics missing nav polygon count");
    }

    void portalDiagnosticsAfterConnectivityBuild(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {}}, {{1, 0}, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        Engine::NavigationConnectivitySystem connectivity;
        connectivity.rebuild({{0, 0}, {1, 0}}, navigation, terrain, agent);
        const Engine::ChunkPortalDiagnostics* diagnostics = connectivity.portalDiagnostics({0, 0});
        ctx.expect(diagnostics != nullptr, "missing portal diagnostics after connectivity build");
        if (!diagnostics) {
            return;
        }
        uint32_t samples = 0;
        uint32_t accepted = 0;
        uint32_t rejected = 0;
        for (const Engine::NavigationPortalEdgeDiagnostics& edge : diagnostics->edges) {
            samples += edge.sampleCount;
            accepted += edge.acceptedPortalCount;
            rejected += edge.rejectedNoNearestPolyCount +
                edge.rejectedEdgeBandCount +
                edge.rejectedCenterReachabilityCount +
                edge.mergedDuplicateCount;
        }
        ctx.expect(samples == connectivity.settings().samplesPerEdge * Engine::NavEdgeDirectionCount,
            "portal diagnostics sample count did not match settings");
        ctx.expect(accepted > 0, "portal diagnostics did not record accepted portals");
        ctx.expect(accepted + rejected <= samples, "portal diagnostics counted more outcomes than samples");
    }

    void actorCommandDiagnosticsAfterRouteFallback(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {}}, {{1, 0}, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        Engine::NavigationConnectivitySystem connectivity;
        connectivity.rebuild({{0, 0}, {1, 0}}, navigation, terrain, agent);
        Engine::WorldNavigationGraph graph({2, ChunkSize});
        graph.rebuild({0, 0}, terrain, connectivity);

        Engine::World world;
        Engine::ActorController actors;
        Engine::ActorControllerSettings actorSettings;
        actorSettings.collisionEnabled = false;
        actorSettings.groundOffset = 0.0f;
        const Engine::ActorHandle actor = createActorAt(actors, world, terrain, {2.0f, 0.0f, 8.0f}, actorSettings);
        const glm::vec3 destination{30.0f, 0.0f, 8.0f};
        const Engine::WorldNavRoute route = actors.setRouteDestination(actor, destination, graph, navigation, agent, world);
        ctx.expect(route.status == Engine::WorldNavRouteStatus::Success, "route fallback setup failed: " + route.message);

        const std::optional<Engine::ActorState> state = actors.state(actor);
        ctx.expect(state.has_value(), "missing actor state after route command");
        if (!state) {
            return;
        }
        ctx.expect(state->commandDiagnostics.hasCommand, "actor command diagnostics did not record command");
        ctx.expect(state->commandDiagnostics.directLocalStatus != Engine::NavQueryStatus::Unsupported,
            "actor command diagnostics did not record direct query status");
        ctx.expect(!state->commandDiagnostics.finalReason.empty(), "actor command diagnostics missing final reason");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"DirectFlatPath", directFlatPath},
        {"CrossChunkSeam", [](TestContext& ctx) { crossChunkSeam(ctx, {0, 0}, {1, 0}); }},
        {"NegativeChunkSeam", [](TestContext& ctx) { crossChunkSeam(ctx, {-1, 0}, {0, 0}); }},
        {"BlockedDestination", blockedDestination},
        {"NarrowCorridor", narrowCorridor},
        {"SlopeThreshold", slopeThreshold},
        {"StepHeightWithinChunk", stepHeightWithinChunk},
        {"StepHeightAcrossChunks", stepHeightAcrossChunks},
        {"StepHeightSameChunkDetour", stepHeightSameChunkDetour},
        {"SlopeSettingsWithinChunk", slopeSettingsWithinChunk},
        {"SlopeSettingsAcrossChunks", slopeSettingsAcrossChunks},
        {"SlopeSameChunkDetour", slopeSameChunkDetour},
        {"StaticBlockerDetour", staticBlockerDetour},
        {"HierarchicalRoute", hierarchicalRoute},
        {"MissingLocalTile", missingLocalTile},
        {"CollisionReaction", collisionReaction},
        {"ManualInputCancelsPath", manualInputCancelsPath},
        {"TileDiagnosticsAfterLiveBuild", tileDiagnosticsAfterLiveBuild},
        {"PortalDiagnosticsAfterConnectivityBuild", portalDiagnosticsAfterConnectivityBuild},
        {"ActorCommandDiagnosticsAfterRouteFallback", actorCommandDiagnosticsAfterRouteFallback},
    };

    for (const auto& [name, test] : tests) {
        TestContext ctx{std::string{name}, failures};
        test(ctx);
        std::cout << "[navigation] " << name << '\n';
    }

    if (failures.empty()) {
        std::cout << "Navigation tests passed: " << tests.size() << '\n';
        return 0;
    }

    std::cerr << "Navigation tests failed: " << failures.size() << '\n';
    for (const TestFailure& failure : failures) {
        std::cerr << "  [" << failure.testName << "] " << failure.message << '\n';
    }
    return 1;
}
