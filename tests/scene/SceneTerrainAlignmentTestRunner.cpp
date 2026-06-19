#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Engine/Navigation.hpp"
#include "Engine/NavigationRuntime.hpp"
#include "Engine/Physics/ScenePhysics.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/TerrainDataset.hpp"
#include "Engine/TerrainNavigationAdapter.hpp"
#include "Engine/TerrainPhysicsColliderAdapter.hpp"
#include "Engine/TerrainSerializationPrep.hpp"

namespace {
    constexpr Engine::AssetId TestTerrainSourceId{0x1050};
    constexpr Engine::TerrainSourceChunkCoord TestChunkCoord{0, 0};
    constexpr float TestChunkSize = 16.0f;
    constexpr uint32_t TestResolution = 5;

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

    Engine::TerrainSourceDescriptor sourceDescriptor()
    {
        Engine::TerrainSourceDescriptor descriptor;
        descriptor.sourceId = TestTerrainSourceId;
        descriptor.type = Engine::TerrainDatasetSourceType::HeightmapImported;
        descriptor.defaultChunkSize = TestChunkSize;
        descriptor.defaultResolution = TestResolution;
        descriptor.settings = {"heightmap_terrain", "alignment", "flat"};
        descriptor.debugName = "scene.alignment.heightmap";
        descriptor.bounds = {{0.0f, 0.0f, 0.0f}, {TestChunkSize, 0.0f, TestChunkSize}};
        return descriptor;
    }

    Engine::TerrainImportedChunk flatImportedChunk()
    {
        Engine::TerrainImportedChunk chunk;
        chunk.id = {TestTerrainSourceId, TestChunkCoord};
        chunk.coord = TestChunkCoord;
        chunk.origin = {0.0f, 0.0f, 0.0f};
        chunk.size = TestChunkSize;
        chunk.resolution = TestResolution;
        chunk.heights.assign(static_cast<size_t>(TestResolution) * TestResolution, 0.0f);
        return chunk;
    }

    struct DatasetFixture {
        Engine::TerrainDataset dataset;
        Engine::TerrainSourceHandle source;
        Engine::TerrainChunkHandle chunk;
    };

    DatasetFixture makeDataset(TestContext& ctx)
    {
        DatasetFixture fixture;
        fixture.source = fixture.dataset.registerSource(sourceDescriptor());
        fixture.chunk = fixture.dataset.loadImportedChunk(fixture.source, flatImportedChunk());
        ctx.expect(Engine::isValid(fixture.source), "terrain source handle was invalid");
        ctx.expect(Engine::isValid(fixture.chunk), "terrain chunk handle was invalid");
        return fixture;
    }

    Engine::TerrainNavigationSourceIdentity navigationIdentity()
    {
        Engine::TerrainNavigationSourceIdentity identity;
        identity.sourceId = TestTerrainSourceId;
        identity.sourceHash = "scene-alignment-flat-heightmap";
        identity.importSettings = {"heightmap_terrain", "alignment", "flat"};
        identity.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        return identity;
    }

    Engine::TerrainPhysicsSourceIdentity physicsIdentity()
    {
        Engine::TerrainPhysicsSourceIdentity identity;
        identity.sourceId = TestTerrainSourceId;
        identity.sourceHash = "scene-alignment-flat-heightmap";
        identity.importSettings = {"heightmap_terrain", "alignment", "flat"};
        identity.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        return identity;
    }

    void terrainNavigationAdapterFeedsSceneNavigationService(TestContext& ctx)
    {
        DatasetFixture fixture = makeDataset(ctx);
        const std::optional<Engine::TerrainNavigationBuildRequest> request =
            Engine::terrainNavigationRequestFromDatasetChunk(
                fixture.dataset,
                fixture.chunk,
                TestResolution,
                navigationIdentity());
        ctx.expect(request.has_value(), "terrain navigation request was not built");
        if (!request) {
            return;
        }

        const Engine::TerrainNavigationBuildResult build = Engine::buildTerrainNavigationData(*request);
        ctx.expect(build.success && build.buildData.has_value(), "terrain navigation build data failed");
        if (!build.buildData) {
            return;
        }

        Engine::NavigationSystem navigation;
        const Engine::NavigationTileHandle tile = navigation.buildTerrainTile(*build.buildData, {});
        ctx.expect(tile.id != UINT32_MAX, "navigation tile was not built: " + navigation.lastBuildMessage());
        if (tile.id == UINT32_MAX) {
            return;
        }

        Engine::SceneNavigationService service{navigation};
        const Engine::NavigationProjectionResult projection = service.projectPoint({8.0f, 0.25f, 8.0f});
        ctx.expect(
            projection.status == Engine::NavigationRuntimeStatus::Success,
            "scene navigation projection failed: " + projection.message);

        const Engine::NavigationPathResult path = service.findPath({2.0f, 0.0f, 2.0f}, {14.0f, 0.0f, 14.0f});
        ctx.expect(path.status == Engine::NavigationRuntimeStatus::Success, "scene navigation path failed: " + path.message);
        ctx.expect(path.path.complete, "scene navigation path was not complete");
        ctx.expect(path.path.points.size() >= 2, "scene navigation path had too few points");
    }

