#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ActorController.hpp"
#include "Engine/BlockingCollision.hpp"
#include "Engine/ChunkStreamer.hpp"
#include "Engine/EventQueue.hpp"
#include "Engine/FrameBudget.hpp"
#include "Engine/NavigationCache.hpp"
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

    Engine::BiomeSystem makeSingleBiome(float rollingAmplitude, float rollingFrequency, float detailAmplitude = 0.0f)
    {
        Engine::BiomeSystem biomes;
        Engine::BiomeDescriptor descriptor;
        descriptor.id = "test";
        descriptor.displayName = "Test";
        descriptor.heightScale = 1.0f;
        descriptor.rollingScale = 1.0f;
        descriptor.detailScale = 1.0f;
        descriptor.rollingAmplitude = rollingAmplitude;
        descriptor.rollingFrequencyX = rollingFrequency;
        descriptor.rollingFrequencyZ = rollingFrequency;
        descriptor.detailAmplitude = detailAmplitude;
        descriptor.detailFrequency = rollingFrequency;
        biomes.add(std::move(descriptor));
        return biomes;
    }

    Engine::TerrainSystem makeTerrainWithBiomes(const Engine::BiomeSystem& biomes)
    {
        Engine::TerrainSettings settings;
        settings.chunkSize = ChunkSize;
        settings.resolution = Resolution;
        settings.createRendererResources = false;
        settings.heightScale = 1.25f;
        settings.biomes = &biomes;
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

    struct ProfileSample {
        std::string name;
        uint32_t count = 0;
        double totalMs = 0.0;
        double maxMs = 0.0;

        double averageMs() const
        {
            return count > 0 ? totalMs / static_cast<double>(count) : 0.0;
        }
    };

    struct ProfileReport {
        std::vector<ProfileSample> samples;

        ProfileSample& sample(std::string_view name)
        {
            const auto found = std::ranges::find_if(samples, [&](const ProfileSample& sample) {
                return sample.name == name;
            });
            if (found != samples.end()) {
                return *found;
            }
            samples.push_back({std::string{name}});
            return samples.back();
        }

        template <typename Function>
        auto measure(std::string_view name, Function&& function)
        {
            const auto start = std::chrono::steady_clock::now();
            if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
                std::forward<Function>(function)();
                record(name, start);
            } else {
                auto result = std::forward<Function>(function)();
                record(name, start);
                return result;
            }
        }

        void record(std::string_view name, std::chrono::steady_clock::time_point start)
        {
            const auto end = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(end - start).count();
            ProfileSample& entry = sample(name);
            ++entry.count;
            entry.totalMs += ms;
            entry.maxMs = std::max(entry.maxMs, ms);
        }

        std::vector<ProfileSample> rankedByTotal() const
        {
            std::vector<ProfileSample> ranked = samples;
            std::ranges::sort(ranked, [](const ProfileSample& lhs, const ProfileSample& rhs) {
                return lhs.totalMs > rhs.totalMs;
            });
            return ranked;
        }

        std::string toString() const
        {
            std::ostringstream output;
            output << "Navigation CPU profile\n";
            output << std::left << std::setw(34) << "bucket"
                   << std::right << std::setw(8) << "count"
                   << std::setw(14) << "total ms"
                   << std::setw(14) << "avg ms"
                   << std::setw(14) << "max ms" << '\n';
            for (const ProfileSample& sample : rankedByTotal()) {
                output << std::left << std::setw(34) << sample.name
                       << std::right << std::setw(8) << sample.count
                       << std::setw(14) << std::fixed << std::setprecision(3) << sample.totalMs
                       << std::setw(14) << sample.averageMs()
                       << std::setw(14) << sample.maxMs << '\n';
            }
            return output.str();
        }
    };

    void writeProfileReport(std::string_view filename, const ProfileReport& report)
    {
        std::ofstream file{std::string{filename}};
        if (file) {
            file << report.toString();
        }
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

    void phasedConnectivityMatchesSynchronousRebuild(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        const std::vector<TileSpec> specs{
            {{0, 0}, heights(0.0f), {}},
            {{1, 0}, heights(0.0f), {}},
            {{0, 1}, heights(0.0f), {}},
            {{1, 1}, heights(0.0f), {}},
        };
        if (!addTiles(terrain, navigation, specs, agent, ctx)) {
            return;
        }

        const std::vector<Engine::ChunkCoord> chunks{{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        Engine::NavigationConnectivitySystem synchronous;
        synchronous.rebuild(chunks, navigation, terrain, agent);

        Engine::NavigationConnectivitySystem phased;
        Engine::NavigationConnectivityBuildRequest request;
        request.chunks = chunks;
        request.clearExisting = true;
        const Engine::NavigationConnectivityBuildHandle handle = phased.beginRebuild(std::move(request));
        uint32_t stepCount = 0;
        while (phased.hasActiveRebuild(handle) && stepCount++ < 256) {
            phased.stepRebuild(handle, navigation, terrain, agent, 9);
        }

        const Engine::NavigationConnectivityCacheData expected = synchronous.cacheData();
        const Engine::NavigationConnectivityCacheData actual = phased.cacheData();
        ctx.expect(actual.chunks.size() == expected.chunks.size(), "phased connectivity chunk count differed");
        ctx.expect(phased.stats().totalPortals == synchronous.stats().totalPortals, "phased connectivity portal count differed");
        ctx.expect(phased.stats().connectedPortals == synchronous.stats().connectedPortals, "phased connectivity connected portal count differed");
    }

    void phasedConnectivityMaxSamplesSplitsWork(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        Engine::NavigationConnectivitySystem phased;
        Engine::NavigationConnectivityBuildRequest request;
        request.chunks = {{0, 0}};
        request.clearExisting = true;
        const Engine::NavigationConnectivityBuildHandle handle = phased.beginRebuild(std::move(request));
        uint32_t stepCount = 0;
        uint32_t sampleSteps = 0;
        while (phased.hasActiveRebuild(handle) && stepCount++ < 128) {
            const Engine::NavigationConnectivityBuildStepResult step =
                phased.stepRebuild(handle, navigation, terrain, agent, 1);
            if (step.samplesProcessed > 0) {
                ++sampleSteps;
                ctx.expect(step.samplesProcessed == 1, "maxSamples=1 processed more than one sample");
            }
        }
        ctx.expect(!phased.hasActiveRebuild(handle), "phased connectivity did not complete");
        ctx.expect(sampleSteps >= 4, "phased connectivity did not split edge sampling across steps");
        const Engine::ChunkPortalDiagnostics* diagnostics = phased.portalDiagnostics({0, 0});
        ctx.expect(diagnostics != nullptr, "phased connectivity produced no diagnostics");
        if (diagnostics) {
            uint32_t samples = 0;
            for (const Engine::NavigationPortalEdgeDiagnostics& edge : diagnostics->edges) {
                samples += edge.sampleCount;
            }
            ctx.expect(samples == phased.settings().samplesPerEdge * Engine::NavEdgeDirectionCount,
                "phased connectivity diagnostics sample count was wrong");
        }
    }

    void phasedConnectivityCancelStopsMutation(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        Engine::NavigationConnectivitySystem phased;
        Engine::NavigationConnectivityBuildRequest request;
        request.chunks = {{0, 0}};
        request.clearExisting = true;
        const Engine::NavigationConnectivityBuildHandle handle = phased.beginRebuild(std::move(request));
        phased.stepRebuild(handle, navigation, terrain, agent, 1);
        phased.cancelRebuild(handle);
        phased.stepRebuild(handle, navigation, terrain, agent, 64);
        ctx.expect(!phased.hasActiveRebuild(handle), "cancelled connectivity rebuild remained active");
        ctx.expect(phased.cacheData().chunks.empty(), "cancelled connectivity rebuild mutated final cache data");
    }

    void phasedConnectivityClearExistingRemovesStaleChunks(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        if (!addTiles(terrain, navigation, {{{0, 0}, heights(0.0f), {}}, {{1, 0}, heights(0.0f), {}}}, agent, ctx)) {
            return;
        }

        Engine::NavigationConnectivitySystem phased;
        phased.rebuild({{0, 0}, {1, 0}}, navigation, terrain, agent);
        ctx.expect(phased.cacheData().chunks.size() == 2, "initial connectivity did not contain two chunks");

        Engine::NavigationConnectivityBuildRequest request;
        request.chunks = {{0, 0}};
        request.clearExisting = true;
        const Engine::NavigationConnectivityBuildHandle handle = phased.beginRebuild(std::move(request));
        uint32_t stepCount = 0;
        while (phased.hasActiveRebuild(handle) && stepCount++ < 128) {
            phased.stepRebuild(handle, navigation, terrain, agent, 64);
        }

        const Engine::NavigationConnectivityCacheData data = phased.cacheData();
        ctx.expect(data.chunks.size() == 1, "clearExisting phased rebuild left stale chunks");
        ctx.expect(!data.chunks.empty() && data.chunks.front().coord == Engine::ChunkCoord{0, 0},
            "clearExisting phased rebuild kept the wrong chunk");
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

    void terrainGenerationIsDeterministicAndContinuous(TestContext& ctx)
    {
        Engine::BiomeSystem biomes = Engine::BiomeSystem::sampleDefaults();
        Engine::TerrainSystem terrain = makeTerrainWithBiomes(biomes);

        const float heightA = terrain.generatedHeight(16.0f, 8.0f);
        const float heightB = terrain.generatedHeight(16.0f, 8.0f);
        ctx.expect(std::abs(heightA - heightB) <= 0.0001f, "generated height was not deterministic");

        const Engine::TerrainTileHandle left = terrain.createTile({0, 0}, {});
        const Engine::TerrainTileHandle right = terrain.createTile({1, 0}, {});
        ctx.expect(left.id != UINT32_MAX && right.id != UINT32_MAX, "failed to create adjacent terrain tiles");
        const std::optional<float> leftEdge = terrain.sampleHeight(ChunkSize, 8.0f);
        const std::optional<float> rightEdge = terrain.sampleHeight(ChunkSize, 8.0f);
        ctx.expect(leftEdge && rightEdge && std::abs(*leftEdge - *rightEdge) <= 0.0001f,
            "adjacent terrain edge heights did not match");
    }

    void terrainDiagnosticsReportRampSlope(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        const Engine::TerrainTileHandle tile = terrain.createTileFromHeights({0, 0}, rampHeights(0.0f, 4.0f));
        const Engine::NavAgentSettings agent;
        const std::optional<Engine::TerrainTileDiagnostics> diagnostics = terrain.tileDiagnostics(tile, &agent);
        ctx.expect(diagnostics.has_value(), "missing terrain diagnostics for ramp tile");
        if (!diagnostics) {
            return;
        }
        ctx.expect(diagnostics->maxHeight > diagnostics->minHeight, "terrain diagnostics did not report height range");
        ctx.expect(diagnostics->maxSlopeDegrees > 0.0f, "terrain diagnostics did not report ramp slope");
        ctx.expect(diagnostics->averageSlopeDegrees > 0.0f, "terrain diagnostics did not report average slope");
    }

    void lowerTerrainProfileReducesSlope(TestContext& ctx)
    {
        Engine::BiomeSystem gentleBiomes = makeSingleBiome(0.25f, 0.035f);
        Engine::BiomeSystem steepBiomes = makeSingleBiome(1.25f, 0.12f);
        Engine::TerrainSystem gentleTerrain = makeTerrainWithBiomes(gentleBiomes);
        Engine::TerrainSystem steepTerrain = makeTerrainWithBiomes(steepBiomes);
        const Engine::TerrainTileHandle gentleTile = gentleTerrain.createTile({0, 0}, {});
        const Engine::TerrainTileHandle steepTile = steepTerrain.createTile({0, 0}, {});
        const std::optional<Engine::TerrainTileDiagnostics> gentle = gentleTerrain.tileDiagnostics(gentleTile);
        const std::optional<Engine::TerrainTileDiagnostics> steep = steepTerrain.tileDiagnostics(steepTile);
        ctx.expect(gentle.has_value() && steep.has_value(), "missing terrain diagnostics for profile comparison");
        if (!gentle || !steep) {
            return;
        }
        ctx.expect(gentle->maxSlopeDegrees < steep->maxSlopeDegrees,
            "lower terrain profile did not reduce max sampled slope");
    }

    void navigationResolutionReducesBuildGeometry(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        const Engine::TerrainTileHandle tile = terrain.createTileFromHeights({0, 0}, heights(0.0f));
        const std::optional<Engine::NavigationTerrainBuildData> full = terrain.navigationBuildData(tile, Resolution);
        const std::optional<Engine::NavigationTerrainBuildData> reduced = terrain.navigationBuildData(tile, 9);
        ctx.expect(full.has_value() && reduced.has_value(), "missing navigation build data for resolution comparison");
        if (!full || !reduced) {
            return;
        }
        ctx.expect(reduced->vertices.size() < full->vertices.size(), "reduced nav resolution did not reduce vertex count");
        ctx.expect(reduced->indices.size() < full->indices.size(), "reduced nav resolution did not reduce index count");
    }

    void terrainRenderMeshBuildDataIsDeterministic(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        const Engine::TerrainTileHandle tile = terrain.createTileFromHeights({0, 0}, rampHeights(0.0f, 3.0f));
        const std::optional<Engine::TerrainRenderMeshBuildInput> input = terrain.renderMeshBuildInput(tile, 1);
        ctx.expect(input.has_value(), "missing terrain render mesh build input");
        if (!input) {
            return;
        }

        const Engine::TerrainRenderMeshBuildResult first = Engine::TerrainSystem::buildRenderMeshData(*input);
        const Engine::TerrainRenderMeshBuildResult second = Engine::TerrainSystem::buildRenderMeshData(*input);
        ctx.expect(first.success && second.success, "terrain render mesh worker build failed");
        ctx.expect(first.mesh.vertices.size() == second.mesh.vertices.size(), "terrain render mesh vertex count was not deterministic");
        ctx.expect(first.mesh.indices == second.mesh.indices, "terrain render mesh indices were not deterministic");
        ctx.expect(!first.mesh.vertices.empty(), "terrain render mesh produced no vertices");
        if (!first.mesh.vertices.empty() && !second.mesh.vertices.empty()) {
            ctx.expect(first.mesh.vertices.front().py == second.mesh.vertices.front().py,
                "terrain render mesh first vertex height was not deterministic");
        }
    }

    void terrainRenderMeshStaleGenerationRejected(TestContext& ctx)
    {
        Engine::TerrainSettings settings;
        settings.chunkSize = ChunkSize;
        settings.resolution = Resolution;
        settings.createRendererResources = true;
        Engine::TerrainSystem terrain{settings};
        const Engine::TerrainTileHandle tile = terrain.createTileFromHeights({0, 0}, heights(0.0f));
        const std::optional<Engine::TerrainRenderMeshBuildInput> input = terrain.renderMeshBuildInput(tile, 1);
        ctx.expect(input.has_value(), "missing stale-generation terrain render input");
        if (!input) {
            return;
        }

        Engine::TerrainRenderMeshBuildResult result = Engine::TerrainSystem::buildRenderMeshData(*input);
        ++result.mesh.generation;
        ctx.expect(!terrain.commitRendererMesh(result.mesh), "stale terrain render mesh generation was committed");
    }

    void terrainRenderMeshBuildDoesNotChangeCpuHeight(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        const Engine::TerrainTileHandle tile = terrain.createTileFromHeights({0, 0}, rampHeights(0.0f, 4.0f));
        const std::optional<float> before = terrain.sampleHeight(8.0f, 8.0f);
        const std::optional<Engine::TerrainRenderMeshBuildInput> input = terrain.renderMeshBuildInput(tile, 2);
        ctx.expect(before.has_value() && input.has_value(), "missing terrain height or render input");
        if (!before || !input) {
            return;
        }

        (void)Engine::TerrainSystem::buildRenderMeshData(*input);
        const std::optional<float> after = terrain.sampleHeight(8.0f, 8.0f);
        ctx.expect(after.has_value(), "missing terrain height after render mesh build");
        if (after) {
            ctx.expect(std::abs(*before - *after) < 0.0001f, "render mesh build changed CPU height query result");
        }
    }

    void chunkStreamerVisitsLoadedChunkByCoord(TestContext& ctx)
    {
        Engine::ChunkStreamer streamer{{ChunkSize, 1}};
        Engine::ChunkContent content;
        content.terrain = {7};
        content.objects = {{1}, {2}};
        ctx.expect(streamer.registerLoadedChunk({2, -1}, content), "failed to register test chunk");

        bool visited = false;
        const bool found = streamer.visitLoadedChunkContent(
            {2, -1},
            [&](Engine::TerrainTileHandle terrainTile, const std::vector<Engine::WorldObjectHandle>& objects, Renderer::RenderGroupHandle) {
                visited = true;
                ctx.expect(terrainTile.id == 7, "visited chunk terrain handle was wrong");
                ctx.expect(objects.size() == 2, "visited chunk object list was wrong");
            });
        ctx.expect(found && visited, "visitLoadedChunkContent did not visit loaded chunk");
        ctx.expect(!streamer.visitLoadedChunkContent({9, 9}, [](Engine::TerrainTileHandle, const std::vector<Engine::WorldObjectHandle>&, Renderer::RenderGroupHandle) {}),
            "visitLoadedChunkContent reported an unloaded chunk as found");
    }

    void frameBudgetDefersNormalWorkWhenExhausted(TestContext& ctx)
    {
        Engine::FrameBudget budget;
        budget.beginFrame({0.0f, true});
        bool ran = false;
        const bool accepted = budget.run(
            Engine::BudgetCategory::General,
            "normal",
            Engine::BudgetPriority::Normal,
            [&]() {
                ran = true;
            });
        ctx.expect(!accepted, "normal work was accepted with zero frame budget");
        ctx.expect(!ran, "normal work ran with zero frame budget");
        ctx.expect(budget.stats().itemsDeferred == 1, "deferred count did not include rejected normal work");
    }

    void frameBudgetRunsCriticalWorkWithOverrun(TestContext& ctx)
    {
        Engine::FrameBudget budget;
        budget.beginFrame({0.0f, true});
        bool ran = false;
        const bool accepted = budget.run(
            Engine::BudgetCategory::NavigationCommit,
            "critical",
            Engine::BudgetPriority::Critical,
            [&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ran = true;
            });
        ctx.expect(accepted, "critical work was rejected with zero frame budget");
        ctx.expect(ran, "critical work did not run");
        ctx.expect(budget.stats().itemsRun == 1, "critical work did not increment run count");
        ctx.expect(budget.stats().overrunMs > 0.0f, "critical work did not record budget overrun");
    }

    void mainThreadWorkQueuePreservesDeferredWork(TestContext& ctx)
    {
        Engine::MainThreadWorkQueue queue;
        uint32_t ran = 0;
        queue.enqueue({Engine::BudgetCategory::General, Engine::BudgetPriority::Normal, "first", [&]() { ++ran; }});
        queue.enqueue({Engine::BudgetCategory::General, Engine::BudgetPriority::Normal, "second", [&]() { ++ran; }});

        Engine::FrameBudget blockedBudget;
        blockedBudget.beginFrame({0.0f, true});
        queue.drain(blockedBudget);
        ctx.expect(ran == 0, "queue ran normal work with exhausted budget");
        ctx.expect(queue.pendingCount() == 2, "queue did not preserve deferred work");

        Engine::FrameBudget openBudget;
        openBudget.beginFrame({1.0f, false});
        queue.drain(openBudget);
        ctx.expect(ran == 2, "queue did not drain deferred work after budget opened");
        ctx.expect(queue.pendingCount() == 0, "queue still had pending work after open drain");
    }

    Engine::NavigationCacheManifest testCacheManifest(std::string identity)
    {
        Engine::NavigationCacheManifest manifest;
        manifest.identityHash = std::move(identity);
        manifest.worldId = "cache_test";
        manifest.formatVersion = 1;
        manifest.chunkSize = ChunkSize;
        manifest.graphRadiusChunks = 4;
        manifest.navigationResolution = Resolution;
        return manifest;
    }

    Engine::NavigationCacheSettings testCacheSettings(std::string_view name)
    {
        Engine::NavigationCacheSettings settings;
        settings.rootPath = std::filesystem::path{"generated/navigation_cache_tests"} / std::string{name};
        settings.worldId = "cache_test";
        settings.formatVersion = 1;
        std::filesystem::remove_all(settings.rootPath);
        return settings;
    }

    Engine::NavigationTileCacheData testTile(Engine::ChunkCoord coord)
    {
        Engine::NavigationTileCacheData tile;
        tile.coord = coord;
        tile.bounds = {{-1.0f, 0.0f, -2.0f}, {3.0f, 4.0f, 5.0f}};
        tile.detourTileData = {1, 2, 3, 4, 5};
        return tile;
    }

    void navigationCacheMissingTileReturnsMiss(TestContext& ctx)
    {
        const Engine::NavigationCacheSettings settings = testCacheSettings("missing_tile");
        const Engine::NavigationCacheManifest manifest = testCacheManifest("identity_a");
        const Engine::NavigationCacheTileReadResult result =
            Engine::NavigationCache::readTileCache(settings, manifest, {42, -7});
        ctx.expect(result.status == Engine::NavigationCacheOperationStatus::Miss,
            "missing tile cache did not return Miss");
        ctx.expect(!result.tile.has_value(), "missing tile returned payload");
    }

    void navigationCacheTileRoundTrips(TestContext& ctx)
    {
        const Engine::NavigationCacheSettings settings = testCacheSettings("tile_roundtrip");
        const Engine::NavigationCacheManifest manifest = testCacheManifest("identity_b");
        const Engine::NavigationTileCacheData tile = testTile({-3, 4});
        const Engine::NavigationCacheWriteResult write =
            Engine::NavigationCache::writeTileCache(settings, manifest, tile);
        const Engine::NavigationCacheTileReadResult read =
            Engine::NavigationCache::readTileCache(settings, manifest, tile.coord);
        ctx.expect(write.status == Engine::NavigationCacheOperationStatus::WriteSuccess,
            "tile cache write failed");
        ctx.expect(read.status == Engine::NavigationCacheOperationStatus::Hit,
            "tile cache read did not hit");
        ctx.expect(read.tile.has_value(), "tile cache hit returned no payload");
        if (read.tile) {
            ctx.expect(read.tile->coord == tile.coord, "tile cache coord did not round-trip");
            ctx.expect(read.tile->detourTileData == tile.detourTileData, "tile cache bytes did not round-trip");
        }
    }

    void navigationCacheCorruptTileIsSafe(TestContext& ctx)
    {
        const Engine::NavigationCacheSettings settings = testCacheSettings("corrupt_tile");
        const Engine::NavigationCacheManifest manifest = testCacheManifest("identity_c");
        const Engine::NavigationTileCacheData tile = testTile({2, 2});
        const Engine::NavigationCacheWriteResult write =
            Engine::NavigationCache::writeTileCache(settings, manifest, tile);
        ctx.expect(write.status == Engine::NavigationCacheOperationStatus::WriteSuccess,
            "failed to write tile before corrupting it");
        {
            std::ofstream file(write.path, std::ios::binary | std::ios::trunc);
            file << "bad";
        }
        const Engine::NavigationCacheTileReadResult read =
            Engine::NavigationCache::readTileCache(settings, manifest, tile.coord);
        ctx.expect(read.status == Engine::NavigationCacheOperationStatus::Stale ||
                read.status == Engine::NavigationCacheOperationStatus::Corrupt,
            "corrupt tile did not return Stale or Corrupt");
        ctx.expect(!read.tile.has_value(), "corrupt tile returned payload");
    }

    void navigationCacheConnectivityIdentityMismatchIsStale(TestContext& ctx)
    {
        const Engine::NavigationCacheSettings settings = testCacheSettings("connectivity_stale");
        const Engine::NavigationCacheManifest manifest = testCacheManifest("identity_d");
        Engine::ChunkNavConnectivity connectivity;
        connectivity.coord = {-2, -5};
        connectivity.biomeId = "test";
        connectivity.traversalCost = 1.0f;
        const Engine::NavigationCacheWriteResult write =
            Engine::NavigationCache::writeConnectivityCache(settings, manifest, connectivity);
        {
            std::ofstream file(write.path, std::ios::trunc);
            file << "identity_hash: stale_identity\nconnectivity:\n  coord: {x: -2, z: -5}\n";
        }
        const Engine::NavigationCacheConnectivityReadResult read =
            Engine::NavigationCache::readConnectivityCache(settings, manifest, connectivity.coord);
        ctx.expect(write.status == Engine::NavigationCacheOperationStatus::WriteSuccess,
            "connectivity cache write failed");
        ctx.expect(read.status == Engine::NavigationCacheOperationStatus::Stale,
            "connectivity identity mismatch did not return Stale");
        ctx.expect(!read.connectivity.has_value(), "stale connectivity returned payload");
    }

    void navigationCacheGraphIdentityMismatchIsStale(TestContext& ctx)
    {
        const Engine::NavigationCacheSettings settings = testCacheSettings("graph_stale");
        const Engine::NavigationCacheManifest manifest = testCacheManifest("identity_f");
        Engine::WorldNavigationGraphCacheData graph;
        graph.centerChunk = {-8, 9};
        graph.hasGraph = true;
        graph.nodes.push_back({graph.centerChunk, "test", {0.0f, 0.0f, 0.0f}, 1.0f});
        const Engine::NavigationCacheWriteResult write =
            Engine::NavigationCache::writeGraphCache(settings, manifest, graph);
        {
            std::ofstream file(write.path, std::ios::trunc);
            file << "identity_hash: stale_identity\ngraph:\n  center: {x: -8, z: 9}\n  has_graph: true\n";
        }
        const Engine::NavigationCacheGraphReadResult read =
            Engine::NavigationCache::readGraphCache(settings, manifest, graph.centerChunk);
        ctx.expect(write.status == Engine::NavigationCacheOperationStatus::WriteSuccess,
            "graph cache write failed");
        ctx.expect(read.status == Engine::NavigationCacheOperationStatus::Stale,
            "graph identity mismatch did not return Stale");
        ctx.expect(!read.graph.has_value(), "stale graph returned payload");
    }

    void worldNavigationGraphWorkerBuildMatchesRebuild(TestContext& ctx)
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

        const Engine::WorldNavigationGraphBuildInput input =
            Engine::WorldNavigationGraph::buildInput({0, 0}, graph.settings(), terrain, connectivity);
        const Engine::WorldNavigationGraphBuildResult result = Engine::WorldNavigationGraph::buildCacheData(input);
        ctx.expect(result.success, "worker graph build did not report success: " + result.message);
        ctx.expect(result.graph.nodes.size() == graph.cacheData().nodes.size(), "worker graph node count differed from synchronous rebuild");
        ctx.expect(result.graph.edges.size() == graph.cacheData().edges.size(), "worker graph edge count differed from synchronous rebuild");

        Engine::WorldNavigationGraph workerGraph({2, ChunkSize});
        workerGraph.loadCacheData(result.graph);
        const Engine::WorldNavRoute route = workerGraph.findRoute({2.0f, 0.0f, 8.0f}, {24.0f, 0.0f, 8.0f});
        ctx.expect(route.status == Engine::WorldNavRouteStatus::Success, "worker graph route failed: " + route.message);
    }

    void worldNavigationGraphWorkerBuildRadiusZero(TestContext& ctx)
    {
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationConnectivitySystem connectivity;
        Engine::WorldNavigationGraph graph({0, ChunkSize});
        const Engine::WorldNavigationGraphBuildInput input =
            Engine::WorldNavigationGraph::buildInput({-3, 4}, graph.settings(), terrain, connectivity);
        const Engine::WorldNavigationGraphBuildResult result = Engine::WorldNavigationGraph::buildCacheData(input);
        ctx.expect(result.success, "radius-zero graph build failed: " + result.message);
        ctx.expect(result.graph.nodes.size() == 1, "radius-zero graph did not produce exactly one node");
        ctx.expect(result.graph.edges.empty(), "radius-zero graph produced edges");

        Engine::WorldNavigationGraph loaded({0, ChunkSize});
        loaded.loadCacheData(result.graph);
        const Engine::WorldNavigationGraphStats stats = loaded.stats();
        ctx.expect(stats.hasGraph, "radius-zero loaded graph was not marked built");
        ctx.expect(stats.centerChunk == Engine::ChunkCoord{-3, 4}, "radius-zero graph center did not round-trip");
    }

    void navigationCpuProfileReport(TestContext& ctx)
    {
        ProfileReport profile;
        Engine::TerrainSystem terrain = makeTerrain();
        Engine::NavigationSystem navigation;
        Engine::NavAgentSettings agent;
        std::vector<Engine::ChunkCoord> loadedChunks;
        std::vector<Engine::TerrainTileHandle> loadedTerrainTiles;
        loadedChunks.reserve(25);
        loadedTerrainTiles.reserve(25);

        for (int32_t z = -2; z <= 2; ++z) {
            for (int32_t x = -2; x <= 2; ++x) {
                const Engine::ChunkCoord coord{x, z};
                const Engine::TerrainTileHandle tile = profile.measure("terrain cpu tile creation", [&]() {
                    return terrain.createTileFromHeights(coord, heights(0.0f));
                });
                ctx.expect(tile.id != UINT32_MAX, "profile failed to create terrain tile");
                if (tile.id == UINT32_MAX) {
                    continue;
                }

                std::optional<Engine::NavigationTerrainBuildData> buildData =
                    profile.measure("navigation build data extraction", [&]() {
                        return terrain.navigationBuildData(tile);
                    });
                ctx.expect(buildData.has_value(), "profile failed to extract navigation build data");
                if (!buildData) {
                    continue;
                }

                if ((x + z) % 3 == 0) {
                    const float minX = static_cast<float>(x) * ChunkSize + 6.0f;
                    const float minZ = static_cast<float>(z) * ChunkSize + 6.0f;
                    profile.measure("blocker geometry append", [&]() {
                        appendAabbBlocker(*buildData, {{minX, -0.5f, minZ}, {minX + 2.0f, 2.0f, minZ + 2.0f}});
                    });
                }

                const Engine::NavigationTileHandle navTile = profile.measure("recast terrain tile build", [&]() {
                    return navigation.buildTerrainTile(*buildData, agent);
                });
                ctx.expect(navTile.id != UINT32_MAX, "profile failed to build navigation tile");
                if (navTile.id != UINT32_MAX) {
                    loadedChunks.push_back(coord);
                    loadedTerrainTiles.push_back(tile);
                }
            }
        }

        std::vector<Engine::NavigationTileCacheData> cachedTiles;
        cachedTiles.reserve(loadedChunks.size());
        for (Engine::ChunkCoord coord : loadedChunks) {
            std::optional<Engine::NavigationTileCacheData> tile = profile.measure("detour tile cache export", [&]() {
                return navigation.tileCacheData(coord);
            });
            ctx.expect(tile.has_value(), "profile failed to export Detour tile cache data");
            if (tile) {
                cachedTiles.push_back(*tile);
            }
        }

        Engine::NavigationSystem cachedNavigation;
        for (const Engine::NavigationTileCacheData& tile : cachedTiles) {
            const Engine::NavigationTileHandle handle = profile.measure("detour tile cache insert", [&]() {
                return cachedNavigation.loadTerrainTileFromCache(tile);
            });
            ctx.expect(handle.id != UINT32_MAX, "profile failed to insert cached Detour tile data");
        }

        Engine::NavigationConnectivitySystem connectivity;
        profile.measure("connectivity rebuild", [&]() {
            connectivity.rebuild(loadedChunks, navigation, terrain, agent);
        });
        ctx.expect(connectivity.stats().chunkCount > 0, "profile connectivity rebuild produced no chunks");
        const std::vector<Engine::ChunkCoord> dirtyConnectivityChunks{{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        profile.measure("connectivity incremental rebuild", [&]() {
            connectivity.rebuildChunks(dirtyConnectivityChunks, navigation, terrain, agent);
        });

        Engine::WorldNavigationGraph graph({4, ChunkSize});
        profile.measure("world graph rebuild", [&]() {
            graph.rebuild({0, 0}, terrain, connectivity);
        });
        ctx.expect(graph.stats().hasGraph, "profile world graph rebuild produced no graph");

        for (uint32_t index = 0; index < 16; ++index) {
            const float offset = 2.0f + static_cast<float>(index % 4) * 4.0f;
            const glm::vec3 point{-ChunkSize + offset, 0.0f, -ChunkSize + offset};
            const Engine::NavQueryResult nearest = profile.measure("nearest nav query", [&]() {
                return navigation.nearestNavigablePoint(point, agent);
            });
            ctx.expect(nearest.status == Engine::NavQueryStatus::Success, "profile nearest nav query failed");
        }

        for (uint32_t index = 0; index < 8; ++index) {
            const glm::vec3 start{-30.0f + static_cast<float>(index), 0.0f, -30.0f};
            const glm::vec3 end{30.0f, 0.0f, 30.0f - static_cast<float>(index)};
            const Engine::NavQueryResult path = profile.measure("detour path query", [&]() {
                return navigation.findPath(start, end, agent);
            });
            ctx.expect(path.status == Engine::NavQueryStatus::Success, "profile detour path query failed");
        }

        const Engine::WorldNavRoute route = profile.measure("coarse graph route query", [&]() {
            return graph.findRoute({-30.0f, 0.0f, -30.0f}, {30.0f, 0.0f, 30.0f});
        });
        ctx.expect(route.status == Engine::WorldNavRouteStatus::Success, "profile coarse graph route failed");

        Engine::World world;
        Engine::ActorController actors;
        Engine::ActorControllerSettings actorSettings;
        actorSettings.collisionEnabled = false;
        actorSettings.groundOffset = 0.0f;
        const Engine::ActorHandle actor = createActorAt(actors, world, terrain, {-30.0f, 0.0f, -30.0f}, actorSettings);
        profile.measure("actor route command", [&]() {
            actors.setRouteDestination(actor, {30.0f, 0.0f, 30.0f}, graph, navigation, agent, world);
        });
        profile.measure("actor fixed updates", [&]() {
            (void)simulate(actors, actor, terrain, world, {30.0f, 0.0f, 30.0f}, 180, nullptr, nullptr, &navigation, &agent);
        });

        Engine::NavigationSystem destroyNavigation;
        for (const Engine::NavigationTileCacheData& tile : cachedTiles) {
            const Engine::NavigationTileHandle handle = destroyNavigation.loadTerrainTileFromCache(tile);
            ctx.expect(handle.id != UINT32_MAX, "profile failed to seed nav tile destroy benchmark");
        }
        for (Engine::ChunkCoord coord : loadedChunks) {
            profile.measure("detour tile destroy", [&]() {
                destroyNavigation.destroyTile(coord);
            });
        }

        Engine::NavigationSystem clearNavigation;
        for (const Engine::NavigationTileCacheData& tile : cachedTiles) {
            const Engine::NavigationTileHandle handle = clearNavigation.loadTerrainTileFromCache(tile);
            ctx.expect(handle.id != UINT32_MAX, "profile failed to seed nav clear benchmark");
        }
        profile.measure("detour nav system clear", [&]() {
            clearNavigation.clear();
        });

        for (Engine::ChunkCoord coord : dirtyConnectivityChunks) {
            profile.measure("connectivity remove chunk", [&]() {
                connectivity.removeChunk(coord);
            });
        }
        profile.measure("world graph clear", [&]() {
            graph.clear();
        });

        Engine::World destroyWorld;
        Engine::SpatialRegistry destroySpatial;
        std::vector<Engine::WorldObjectHandle> destroyObjects;
        destroyObjects.reserve(256);
        for (uint32_t index = 0; index < 256; ++index) {
            Engine::WorldObjectHandle object = destroyWorld.createObject(
                Engine::ObjectId::fromString("profile/object/" + std::to_string(index)));
            const glm::vec3 position{
                static_cast<float>(index % 16),
                0.0f,
                static_cast<float>(index / 16),
            };
            destroyWorld.setPosition(object, position);
            destroyWorld.setLocalBounds(object, {{-0.5f, 0.0f, -0.5f}, {0.5f, 2.0f, 0.5f}});
            destroyWorld.attachRendererInstance(object, Renderer::MeshInstanceHandle{index});
            destroySpatial.insert(object, position);
            destroyObjects.push_back(object);
        }
        for (Engine::WorldObjectHandle object : destroyObjects) {
            profile.measure("world object destroy", [&]() {
                destroySpatial.remove(object);
                destroyWorld.destroyObjectAndRendererInstance(object);
            });
        }

        Engine::ChunkStreamer detachStreamer{{ChunkSize, 2}};
        for (size_t index = 0; index < loadedChunks.size(); ++index) {
            Engine::ChunkContent content;
            content.terrain = Engine::TerrainTileHandle{static_cast<uint32_t>(index)};
            content.renderGroup = Renderer::RenderGroupHandle{static_cast<uint32_t>(index)};
            detachStreamer.registerLoadedChunk(loadedChunks[index], content);
        }
        for (Engine::ChunkCoord coord : loadedChunks) {
            profile.measure("chunk detach bookkeeping", [&]() {
                (void)detachStreamer.detachLoadedChunk(coord);
            });
        }

        for (Engine::TerrainTileHandle tile : loadedTerrainTiles) {
            profile.measure("terrain cpu tile destroy", [&]() {
                terrain.destroyTile(tile);
            });
        }

        Engine::TerrainSettings fullTerrainSettings;
        fullTerrainSettings.chunkSize = ChunkSize;
        fullTerrainSettings.resolution = 33;
        fullTerrainSettings.navigationResolution = 17;
        fullTerrainSettings.createRendererResources = false;
        Engine::TerrainSystem fullTerrain(fullTerrainSettings);
        std::vector<float> fullHeights(static_cast<size_t>(33) * 33, 0.0f);
        const Engine::TerrainTileHandle fullTile = fullTerrain.createTileFromHeights({10, 10}, fullHeights);
        const std::optional<Engine::NavigationTerrainBuildData> fullBuildData =
            profile.measure("full 33 nav build data extraction", [&]() {
                return fullTerrain.navigationBuildData(fullTile, 33);
            });
        const std::optional<Engine::NavigationTerrainBuildData> reducedBuildData =
            profile.measure("reduced 17 nav build data extraction", [&]() {
                return fullTerrain.navigationBuildData(fullTile, 17);
            });
        ctx.expect(fullBuildData.has_value() && reducedBuildData.has_value(),
            "profile failed to extract full/reduced nav build data");
        if (fullBuildData && reducedBuildData) {
            Engine::NavigationSystem fullSourceNavigation;
            const Engine::NavigationTileBuildResult fullResult = profile.measure("recast full 33 source tile build", [&]() {
                return Engine::NavigationSystem::buildTerrainTileData(*fullBuildData, agent, fullSourceNavigation.settings());
            });
            Engine::NavigationSystem reducedSourceNavigation;
            const Engine::NavigationTileBuildResult reducedResult = profile.measure("recast reduced 17 source tile build", [&]() {
                return Engine::NavigationSystem::buildTerrainTileData(*reducedBuildData, agent, reducedSourceNavigation.settings());
            });
            ctx.expect(fullResult.status == Engine::NavQueryStatus::Success,
                "profile full-source Recast build failed");
            ctx.expect(reducedResult.status == Engine::NavQueryStatus::Success,
                "profile reduced-source Recast build failed");
            ctx.expect(reducedBuildData->vertices.size() < fullBuildData->vertices.size(),
                "profile reduced source did not have fewer vertices");
        }

        const std::string report = profile.toString();
        writeProfileReport("navigation_cpu_profile.txt", profile);
        std::cout << report;

        const std::vector<ProfileSample> ranked = profile.rankedByTotal();
        ctx.expect(!ranked.empty(), "profile did not record any samples");
        if (!ranked.empty()) {
            ctx.expect(ranked.front().totalMs >= ranked.back().totalMs, "profile ranking was not sorted by total time");
        }
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
        {"PhasedConnectivityMatchesSynchronousRebuild", phasedConnectivityMatchesSynchronousRebuild},
        {"PhasedConnectivityMaxSamplesSplitsWork", phasedConnectivityMaxSamplesSplitsWork},
        {"PhasedConnectivityCancelStopsMutation", phasedConnectivityCancelStopsMutation},
        {"PhasedConnectivityClearExistingRemovesStaleChunks", phasedConnectivityClearExistingRemovesStaleChunks},
        {"ActorCommandDiagnosticsAfterRouteFallback", actorCommandDiagnosticsAfterRouteFallback},
        {"TerrainGenerationIsDeterministicAndContinuous", terrainGenerationIsDeterministicAndContinuous},
        {"TerrainDiagnosticsReportRampSlope", terrainDiagnosticsReportRampSlope},
        {"LowerTerrainProfileReducesSlope", lowerTerrainProfileReducesSlope},
        {"NavigationResolutionReducesBuildGeometry", navigationResolutionReducesBuildGeometry},
        {"TerrainRenderMeshBuildDataIsDeterministic", terrainRenderMeshBuildDataIsDeterministic},
        {"TerrainRenderMeshStaleGenerationRejected", terrainRenderMeshStaleGenerationRejected},
        {"TerrainRenderMeshBuildDoesNotChangeCpuHeight", terrainRenderMeshBuildDoesNotChangeCpuHeight},
        {"ChunkStreamerVisitsLoadedChunkByCoord", chunkStreamerVisitsLoadedChunkByCoord},
        {"FrameBudgetDefersNormalWorkWhenExhausted", frameBudgetDefersNormalWorkWhenExhausted},
        {"FrameBudgetRunsCriticalWorkWithOverrun", frameBudgetRunsCriticalWorkWithOverrun},
        {"MainThreadWorkQueuePreservesDeferredWork", mainThreadWorkQueuePreservesDeferredWork},
        {"NavigationCacheMissingTileReturnsMiss", navigationCacheMissingTileReturnsMiss},
        {"NavigationCacheTileRoundTrips", navigationCacheTileRoundTrips},
        {"NavigationCacheCorruptTileIsSafe", navigationCacheCorruptTileIsSafe},
        {"NavigationCacheConnectivityIdentityMismatchIsStale", navigationCacheConnectivityIdentityMismatchIsStale},
        {"NavigationCacheGraphIdentityMismatchIsStale", navigationCacheGraphIdentityMismatchIsStale},
        {"WorldNavigationGraphWorkerBuildMatchesRebuild", worldNavigationGraphWorkerBuildMatchesRebuild},
        {"WorldNavigationGraphWorkerBuildRadiusZero", worldNavigationGraphWorkerBuildRadiusZero},
        {"NavigationCpuProfileReport", navigationCpuProfileReport},
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
