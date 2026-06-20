#include <cmath>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/NavigationRuntime.hpp"
#include "Engine/NavigationSceneGeometry.hpp"
#include "Engine/Scene/AuthoredSceneAdapter.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/SceneRenderBridge.hpp"

namespace TestRenderer {
    void reset();
}

namespace {
    constexpr float ChunkSize = 16.0f;

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
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool nearVec3(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 0.001f)
    {
        return near(lhs.x, rhs.x, epsilon) && near(lhs.y, rhs.y, epsilon) && near(lhs.z, rhs.z, epsilon);
    }

    Renderer::Aabb tileBounds()
    {
        return {{0.0f, -1.0f, 0.0f}, {ChunkSize, 2.0f, ChunkSize}};
    }

    Engine::SceneNavigationBuildRequest buildRequest()
    {
        Engine::SceneNavigationBuildRequest request;
        request.coord = {0, 0};
        request.bounds = tileBounds();
        return request;
    }

    Engine::SceneNavigationSourceDescriptor planeSource(
        std::optional<Engine::SceneActorHandle> actor = std::nullopt,
        Engine::SceneNavigationSourceRole role = Engine::SceneNavigationSourceRole::Walkable)
    {
        Engine::SceneNavigationSourceDescriptor descriptor;
        descriptor.actor = actor;
        descriptor.role = role;
        descriptor.debugName = role == Engine::SceneNavigationSourceRole::Walkable ? "test plane" : "test blocker";
        descriptor.vertices = {
            {0.0f, 0.0f, 0.0f},
            {ChunkSize, 0.0f, 0.0f},
            {0.0f, 0.0f, ChunkSize},
            {ChunkSize, 0.0f, ChunkSize},
        };
        descriptor.indices = {0, 2, 1, 1, 2, 3};
        descriptor.localBounds = {{descriptor.vertices[0], descriptor.vertices[3]}};
        return descriptor;
    }

    Engine::SceneNavigationSourceDescriptor blockerSource()
    {
        Engine::SceneNavigationSourceDescriptor descriptor;
        descriptor.role = Engine::SceneNavigationSourceRole::Blocker;
        descriptor.debugName = "test wall";
        descriptor.vertices = {
            {7.4f, -0.2f, 0.0f},
            {8.6f, -0.2f, 0.0f},
            {8.6f, -0.2f, ChunkSize},
            {7.4f, -0.2f, ChunkSize},
            {7.4f, 2.0f, 0.0f},
            {8.6f, 2.0f, 0.0f},
            {8.6f, 2.0f, ChunkSize},
            {7.4f, 2.0f, ChunkSize},
        };
        descriptor.indices = {
            0, 1, 2, 0, 2, 3,
            4, 6, 5, 4, 7, 6,
            0, 4, 5, 0, 5, 1,
            1, 5, 6, 1, 6, 2,
            2, 6, 7, 2, 7, 3,
            3, 7, 4, 3, 4, 0,
        };
        descriptor.localBounds = {{{7.4f, -0.2f, 0.0f}, {8.6f, 2.0f, ChunkSize}}};
        return descriptor;
    }

    Engine::SceneNavigationSourceDescriptor steepTriangleSource()
    {
        Engine::SceneNavigationSourceDescriptor descriptor;
        descriptor.role = Engine::SceneNavigationSourceRole::Walkable;
        descriptor.debugName = "steep triangle";
        descriptor.vertices = {
            {1.0f, 0.0f, 1.0f},
            {1.0f, 4.0f, 1.2f},
            {4.0f, 0.0f, 1.0f},
        };
        descriptor.indices = {0, 1, 2};
        descriptor.localBounds = {{{1.0f, 0.0f, 1.0f}, {4.0f, 4.0f, 1.2f}}};
        return descriptor;
    }

    Engine::SceneNavigationSourceDescriptor outsideTriangleSource()
    {
        Engine::SceneNavigationSourceDescriptor descriptor;
        descriptor.role = Engine::SceneNavigationSourceRole::Walkable;
        descriptor.debugName = "outside triangle";
        descriptor.vertices = {
            {40.0f, 0.0f, 40.0f},
            {42.0f, 0.0f, 40.0f},
            {40.0f, 0.0f, 42.0f},
        };
        descriptor.indices = {0, 2, 1};
        descriptor.localBounds = {{{0.0f, 0.0f, 0.0f}, {42.0f, 0.0f, 42.0f}}};
        return descriptor;
    }

