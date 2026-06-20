#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Engine/Navigation.hpp"
#include "Engine/NavigationCache.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/TerrainDataset.hpp"
#include "Engine/TerrainNavigationAdapter.hpp"

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

    bool sameBuildData(const Engine::NavigationTerrainBuildData& lhs, const Engine::NavigationTerrainBuildData& rhs)
    {
        if (lhs.coord != rhs.coord ||
            lhs.vertices.size() != rhs.vertices.size() ||
            lhs.indices != rhs.indices ||
            !sameVec3(lhs.bounds.min, rhs.bounds.min) ||
            !sameVec3(lhs.bounds.max, rhs.bounds.max)) {
            return false;
        }
        for (size_t index = 0; index < lhs.vertices.size(); ++index) {
            if (!sameVec3(lhs.vertices[index], rhs.vertices[index])) {
                return false;
            }
        }
        return true;
    }

    Engine::TerrainNavigationSourceIdentity identity(uint64_t id = 901)
    {
        Engine::TerrainNavigationSourceIdentity result;
        result.sourceId = {id};
        result.sourceHash = "terrain_source_hash";
        result.importSettings = {"terrain_navigation", "1", "test"};
        result.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        return result;
    }

    Engine::TerrainSourceDescriptor importedSourceDescriptor(uint64_t id = 901)
    {
        Engine::TerrainSourceDescriptor descriptor;
        descriptor.sourceId = {id};
        descriptor.type = Engine::TerrainDatasetSourceType::HeightmapImported;
        descriptor.defaultChunkSize = 4.0f;
        descriptor.defaultResolution = 3;
        descriptor.settings = {"terrain_navigation", "1", "test"};
        descriptor.debugName = "terrain.nav.imported";
        return descriptor;
    }

    Engine::TerrainSourceDescriptor proceduralSourceDescriptor(uint64_t id = 902)
    {
        Engine::TerrainSourceDescriptor descriptor;
        descriptor.sourceId = {id};
        descriptor.type = Engine::TerrainDatasetSourceType::Procedural;
        descriptor.defaultChunkSize = 4.0f;
        descriptor.defaultResolution = 5;
        descriptor.settings = {"procedural_terrain", "1", "test"};
        descriptor.debugName = "terrain.nav.procedural";
        descriptor.procedural.chunkSize = 4.0f;
        descriptor.procedural.resolution = 5;
        descriptor.procedural.heightScale = 1.0f;
        return descriptor;
    }

    Engine::TerrainImportedChunk importedChunk()
    {
        Engine::TerrainImportedChunk chunk;
        chunk.id = {{901}, {0, 0}};
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

    Engine::TerrainImportedChunk flatImportedChunk(int32_t x, int32_t z, float originX, float originZ)
    {
        Engine::TerrainImportedChunk chunk;
        chunk.id = {{901}, {x, z}};
        chunk.coord = {x, z};
        chunk.origin = {originX, 0.0f, originZ};
        chunk.size = 4.0f;
        chunk.resolution = 3;
        chunk.heights.assign(9, 0.0f);
        return chunk;
    }

    Engine::GeneratedTerrainTileData generatedTile()
    {
        Engine::GeneratedTerrainTileData tile;
        tile.coord = {0, 0};
        tile.origin = {0.0f, 0.0f, 0.0f};
        tile.size = 4.0f;
        tile.resolution = 3;
        tile.heights = {
            0.0f, 1.0f, 2.0f,
            1.0f, 2.0f, 3.0f,
            2.0f, 3.0f, 4.0f,
        };
        return tile;
    }

    Engine::TerrainSettings terrainSettings()
    {
        Engine::TerrainSettings settings;
        settings.createRendererResources = false;
        settings.chunkSize = 4.0f;
        settings.resolution = 3;
        settings.navigationResolution = 5;
        settings.heightScale = 1.0f;
        return settings;
    }

    Engine::NavigationCacheManifest manifestFor(
        Engine::TerrainNavigationSourceIdentity source,
        uint32_t navigationResolution = 5,
        float borderPadding = 0.0f,
        uint32_t borderSamples = 0)
    {
        Engine::NavigationCacheSettings settings;
        settings.worldId = "terrain-nav-adapter-test";
        return Engine::NavigationCache::buildManifest(
            settings,
            4.0f,
            8,
            navigationResolution,
            {},
            {},
            "default",
            "missing_biomes.yaml",
            "missing_archetypes.yaml",
            source.sourceId,
            source.sourceHash,
            source.importSettings,
            source.sourceType == Engine::TerrainDatasetSourceType::HeightmapImported ? "heightmap_imported" : "procedural",
            Engine::TerrainNavigationAdapterVersion,
            borderPadding,
            borderSamples);
    }

    void datasetImportedChunkProducesDeterministicBuildData(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());
        const auto request = Engine::terrainNavigationRequestFromDatasetChunk(dataset, chunk, 5, identity());
        ctx.expect(request.has_value(), "dataset imported request was not built");
        if (!request) {
            return;
        }
        const Engine::TerrainNavigationBuildResult first = Engine::buildTerrainNavigationData(*request);
        const Engine::TerrainNavigationBuildResult second = Engine::buildTerrainNavigationData(*request);
        ctx.expect(first.success && second.success && first.buildData && second.buildData, "dataset imported build failed");
        if (first.buildData && second.buildData) {
            ctx.expect(sameBuildData(*first.buildData, *second.buildData), "dataset imported build was not deterministic");
            ctx.expect(first.buildData->vertices.size() == 25, "dataset imported nav vertex count was wrong");
            ctx.expect(first.buildData->indices.size() == 96, "dataset imported nav index count was wrong");
        }
    }

    void datasetProceduralChunkUsesSameAdapterPath(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(proceduralSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadProceduralChunk(source, {0, 0});
        Engine::TerrainNavigationSourceIdentity sourceIdentity = identity(902);
        sourceIdentity.sourceType = Engine::TerrainDatasetSourceType::Procedural;
        sourceIdentity.importSettings = {"procedural_terrain", "1", "test"};
        const auto request = Engine::terrainNavigationRequestFromDatasetChunk(dataset, chunk, 5, sourceIdentity);
        ctx.expect(request.has_value(), "dataset procedural request was not built");
        if (!request) {
            return;
        }
        const Engine::TerrainNavigationBuildResult result = Engine::buildTerrainNavigationData(*request);
        ctx.expect(result.success && result.buildData.has_value(), "dataset procedural build failed");
        if (result.buildData) {
            ctx.expect(!result.buildData->vertices.empty() && !result.buildData->indices.empty(), "dataset procedural build produced no geometry");
        }
    }

    void legacyTerrainTileMatchesExistingBuildData(TestContext& ctx)
    {
        Engine::TerrainSystem terrain(terrainSettings());
        const Engine::TerrainTileHandle tile = terrain.createTileFromHeights({0, 0}, importedChunk().heights);
        const std::optional<Engine::NavigationTerrainBuildData> oldBuild = terrain.navigationBuildData(tile, 5);
        const auto request = Engine::terrainNavigationRequestFromTerrainSystemTile(
            terrain,
            tile,
            5,
            Engine::legacyProceduralTerrainNavigationIdentity(terrain.settings()));
        ctx.expect(oldBuild.has_value() && request.has_value(), "legacy terrain request was not built");
        if (!oldBuild || !request) {
            return;
        }
        const Engine::TerrainNavigationBuildResult result = Engine::buildTerrainNavigationData(*request);
        ctx.expect(result.success && result.buildData.has_value(), "legacy terrain adapter build failed");
        if (result.buildData) {
            ctx.expect(sameBuildData(*oldBuild, *result.buildData), "legacy terrain adapter build differed from existing build data");
        }
    }

    void generatedTileMatchesExistingStaticBuildData(TestContext& ctx)
    {
        const Engine::GeneratedTerrainTileData generated = generatedTile();
        const std::optional<Engine::NavigationTerrainBuildData> oldBuild = Engine::TerrainSystem::navigationBuildData(generated, 5);
        const auto request = Engine::terrainNavigationRequestFromGeneratedTile(generated, 5, identity());
        ctx.expect(oldBuild.has_value() && request.has_value(), "generated tile request was not built");
        if (!oldBuild || !request) {
            return;
        }
        const Engine::TerrainNavigationBuildResult result = Engine::buildTerrainNavigationData(*request);
        ctx.expect(result.success && result.buildData.has_value(), "generated tile adapter build failed");
        if (result.buildData) {
            ctx.expect(sameBuildData(*oldBuild, *result.buildData), "generated tile adapter build differed from existing build data");
        }
    }

    void navigationResolutionReducesBuildGeometry(TestContext& ctx)
    {
        const Engine::GeneratedTerrainTileData generated = generatedTile();
        const auto fullRequest = Engine::terrainNavigationRequestFromGeneratedTile(generated, 5, identity());
        const auto reducedRequest = Engine::terrainNavigationRequestFromGeneratedTile(generated, 3, identity());
        ctx.expect(fullRequest.has_value() && reducedRequest.has_value(), "resolution requests were not built");
        if (!fullRequest || !reducedRequest) {
            return;
        }
        const Engine::TerrainNavigationBuildResult full = Engine::buildTerrainNavigationData(*fullRequest);
        const Engine::TerrainNavigationBuildResult reduced = Engine::buildTerrainNavigationData(*reducedRequest);
        ctx.expect(full.success && reduced.success && full.buildData && reduced.buildData, "resolution builds failed");
        if (full.buildData && reduced.buildData) {
            ctx.expect(full.buildData->vertices.size() == 25, "full nav resolution vertex count was wrong");
            ctx.expect(reduced.buildData->vertices.size() == 9, "reduced nav resolution vertex count was wrong");
            ctx.expect(reduced.buildData->indices.size() < full.buildData->indices.size(), "reduced nav resolution did not reduce indices");
        }
    }

    void borderNeighborhoodProducesExpandedGeometry(TestContext& ctx)
    {
        const std::vector<Engine::TerrainImportedChunk> chunks{
            flatImportedChunk(0, 0, 0.0f, 0.0f),
            flatImportedChunk(1, 0, 4.0f, 0.0f),
        };
        Engine::TerrainNavigationBuildSettings settings;
        settings.navigationResolution = 5;
        settings.borderSampleCount = 1;
        const auto request = Engine::terrainNavigationRequestFromImportedChunkNeighborhood(
            chunks,
            chunks.front().id,
            settings,
            identity());
        ctx.expect(request.has_value(), "border request was not built");
        if (!request) {
            return;
        }
        const Engine::TerrainNavigationBuildResult result = Engine::buildTerrainNavigationData(*request);
        ctx.expect(result.success && result.buildData.has_value(), "border build failed");
        if (!result.buildData) {
            return;
        }
        ctx.expect(result.buildData->vertices.size() == 49, "border build vertex count was wrong");
        ctx.expect(result.buildData->indices.size() == 216, "border build index count was wrong");
        ctx.expect(result.buildData->rasterizationBounds.has_value(), "border build did not set rasterization bounds");
        ctx.expect(near(result.buildData->bounds.min.x, 0.0f) && near(result.buildData->bounds.max.x, 4.0f),
            "output tile bounds changed");
        ctx.expect(result.buildData->rasterizationBounds &&
                near(result.buildData->rasterizationBounds->min.x, -1.0f) &&
                near(result.buildData->rasterizationBounds->max.x, 5.0f),
            "rasterization bounds were not expanded");
    }

    void zeroBorderPreservesCompatibilityOutput(TestContext& ctx)
    {
        const std::vector<Engine::TerrainImportedChunk> chunks{importedChunk()};
        Engine::TerrainNavigationBuildSettings settings;
        settings.navigationResolution = 5;
        settings.borderSampleCount = 0;
        settings.borderPaddingWorld = 0.0f;
        const auto request = Engine::terrainNavigationRequestFromImportedChunkNeighborhood(
            chunks,
            chunks.front().id,
            settings,
            identity());
        const auto legacyRequest = Engine::terrainNavigationRequestFromGeneratedTile(generatedTile(), 5, identity());
        ctx.expect(request.has_value() && legacyRequest.has_value(), "zero border requests were not built");
        if (!request || !legacyRequest) {
            return;
        }
        const Engine::TerrainNavigationBuildResult border = Engine::buildTerrainNavigationData(*request);
        const Engine::TerrainNavigationBuildResult legacy = Engine::buildTerrainNavigationData(*legacyRequest);
        ctx.expect(border.success && legacy.success && border.buildData && legacy.buildData, "zero border builds failed");
        if (border.buildData && legacy.buildData) {
            ctx.expect(sameBuildData(*border.buildData, *legacy.buildData), "zero border output differed from legacy output");
        }
    }

    void seamProjectionSucceedsAcrossBorderAwareTiles(TestContext& ctx)
    {
        const std::vector<Engine::TerrainImportedChunk> chunks{
            flatImportedChunk(0, 0, 0.0f, 0.0f),
            flatImportedChunk(1, 0, 4.0f, 0.0f),
        };
        Engine::TerrainNavigationBuildSettings settings;
        settings.navigationResolution = 9;
        settings.borderSampleCount = 1;
        Engine::NavigationSystem navigation;
        Engine::NavBuildSettings buildSettings;
        buildSettings.maxTiles = 8;
        for (const Engine::TerrainImportedChunk& chunk : chunks) {
            const auto request = Engine::terrainNavigationRequestFromImportedChunkNeighborhood(
                chunks,
                chunk.id,
                settings,
                identity());
            ctx.expect(request.has_value(), "seam tile request was not built");
            if (!request) {
                return;
            }
            const Engine::TerrainNavigationBuildResult terrain = Engine::buildTerrainNavigationData(*request);
            ctx.expect(terrain.success && terrain.buildData.has_value(), "seam terrain build failed");
            if (!terrain.buildData) {
                return;
            }
            const Engine::NavigationTileBuildResult tile =
                Engine::NavigationSystem::buildTerrainTileData(*terrain.buildData, {}, buildSettings);
            ctx.expect(tile.status == Engine::NavQueryStatus::Success && tile.tileData.has_value(),
                "seam Detour tile build failed: " + tile.message);
            if (tile.tileData) {
                navigation.loadTerrainTileFromCache(*tile.tileData, tile.diagnostics);
            }
        }
        const Engine::NavQueryResult left = navigation.nearestNavigablePoint({3.98f, 0.5f, 2.0f}, {});
        const Engine::NavQueryResult right = navigation.nearestNavigablePoint({4.02f, 0.5f, 2.0f}, {});
        ctx.expect(left.status == Engine::NavQueryStatus::Success, "left seam projection failed: " + left.message);
        ctx.expect(right.status == Engine::NavQueryStatus::Success, "right seam projection failed: " + right.message);
    }

    void invalidInputsFailCleanly(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const auto staleRequest = Engine::terrainNavigationRequestFromDatasetChunk(dataset, {}, 5, identity());
        ctx.expect(!staleRequest.has_value(), "default dataset chunk unexpectedly built request");

        Engine::GeneratedTerrainTileData malformed = generatedTile();
        malformed.heights.pop_back();
        ctx.expect(!Engine::terrainNavigationRequestFromGeneratedTile(malformed, 5, identity()).has_value(),
            "malformed generated tile unexpectedly built request");

        Engine::TerrainNavigationBuildRequest request;
        request.chunkId = {{901}, {0, 0}};
        request.coord = {0, 0};
        request.origin = {0.0f, 0.0f, 0.0f};
        request.size = -1.0f;
        request.sourceResolution = 3;
        request.navigationResolution = 5;
        request.heights = importedChunk().heights;
        const Engine::TerrainNavigationBuildResult result = Engine::buildTerrainNavigationData(request);
        ctx.expect(!result.success && !result.buildData.has_value(), "invalid direct request unexpectedly succeeded");
    }

    void adapterBuildDataBuildsDetourTile(TestContext& ctx)
    {
        Engine::GeneratedTerrainTileData flat = generatedTile();
        flat.size = 16.0f;
        flat.resolution = 5;
        flat.heights.assign(25, 0.0f);
        const auto request = Engine::terrainNavigationRequestFromGeneratedTile(flat, 9, identity());
        ctx.expect(request.has_value(), "detour request was not built");
        if (!request) {
            return;
        }
        const Engine::TerrainNavigationBuildResult terrainBuild = Engine::buildTerrainNavigationData(*request);
        ctx.expect(terrainBuild.success && terrainBuild.buildData.has_value(), "terrain nav build data failed");
        if (!terrainBuild.buildData) {
            return;
        }
        Engine::NavBuildSettings buildSettings;
        buildSettings.maxTiles = 8;
        Engine::NavigationTileBuildResult tile =
            Engine::NavigationSystem::buildTerrainTileData(*terrainBuild.buildData, {}, buildSettings);
        ctx.expect(tile.status == Engine::NavQueryStatus::Success && tile.tileData.has_value(),
            "adapter build data did not produce Detour tile: " + tile.message);
    }

    void blockersRemainExternal(TestContext& ctx)
    {
        const auto request = Engine::terrainNavigationRequestFromGeneratedTile(generatedTile(), 5, identity());
        ctx.expect(request.has_value(), "blocker request was not built");
        if (!request) {
            return;
        }
        const Engine::TerrainNavigationBuildResult result = Engine::buildTerrainNavigationData(*request);
        ctx.expect(result.success && result.buildData.has_value(), "blocker test build failed");
        if (result.buildData) {
            ctx.expect(result.buildData->blockingVertices.empty(), "adapter unexpectedly emitted blocker vertices");
            ctx.expect(result.buildData->blockingIndices.empty(), "adapter unexpectedly emitted blocker indices");
        }
    }

    void navigationCacheIdentityIncludesTerrainInputs(TestContext& ctx)
    {
        const Engine::TerrainNavigationSourceIdentity base = identity();
        const Engine::NavigationCacheManifest baseManifest = manifestFor(base);

        Engine::TerrainNavigationSourceIdentity changedSource = base;
        changedSource.sourceId = {999};
        ctx.expect(baseManifest.identityHash != manifestFor(changedSource).identityHash, "terrain source ID did not affect nav cache identity");

        Engine::TerrainNavigationSourceIdentity changedHash = base;
        changedHash.sourceHash = "other";
        ctx.expect(baseManifest.identityHash != manifestFor(changedHash).identityHash, "terrain source hash did not affect nav cache identity");

        Engine::TerrainNavigationSourceIdentity changedSettings = base;
        changedSettings.importSettings.optionsHash = "other";
        ctx.expect(baseManifest.identityHash != manifestFor(changedSettings).identityHash, "terrain import settings did not affect nav cache identity");

        Engine::TerrainNavigationSourceIdentity changedType = base;
        changedType.sourceType = Engine::TerrainDatasetSourceType::Procedural;
        ctx.expect(baseManifest.identityHash != manifestFor(changedType).identityHash, "terrain source type did not affect nav cache identity");

        ctx.expect(baseManifest.identityHash != manifestFor(base, 3).identityHash, "navigation resolution did not affect nav cache identity");
        ctx.expect(baseManifest.identityHash != manifestFor(base, 5, 1.0f, 1).identityHash,
            "terrain navigation border settings did not affect nav cache identity");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"DatasetImportedChunkProducesDeterministicBuildData", datasetImportedChunkProducesDeterministicBuildData},
        {"DatasetProceduralChunkUsesSameAdapterPath", datasetProceduralChunkUsesSameAdapterPath},
        {"LegacyTerrainTileMatchesExistingBuildData", legacyTerrainTileMatchesExistingBuildData},
        {"GeneratedTileMatchesExistingStaticBuildData", generatedTileMatchesExistingStaticBuildData},
        {"NavigationResolutionReducesBuildGeometry", navigationResolutionReducesBuildGeometry},
        {"BorderNeighborhoodProducesExpandedGeometry", borderNeighborhoodProducesExpandedGeometry},
        {"ZeroBorderPreservesCompatibilityOutput", zeroBorderPreservesCompatibilityOutput},
        {"SeamProjectionSucceedsAcrossBorderAwareTiles", seamProjectionSucceedsAcrossBorderAwareTiles},
        {"InvalidInputsFailCleanly", invalidInputsFailCleanly},
        {"AdapterBuildDataBuildsDetourTile", adapterBuildDataBuildsDetourTile},
        {"BlockersRemainExternal", blockersRemainExternal},
        {"NavigationCacheIdentityIncludesTerrainInputs", navigationCacheIdentityIncludesTerrainInputs},
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

    std::cout << "Terrain navigation adapter tests passed (" << tests.size() << " tests)\n";
    return 0;
}
