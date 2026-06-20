#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Engine/Physics/ScenePhysics.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/TerrainDataset.hpp"
#include "Engine/TerrainPhysicsColliderAdapter.hpp"

namespace {
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

    bool near(float lhs, float rhs, float epsilon = 0.001f)
    {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    bool sameVec3(const glm::vec3& lhs, const glm::vec3& rhs)
    {
        return near(lhs.x, rhs.x) && near(lhs.y, rhs.y) && near(lhs.z, rhs.z);
    }

    Engine::TerrainPhysicsSourceIdentity identity(uint64_t id = 1201)
    {
        Engine::TerrainPhysicsSourceIdentity result;
        result.sourceId = {id};
        result.sourceHash = "terrain_physics_source_hash";
        result.importSettings = {"terrain_physics", "1", "test"};
        result.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        return result;
    }

    Engine::TerrainSourceDescriptor importedSourceDescriptor(uint64_t id = 1201)
    {
        Engine::TerrainSourceDescriptor descriptor;
        descriptor.sourceId = {id};
        descriptor.type = Engine::TerrainDatasetSourceType::HeightmapImported;
        descriptor.defaultChunkSize = 4.0f;
        descriptor.defaultResolution = 3;
        descriptor.settings = {"terrain_physics", "1", "test"};
        descriptor.debugName = "terrain.physics.imported";
        return descriptor;
    }

    Engine::TerrainSourceDescriptor proceduralSourceDescriptor(uint64_t id = 1202)
    {
        Engine::TerrainSourceDescriptor descriptor;
        descriptor.sourceId = {id};
        descriptor.type = Engine::TerrainDatasetSourceType::Procedural;
        descriptor.defaultChunkSize = 4.0f;
        descriptor.defaultResolution = 5;
        descriptor.settings = {"procedural_terrain", "1", "test"};
        descriptor.debugName = "terrain.physics.procedural";
        descriptor.procedural.chunkSize = 4.0f;
        descriptor.procedural.resolution = 5;
        descriptor.procedural.heightScale = 1.0f;
        return descriptor;
    }

    Engine::TerrainImportedChunk importedChunk()
    {
        Engine::TerrainImportedChunk chunk;
        chunk.id = {{1201}, {0, 0}};
        chunk.coord = {0, 0};
        chunk.origin = {0.0f, 0.0f, 0.0f};
        chunk.size = 4.0f;
        chunk.resolution = 3;
        chunk.heights = {
            0.0f, 1.0f, 2.0f,
            1.0f, 2.0f, 3.0f,
            2.0f, 3.0f, 4.0f,
        };
        return chunk;
    }

    std::optional<Engine::TerrainPhysicsColliderBuildRequest> importedRequest(uint32_t colliderResolution)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());
        return Engine::terrainPhysicsColliderRequestFromDatasetChunk(dataset, chunk, colliderResolution, identity());
    }

    Engine::TerrainPhysicsColliderPayload flatPayload(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        Engine::TerrainImportedChunk flat = importedChunk();
        flat.heights.assign(9, 0.0f);
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, flat);
        const auto request = Engine::terrainPhysicsColliderRequestFromDatasetChunk(dataset, chunk, 5, identity());
        ctx.expect(request.has_value(), "flat collider request was not built");
        if (!request) {
            return {};
        }
        const Engine::TerrainPhysicsColliderBuildResult result = Engine::buildTerrainPhysicsCollider(*request);
        ctx.expect(result.success && result.payload.has_value(), "flat collider payload was not built");
        return result.payload.value_or(Engine::TerrainPhysicsColliderPayload{});
    }

    void datasetImportedChunkBuildsDeterministicCollider(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());
        const auto request = Engine::terrainPhysicsColliderRequestFromDatasetChunk(dataset, chunk, 5, identity());
        ctx.expect(request.has_value(), "dataset imported request was not built");
        if (!request) {
            return;
        }

        const Engine::TerrainPhysicsColliderBuildResult first = Engine::buildTerrainPhysicsCollider(*request);
        const Engine::TerrainPhysicsColliderBuildResult second = Engine::buildTerrainPhysicsCollider(*request);
        ctx.expect(first.success && second.success && first.payload && second.payload, "dataset imported collider build failed");
        if (first.payload && second.payload) {
            ctx.expect(first.payload->vertices == second.payload->vertices, "collider vertices were not deterministic");
            ctx.expect(first.payload->indices == second.payload->indices, "collider indices were not deterministic");
            ctx.expect(first.payload->vertices.size() == 25, "collider vertex count was wrong");
            ctx.expect(first.payload->indices.size() == 96, "collider index count was wrong");
            ctx.expect(sameVec3(first.payload->vertices[12], {2.0f, 2.0f, 2.0f}), "center collider vertex was wrong");
            ctx.expect(near(first.payload->bounds.min.y, 0.0f) && near(first.payload->bounds.max.y, 4.0f), "collider bounds were wrong");
        }
    }

    void datasetProceduralChunkUsesSameAdapterPath(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(proceduralSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadProceduralChunk(source, {0, 0});
        Engine::TerrainPhysicsSourceIdentity sourceIdentity = identity(1202);
        sourceIdentity.sourceType = Engine::TerrainDatasetSourceType::Procedural;
        sourceIdentity.importSettings = {"procedural_terrain", "1", "test"};
        const auto request = Engine::terrainPhysicsColliderRequestFromDatasetChunk(dataset, chunk, 5, sourceIdentity);
        ctx.expect(request.has_value(), "dataset procedural request was not built");
        if (!request) {
            return;
        }

        const Engine::TerrainPhysicsColliderBuildResult result = Engine::buildTerrainPhysicsCollider(*request);
        ctx.expect(result.success && result.payload.has_value(), "dataset procedural collider build failed");
        if (result.payload) {
            ctx.expect(!result.payload->vertices.empty() && !result.payload->indices.empty(), "dataset procedural build produced no geometry");
        }
    }

    void colliderResolutionControlsGeometry(TestContext& ctx)
    {
        const auto fullRequest = importedRequest(5);
        const auto reducedRequest = importedRequest(3);
        ctx.expect(fullRequest.has_value() && reducedRequest.has_value(), "resolution requests were not built");
        if (!fullRequest || !reducedRequest) {
            return;
        }

        const Engine::TerrainPhysicsColliderBuildResult full = Engine::buildTerrainPhysicsCollider(*fullRequest);
        const Engine::TerrainPhysicsColliderBuildResult reduced = Engine::buildTerrainPhysicsCollider(*reducedRequest);
        ctx.expect(full.success && reduced.success && full.payload && reduced.payload, "resolution builds failed");
        if (full.payload && reduced.payload) {
            ctx.expect(full.payload->vertices.size() == 25, "full collider resolution vertex count was wrong");
            ctx.expect(reduced.payload->vertices.size() == 9, "reduced collider resolution vertex count was wrong");
            ctx.expect(reduced.payload->indices.size() < full.payload->indices.size(), "reduced collider resolution did not reduce indices");
        }
    }

    void invalidInputsFailCleanly(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const auto staleRequest = Engine::terrainPhysicsColliderRequestFromDatasetChunk(dataset, {}, 5, identity());
        ctx.expect(!staleRequest.has_value(), "default dataset chunk unexpectedly built request");

        Engine::TerrainDataset malformedDataset;
        const Engine::TerrainSourceHandle malformedSource = malformedDataset.registerSource(importedSourceDescriptor());
        Engine::TerrainImportedChunk malformed = importedChunk();
        malformed.heights.pop_back();
        ctx.expect(!Engine::isValid(malformedDataset.loadImportedChunk(malformedSource, malformed)),
            "malformed imported chunk unexpectedly loaded");

        Engine::TerrainDataset nonFiniteDataset;
        const Engine::TerrainSourceHandle nonFiniteSource = nonFiniteDataset.registerSource(importedSourceDescriptor());
        Engine::TerrainImportedChunk nonFinite = importedChunk();
        nonFinite.heights[4] = std::numeric_limits<float>::quiet_NaN();
        const Engine::TerrainChunkHandle nonFiniteChunk = nonFiniteDataset.loadImportedChunk(nonFiniteSource, nonFinite);
        ctx.expect(!Engine::terrainPhysicsColliderRequestFromDatasetChunk(nonFiniteDataset, nonFiniteChunk, 5, identity()).has_value(),
            "non-finite imported chunk unexpectedly built request");

        Engine::TerrainPhysicsColliderBuildRequest request;
        request.chunkId = {{1201}, {0, 0}};
        request.coord = {0, 0};
        request.origin = {0.0f, 0.0f, 0.0f};
        request.size = -1.0f;
        request.sourceResolution = 3;
        request.colliderResolution = 5;
        request.heights = importedChunk().heights;
        const Engine::TerrainPhysicsColliderBuildResult result = Engine::buildTerrainPhysicsCollider(request);
        ctx.expect(!result.success && !result.payload.has_value(), "invalid direct request unexpectedly succeeded");
    }

    void createStaticColliderAndQueriesHitTerrain(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::TerrainPhysicsColliderAdapter adapter;
        const Engine::TerrainPhysicsColliderPayload payload = flatPayload(ctx);
        if (payload.vertices.empty()) {
            return;
        }

        Engine::TerrainPhysicsColliderCreateDescriptor descriptor;
        descriptor.debugName = "flat-test-collider";
        const Engine::TerrainPhysicsColliderHandle handle =
            adapter.createStaticCollider(scene, physics, payload, descriptor);
        ctx.expect(Engine::isValid(handle), "terrain physics collider handle was invalid");
        const std::optional<Engine::TerrainPhysicsColliderBinding> binding = adapter.binding(handle);
        ctx.expect(binding.has_value(), "terrain physics binding was missing");
        if (!binding) {
            return;
        }
        ctx.expect(scene.contains(binding->actor), "adapter-created actor was not live");
        ctx.expect(physics.contains(binding->body), "adapter-created physics body was not live");
        ctx.expect(physics.contains(binding->collider), "adapter-created physics collider was not live");

        const Engine::ScenePhysicsRaycastResult ray = physics.raycast({2.0f, 4.0f, 2.0f}, {2.0f, -2.0f, 2.0f});
        ctx.expect(ray.status == Engine::ScenePhysicsQueryStatus::Success, "raycast did not hit terrain collider");
        if (ray.hit) {
            ctx.expect(ray.hit->actor == binding->actor, "raycast hit wrong terrain actor");
        }

        const Engine::ScenePhysicsShapeDescriptor overlapShape{
            Engine::ScenePhysicsShapeType::Box,
            {{2.0f, 0.25f, 2.0f}},
        };
        const Engine::ScenePhysicsOverlapResult overlap = physics.overlap(overlapShape, {2.0f, 0.0f, 2.0f});
        ctx.expect(overlap.status == Engine::ScenePhysicsQueryStatus::Success, "overlap did not hit terrain collider");

        const Engine::ScenePhysicsSweepResult sweep =
            physics.sweepCapsule({0.25f, 0.5f}, {2.0f, 2.0f, 2.0f}, {2.0f, -2.0f, 2.0f});
        ctx.expect(sweep.status == Engine::ScenePhysicsQueryStatus::Success, "capsule sweep did not hit terrain collider");
    }

    void destroyAndReleaseAreIdempotent(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::TerrainPhysicsColliderAdapter adapter;
        const Engine::TerrainPhysicsColliderPayload payload = flatPayload(ctx);
        if (payload.vertices.empty()) {
            return;
        }
        const Engine::TerrainPhysicsColliderHandle handle = adapter.createStaticCollider(scene, physics, payload);
        const std::optional<Engine::TerrainPhysicsColliderBinding> binding = adapter.binding(handle);
        ctx.expect(binding.has_value(), "binding was missing before destroy");
        if (!binding) {
            return;
        }

        ctx.expect(adapter.destroyCollider(scene, physics, handle), "destroy terrain collider failed");
        ctx.expect(!adapter.contains(handle), "destroyed terrain collider still validates");
        ctx.expect(!physics.contains(binding->body), "destroyed terrain body still validates");
        ctx.expect(!scene.contains(binding->actor), "destroyed terrain actor still validates");
        ctx.expect(!adapter.destroyCollider(scene, physics, handle), "stale terrain collider destroy should fail cleanly");
        adapter.releaseAll(scene, physics);
        adapter.releaseAll(scene, physics);
    }

    void dirtyChunksAreChunkScoped(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ScenePhysicsWorld physics(scene);
        Engine::TerrainPhysicsColliderAdapter adapter;
        const Engine::TerrainPhysicsColliderPayload payload = flatPayload(ctx);
        if (payload.vertices.empty()) {
            return;
        }

        const Engine::TerrainPhysicsColliderHandle first = adapter.createStaticCollider(scene, physics, payload);
        ctx.expect(Engine::isValid(first), "first terrain collider was invalid");
        ctx.expect(adapter.dirtyChunks().size() == 1, "create did not mark exactly one dirty chunk");
        adapter.clearDirty();
        adapter.markDirty(payload.chunkId, Engine::TerrainPhysicsColliderDirtyReason::Settings);
        std::vector<Engine::TerrainPhysicsColliderDirtyChunk> dirty = adapter.dirtyChunks();
        ctx.expect(dirty.size() == 1 &&
                (dirty.front().reasons & static_cast<uint32_t>(Engine::TerrainPhysicsColliderDirtyReason::Settings)) != 0,
            "settings dirty reason was not recorded");

        adapter.clearDirty();
        const Engine::TerrainPhysicsColliderHandle second = adapter.createStaticCollider(scene, physics, payload);
        ctx.expect(Engine::isValid(second), "replacement terrain collider was invalid");
        ctx.expect(!adapter.contains(first), "replaced terrain collider handle still validates");
        dirty = adapter.dirtyChunks();
        ctx.expect(dirty.size() == 1 &&
                (dirty.front().reasons & static_cast<uint32_t>(Engine::TerrainPhysicsColliderDirtyReason::Geometry)) != 0,
            "replacement did not mark geometry dirty");

        adapter.clearDirty();
        ctx.expect(adapter.destroyColliderForChunk(scene, physics, payload.chunkId), "destroy by chunk failed");
        dirty = adapter.dirtyChunks();
        ctx.expect(dirty.size() == 1 &&
                (dirty.front().reasons & static_cast<uint32_t>(Engine::TerrainPhysicsColliderDirtyReason::Removal)) != 0,
            "destroy by chunk did not mark removal dirty");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"DatasetImportedChunkBuildsDeterministicCollider", datasetImportedChunkBuildsDeterministicCollider},
        {"DatasetProceduralChunkUsesSameAdapterPath", datasetProceduralChunkUsesSameAdapterPath},
        {"ColliderResolutionControlsGeometry", colliderResolutionControlsGeometry},
        {"InvalidInputsFailCleanly", invalidInputsFailCleanly},
        {"CreateStaticColliderAndQueriesHitTerrain", createStaticColliderAndQueriesHitTerrain},
        {"DestroyAndReleaseAreIdempotent", destroyAndReleaseAreIdempotent},
        {"DirtyChunksAreChunkScoped", dirtyChunksAreChunkScoped},
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

    std::cout << "Terrain physics collider adapter tests passed (" << tests.size() << " tests)\n";
    return 0;
}