    Engine::SceneNavigationSourceDescriptor paddingIntersectingTriangleSource()
    {
        Engine::SceneNavigationSourceDescriptor descriptor;
        descriptor.role = Engine::SceneNavigationSourceRole::Walkable;
        descriptor.debugName = "padding triangle";
        descriptor.vertices = {
            {ChunkSize + 0.2f, 0.0f, 1.0f},
            {ChunkSize + 1.0f, 0.0f, 1.0f},
            {ChunkSize + 0.2f, 0.0f, 2.0f},
        };
        descriptor.indices = {0, 2, 1};
        descriptor.localBounds = {{{ChunkSize + 0.2f, 0.0f, 1.0f}, {ChunkSize + 1.0f, 0.0f, 2.0f}}};
        return descriptor;
    }

    Engine::NavigationTerrainBuildData terrainBase()
    {
        Engine::NavigationTerrainBuildData data;
        data.coord = {0, 0};
        data.bounds = tileBounds();
        data.vertices = {
            {0.0f, 0.0f, 0.0f},
            {ChunkSize, 0.0f, 0.0f},
            {0.0f, 0.0f, ChunkSize},
            {ChunkSize, 0.0f, ChunkSize},
        };
        data.indices = {0, 2, 1, 1, 2, 3};
        return data;
    }

    void registerSourceCreatesDeterministicSnapshot(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneNavigationGeometryRegistry registry;
        const Engine::SceneNavigationSourceHandle source = registry.registerSource(planeSource());
        ctx.expect(Engine::isValid(source), "source handle was invalid");

        const std::optional<Engine::NavigationTerrainBuildData> first =
            registry.buildNavigationData(scene, buildRequest());
        const std::optional<Engine::NavigationTerrainBuildData> second =
            registry.buildNavigationData(scene, buildRequest());

        ctx.expect(first.has_value(), "first snapshot failed");
        ctx.expect(second.has_value(), "second snapshot failed");
        if (first && second) {
            ctx.expect(first->vertices.size() == 4, "snapshot vertex count was not deterministic");
            ctx.expect(first->indices == second->indices, "snapshot indices changed between builds");
            ctx.expect(nearVec3(first->vertices[3], {ChunkSize, 0.0f, ChunkSize}), "snapshot vertex position was wrong");
        }

        const Engine::SceneNavigationGeometryDiagnostics diagnostics = registry.diagnostics();
        ctx.expect(diagnostics.includedSourceCount == 1, "diagnostics did not count included source");
        ctx.expect(diagnostics.walkableTriangleCount == 2, "diagnostics did not count walkable triangles");
    }

    void sceneTransformAffectsGeometry(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle parent = scene.createActor();
        const Engine::SceneActorHandle child = scene.createActor();
        Engine::SceneTransform parentTransform;
        parentTransform.translation = {2.0f, 0.0f, 3.0f};
        scene.setLocalTransform(parent, parentTransform);

        Engine::SceneTransform childTransform;
        childTransform.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
        childTransform.scale = {2.0f, 1.0f, 1.0f};
        scene.setLocalTransform(child, childTransform);
        scene.attachChild(child, parent, false);

        Engine::SceneNavigationGeometryRegistry registry;
        [[maybe_unused]] const Engine::SceneNavigationSourceHandle source = registry.registerSource(planeSource(child));
        Engine::SceneNavigationBuildRequest request = buildRequest();
        request.bounds = {{-40.0f, -2.0f, -40.0f}, {40.0f, 2.0f, 40.0f}};
        const std::optional<Engine::NavigationTerrainBuildData> snapshot =
            registry.buildNavigationData(scene, request);

        ctx.expect(snapshot.has_value(), "transformed snapshot failed");
        if (snapshot) {
            const bool hasRootVertex = std::ranges::any_of(snapshot->vertices, [](const glm::vec3& vertex) {
                return nearVec3(vertex, {2.0f, 0.0f, 3.0f});
            });
            const bool hasRotatedVertex = std::ranges::any_of(snapshot->vertices, [](const glm::vec3& vertex) {
                return nearVec3(vertex, {2.0f, 0.0f, -29.0f});
            });
            ctx.expect(hasRootVertex, "root transformed vertex was missing");
            ctx.expect(hasRotatedVertex, "rotated/scaled vertex was missing");
        }
    }

