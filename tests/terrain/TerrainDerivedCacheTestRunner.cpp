#include <any>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/TerrainDerivedCache.hpp"
#include "Engine/TerrainDerivedCacheAsync.hpp"

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

    std::filesystem::path rootPath(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / ("manual_engine_terrain_cache_" + std::string{name});
    }

    Engine::TerrainDerivedCacheSettings settings(std::string_view name)
    {
        Engine::TerrainDerivedCacheSettings result;
        result.rootPath = rootPath(name);
        return result;
    }

    Engine::TerrainCachedChunkPayload chunkPayload(uint64_t source = 100, int32_t x = 1, int32_t z = 2)
    {
        Engine::TerrainCachedChunkPayload payload;
        payload.chunkId = {{source}, {x, z}};
        payload.origin = {static_cast<float>(x) * 4.0f, 0.0f, static_cast<float>(z) * 4.0f};
        payload.size = 4.0f;
        payload.resolution = 3;
        payload.heights = {
            0.0f, 1.0f, 2.0f,
            1.0f, 2.0f, 3.0f,
            2.0f, 3.0f, 4.0f,
        };
        payload.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        payload.importSettings = {"heightmap_terrain", "1", "default"};
        payload.warnings = {"test warning"};
        return payload;
    }

    void clearRoot(const std::filesystem::path& path)
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    void stableManifestIdentityForIdenticalInputs(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload payload = chunkPayload();
        const Engine::TerrainDerivedCacheManifest first = Engine::TerrainDerivedCache::buildChunkManifest(settings("stable"), payload, "source_hash");
        const Engine::TerrainDerivedCacheManifest second = Engine::TerrainDerivedCache::buildChunkManifest(settings("stable"), payload, "source_hash");
        ctx.expect(first.identityHash == second.identityHash, "identical inputs produced different cache identity");
        ctx.expect(first.payloadFileName == second.payloadFileName, "identical inputs produced different payload file name");
    }

    void manifestInputsProduceDistinctIdentities(TestContext& ctx)
    {
        Engine::TerrainCachedChunkPayload payload = chunkPayload();
        const Engine::TerrainDerivedCacheManifest base = Engine::TerrainDerivedCache::buildChunkManifest(settings("distinct"), payload, "source_hash");

        Engine::TerrainCachedChunkPayload changedSettings = payload;
        changedSettings.importSettings.optionsHash = "other";
        ctx.expect(base.identityHash != Engine::TerrainDerivedCache::buildChunkManifest(settings("distinct"), changedSettings, "source_hash").identityHash,
            "settings change did not affect identity");

        Engine::TerrainCachedChunkPayload changedSource = payload;
        changedSource.chunkId.source = {999};
        ctx.expect(base.identityHash != Engine::TerrainDerivedCache::buildChunkManifest(settings("distinct"), changedSource, "source_hash").identityHash,
            "source ID change did not affect identity");

        Engine::TerrainCachedChunkPayload changedCoord = payload;
        changedCoord.chunkId.coord.x = 9;
        ctx.expect(base.identityHash != Engine::TerrainDerivedCache::buildChunkManifest(settings("distinct"), changedCoord, "source_hash").identityHash,
            "chunk coord change did not affect identity");

        Engine::TerrainCachedChunkPayload changedResolution = payload;
        changedResolution.resolution = 4;
        changedResolution.heights.assign(16, 1.0f);
        ctx.expect(base.identityHash != Engine::TerrainDerivedCache::buildChunkManifest(settings("distinct"), changedResolution, "source_hash").identityHash,
            "chunk resolution change did not affect identity");

        Engine::TerrainLodMeshBuildSettings lod;
        lod.lodIndex = 0;
        lod.renderResolution = 3;
        const Engine::TerrainDerivedCacheManifest lodBase = Engine::TerrainDerivedCache::buildLodMeshManifest(settings("distinct"), payload, lod, "source_hash");
        lod.lodIndex = 1;
        ctx.expect(lodBase.identityHash != Engine::TerrainDerivedCache::buildLodMeshManifest(settings("distinct"), payload, lod, "source_hash").identityHash,
            "LOD index change did not affect identity");

        Engine::TerrainDerivedCacheSettings versioned = settings("distinct");
        versioned.chunkPayloadVersion = "other";
        ctx.expect(base.identityHash != Engine::TerrainDerivedCache::buildChunkManifest(versioned, payload, "source_hash").identityHash,
            "payload version change did not affect identity");
    }

    void chunkPayloadBinaryRoundTrip(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload payload = chunkPayload();
        Engine::TerrainDerivedCacheManifest manifest = Engine::TerrainDerivedCache::buildChunkManifest(settings("chunk_roundtrip"), payload, "source_hash");
        clearRoot(manifest.settings.rootPath);
        const Engine::TerrainDerivedCacheWriteResult write = Engine::TerrainDerivedCache::writeChunk(manifest, payload);
        const Engine::TerrainDerivedCacheChunkReadResult read = Engine::TerrainDerivedCache::readChunk(manifest);
        ctx.expect(write.status == Engine::TerrainDerivedCacheStatus::WriteSuccess, "chunk write failed: " + write.message);
        ctx.expect(read.status == Engine::TerrainDerivedCacheStatus::Hit && read.payload.has_value(), "chunk read did not hit: " + read.message);
        if (read.payload) {
            ctx.expect(read.payload->chunkId == payload.chunkId, "chunk ID did not round-trip");
            ctx.expect(read.payload->heights == payload.heights, "chunk heights did not round-trip");
            ctx.expect(read.payload->importSettings == payload.importSettings, "chunk settings did not round-trip");
        }
    }

    void lodMeshPayloadBinaryRoundTrip(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload chunk = chunkPayload();
        Engine::TerrainLodMeshBuildSettings lod;
        lod.lodIndex = 2;
        lod.renderResolution = 4;
        lod.skirtDepth = 0.5f;
        const Engine::TerrainCachedLodMeshPayload payload = Engine::buildTerrainCachedLodMesh(chunk, lod);
        Engine::TerrainDerivedCacheManifest manifest = Engine::TerrainDerivedCache::buildLodMeshManifest(settings("lod_roundtrip"), chunk, lod, "source_hash");
        clearRoot(manifest.settings.rootPath);
        const Engine::TerrainDerivedCacheWriteResult write = Engine::TerrainDerivedCache::writeLodMesh(manifest, payload);
        const Engine::TerrainDerivedCacheLodMeshReadResult read = Engine::TerrainDerivedCache::readLodMesh(manifest);
        ctx.expect(write.status == Engine::TerrainDerivedCacheStatus::WriteSuccess, "LOD write failed: " + write.message);
        ctx.expect(read.status == Engine::TerrainDerivedCacheStatus::Hit && read.payload.has_value(), "LOD read did not hit: " + read.message);
        if (read.payload) {
            ctx.expect(read.payload->vertices.size() == payload.vertices.size(), "LOD vertices did not round-trip");
            ctx.expect(read.payload->indices == payload.indices, "LOD indices did not round-trip");
            ctx.expect(read.payload->lodIndex == 2 && read.payload->renderResolution == 4, "LOD metadata did not round-trip");
        }
    }

    void missingCacheReturnsMiss(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload payload = chunkPayload();
        const Engine::TerrainDerivedCacheManifest manifest = Engine::TerrainDerivedCache::buildChunkManifest(settings("missing"), payload, "source_hash");
        clearRoot(manifest.settings.rootPath);
        const Engine::TerrainDerivedCacheChunkReadResult read = Engine::TerrainDerivedCache::readChunk(manifest);
        ctx.expect(read.status == Engine::TerrainDerivedCacheStatus::Miss, "missing cache did not return miss");
    }

    void identityMismatchReturnsStale(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload payload = chunkPayload();
        Engine::TerrainDerivedCacheManifest manifest = Engine::TerrainDerivedCache::buildChunkManifest(settings("stale"), payload, "source_hash");
        clearRoot(manifest.settings.rootPath);
        (void)Engine::TerrainDerivedCache::writeChunk(manifest, payload);

        const std::filesystem::path manifestPath = Engine::TerrainDerivedCache::cacheRoot(manifest) / "manifest.yaml";
        YAML::Node node = YAML::LoadFile(manifestPath.string());
        node["identity_hash"] = "different";
        std::ofstream output(manifestPath, std::ios::trunc);
        output << node;
        output.close();

        const Engine::TerrainDerivedCacheChunkReadResult read = Engine::TerrainDerivedCache::readChunk(manifest);
        ctx.expect(read.status == Engine::TerrainDerivedCacheStatus::Stale, "identity mismatch did not return stale");
    }

    void badMagicAndTruncatedPayloadsAreReported(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload payload = chunkPayload();
        Engine::TerrainDerivedCacheManifest manifest = Engine::TerrainDerivedCache::buildChunkManifest(settings("corrupt"), payload, "source_hash");
        clearRoot(manifest.settings.rootPath);
        (void)Engine::TerrainDerivedCache::writeChunk(manifest, payload);

        {
            std::fstream file(Engine::TerrainDerivedCache::cacheRoot(manifest) / manifest.payloadFileName, std::ios::binary | std::ios::in | std::ios::out);
            file.put('X');
        }
        Engine::TerrainDerivedCacheChunkReadResult stale = Engine::TerrainDerivedCache::readChunk(manifest);
        ctx.expect(stale.status == Engine::TerrainDerivedCacheStatus::Stale, "bad magic did not return stale");

        (void)Engine::TerrainDerivedCache::writeChunk(manifest, payload);
        {
            std::ofstream file(Engine::TerrainDerivedCache::cacheRoot(manifest) / manifest.payloadFileName, std::ios::binary | std::ios::trunc);
            file.write("MTC1", 4);
        }
        Engine::TerrainDerivedCacheChunkReadResult corrupt = Engine::TerrainDerivedCache::readChunk(manifest);
        ctx.expect(corrupt.status == Engine::TerrainDerivedCacheStatus::Corrupt ||
                   corrupt.status == Engine::TerrainDerivedCacheStatus::Stale,
            "truncated payload did not report stale/corrupt");
    }

    void payloadHashMismatchReturnsStale(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload payload = chunkPayload();
        Engine::TerrainDerivedCacheManifest manifest = Engine::TerrainDerivedCache::buildChunkManifest(settings("hash_mismatch"), payload, "source_hash");
        clearRoot(manifest.settings.rootPath);
        (void)Engine::TerrainDerivedCache::writeChunk(manifest, payload);
        {
            std::ofstream file(Engine::TerrainDerivedCache::cacheRoot(manifest) / manifest.payloadFileName, std::ios::binary | std::ios::app);
            file.put('x');
        }

        const Engine::TerrainDerivedCacheChunkReadResult read = Engine::TerrainDerivedCache::readChunk(manifest);
        ctx.expect(read.status == Engine::TerrainDerivedCacheStatus::Stale, "payload hash mismatch did not return stale");
    }

    void writeCreatesManifestAndPayloadUnderDeterministicRoot(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload payload = chunkPayload();
        Engine::TerrainDerivedCacheManifest manifest = Engine::TerrainDerivedCache::buildChunkManifest(settings("paths"), payload, "source_hash");
        clearRoot(manifest.settings.rootPath);
        const Engine::TerrainDerivedCacheWriteResult write = Engine::TerrainDerivedCache::writeChunk(manifest, payload);
        const std::filesystem::path root = Engine::TerrainDerivedCache::cacheRoot(manifest);
        ctx.expect(write.status == Engine::TerrainDerivedCacheStatus::WriteSuccess, "write failed");
        ctx.expect(std::filesystem::is_regular_file(root / "manifest.yaml"), "manifest was not written");
        ctx.expect(std::filesystem::is_regular_file(root / manifest.payloadFileName), "payload was not written");
    }

    void statsCountOperations(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload payload = chunkPayload();
        Engine::TerrainDerivedCacheManifest manifest = Engine::TerrainDerivedCache::buildChunkManifest(settings("stats"), payload, "source_hash");
        clearRoot(manifest.settings.rootPath);
        Engine::TerrainDerivedCache cache;
        const Engine::TerrainDerivedCacheChunkReadResult miss = Engine::TerrainDerivedCache::readChunk(manifest);
        cache.recordResult(miss);
        const Engine::TerrainDerivedCacheWriteResult write = Engine::TerrainDerivedCache::writeChunk(manifest, payload);
        cache.recordResult(write);
        const Engine::TerrainDerivedCacheChunkReadResult hit = Engine::TerrainDerivedCache::readChunk(manifest);
        cache.recordResult(hit);
        const Engine::TerrainDerivedCacheStats& stats = cache.stats();
        ctx.expect(stats.misses == 1 && stats.writes == 1 && stats.hits == 1, "cache stats counts were wrong");
        ctx.expect(!stats.lastMessage.empty() && !stats.lastPath.empty(), "cache stats did not retain last message/path");
        ctx.expect(stats.bytesRead > 0 && stats.bytesWritten > 0, "cache byte stats were not recorded");
    }

    void asyncWrappersReturnPlainResults(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload payload = chunkPayload();
        Engine::TerrainDerivedCacheManifest manifest = Engine::TerrainDerivedCache::buildChunkManifest(settings("async"), payload, "source_hash");
        clearRoot(manifest.settings.rootPath);
        Engine::AsyncWorkQueue queue(1);
        (void)Engine::enqueueTerrainChunkCacheWrite(queue, manifest, payload);
        bool writeCompleted = false;
        for (int attempt = 0; attempt < 100 && !writeCompleted; ++attempt) {
            for (Engine::AsyncCompletedJob& completed : queue.pollCompleted()) {
                const auto result = std::any_cast<Engine::TerrainDerivedCacheWriteJobResult>(completed.result);
                writeCompleted = result.result.status == Engine::TerrainDerivedCacheStatus::WriteSuccess;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ctx.expect(writeCompleted, "async write did not complete successfully");

        (void)Engine::enqueueTerrainChunkCacheRead(queue, manifest);
        bool readCompleted = false;
        for (int attempt = 0; attempt < 100 && !readCompleted; ++attempt) {
            for (Engine::AsyncCompletedJob& completed : queue.pollCompleted()) {
                const auto result = std::any_cast<Engine::TerrainDerivedChunkCacheReadJobResult>(completed.result);
                readCompleted = result.result.status == Engine::TerrainDerivedCacheStatus::Hit && result.result.payload.has_value();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ctx.expect(readCompleted, "async read did not complete successfully");
    }

    void lodPayloadIsDeterministicAndRendererIndependent(TestContext& ctx)
    {
        const Engine::TerrainCachedChunkPayload chunk = chunkPayload();
        Engine::TerrainLodMeshBuildSettings lod;
        lod.renderResolution = 5;
        const Engine::TerrainCachedLodMeshPayload first = Engine::buildTerrainCachedLodMesh(chunk, lod);
        const Engine::TerrainCachedLodMeshPayload second = Engine::buildTerrainCachedLodMesh(chunk, lod);
        ctx.expect(!first.vertices.empty() && !first.indices.empty(), "LOD payload did not build geometry");
        ctx.expect(first.vertices.size() == second.vertices.size() && first.indices == second.indices, "LOD payload was not deterministic");
        ctx.expect(first.vertices.front().position == second.vertices.front().position, "LOD vertex positions were not deterministic");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"StableManifestIdentityForIdenticalInputs", stableManifestIdentityForIdenticalInputs},
        {"ManifestInputsProduceDistinctIdentities", manifestInputsProduceDistinctIdentities},
        {"ChunkPayloadBinaryRoundTrip", chunkPayloadBinaryRoundTrip},
        {"LodMeshPayloadBinaryRoundTrip", lodMeshPayloadBinaryRoundTrip},
        {"MissingCacheReturnsMiss", missingCacheReturnsMiss},
        {"IdentityMismatchReturnsStale", identityMismatchReturnsStale},
        {"BadMagicAndTruncatedPayloadsAreReported", badMagicAndTruncatedPayloadsAreReported},
        {"PayloadHashMismatchReturnsStale", payloadHashMismatchReturnsStale},
        {"WriteCreatesManifestAndPayloadUnderDeterministicRoot", writeCreatesManifestAndPayloadUnderDeterministicRoot},
        {"StatsCountOperations", statsCountOperations},
        {"AsyncWrappersReturnPlainResults", asyncWrappersReturnPlainResults},
        {"LodPayloadIsDeterministicAndRendererIndependent", lodPayloadIsDeterministicAndRendererIndependent},
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

    std::cout << "Terrain derived cache tests passed (" << tests.size() << " tests)\n";
    return 0;
}
