#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "Engine/TerrainDerivedCache.hpp"
#include "Engine/TerrainSerializationPrep.hpp"

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

    Engine::TerrainChunkStableIdentity identity(
        uint64_t source = 42,
        int32_t x = -3,
        int32_t z = 7)
    {
        Engine::TerrainChunkStableIdentity result;
        result.chunkId = {{source}, {x, z}};
        result.sourceType = Engine::TerrainDatasetSourceType::HeightmapImported;
        result.importSettings = {"heightmap_terrain", "1", "spacing_1_scale_2"};
        result.sourceRevision = "source_content_hash";
        result.materialRevision = "material_rules_v1";
        result.chunkResolution = 33;
        result.chunkSize = 64.0f;
        return result;
    }

    Engine::TerrainCachedChunkPayload cachedPayload(const Engine::TerrainSerializedChunkFileMetadata& metadata)
    {
        Engine::TerrainCachedChunkPayload payload;
        payload.chunkId = metadata.identity.chunkId;
        payload.origin = {
            static_cast<float>(metadata.identity.chunkId.coord.x) * metadata.identity.chunkSize,
            0.0f,
            static_cast<float>(metadata.identity.chunkId.coord.z) * metadata.identity.chunkSize,
        };
        payload.size = metadata.identity.chunkSize;
        payload.resolution = metadata.identity.chunkResolution;
        payload.heights.assign(
            static_cast<size_t>(payload.resolution) * payload.resolution,
            1.0f);
        payload.sourceType = metadata.identity.sourceType;
        payload.importSettings = metadata.identity.importSettings;
        return payload;
    }

    Engine::TerrainDerivedCacheSettings cacheSettings()
    {
        Engine::TerrainDerivedCacheSettings settings;
        settings.rootPath = "generated/terrain_serialization_prep_tests";
        return settings;
    }

    void stableIdentityIsDeterministic(TestContext& ctx)
    {
        const Engine::TerrainChunkStableIdentity base = identity();
        const std::string first = Engine::terrainChunkStableIdentityHash(base);
        const std::string second = Engine::terrainChunkStableIdentityHash(base);
        const std::string text = Engine::terrainChunkStableIdentityString(base);
        ctx.expect(first == second, "stable identity hash changed for identical inputs");
        ctx.expect(text.find("heightmap_terrain") != std::string::npos, "stable identity omitted import pipeline");
        ctx.expect(text.find("source_content_hash") != std::string::npos, "stable identity omitted source revision");
    }

    void runtimeHandlesDoNotAffectIdentity(TestContext& ctx)
    {
        const Engine::TerrainChunkStableIdentity base = identity();
        const Engine::TerrainSerializedChunkFileMetadata first =
            Engine::buildTerrainSerializedChunkFileMetadata(base);
        Engine::TerrainSourceHandle sourceHandleA{1, 4};
        Engine::TerrainSourceHandle sourceHandleB{99, 100};
        Engine::TerrainChunkHandle chunkHandleA{2, 5};
        Engine::TerrainChunkHandle chunkHandleB{88, 9};
        (void)sourceHandleA;
        (void)sourceHandleB;
        (void)chunkHandleA;
        (void)chunkHandleB;
        const Engine::TerrainSerializedChunkFileMetadata second =
            Engine::buildTerrainSerializedChunkFileMetadata(base);
        ctx.expect(first.identityHash == second.identityHash, "runtime handles affected durable terrain identity");
        ctx.expect(first.payloadFileName == second.payloadFileName, "runtime handles affected serialized file name");
    }

    void identityInputsInvalidate(TestContext& ctx)
    {
        const Engine::TerrainChunkStableIdentity baseIdentity = identity();
        const std::string base = Engine::terrainChunkStableIdentityHash(baseIdentity);

        Engine::TerrainChunkStableIdentity changed = baseIdentity;
        changed.chunkId.source = {77};
        ctx.expect(base != Engine::terrainChunkStableIdentityHash(changed), "source ID did not affect identity");

        changed = baseIdentity;
        changed.chunkId.coord.x += 1;
        ctx.expect(base != Engine::terrainChunkStableIdentityHash(changed), "chunk coord did not affect identity");

        changed = baseIdentity;
        changed.importSettings.optionsHash = "other";
        ctx.expect(base != Engine::terrainChunkStableIdentityHash(changed), "import settings did not affect identity");

        changed = baseIdentity;
        changed.sourceRevision = "other_source";
        ctx.expect(base != Engine::terrainChunkStableIdentityHash(changed), "source revision did not affect identity");

        changed = baseIdentity;
        changed.materialRevision = "material_rules_v2";
        ctx.expect(base != Engine::terrainChunkStableIdentityHash(changed), "material revision did not affect identity");

        changed = baseIdentity;
        changed.chunkResolution = 65;
        ctx.expect(base != Engine::terrainChunkStableIdentityHash(changed), "chunk resolution did not affect identity");

        changed = baseIdentity;
        changed.chunkSize = 128.0f;
        ctx.expect(base != Engine::terrainChunkStableIdentityHash(changed), "chunk size did not affect identity");
    }

    void metadataValidationRejectsInvalidContracts(TestContext& ctx)
    {
        Engine::TerrainSerializedChunkFileMetadata valid =
            Engine::buildTerrainSerializedChunkFileMetadata(identity());
        ctx.expect(Engine::validateTerrainSerializedChunkFileMetadata(valid).valid, "valid metadata failed validation");

        Engine::TerrainSerializedChunkFileMetadata invalid = valid;
        invalid.identity.chunkId.source = {};
        invalid.identityHash = Engine::terrainChunkStableIdentityHash(invalid.identity, invalid.schemaVersion);
        ctx.expect(!Engine::validateTerrainSerializedChunkFileMetadata(invalid).valid, "missing source ID was accepted");

        invalid = valid;
        invalid.identity.sourceRevision.clear();
        invalid.identityHash = Engine::terrainChunkStableIdentityHash(invalid.identity, invalid.schemaVersion);
        ctx.expect(!Engine::validateTerrainSerializedChunkFileMetadata(invalid).valid, "missing source revision was accepted");

        invalid = valid;
        invalid.boundary.storesLiveRuntimeHandles = true;
        ctx.expect(!Engine::validateTerrainSerializedChunkFileMetadata(invalid).valid, "runtime handles were accepted in payload boundary");
    }

    void payloadBoundariesSeparateSourceAndDerivedData(TestContext& ctx)
    {
        Engine::TerrainSerializedChunkPayloadBoundary boundary;
        boundary.storesAuthoritativeHeights = false;
        boundary.storesRendererLodMeshes = true;
        boundary.storesNavigationBuildData = true;
        const Engine::TerrainSerializedChunkFileMetadata metadata =
            Engine::buildTerrainSerializedChunkFileMetadata(
                identity(),
                boundary,
                Engine::TerrainSerializedChunkPayloadRole::DerivedCacheReference,
                "derived_reference_payload");
        const Engine::TerrainSerializationPrepValidation validation =
            Engine::validateTerrainSerializedChunkFileMetadata(metadata);
        ctx.expect(validation.valid, "derived boundary metadata should be valid");
        ctx.expect(!validation.warnings.empty(), "derived boundary did not report derived-data warning");
        ctx.expect(metadata.boundary.storesRendererLodMeshes, "renderer LOD boundary flag was lost");
        ctx.expect(!metadata.boundary.storesAuthoritativeHeights, "derived boundary became authoritative");
    }

    void derivedCacheIdentityUsesSerializationIdentity(TestContext& ctx)
    {
        const Engine::TerrainSerializedChunkFileMetadata metadata =
            Engine::buildTerrainSerializedChunkFileMetadata(identity());
        const Engine::TerrainCachedChunkPayload payload = cachedPayload(metadata);
        const Engine::TerrainDerivedCacheManifest first =
            Engine::TerrainDerivedCache::buildChunkManifest(
                cacheSettings(),
                payload,
                Engine::terrainDerivedCacheSourceHash(metadata));
        const Engine::TerrainDerivedCacheManifest second =
            Engine::TerrainDerivedCache::buildChunkManifest(
                cacheSettings(),
                payload,
                Engine::terrainDerivedCacheSourceHash(metadata));
        ctx.expect(first.identityHash == second.identityHash, "cache identity was not deterministic from serialization metadata");

        Engine::TerrainSerializedChunkFileMetadata changed = metadata;
        changed.identity.sourceRevision = "new_revision";
        changed.identityHash = Engine::terrainChunkStableIdentityHash(changed.identity, changed.schemaVersion);
        changed.payloadFileName = Engine::terrainSerializedChunkFileName(changed.identity, changed.identityHash);
        const Engine::TerrainCachedChunkPayload changedPayload = cachedPayload(changed);
        const Engine::TerrainDerivedCacheManifest changedManifest =
            Engine::TerrainDerivedCache::buildChunkManifest(
                cacheSettings(),
                changedPayload,
                Engine::terrainDerivedCacheSourceHash(changed));
        ctx.expect(first.identityHash != changedManifest.identityHash, "source revision did not invalidate derived cache identity");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests = {
        {"StableIdentityIsDeterministic", stableIdentityIsDeterministic},
        {"RuntimeHandlesDoNotAffectIdentity", runtimeHandlesDoNotAffectIdentity},
        {"IdentityInputsInvalidate", identityInputsInvalidate},
        {"MetadataValidationRejectsInvalidContracts", metadataValidationRejectsInvalidContracts},
        {"PayloadBoundariesSeparateSourceAndDerivedData", payloadBoundariesSeparateSourceAndDerivedData},
        {"DerivedCacheIdentityUsesSerializationIdentity", derivedCacheIdentityUsesSerializationIdentity},
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

    std::cout << "Terrain serialization prep tests passed (" << tests.size() << ")\n";
    return 0;
}