    void disabledAndInvalidSourcesAreSkipped(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneNavigationGeometryRegistry registry;
        Engine::SceneNavigationSourceHandle disabled = registry.registerSource(planeSource());
        registry.setSourceEnabled(disabled, false);

        Engine::SceneNavigationSourceDescriptor malformed = planeSource();
        malformed.indices = {0, 99, 1};
        [[maybe_unused]] const Engine::SceneNavigationSourceHandle malformedSource =
            registry.registerSource(std::move(malformed));

        Engine::SceneActorHandle actor = scene.createActor();
        Engine::SceneNavigationSourceDescriptor stale = planeSource(actor);
        [[maybe_unused]] const Engine::SceneNavigationSourceHandle staleSource =
            registry.registerSource(std::move(stale));
        scene.destroyActor(actor);

        const std::optional<Engine::NavigationTerrainBuildData> snapshot =
            registry.buildNavigationData(scene, buildRequest());
        ctx.expect(snapshot.has_value(), "invalid-source snapshot failed");
        if (snapshot) {
            ctx.expect(snapshot->vertices.empty(), "invalid sources should not append vertices");
            ctx.expect(snapshot->indices.empty(), "invalid sources should not append indices");
        }

        const Engine::SceneNavigationGeometryDiagnostics diagnostics = registry.diagnostics();
        ctx.expect(diagnostics.disabledSourceCount == 1, "disabled source was not counted");
        ctx.expect(diagnostics.invalidGeometryCount == 1, "invalid geometry was not counted");
        ctx.expect(diagnostics.invalidActorCount == 1, "invalid actor was not counted");
        ctx.expect(diagnostics.skippedSourceCount == 3, "skipped source count was wrong");
    }

    void blockerSourceProducesBlockingGeometry(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneNavigationGeometryRegistry registry;
        [[maybe_unused]] const Engine::SceneNavigationSourceHandle source = registry.registerSource(blockerSource());
        const Engine::NavigationTerrainBuildData terrain = terrainBase();
        const std::optional<Engine::NavigationTerrainBuildData> snapshot =
            registry.buildNavigationData(scene, buildRequest(), &terrain);

        ctx.expect(snapshot.has_value(), "blocker snapshot failed");
        if (!snapshot) {
            return;
        }
        ctx.expect(snapshot->vertices.size() == terrain.vertices.size(), "blocker changed walkable vertex count");
        ctx.expect(snapshot->blockingVertices.size() == 8, "blocker vertices were not populated");
        ctx.expect(snapshot->blockingIndices.size() == 36, "blocker indices were not populated");

        Engine::NavigationSystem clearNavigation;
        clearNavigation.buildTerrainTile(terrain, {});
        Engine::SceneNavigationService clearService(clearNavigation);
        const Engine::NavigationReachabilityResult clear =
            clearService.reachable({2.0f, 0.0f, 8.0f}, {14.0f, 0.0f, 8.0f});

        Engine::NavigationSystem blockedNavigation;
        blockedNavigation.buildTerrainTile(*snapshot, {});
        Engine::SceneNavigationService blockedService(blockedNavigation);
        const Engine::NavigationReachabilityResult blocked =
            blockedService.reachable({2.0f, 0.0f, 8.0f}, {14.0f, 0.0f, 8.0f});

        ctx.expect(clear.reachable, "clear terrain should be reachable");
        ctx.expect(!blocked.reachable, "blocker should prevent reachability across wall");
    }

    void terrainAndSceneSourcesMerge(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneNavigationGeometryRegistry registry;
        [[maybe_unused]] const Engine::SceneNavigationSourceHandle source = registry.registerSource(planeSource());
        const Engine::NavigationTerrainBuildData terrain = terrainBase();
        const std::optional<Engine::NavigationTerrainBuildData> snapshot =
            registry.buildNavigationData(scene, buildRequest(), &terrain);

        ctx.expect(snapshot.has_value(), "merged snapshot failed");
        if (snapshot) {
            ctx.expect(snapshot->vertices.size() == 8, "merged walkable vertex count was wrong");
            ctx.expect(snapshot->indices.size() == 12, "merged walkable index count was wrong");
            ctx.expect(snapshot->indices[6] == 4, "scene source indices were not offset after terrain");
        }
    }