    void terrainPhysicsAdapterFeedsScenePhysicsWorld(TestContext& ctx)
    {
        DatasetFixture fixture = makeDataset(ctx);
        const std::optional<Engine::TerrainPhysicsColliderBuildRequest> request =
            Engine::terrainPhysicsColliderRequestFromDatasetChunk(
                fixture.dataset,
                fixture.chunk,
                TestResolution,
                physicsIdentity());
        ctx.expect(request.has_value(), "terrain physics request was not built");
        if (!request) {
            return;
        }

        const Engine::TerrainPhysicsColliderBuildResult build = Engine::buildTerrainPhysicsCollider(*request);
        ctx.expect(build.success && build.payload.has_value(), "terrain physics payload build failed");
        if (!build.payload) {
            return;
        }

        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::TerrainPhysicsColliderAdapter adapter;
        Engine::TerrainPhysicsColliderCreateDescriptor descriptor;
        descriptor.debugName = "scene-alignment-terrain-collider";
        const Engine::TerrainPhysicsColliderHandle collider =
            adapter.createStaticCollider(scene, physics, *build.payload, descriptor);
        ctx.expect(Engine::isValid(collider), "terrain physics collider handle was invalid");
        const std::optional<Engine::TerrainPhysicsColliderBinding> binding = adapter.binding(collider);
        ctx.expect(binding.has_value(), "terrain physics binding was missing");
        if (!binding) {
            return;
        }

        const Engine::ScenePhysicsRaycastResult ray = physics.raycast({8.0f, 5.0f, 8.0f}, {8.0f, -2.0f, 8.0f});
        ctx.expect(ray.status == Engine::ScenePhysicsQueryStatus::Success, "raycast did not hit terrain collider");
        if (ray.hit) {
            ctx.expect(ray.hit->actor == binding->actor, "raycast hit the wrong terrain actor");
        }

        const Engine::ScenePhysicsSweepResult sweep =
            physics.sweepCapsule({0.35f, 0.75f}, {8.0f, 3.0f, 8.0f}, {8.0f, -2.0f, 8.0f});
        ctx.expect(sweep.status == Engine::ScenePhysicsQueryStatus::Success, "capsule sweep did not hit terrain collider");
        if (sweep.hit) {
            ctx.expect(sweep.hit->actor == binding->actor, "capsule sweep hit the wrong terrain actor");
        }

        adapter.releaseAll(scene, physics);
    }

    void terrainSerializationIdentityUsesDurableChunkId(TestContext& ctx)
    {
        DatasetFixture fixture = makeDataset(ctx);
        const std::optional<Engine::TerrainChunkData> chunk = fixture.dataset.chunk(fixture.chunk);
        ctx.expect(chunk.has_value(), "terrain chunk was missing");
        if (!chunk) {
            return;
        }

        Engine::TerrainChunkStableIdentity identity;
        identity.chunkId = chunk->id;
        identity.sourceType = chunk->sourceType;
        identity.importSettings = sourceDescriptor().settings;
        identity.sourceRevision = "scene-alignment-flat-heightmap";
        identity.materialRevision = "none";
        identity.chunkResolution = chunk->resolution;
        identity.chunkSize = chunk->size;

        const Engine::TerrainSerializedChunkFileMetadata metadata =
            Engine::buildTerrainSerializedChunkFileMetadata(identity);
        const Engine::TerrainSerializationPrepValidation validation =
            Engine::validateTerrainSerializedChunkFileMetadata(metadata);

        ctx.expect(validation.valid, "terrain serialized chunk metadata did not validate");
        ctx.expect(metadata.identity.chunkId == chunk->id, "durable chunk ID was not preserved");
        ctx.expect(!metadata.boundary.storesLiveRuntimeHandles, "metadata boundary permits live runtime handles");
        ctx.expect(metadata.identityHash == Engine::terrainChunkStableIdentityHash(identity), "identity hash was not deterministic");
        ctx.expect(metadata.payloadFileName.find("terrain_chunk_") == 0, "payload file name was not deterministic");
    }

    void noImplicitTerrainSideEffects(TestContext& ctx)
    {
        DatasetFixture fixture = makeDataset(ctx);
        const std::optional<Engine::TerrainNavigationBuildRequest> navRequest =
            Engine::terrainNavigationRequestFromDatasetChunk(
                fixture.dataset,
                fixture.chunk,
                TestResolution,
                navigationIdentity());
        const std::optional<Engine::TerrainPhysicsColliderBuildRequest> physicsRequest =
            Engine::terrainPhysicsColliderRequestFromDatasetChunk(
                fixture.dataset,
                fixture.chunk,
                TestResolution,
                physicsIdentity());
        ctx.expect(navRequest.has_value(), "navigation request was not built");
        ctx.expect(physicsRequest.has_value(), "physics request was not built");

        Engine::NavigationSystem navigation;
        Engine::SceneNavigationService service{navigation};
        const Engine::NavigationProjectionResult missing = service.projectPoint({8.0f, 0.0f, 8.0f});
        ctx.expect(missing.status == Engine::NavigationRuntimeStatus::NoTile, "navigation request implicitly created a tile");

        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        ctx.expect(physics.diagnostics().bodyCount == 0, "terrain requests implicitly created physics bodies");

        if (physicsRequest) {
            const Engine::TerrainPhysicsColliderBuildResult build = Engine::buildTerrainPhysicsCollider(*physicsRequest);
            ctx.expect(build.success && build.payload.has_value(), "physics payload build failed");
            ctx.expect(physics.diagnostics().bodyCount == 0, "terrain payload build implicitly created physics bodies");
        }
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"TerrainNavigationAdapterFeedsSceneNavigationService", terrainNavigationAdapterFeedsSceneNavigationService},
        {"TerrainPhysicsAdapterFeedsScenePhysicsWorld", terrainPhysicsAdapterFeedsScenePhysicsWorld},
        {"TerrainSerializationIdentityUsesDurableChunkId", terrainSerializationIdentityUsesDurableChunkId},
        {"NoImplicitTerrainSideEffects", noImplicitTerrainSideEffects},
    };

    for (const auto& [name, test] : tests) {
        TestContext context{name, failures};
        test(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "Scene terrain alignment tests passed (" << tests.size() << ")\n";
    return 0;
}
