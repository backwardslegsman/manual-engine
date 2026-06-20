#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Engine/Terrain.hpp"
#include "Engine/TerrainDataset.hpp"
#include "Engine/TerrainDerivedCache.hpp"
#include "Engine/TerrainRenderLodAdapter.hpp"

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

    std::filesystem::path rootPath(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / ("manual_engine_terrain_lod_adapter_" + std::string{name});
    }

    void clearRoot(const std::filesystem::path& path)
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    Engine::TerrainDerivedCacheSettings cacheSettings(std::string_view name)
    {
        Engine::TerrainDerivedCacheSettings settings;
        settings.rootPath = rootPath(name);
        settings.policy = Engine::TerrainDerivedCachePolicy::ReadOnly;
        return settings;
    }

    Engine::TerrainRenderLodSourceIdentity sourceIdentity(uint64_t id = 800)
    {
        Engine::TerrainRenderLodSourceIdentity identity;
        identity.sourceId = {id};
        identity.sourceHash = "test_source_hash";
        identity.importSettings = {"terrain_lod_adapter", "1", "test"};
        identity.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        return identity;
    }

    Engine::TerrainSourceDescriptor importedSourceDescriptor(uint64_t id = 800)
    {
        Engine::TerrainSourceDescriptor descriptor;
        descriptor.sourceId = {id};
        descriptor.type = Engine::TerrainDatasetSourceType::HeightmapImported;
        descriptor.defaultChunkSize = 4.0f;
        descriptor.defaultResolution = 3;
        descriptor.settings = {"terrain_lod_adapter", "1", "test"};
        descriptor.debugName = "lod.adapter.test";
        return descriptor;
    }

    Engine::TerrainImportedChunk importedChunk(Engine::AssetId sourceId = {800})
    {
        Engine::TerrainImportedChunk chunk;
        chunk.id = {sourceId, {0, 0}};
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

    std::optional<Engine::TerrainRenderLodBuildRequest> datasetRequest(
        std::string_view cacheName,
        uint32_t renderResolution = 5,
        float skirtDepth = 0.5f,
        Engine::TerrainDerivedCachePolicy policy = Engine::TerrainDerivedCachePolicy::ReadOnly)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());
        Engine::TerrainLodMeshBuildSettings lod;
        lod.lodIndex = 1;
        lod.renderResolution = renderResolution;
        lod.skirtDepth = skirtDepth;
        Engine::TerrainDerivedCacheSettings settings = cacheSettings(cacheName);
        settings.policy = policy;
        return Engine::renderLodRequestFromDatasetChunk(dataset, chunk, lod, sourceIdentity(), 42, settings);
    }

    bool sameVertex(const Renderer::MeshVertex& lhs, const Renderer::MeshVertex& rhs)
    {
        return near(lhs.px, rhs.px) &&
            near(lhs.py, rhs.py) &&
            near(lhs.pz, rhs.pz) &&
            near(lhs.nx, rhs.nx) &&
            near(lhs.ny, rhs.ny) &&
            near(lhs.nz, rhs.nz) &&
            near(lhs.tx, rhs.tx) &&
            near(lhs.ty, rhs.ty) &&
            near(lhs.tz, rhs.tz) &&
            near(lhs.tw, rhs.tw) &&
            near(lhs.u, rhs.u) &&
            near(lhs.v, rhs.v) &&
            near(lhs.u1, rhs.u1) &&
            near(lhs.v1, rhs.v1) &&
            lhs.abgr == rhs.abgr;
    }

    void datasetChunkBuildsDeterministicRendererMesh(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());
        Engine::TerrainLodMeshBuildSettings lod;
        lod.lodIndex = 1;
        lod.renderResolution = 5;
        lod.skirtDepth = 0.5f;

        const auto firstRequest = Engine::renderLodRequestFromDatasetChunk(dataset, chunk, lod, sourceIdentity(), 42, cacheSettings("dataset"));
        const auto secondRequest = Engine::renderLodRequestFromDatasetChunk(dataset, chunk, lod, sourceIdentity(), 42, cacheSettings("dataset"));
        ctx.expect(firstRequest.has_value() && secondRequest.has_value(), "dataset request was not built");
        if (!firstRequest || !secondRequest) {
            return;
        }

        const Engine::TerrainRenderLodBuildResult first = Engine::buildTerrainRenderLod(*firstRequest);
        const Engine::TerrainRenderLodBuildResult second = Engine::buildTerrainRenderLod(*secondRequest);
        ctx.expect(first.success && second.success, "dataset render LOD build failed");
        ctx.expect(first.build.mesh.vertices.size() == second.build.mesh.vertices.size(), "dataset render LOD vertex count was not deterministic");
        ctx.expect(first.build.mesh.indices == second.build.mesh.indices, "dataset render LOD indices were not deterministic");
        ctx.expect(!first.build.mesh.vertices.empty() && sameVertex(first.build.mesh.vertices.front(), second.build.mesh.vertices.front()),
            "dataset render LOD first vertex was not deterministic");
    }

    void importedHeightmapChunkRendersWithExpectedBounds(TestContext& ctx)
    {
        Engine::TerrainDataset dataset;
        const Engine::TerrainSourceHandle source = dataset.registerSource(importedSourceDescriptor());
        const Engine::TerrainChunkHandle chunk = dataset.loadImportedChunk(source, importedChunk());
        Engine::TerrainLodMeshBuildSettings lod;
        lod.renderResolution = 5;
        lod.skirtDepth = 1.0f;
        const auto request = Engine::renderLodRequestFromDatasetChunk(dataset, chunk, lod, sourceIdentity(), 1, cacheSettings("bounds"));
        ctx.expect(request.has_value(), "heightmap request was not built");
        if (!request) {
            return;
        }
        const Engine::TerrainRenderLodBuildResult result = Engine::buildTerrainRenderLod(*request);
        ctx.expect(result.success, "heightmap LOD build failed");
        ctx.expect(!result.build.mesh.vertices.empty() && !result.build.mesh.indices.empty(), "heightmap LOD produced no geometry");
        ctx.expect(near(result.build.mesh.bounds.min.x, 0.0f) && near(result.build.mesh.bounds.max.x, 4.0f), "heightmap LOD x bounds were wrong");
        ctx.expect(near(result.build.mesh.bounds.min.y, -1.0f) && near(result.build.mesh.bounds.max.y, 4.0f), "heightmap LOD y bounds were wrong");
    }

    void lodResolutionAndSkirtsProduceExpectedCounts(TestContext& ctx)
    {
        const auto request = datasetRequest("counts", 3, 1.0f, Engine::TerrainDerivedCachePolicy::Disabled);
        ctx.expect(request.has_value(), "count request was not built");
        if (!request) {
            return;
        }
        const Engine::TerrainRenderLodBuildResult result = Engine::buildTerrainRenderLod(*request);
        ctx.expect(result.success, "count LOD build failed");
        ctx.expect(result.build.mesh.vertices.size() == 25, "3x3 LOD with skirts should have 25 vertices");
        ctx.expect(result.build.mesh.indices.size() == 72, "3x3 LOD with skirts should have 72 indices");
    }

    void cacheHitReturnsPayloadWithoutRegeneration(TestContext& ctx)
    {
        Engine::TerrainDerivedCacheSettings settings = cacheSettings("cache_hit");
        clearRoot(settings.rootPath);
        const auto request = datasetRequest("cache_hit");
        ctx.expect(request.has_value(), "cache hit request was not built");
        if (!request) {
            return;
        }

        Engine::TerrainLodMeshBuildSettings lod;
        lod.lodIndex = request->lodIndex;
        lod.renderResolution = request->renderResolution;
        lod.skirtDepth = request->skirtDepth;
        const Engine::TerrainCachedChunkPayload chunkPayload = Engine::cachedChunkPayloadFromRenderLodRequest(*request);
        const Engine::TerrainCachedLodMeshPayload lodPayload = Engine::buildTerrainCachedLodMesh(chunkPayload, lod);
        const Engine::TerrainDerivedCacheManifest manifest =
            Engine::TerrainDerivedCache::buildLodMeshManifest(settings, chunkPayload, lod, request->identity.sourceHash);
        const Engine::TerrainDerivedCacheWriteResult write = Engine::TerrainDerivedCache::writeLodMesh(manifest, lodPayload);
        ctx.expect(write.status == Engine::TerrainDerivedCacheStatus::WriteSuccess, "failed to seed cache hit");

        const Engine::TerrainRenderLodBuildResult result = Engine::buildTerrainRenderLod(*request);
        ctx.expect(result.success, "cache hit LOD build failed");
        ctx.expect(result.diagnostics.usedCache, "cache hit did not use cache");
        ctx.expect(!result.diagnostics.generated, "cache hit regenerated mesh");
        ctx.expect(result.diagnostics.cacheHitCount == 1, "cache hit diagnostic count was wrong");
    }

    void cacheMissStaleCorruptFallbackGenerates(TestContext& ctx)
    {
        Engine::TerrainDerivedCacheSettings missSettings = cacheSettings("cache_miss");
        clearRoot(missSettings.rootPath);
        const auto missRequest = datasetRequest("cache_miss");
        ctx.expect(missRequest.has_value(), "miss request was not built");
        if (missRequest) {
            const Engine::TerrainRenderLodBuildResult miss = Engine::buildTerrainRenderLod(*missRequest);
            ctx.expect(miss.success && miss.diagnostics.generated, "cache miss did not generate");
            ctx.expect(miss.diagnostics.cacheMissCount == 1, "cache miss diagnostic count was wrong");
        }

        Engine::TerrainDerivedCacheSettings staleSettings = cacheSettings("cache_stale");
        clearRoot(staleSettings.rootPath);
        const auto staleRequest = datasetRequest("cache_stale");
        ctx.expect(staleRequest.has_value(), "stale request was not built");
        if (staleRequest) {
            Engine::TerrainLodMeshBuildSettings lod;
            lod.lodIndex = staleRequest->lodIndex;
            lod.renderResolution = staleRequest->renderResolution;
            lod.skirtDepth = staleRequest->skirtDepth;
            const Engine::TerrainCachedChunkPayload chunkPayload = Engine::cachedChunkPayloadFromRenderLodRequest(*staleRequest);
            const Engine::TerrainCachedLodMeshPayload lodPayload = Engine::buildTerrainCachedLodMesh(chunkPayload, lod);
            const Engine::TerrainDerivedCacheManifest manifest =
                Engine::TerrainDerivedCache::buildLodMeshManifest(staleSettings, chunkPayload, lod, staleRequest->identity.sourceHash);
            (void)Engine::TerrainDerivedCache::writeLodMesh(manifest, lodPayload);
            {
                std::ofstream file(Engine::TerrainDerivedCache::cacheRoot(manifest) / manifest.payloadFileName, std::ios::binary | std::ios::app);
                file.put('x');
            }
            const Engine::TerrainRenderLodBuildResult stale = Engine::buildTerrainRenderLod(*staleRequest);
            ctx.expect(stale.success && stale.diagnostics.generated, "cache stale did not generate");
            ctx.expect(stale.diagnostics.cacheStaleCount == 1, "cache stale diagnostic count was wrong");
        }

        Engine::TerrainDerivedCacheSettings corruptSettings = cacheSettings("cache_corrupt");
        clearRoot(corruptSettings.rootPath);
        const auto corruptRequest = datasetRequest("cache_corrupt");
        ctx.expect(corruptRequest.has_value(), "corrupt request was not built");
        if (corruptRequest) {
            Engine::TerrainLodMeshBuildSettings lod;
            lod.lodIndex = corruptRequest->lodIndex;
            lod.renderResolution = corruptRequest->renderResolution;
            lod.skirtDepth = corruptRequest->skirtDepth;
            const Engine::TerrainCachedChunkPayload chunkPayload = Engine::cachedChunkPayloadFromRenderLodRequest(*corruptRequest);
            const Engine::TerrainCachedLodMeshPayload lodPayload = Engine::buildTerrainCachedLodMesh(chunkPayload, lod);
            const Engine::TerrainDerivedCacheManifest manifest =
                Engine::TerrainDerivedCache::buildLodMeshManifest(corruptSettings, chunkPayload, lod, corruptRequest->identity.sourceHash);
            (void)Engine::TerrainDerivedCache::writeLodMesh(manifest, lodPayload);
            {
                std::ofstream file(Engine::TerrainDerivedCache::cacheRoot(manifest) / "manifest.yaml", std::ios::trunc);
                file << "not: [valid";
            }
            const Engine::TerrainRenderLodBuildResult corrupt = Engine::buildTerrainRenderLod(*corruptRequest);
            ctx.expect(corrupt.success && corrupt.diagnostics.generated, "cache corrupt did not generate");
            ctx.expect(corrupt.diagnostics.cacheCorruptCount == 1, "cache corrupt diagnostic count was wrong");
        }
    }

}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"DatasetChunkBuildsDeterministicRendererMesh", datasetChunkBuildsDeterministicRendererMesh},
        {"ImportedHeightmapChunkRendersWithExpectedBounds", importedHeightmapChunkRendersWithExpectedBounds},
        {"LodResolutionAndSkirtsProduceExpectedCounts", lodResolutionAndSkirtsProduceExpectedCounts},
        {"CacheHitReturnsPayloadWithoutRegeneration", cacheHitReturnsPayloadWithoutRegeneration},
        {"CacheMissStaleCorruptFallbackGenerates", cacheMissStaleCorruptFallbackGenerates},
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

    std::cout << "Terrain render LOD adapter tests passed (" << tests.size() << " tests)\n";
    return 0;
}