    void slopeAndBoundsFiltering(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneNavigationGeometryRegistry registry;
        [[maybe_unused]] const Engine::SceneNavigationSourceHandle flat = registry.registerSource(planeSource());
        [[maybe_unused]] const Engine::SceneNavigationSourceHandle steep = registry.registerSource(steepTriangleSource());
        [[maybe_unused]] const Engine::SceneNavigationSourceHandle outside = registry.registerSource(outsideTriangleSource());

        Engine::SceneNavigationGeometryBuildSettings settings;
        settings.maxWalkableSlopeDegrees = 30.0f;
        settings.tileBoundsPadding = 0.0f;
        const std::optional<Engine::NavigationTerrainBuildData> snapshot =
            registry.buildNavigationData(scene, buildRequest(), nullptr, settings);

        ctx.expect(snapshot.has_value(), "filtered snapshot failed");
        if (snapshot) {
            ctx.expect(snapshot->vertices.size() == 4, "filtered snapshot should keep only compact flat plane vertices");
            ctx.expect(snapshot->indices.size() == 6, "filtered snapshot should keep only flat plane triangles");
        }

        const Engine::SceneNavigationGeometryDiagnostics diagnostics = registry.diagnostics();
        ctx.expect(diagnostics.consideredTriangleCount == 4, "considered triangle count was wrong");
        ctx.expect(diagnostics.slopeCulledTriangleCount == 1, "steep triangle was not slope culled");
        ctx.expect(diagnostics.boundsCulledTriangleCount == 1, "outside triangle was not bounds culled");
        ctx.expect(diagnostics.appendedTriangleCount == 2, "appended triangle count was wrong");
    }

    void paddedTileBoundsIncludeIntersectingTriangle(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneNavigationGeometryRegistry registry;
        [[maybe_unused]] const Engine::SceneNavigationSourceHandle source =
            registry.registerSource(paddingIntersectingTriangleSource());

        Engine::SceneNavigationGeometryBuildSettings noPadding;
        noPadding.tileBoundsPadding = 0.0f;
        const std::optional<Engine::NavigationTerrainBuildData> withoutPadding =
            registry.buildNavigationData(scene, buildRequest(), nullptr, noPadding);
        ctx.expect(withoutPadding.has_value(), "no-padding snapshot failed");
        if (withoutPadding) {
            ctx.expect(withoutPadding->indices.empty(), "triangle outside unpadded tile should be culled");
        }

        Engine::SceneNavigationGeometryBuildSettings withPadding;
        withPadding.tileBoundsPadding = 0.5f;
        const std::optional<Engine::NavigationTerrainBuildData> padded =
            registry.buildNavigationData(scene, buildRequest(), nullptr, withPadding);
        ctx.expect(padded.has_value(), "padded snapshot failed");
        if (padded) {
            ctx.expect(padded->vertices.size() == 3, "padded bounds should include triangle vertices");
            ctx.expect(padded->indices.size() == 3, "padded bounds should include triangle indices");
        }
    }

    void dirtyTrackingMarksExpectedChunks(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneNavigationGeometryRegistry registry;
        const Engine::SceneActorHandle actor = scene.createActor();
        const Engine::SceneNavigationSourceHandle source = registry.registerSource(planeSource(actor));
        registry.refreshDirtySources(scene, ChunkSize);
        std::vector<Engine::ChunkCoord> dirty = registry.dirtyChunks();
        ctx.expect(!dirty.empty() && dirty.front() == Engine::ChunkCoord{0, 0}, "initial dirty chunk was missing");

        registry.clearDirty();
        Engine::SceneTransform transform;
        transform.translation = {ChunkSize, 0.0f, 0.0f};
        scene.setLocalTransform(actor, transform);
        registry.markSourceDirty(source, Engine::SceneNavigationDirtyReason::Transform);
        registry.refreshDirtySources(scene, ChunkSize);
        dirty = registry.dirtyChunks();
        ctx.expect(std::ranges::find(dirty, Engine::ChunkCoord{0, 0}) != dirty.end(), "previous chunk was not dirtied");
        ctx.expect(std::ranges::find(dirty, Engine::ChunkCoord{1, 0}) != dirty.end(), "new chunk was not dirtied");

        registry.clearDirty();
        registry.unregisterSource(scene, source, ChunkSize);
        dirty = registry.dirtyChunks();
        ctx.expect(std::ranges::find(dirty, Engine::ChunkCoord{1, 0}) != dirty.end(), "removed source chunk was not dirtied");
    }

    void unregisterBeforeSnapshotMarksDirtyChunks(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::SceneNavigationGeometryRegistry registry;
        const Engine::SceneActorHandle actor = scene.createActor();
        Engine::SceneTransform transform;
        transform.translation = {ChunkSize, 0.0f, 0.0f};
        scene.setLocalTransform(actor, transform);

        const Engine::SceneNavigationSourceHandle source = registry.registerSource(planeSource(actor));
        registry.clearDirty();
        ctx.expect(registry.unregisterSource(scene, source, ChunkSize), "scene-aware unregister succeeds before snapshot");
        const std::vector<Engine::ChunkCoord> dirty = registry.dirtyChunks();

        ctx.expect(std::ranges::find(dirty, Engine::ChunkCoord{1, 0}) != dirty.end(), "current source chunk was dirtied before snapshot");
        ctx.expect(!registry.contains(source), "unregistered source handle is invalidated");
    }

    Assets::Assimp::ImportedScene syntheticImportedScene()
    {
        Assets::Assimp::ImportedScene imported;
        imported.success = true;

        Assets::Assimp::ImportedScenePrimitive primitive;
        primitive.vertices.resize(3);
        primitive.vertices[0].position = {0.0f, 0.0f, 0.0f};
        primitive.vertices[1].position = {1.0f, 0.0f, 0.0f};
        primitive.vertices[2].position = {0.0f, 0.0f, 1.0f};
        primitive.indices = {0, 1, 2};

        Assets::Assimp::ImportedSceneMesh mesh;
        mesh.name = "nav fixture mesh";
        mesh.primitives.push_back(std::move(primitive));
        mesh.bounds = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, true};
        imported.meshes.push_back(std::move(mesh));

        Assets::Assimp::ImportedSceneNode node;
        node.meshIndices.push_back(0);
        imported.nodes.push_back(std::move(node));

        return imported;
    }

    void authoredAdapterRegistersNavSourcesWhenOptedIn(TestContext& ctx)
    {
        TestRenderer::reset();
        Engine::Scene scene;
        Engine::SceneRenderBridge bridge(scene);
        Engine::AssetCache cache;
        Engine::SceneNavigationGeometryRegistry registry;
        Engine::SceneAuthoredAdapterSettings settings;
        settings.loadTextures = false;
        settings.navigationGeometry = &registry;
        settings.registerNavigationSources = true;

        Assets::Assimp::ImportedScene imported = syntheticImportedScene();
        Engine::SceneAuthoredAdapterResult result = Engine::adaptImportedSceneToScene(
            imported,
            std::filesystem::path{"synthetic.gltf"},
            cache,
            scene,
            bridge,
            settings);

        ctx.expect(result.success, "authored adapter failed");
        ctx.expect(result.diagnostics.createdNavigationSourceCount == 1, "authored adapter did not create nav source");
        ctx.expect(result.nodes.size() == 1 && result.nodes.front().navigationSources.size() == 1,
            "node binding did not retain nav source handle");
        const std::optional<Engine::NavigationTerrainBuildData> snapshot =
            registry.buildNavigationData(scene, buildRequest());
        ctx.expect(snapshot.has_value() && snapshot->vertices.size() == 3, "authored nav source did not build");

        Engine::releaseSceneAuthoredAdapterResources(result.resources, cache);
        cache.shutdown();
    }

    using TestFn = void (*)(TestContext&);

    struct TestCase {
        std::string_view name;
        TestFn fn;
    };
}

int main()
{
    const std::vector<TestCase> tests = {
        {"RegisterSourceCreatesDeterministicSnapshot", registerSourceCreatesDeterministicSnapshot},
        {"SceneTransformAffectsGeometry", sceneTransformAffectsGeometry},
        {"DisabledAndInvalidSourcesAreSkipped", disabledAndInvalidSourcesAreSkipped},
        {"BlockerSourceProducesBlockingGeometry", blockerSourceProducesBlockingGeometry},
        {"TerrainAndSceneSourcesMerge", terrainAndSceneSourcesMerge},
        {"SlopeAndBoundsFiltering", slopeAndBoundsFiltering},
        {"PaddedTileBoundsIncludeIntersectingTriangle", paddedTileBoundsIncludeIntersectingTriangle},
        {"DirtyTrackingMarksExpectedChunks", dirtyTrackingMarksExpectedChunks},
        {"UnregisterBeforeSnapshotMarksDirtyChunks", unregisterBeforeSnapshotMarksDirtyChunks},
        {"AuthoredAdapterRegistersNavSourcesWhenOptedIn", authoredAdapterRegistersNavSourcesWhenOptedIn},
    };

    std::vector<TestFailure> failures;
    for (const TestCase& test : tests) {
        TestContext ctx{std::string{test.name}, failures};
        test.fn(ctx);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << "[FAIL] " << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "Navigation scene geometry tests passed (" << tests.size() << " cases)\n";
    return 0;
}
