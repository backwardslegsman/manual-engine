#include <any>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/FrameBudget.hpp"
#include "Engine/NavigationCache.hpp"
#include "Engine/OpenWorldStreaming.hpp"
#include "Engine/OpenWorldStreamingAssets.hpp"
#include "Engine/OpenWorldStreamingBake.hpp"
#include "Engine/TerrainDerivedCache.hpp"

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

    std::string readSourceFile(const char* relativePath)
    {
        std::ifstream input{std::string{MANUAL_ENGINE_SOURCE_DIR} + "/" + relativePath};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::filesystem::path tempPath(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / ("manual_engine_streaming_" + std::string{name});
    }

    void writeU32BE(std::vector<uint8_t>& output, uint32_t value)
    {
        output.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
        output.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
        output.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
        output.push_back(static_cast<uint8_t>(value & 0xffu));
    }

    void writeU16LE(std::vector<uint8_t>& output, uint16_t value)
    {
        output.push_back(static_cast<uint8_t>(value & 0xffu));
        output.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
    }

    uint32_t crc32(const uint8_t* data, size_t size)
    {
        uint32_t crc = 0xffffffffu;
        for (size_t index = 0; index < size; ++index) {
            crc ^= data[index];
            for (uint32_t bit = 0; bit < 8; ++bit) {
                crc = (crc & 1u) ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
            }
        }
        return crc ^ 0xffffffffu;
    }

    uint32_t adler32(const std::vector<uint8_t>& data)
    {
        constexpr uint32_t mod = 65521u;
        uint32_t a = 1u;
        uint32_t b = 0u;
        for (uint8_t byte : data) {
            a = (a + byte) % mod;
            b = (b + a) % mod;
        }
        return (b << 16u) | a;
    }

    void appendChunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& data)
    {
        writeU32BE(png, static_cast<uint32_t>(data.size()));
        const size_t typeOffset = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), data.begin(), data.end());
        writeU32BE(png, crc32(png.data() + typeOffset, png.size() - typeOffset));
    }

    std::vector<uint8_t> zlibStored(const std::vector<uint8_t>& raw)
    {
        std::vector<uint8_t> data{0x78u, 0x01u};
        size_t offset = 0;
        while (offset < raw.size()) {
            const size_t remaining = raw.size() - offset;
            const uint16_t blockSize = static_cast<uint16_t>(std::min<size_t>(remaining, 65535u));
            const bool finalBlock = offset + blockSize == raw.size();
            data.push_back(finalBlock ? 0x01u : 0x00u);
            writeU16LE(data, blockSize);
            writeU16LE(data, static_cast<uint16_t>(~blockSize));
            data.insert(data.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
            offset += blockSize;
        }
        writeU32BE(data, adler32(raw));
        return data;
    }

    std::filesystem::path writeGrayPng8(
        std::string_view name,
        uint32_t width,
        uint32_t height,
        const std::vector<uint8_t>& samples)
    {
        const std::filesystem::path path = tempPath(name);
        std::vector<uint8_t> png{0x89u, 'P', 'N', 'G', '\r', '\n', 0x1au, '\n'};
        std::vector<uint8_t> ihdr;
        writeU32BE(ihdr, width);
        writeU32BE(ihdr, height);
        ihdr.push_back(8u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        appendChunk(png, "IHDR", ihdr);
        std::vector<uint8_t> raw;
        for (uint32_t y = 0; y < height; ++y) {
            raw.push_back(0u);
            for (uint32_t x = 0; x < width; ++x) {
                raw.push_back(samples[static_cast<size_t>(y) * width + x]);
            }
        }
        appendChunk(png, "IDAT", zlibStored(raw));
        appendChunk(png, "IEND", {});
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        return path;
    }

    void EnumNamesAreStable(TestContext& ctx)
    {
        ctx.expect(Engine::StreamingResidencyStateCount == 8, "residency state count is stable");
        ctx.expect(Engine::StreamingTransitionLaneCount == 7, "transition lane count is stable");
        ctx.expect(Engine::StreamingPayloadKindCount == 7, "payload kind count is stable");

        ctx.expect(std::string{Engine::streamingResidencyStateName(Engine::StreamingResidencyState::ColdOnDisk)} == "ColdOnDisk", "ColdOnDisk name");
        ctx.expect(std::string{Engine::streamingResidencyStateName(Engine::StreamingResidencyState::ReadQueued)} == "ReadQueued", "ReadQueued name");
        ctx.expect(std::string{Engine::streamingResidencyStateName(Engine::StreamingResidencyState::CachedCpu)} == "CachedCpu", "CachedCpu name");
        ctx.expect(std::string{Engine::streamingResidencyStateName(Engine::StreamingResidencyState::PromoteQueued)} == "PromoteQueued", "PromoteQueued name");
        ctx.expect(std::string{Engine::streamingResidencyStateName(Engine::StreamingResidencyState::LiveActive)} == "LiveActive", "LiveActive name");
        ctx.expect(std::string{Engine::streamingResidencyStateName(Engine::StreamingResidencyState::DemoteQueued)} == "DemoteQueued", "DemoteQueued name");
        ctx.expect(std::string{Engine::streamingResidencyStateName(Engine::StreamingResidencyState::WriteQueued)} == "WriteQueued", "WriteQueued name");
        ctx.expect(std::string{Engine::streamingResidencyStateName(Engine::StreamingResidencyState::Failed)} == "Failed", "Failed name");

        ctx.expect(std::string{Engine::streamingTransitionLaneName(Engine::StreamingTransitionLane::DiskReadDecode)} == "DiskReadDecode", "DiskReadDecode name");
        ctx.expect(std::string{Engine::streamingTransitionLaneName(Engine::StreamingTransitionLane::DerivedGeneration)} == "DerivedGeneration", "DerivedGeneration name");
        ctx.expect(std::string{Engine::streamingTransitionLaneName(Engine::StreamingTransitionLane::AssetPreload)} == "AssetPreload", "AssetPreload name");
        ctx.expect(std::string{Engine::streamingTransitionLaneName(Engine::StreamingTransitionLane::MainThreadPromote)} == "MainThreadPromote", "MainThreadPromote name");
        ctx.expect(std::string{Engine::streamingTransitionLaneName(Engine::StreamingTransitionLane::MainThreadDemote)} == "MainThreadDemote", "MainThreadDemote name");
        ctx.expect(std::string{Engine::streamingTransitionLaneName(Engine::StreamingTransitionLane::DiskCacheWrite)} == "DiskCacheWrite", "DiskCacheWrite name");
        ctx.expect(std::string{Engine::streamingTransitionLaneName(Engine::StreamingTransitionLane::ManifestValidation)} == "ManifestValidation", "ManifestValidation name");

        ctx.expect(std::string{Engine::streamingPayloadKindName(Engine::StreamingPayloadKind::TerrainChunk)} == "TerrainChunk", "TerrainChunk name");
        ctx.expect(std::string{Engine::streamingPayloadKindName(Engine::StreamingPayloadKind::TerrainRenderLod)} == "TerrainRenderLod", "TerrainRenderLod name");
        ctx.expect(std::string{Engine::streamingPayloadKindName(Engine::StreamingPayloadKind::NavigationTile)} == "NavigationTile", "NavigationTile name");
        ctx.expect(std::string{Engine::streamingPayloadKindName(Engine::StreamingPayloadKind::PhysicsCollider)} == "PhysicsCollider", "PhysicsCollider name");
        ctx.expect(std::string{Engine::streamingPayloadKindName(Engine::StreamingPayloadKind::SceneChunk)} == "SceneChunk", "SceneChunk name");
        ctx.expect(std::string{Engine::streamingPayloadKindName(Engine::StreamingPayloadKind::AssetDependency)} == "AssetDependency", "AssetDependency name");
        ctx.expect(std::string{Engine::streamingPayloadKindName(Engine::StreamingPayloadKind::Unknown)} == "Unknown", "Unknown payload name");
    }

    void DefaultDiagnosticsAreZeroed(TestContext& ctx)
    {
        const Engine::OpenWorldStreamingDiagnostics diagnostics =
            Engine::makeEmptyOpenWorldStreamingDiagnostics();
        ctx.expect(!diagnostics.lastFailure.hasFailure, "empty diagnostics have no failure");
        ctx.expect(diagnostics.desiredChunksByState.size() == Engine::StreamingResidencyStateCount, "desired state array size");
        ctx.expect(diagnostics.actualChunksByState.size() == Engine::StreamingResidencyStateCount, "actual state array size");
        ctx.expect(diagnostics.transitionCountThisFrame.size() == Engine::StreamingTransitionLaneCount, "transition array size");
        ctx.expect(diagnostics.lanes.size() == Engine::StreamingTransitionLaneCount, "lane array size");
        ctx.expect(diagnostics.payloads.size() == Engine::StreamingPayloadKindCount, "payload array size");

        uint64_t total = 0;
        for (uint32_t value : diagnostics.desiredChunksByState) {
            total += value;
        }
        for (uint32_t value : diagnostics.actualChunksByState) {
            total += value;
        }
        for (const Engine::StreamingLaneDiagnostics& lane : diagnostics.lanes) {
            total += lane.queuedCount + lane.activeJobCount + lane.completedCount + lane.cancelledCount +
                lane.staleCount + lane.failedCount + lane.elapsedMicroseconds + lane.bytesRead + lane.bytesWritten;
        }
        for (const Engine::StreamingPayloadCacheDiagnostics& payload : diagnostics.payloads) {
            total += payload.hits + payload.misses + payload.stale + payload.corrupt + payload.writes;
        }
        total += diagnostics.mainThreadPromoteItemsRun + diagnostics.mainThreadPromoteItemsDeferred +
            diagnostics.mainThreadDemoteItemsRun + diagnostics.mainThreadDemoteItemsDeferred +
            diagnostics.estimatedResidentBytes + diagnostics.manifestRecordCount +
            diagnostics.manifestRecordsConsidered + diagnostics.manifestRecordsSkipped +
            diagnostics.transitionCandidateCount + diagnostics.transitionLimitedCount +
            diagnostics.hysteresisRetainedCount + diagnostics.invalidBoundsCount +
            diagnostics.hysteresisChurnCount + diagnostics.evictionBlockedCount +
            diagnostics.assetDependencyManifestCount + diagnostics.assetMetadataCacheHitCount +
            diagnostics.liveAssetMeshCount + diagnostics.liveAssetTextureCount +
            diagnostics.missingAssetDependencyCount + diagnostics.unsupportedAssetDependencyCount +
            diagnostics.sharedAssetReferenceCount + diagnostics.assetReleaseLatencyMicroseconds;
        for (uint32_t value : diagnostics.desiredChunksByPayload) {
            total += value;
        }
        ctx.expect(total == 0, "empty diagnostics counters are zero");
    }

    void DirtyFlagsCombineAndTest(TestContext& ctx)
    {
        Engine::StreamingDirtyFlags flags = Engine::StreamingDirtyFlags::SourceDirty |
            Engine::StreamingDirtyFlags::RuntimeDirty;
        ctx.expect(Engine::hasAny(flags, Engine::StreamingDirtyFlags::SourceDirty), "source dirty detected");
        ctx.expect(Engine::hasAny(flags, Engine::StreamingDirtyFlags::RuntimeDirty), "runtime dirty detected");
        ctx.expect(!Engine::hasAny(flags, Engine::StreamingDirtyFlags::DerivedDirty), "derived dirty absent");
        flags |= Engine::StreamingDirtyFlags::SaveDirty;
        ctx.expect(Engine::hasAny(flags, Engine::StreamingDirtyFlags::SaveDirty), "save dirty added");
    }

    void DiagnosticsArePlainEngineData(TestContext& ctx)
    {
        ctx.expect(std::is_copy_constructible_v<Engine::OpenWorldStreamingDiagnostics>, "diagnostics copy constructible");
        ctx.expect(std::is_copy_assignable_v<Engine::OpenWorldStreamingDiagnostics>, "diagnostics copy assignable");
        ctx.expect(std::is_move_constructible_v<Engine::OpenWorldStreamingDiagnostics>, "diagnostics move constructible");

        const std::string header = readSourceFile("src/Engine/OpenWorldStreaming.hpp");
        ctx.expect(!header.empty(), "streaming header readable");
        ctx.expect(header.find("Renderer") == std::string::npos, "header has no Renderer dependency");
        ctx.expect(header.find("bgfx") == std::string::npos, "header has no bgfx dependency");
        ctx.expect(header.find("Jolt") == std::string::npos, "header has no Jolt dependency");
        ctx.expect(header.find("Detour") == std::string::npos, "header has no Detour dependency");
        ctx.expect(header.find("Recast") == std::string::npos, "header has no Recast dependency");
        ctx.expect(header.find("SDL") == std::string::npos, "header has no SDL/App dependency");
        ctx.expect(header.find("SceneActorHandle") == std::string::npos, "header has no scene actor handles");
        ctx.expect(header.find("TerrainChunkHandle") == std::string::npos, "header has no terrain chunk handles");
        ctx.expect(header.find("NavigationTileHandle") == std::string::npos, "header has no navigation tile handles");
        ctx.expect(header.find("ScenePhysicsBodyHandle") == std::string::npos, "header has no physics body handles");
        ctx.expect(header.find("TextureHandle") == std::string::npos, "header has no renderer texture handles");
        ctx.expect(header.find("Renderer") == std::string::npos, "header still has no renderer types after S1");
        const std::string assetHeader = readSourceFile("src/Engine/OpenWorldStreamingAssets.hpp");
        ctx.expect(assetHeader.find("AssetCache") != std::string::npos, "asset streaming adapter owns AssetCache boundary");
        ctx.expect(assetHeader.find("AssetHandle") == std::string::npos, "asset streaming adapter avoids transient AssetHandle identity");
    }

    Engine::StreamingWorldBounds bounds(float minX, float minZ, float maxX, float maxZ)
    {
        return {{minX, 0.0f, minZ}, {maxX, 10.0f, maxZ}};
    }

    Engine::TerrainSourceChunkId terrainId(int32_t x, int32_t z)
    {
        return {Engine::AssetId{42}, {x, z}};
    }

    Engine::StreamingHaloPlannerSettings plannerSettings(float active, float cache, float hysteresis = 0.0f)
    {
        Engine::StreamingHaloPlannerSettings settings = Engine::defaultStreamingHaloPlannerSettings();
        for (Engine::StreamingPayloadResidencyPolicy& policy : settings.payloadPolicies) {
            policy.activeRadius = active;
            policy.cacheRadius = cache;
            policy.hysteresis = hysteresis;
            policy.maxTransitionsPerFrame = 64;
        }
        return settings;
    }

    void ManifestRecordsUseDurableIdentity(TestContext& ctx)
    {
        const Engine::TerrainSourceChunkId id = terrainId(3, -2);
        Engine::StreamingChunkManifestRecord terrain = Engine::makeTerrainStreamingManifestRecord(
            id,
            bounds(0.0f, 0.0f, 16.0f, 16.0f),
            11,
            22,
            4096,
            Engine::StreamingPayloadKind::TerrainChunk,
            "terrain");

        ctx.expect(terrain.key.kind == Engine::StreamingChunkKeyKind::TerrainSourceChunk, "terrain key kind");
        ctx.expect(terrain.key.terrainChunk == id, "terrain durable chunk id");
        ctx.expect(Engine::isValid(terrain.key), "terrain key valid");
        ctx.expect(terrain.availablePayloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)], "terrain payload available");
        ctx.expect(Engine::stableStreamingChunkKeyString(terrain.key) == "terrain:42:3:-2", "terrain stable key string");

        Engine::StreamingChunkManifestRecord scene = Engine::makeSceneStreamingManifestRecord(
            "sector/a",
            bounds(20.0f, 0.0f, 36.0f, 16.0f),
            33,
            44,
            1024,
            Engine::StreamingPayloadKind::SceneChunk,
            "scene");
        ctx.expect(scene.key.kind == Engine::StreamingChunkKeyKind::SceneChunk, "scene key kind");
        ctx.expect(scene.key.stableId == "sector/a", "scene stable id");
        ctx.expect(Engine::stableStreamingChunkKeyString(scene.key) == "scene:sector/a", "scene stable key string");

        Engine::StreamingChunkManifestRecord asset = Engine::makeAssetStreamingManifestRecord(
            Engine::AssetId{77},
            bounds(-10.0f, -10.0f, 10.0f, 10.0f),
            55,
            66,
            2048,
            "asset");
        ctx.expect(asset.key.kind == Engine::StreamingChunkKeyKind::AssetDependency, "asset key kind");
        ctx.expect(asset.key.asset == Engine::AssetId{77}, "asset stable id");
        ctx.expect(asset.payload == Engine::StreamingPayloadKind::AssetDependency, "asset payload kind");
        ctx.expect(Engine::hashStreamingManifestRecord(terrain) == Engine::hashStreamingManifestRecord(terrain), "manifest hash stable");
        ctx.expect(Engine::hashStreamingManifestRecord(terrain) != Engine::hashStreamingManifestRecord(scene), "manifest hash differs by identity");
    }

    void PlannerSelectsActiveCachedAndCold(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0), bounds(0.0f, 0.0f, 10.0f, 10.0f), 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(1, 0), bounds(30.0f, 0.0f, 40.0f, 10.0f), 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(2, 0), bounds(100.0f, 0.0f, 110.0f, 10.0f), 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));

        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(
            manifest,
            {0.0f, 0.0f, 0.0f},
            {},
            plannerSettings(15.0f, 50.0f));

        ctx.expect(plan.decisions.size() == 3, "three decisions");
        ctx.expect(plan.diagnostics.desiredChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::LiveActive)] == 1, "one active");
        ctx.expect(plan.diagnostics.desiredChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::CachedCpu)] == 1, "one cached");
        ctx.expect(plan.diagnostics.desiredChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::ColdOnDisk)] == 1, "one cold");
        ctx.expect(plan.decisions[0].desired == Engine::StreamingResidencyState::LiveActive, "active decisions sort first");
        ctx.expect(plan.decisions[1].desired == Engine::StreamingResidencyState::CachedCpu, "cached decisions sort second");
        ctx.expect(plan.decisions[2].desired == Engine::StreamingResidencyState::ColdOnDisk, "cold decisions sort last");
    }

    void HysteresisPreventsBoundaryChurn(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0), bounds(18.0f, 0.0f, 28.0f, 10.0f), 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));
        const std::vector<Engine::StreamingChunkResidencyInput> current{
            {manifest.records.front().key, Engine::StreamingPayloadKind::TerrainChunk, Engine::StreamingResidencyState::LiveActive},
        };

        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(
            manifest,
            {0.0f, 0.0f, 0.0f},
            current,
            plannerSettings(15.0f, 50.0f, 5.0f));
        ctx.expect(plan.decisions.size() == 1, "one hysteresis decision");
        ctx.expect(plan.decisions.front().desired == Engine::StreamingResidencyState::LiveActive, "active retained inside hysteresis");
        ctx.expect(plan.decisions.front().hysteresisRetained, "hysteresis retained flag");
        ctx.expect(plan.diagnostics.hysteresisRetainedCount == 1, "hysteresis diagnostic");
        ctx.expect(plan.diagnostics.transitionCandidateCount == 0, "no transition while retained");
    }

    void TransitionCapsLimitDeterministically(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        for (int32_t index = 0; index < 3; ++index) {
            manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
                terrainId(index, 0),
                bounds(static_cast<float>(index * 2), 0.0f, static_cast<float>(index * 2 + 1), 1.0f),
                1,
                1,
                1,
                Engine::StreamingPayloadKind::TerrainChunk));
        }
        Engine::StreamingHaloPlannerSettings settings = plannerSettings(20.0f, 40.0f);
        settings.payloadPolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)].maxTransitionsPerFrame = 1;

        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(
            manifest,
            {0.0f, 0.0f, 0.0f},
            {},
            settings);

        ctx.expect(plan.diagnostics.transitionCandidateCount == 3, "three transition candidates before cap");
        ctx.expect(plan.diagnostics.transitionLimitedCount == 2, "two transitions limited");
        ctx.expect(plan.diagnostics.desiredChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::LiveActive)] == 1, "only one active after cap");
        ctx.expect(plan.diagnostics.desiredChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::ColdOnDisk)] == 2, "limited transitions stay cold");
        ctx.expect(!plan.decisions[0].transitionLimited, "nearest transition allowed");
        ctx.expect(plan.decisions[1].transitionLimited && plan.decisions[2].transitionLimited, "later transitions limited deterministically");
    }

    void IndependentPayloadPolicies(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0), bounds(80.0f, 0.0f, 90.0f, 10.0f), 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0), bounds(80.0f, 0.0f, 90.0f, 10.0f), 1, 1, 1, Engine::StreamingPayloadKind::NavigationTile));

        Engine::StreamingHaloPlannerSettings settings = plannerSettings(10.0f, 20.0f);
        settings.payloadPolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)].activeRadius = 100.0f;
        settings.payloadPolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)].cacheRadius = 140.0f;

        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(
            manifest,
            {0.0f, 0.0f, 0.0f},
            {},
            settings);

        ctx.expect(plan.decisions.size() == 2, "two payload decisions");
        ctx.expect(plan.diagnostics.desiredChunksByPayload[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)] == 1, "terrain payload counted");
        ctx.expect(plan.diagnostics.desiredChunksByPayload[static_cast<uint32_t>(Engine::StreamingPayloadKind::NavigationTile)] == 1, "navigation payload counted");
        bool sawTerrainActive = false;
        bool sawNavigationCold = false;
        for (const Engine::StreamingChunkResidencyDecision& decision : plan.decisions) {
            sawTerrainActive |= decision.payload == Engine::StreamingPayloadKind::TerrainChunk &&
                decision.desired == Engine::StreamingResidencyState::LiveActive;
            sawNavigationCold |= decision.payload == Engine::StreamingPayloadKind::NavigationTile &&
                decision.desired == Engine::StreamingResidencyState::ColdOnDisk;
        }
        ctx.expect(sawTerrainActive, "terrain uses larger active radius");
        ctx.expect(sawNavigationCold, "navigation uses smaller cold radius");
    }

    void InvalidInputsAreSkipped(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0), {{10.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 1.0f}}, 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));
        manifest.records.push_back(Engine::makeSceneStreamingManifestRecord(
            "", bounds(0.0f, 0.0f, 1.0f, 1.0f), 1, 1, 1, Engine::StreamingPayloadKind::SceneChunk));
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(1, 0), bounds(0.0f, 0.0f, 1.0f, 1.0f), 1, 1, 1, Engine::StreamingPayloadKind::Unknown));

        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(
            manifest,
            {0.0f, 0.0f, 0.0f},
            {},
            plannerSettings(10.0f, 20.0f));
        ctx.expect(plan.decisions.empty(), "invalid records produce no decisions");
        ctx.expect(plan.diagnostics.manifestRecordsSkipped == 3, "three skipped records");
        ctx.expect(plan.diagnostics.invalidBoundsCount == 1, "one invalid bounds record");

        const Engine::StreamingHaloPlan badFocus = Engine::planStreamingHalo(
            manifest,
            {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
            {},
            plannerSettings(10.0f, 20.0f));
        ctx.expect(badFocus.decisions.empty(), "bad focus produces no decisions");
        ctx.expect(badFocus.diagnostics.invalidBoundsCount == manifest.records.size(), "bad focus marks manifest skipped");
    }

    void LargeManifestScanIsMetadataOnly(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.reserve(10000);
        for (int32_t index = 0; index < 10000; ++index) {
            manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
                terrainId(index, 0),
                bounds(static_cast<float>(index * 4), 0.0f, static_cast<float>(index * 4 + 2), 2.0f),
                1,
                1,
                64,
                Engine::StreamingPayloadKind::TerrainChunk));
        }

        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(
            manifest,
            {0.0f, 0.0f, 0.0f},
            {},
            plannerSettings(10.0f, 30.0f));
        ctx.expect(plan.diagnostics.manifestRecordCount == 10000, "large manifest counted");
        ctx.expect(plan.diagnostics.manifestRecordsConsidered == 10000, "large manifest considered");
        ctx.expect(plan.decisions.size() == 10000, "large manifest produces metadata decisions");
        ctx.expect(plan.diagnostics.desiredChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::LiveActive)] > 0, "large manifest active subset");
        ctx.expect(plan.diagnostics.desiredChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::ColdOnDisk)] > 0, "large manifest cold subset");
    }

    std::vector<Engine::AsyncCompletedJob> waitCompleted(Engine::AsyncWorkQueue& queue, uint32_t minimum = 1)
    {
        std::vector<Engine::AsyncCompletedJob> all;
        for (uint32_t attempt = 0; attempt < 200 && all.size() < minimum; ++attempt) {
            std::vector<Engine::AsyncCompletedJob> completed = queue.pollCompleted();
            all.insert(all.end(), std::make_move_iterator(completed.begin()), std::make_move_iterator(completed.end()));
            if (all.size() >= minimum) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return all;
    }

    Engine::StreamingHaloPlan cachePlanForRecords(const Engine::StreamingChunkManifest& manifest)
    {
        return Engine::planStreamingHalo(
            manifest,
            {0.0f, 0.0f, 0.0f},
            {},
            plannerSettings(20.0f, 40.0f));
    }

    void CacheHaloQueuesWarmDecisions(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0), bounds(0.0f, 0.0f, 1.0f, 1.0f), 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(1, 0), bounds(30.0f, 0.0f, 31.0f, 1.0f), 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(2, 0), bounds(100.0f, 0.0f, 101.0f, 1.0f), 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));

        Engine::StreamingReadDescriptorTable descriptors;
        for (const Engine::StreamingChunkManifestRecord& record : manifest.records) {
            Engine::setStreamingReadDescriptor(
                descriptors,
                record.key,
                record.payload,
                Engine::hashStreamingManifestRecord(record),
                Engine::fakeStreamingReadDescriptor(
                    Engine::StreamingReadStatus::Hit,
                    Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"cached"}}));
        }

        Engine::AsyncWorkQueue queue{1};
        Engine::OpenWorldStreamingCacheHalo halo;
        halo.update(queue, cachePlanForRecords(manifest), descriptors);
        Engine::StreamingCacheHaloSnapshot snapshot = halo.snapshot();
        ctx.expect(snapshot.diagnostics.pendingReadCount == 2, "active and cache decisions queued reads");
        ctx.expect(snapshot.diagnostics.transitionCountThisFrame[static_cast<uint32_t>(Engine::StreamingTransitionLane::DiskReadDecode)] == 2, "two disk read transitions queued");

        halo.update(queue, cachePlanForRecords(manifest), descriptors);
        snapshot = halo.snapshot();
        ctx.expect(snapshot.diagnostics.pendingReadCount == 2, "duplicate update does not duplicate pending reads");
        ctx.expect(snapshot.diagnostics.transitionCountThisFrame[static_cast<uint32_t>(Engine::StreamingTransitionLane::DiskReadDecode)] == 2, "duplicate update does not queue more reads");
        queue.shutdown();
    }

    void CompletedFakeHitBecomesCached(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0), bounds(0.0f, 0.0f, 1.0f, 1.0f), 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));
        Engine::StreamingTerrainChunkPayload terrain;
        terrain.chunkId = terrainId(0, 0);
        terrain.resolution = 2;
        terrain.heights = {1.0f, 2.0f, 3.0f, 4.0f};

        Engine::StreamingReadDescriptorTable descriptors;
        Engine::setStreamingReadDescriptor(
            descriptors,
            manifest.records.front().key,
            manifest.records.front().payload,
            Engine::hashStreamingManifestRecord(manifest.records.front()),
            Engine::fakeStreamingReadDescriptor(
                Engine::StreamingReadStatus::Hit,
                Engine::StreamingCachedPayload{terrain},
                terrain.heights.size() * sizeof(float),
                "terrain hit"));

        Engine::AsyncWorkQueue queue{1};
        Engine::OpenWorldStreamingCacheHalo halo;
        halo.update(queue, cachePlanForRecords(manifest), descriptors);
        halo.mergeCompleted(waitCompleted(queue));

        const std::optional<Engine::StreamingCachedPayload> payload =
            halo.cachedPayload(manifest.records.front().key, manifest.records.front().payload);
        ctx.expect(payload.has_value(), "cached payload stored");
        ctx.expect(payload && std::holds_alternative<Engine::StreamingTerrainChunkPayload>(*payload), "terrain payload type stored");
        ctx.expect(halo.snapshot().diagnostics.cachedCpuPayloadCount == 1, "cached payload counted");
        ctx.expect(halo.snapshot().diagnostics.payloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)].hits == 1, "terrain hit counted");
        queue.shutdown();
    }

    void CacheReadFailuresAreDiagnosed(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        std::vector<Engine::StreamingReadStatus> statuses{
            Engine::StreamingReadStatus::Miss,
            Engine::StreamingReadStatus::Stale,
            Engine::StreamingReadStatus::Corrupt,
            Engine::StreamingReadStatus::Unsupported,
            Engine::StreamingReadStatus::Failed,
        };
        for (int32_t index = 0; index < static_cast<int32_t>(statuses.size()); ++index) {
            manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
                terrainId(index, 0),
                bounds(static_cast<float>(index), 0.0f, static_cast<float>(index + 1), 1.0f),
                1,
                1,
                1,
                Engine::StreamingPayloadKind::TerrainChunk));
        }

        Engine::StreamingReadDescriptorTable descriptors;
        for (size_t index = 0; index < statuses.size(); ++index) {
            const Engine::StreamingChunkManifestRecord& record = manifest.records[index];
            Engine::setStreamingReadDescriptor(
                descriptors,
                record.key,
                record.payload,
                Engine::hashStreamingManifestRecord(record),
                Engine::fakeStreamingReadDescriptor(statuses[index], std::nullopt, 0, "expected failure"));
        }

        Engine::AsyncWorkQueue queue{1};
        Engine::OpenWorldStreamingCacheHalo halo;
        halo.update(queue, cachePlanForRecords(manifest), descriptors);
        halo.mergeCompleted(waitCompleted(queue, static_cast<uint32_t>(statuses.size())));
        const Engine::OpenWorldStreamingDiagnostics diagnostics = halo.snapshot().diagnostics;
        ctx.expect(diagnostics.actualChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::Failed)] == statuses.size(), "failed cache reads enter failed state");
        ctx.expect(diagnostics.payloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)].misses == 1, "miss counted");
        ctx.expect(diagnostics.payloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)].stale == 1, "stale counted");
        ctx.expect(diagnostics.payloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)].corrupt >= 2, "corrupt/unsupported counted");
        ctx.expect(diagnostics.lanes[static_cast<uint32_t>(Engine::StreamingTransitionLane::DiskReadDecode)].failedCount == 1, "failed job counted");
        ctx.expect(diagnostics.unsupportedReadCount == 1, "unsupported read counted");
        ctx.expect(diagnostics.lastFailure.hasFailure, "last failure captured");
        queue.shutdown();
    }

    void GenerationOnMissIsExplicitPolicy(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(7, 0),
            bounds(0.0f, 0.0f, 1.0f, 1.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainChunk));
        const Engine::StreamingChunkManifestRecord& record = manifest.records.front();
        const uint64_t recordHash = Engine::hashStreamingManifestRecord(record);
        const Engine::StreamingHaloPlan plan = cachePlanForRecords(manifest);

        Engine::StreamingGenerationDescriptorTable descriptors;
        Engine::StreamingGenerationDescriptor descriptor;
        descriptor.descriptor = Engine::metadataOnlyStreamingReadDescriptor("generated");
        descriptor.generate = [](const Engine::StreamingGenerationRequest& request, std::stop_token) {
            Engine::StreamingReadJobResult result;
            result.status = Engine::StreamingReadStatus::Hit;
            result.bytesRead = 16;
            result.message = "generated payload";
            result.payload = Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{
                Engine::stableStreamingChunkKeyString(request.key)}};
            return result;
        };
        Engine::setStreamingGenerationDescriptor(
            descriptors,
            record.key,
            record.payload,
            recordHash,
            std::move(descriptor));

        Engine::AsyncWorkQueue queue{1};
        Engine::OpenWorldStreamingDerivedGenerationHalo readOnly;
        readOnly.update(queue, plan, {}, descriptors);
        ctx.expect(readOnly.snapshot().diagnostics.generationQueuedCount == 0, "read-only generation policy does not queue work");

        Engine::StreamingGenerationSettings settings;
        settings.policy = Engine::StreamingDerivedGenerationPolicy::GenerateOnMiss;
        Engine::OpenWorldStreamingDerivedGenerationHalo generator{settings};
        generator.update(queue, plan, {}, descriptors);
        ctx.expect(generator.snapshot().diagnostics.generationQueuedCount == 1, "generate-on-miss queues worker generation");
        generator.mergeCompleted(waitCompleted(queue));
        const Engine::StreamingGenerationHaloSnapshot snapshot = generator.snapshot();
        ctx.expect(snapshot.diagnostics.generationCompletedCount == 1, "generation completion counted");
        ctx.expect(snapshot.diagnostics.cachedCpuPayloadCount == 1, "generated payload becomes cached CPU data");
        ctx.expect(snapshot.diagnostics.lanes[static_cast<uint32_t>(Engine::StreamingTransitionLane::DerivedGeneration)].bytesWritten == 16, "generation bytes counted");
        ctx.expect(generator.cachedPayload(record.key, record.payload).has_value(), "generated payload is queryable");
        queue.shutdown();
    }

    void LeavingCacheHaloCancelsAndEvicts(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0), bounds(0.0f, 0.0f, 1.0f, 1.0f), 1, 1, 1, Engine::StreamingPayloadKind::TerrainChunk));
        Engine::StreamingReadDescriptorTable descriptors;
        Engine::setStreamingReadDescriptor(
            descriptors,
            manifest.records.front().key,
            manifest.records.front().payload,
            Engine::hashStreamingManifestRecord(manifest.records.front()),
            Engine::fakeStreamingReadDescriptor(
                Engine::StreamingReadStatus::Hit,
                Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"cached"}}));

        Engine::AsyncWorkQueue queue{1};
        const Engine::AsyncJobHandle blocker = queue.submit("blocker", [](std::stop_token) -> std::any {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return std::string{"blocker"};
        });
        (void)blocker;

        Engine::OpenWorldStreamingCacheHalo halo;
        halo.update(queue, cachePlanForRecords(manifest), descriptors);
        ctx.expect(halo.snapshot().diagnostics.pendingReadCount == 1, "read pending before leaving halo");

        const Engine::StreamingHaloPlan coldPlan = Engine::planStreamingHalo(
            manifest,
            {200.0f, 0.0f, 0.0f},
            halo.snapshot().residency,
            plannerSettings(20.0f, 40.0f));
        halo.update(queue, coldPlan, descriptors);
        ctx.expect(halo.snapshot().diagnostics.actualChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::ColdOnDisk)] == 1, "record returns cold after leaving halo");
        ctx.expect(halo.snapshot().diagnostics.pendingReadCount == 0, "cancelled read no longer active");

        halo.mergeCompleted(waitCompleted(queue, 2));
        ctx.expect(halo.snapshot().diagnostics.staleReadCompletionCount >= 1, "cancelled read completion counted stale");
        queue.shutdown();
    }

    void QueueAndMergeCapsAreDeterministic(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        for (int32_t index = 0; index < 4; ++index) {
            manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
                terrainId(index, 0),
                bounds(static_cast<float>(index), 0.0f, static_cast<float>(index + 1), 1.0f),
                1,
                1,
                1,
                Engine::StreamingPayloadKind::TerrainChunk));
        }
        Engine::StreamingReadDescriptorTable descriptors;
        for (const Engine::StreamingChunkManifestRecord& record : manifest.records) {
            Engine::setStreamingReadDescriptor(
                descriptors,
                record.key,
                record.payload,
                Engine::hashStreamingManifestRecord(record),
                Engine::fakeStreamingReadDescriptor(
                    Engine::StreamingReadStatus::Hit,
                    Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"cached"}}));
        }

        Engine::StreamingCacheHaloSettings settings;
        settings.maxReadJobsQueuedPerUpdate = 2;
        settings.maxCompletedJobsMergedPerUpdate = 1;
        Engine::AsyncWorkQueue queue{1};
        Engine::OpenWorldStreamingCacheHalo halo{settings};
        halo.update(queue, cachePlanForRecords(manifest), descriptors);
        ctx.expect(halo.snapshot().diagnostics.pendingReadCount == 2, "queue cap limits first update");

        std::vector<Engine::AsyncCompletedJob> completed = waitCompleted(queue, 2);
        halo.mergeCompleted(completed);
        ctx.expect(halo.snapshot().diagnostics.cachedCpuPayloadCount == 1, "merge cap limits completions");
        halo.mergeCompleted(completed);
        ctx.expect(halo.snapshot().diagnostics.cachedCpuPayloadCount == 2, "second merge consumes the second pending completion");
        halo.mergeCompleted(completed);
        ctx.expect(halo.snapshot().diagnostics.cachedCpuPayloadCount == 2, "duplicate completed jobs are ignored after all pending records merge");
        queue.shutdown();
    }

    void DescriptorHelpersExposeCacheKinds(TestContext& ctx)
    {
        Engine::TerrainCachedChunkPayload payload;
        payload.chunkId = terrainId(0, 0);
        payload.origin = {0.0f, 0.0f, 0.0f};
        payload.size = 8.0f;
        payload.resolution = 2;
        payload.heights = {0.0f, 0.0f, 0.0f, 0.0f};
        Engine::TerrainDerivedCacheSettings terrainSettings;
        terrainSettings.rootPath = std::filesystem::temp_directory_path() / "manual_engine_streaming_s2_descriptor";
        const Engine::TerrainDerivedCacheManifest terrainManifest =
            Engine::TerrainDerivedCache::buildChunkManifest(terrainSettings, payload, "source");
        const Engine::StreamingReadDescriptor terrainDescriptor =
            Engine::terrainChunkCacheReadDescriptor(terrainManifest);
        ctx.expect(terrainDescriptor.kind == Engine::StreamingReadDescriptorKind::TerrainChunkCache, "terrain descriptor kind");

        Engine::NavigationCacheSettings navSettings;
        navSettings.rootPath = std::filesystem::temp_directory_path() / "manual_engine_streaming_s2_nav";
        const Engine::NavigationCacheManifest navManifest = Engine::NavigationCache::buildManifest(
            navSettings,
            8.0f,
            1,
            2,
            {},
            {},
            "test",
            {},
            {});
        const Engine::StreamingReadDescriptor navDescriptor =
            Engine::navigationTileCacheReadDescriptor(navSettings, navManifest, {0, 0});
        ctx.expect(navDescriptor.kind == Engine::StreamingReadDescriptorKind::NavigationTileCache, "navigation descriptor kind");

        const Engine::StreamingReadDescriptor metadata = Engine::metadataOnlyStreamingReadDescriptor("asset");
        ctx.expect(metadata.kind == Engine::StreamingReadDescriptorKind::MetadataOnly, "metadata descriptor kind");
        const Engine::StreamingReadDescriptor unsupported = Engine::unsupportedStreamingReadDescriptor("not yet");
        ctx.expect(unsupported.kind == Engine::StreamingReadDescriptorKind::Unsupported, "unsupported descriptor kind");
    }

    Engine::OpenWorldStreamingCacheHalo warmCacheForManifest(
        const Engine::StreamingChunkManifest& manifest,
        const Engine::StreamingCachedPayload& payload)
    {
        Engine::StreamingReadDescriptorTable descriptors;
        for (const Engine::StreamingChunkManifestRecord& record : manifest.records) {
            Engine::setStreamingReadDescriptor(
                descriptors,
                record.key,
                record.payload,
                Engine::hashStreamingManifestRecord(record),
                Engine::fakeStreamingReadDescriptor(Engine::StreamingReadStatus::Hit, payload));
        }

        Engine::AsyncWorkQueue queue{1};
        Engine::OpenWorldStreamingCacheHalo cache;
        cache.update(queue, cachePlanForRecords(manifest), descriptors);
        cache.mergeCompleted(waitCompleted(queue, static_cast<uint32_t>(manifest.records.size())));
        queue.shutdown();
        return cache;
    }

    Engine::StreamingPromotionCallbacks fakeLiveCallbacks(
        uint32_t& promoteCount,
        uint32_t& demoteCount,
        Engine::StreamingPromotionStatus promoteStatus = Engine::StreamingPromotionStatus::Success,
        Engine::StreamingPromotionStatus demoteStatus = Engine::StreamingPromotionStatus::Success)
    {
        Engine::StreamingPromotionCallbacks callbacks;
        callbacks.promote = [&promoteCount, promoteStatus](
            const Engine::StreamingPromotionRequest&,
            const Engine::StreamingCachedPayload&) {
            ++promoteCount;
            Engine::StreamingPromotionResult result;
            result.status = promoteStatus;
            result.liveToken = {1000 + promoteCount};
            result.liveResources.terrainRenderHandles = 1;
            result.estimatedLiveBytes = 64;
            result.message = "promoted";
            return result;
        };
        callbacks.demote = [&demoteCount, demoteStatus](
            const Engine::StreamingDemotionRequest&,
            Engine::StreamingRuntimeToken) {
            ++demoteCount;
            Engine::StreamingPromotionResult result;
            result.status = demoteStatus;
            result.message = "demoted";
            return result;
        };
        return callbacks;
    }

    void LiveHaloPromotesCachedPayloadUnderBudget(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0),
            bounds(0.0f, 0.0f, 1.0f, 1.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainChunk));
        Engine::OpenWorldStreamingCacheHalo cache = warmCacheForManifest(
            manifest,
            Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"cached"}});

        uint32_t promoteCount = 0;
        uint32_t demoteCount = 0;
        Engine::MainThreadWorkQueue work;
        Engine::OpenWorldStreamingLiveHalo live;
        live.update(work, cachePlanForRecords(manifest), cache, fakeLiveCallbacks(promoteCount, demoteCount));
        ctx.expect(live.snapshot().diagnostics.pendingPromoteCount == 1, "promotion pending before budget drain");
        ctx.expect(work.pendingCount(Engine::BudgetCategory::StreamingCommit) == 1, "one main-thread promote item queued");

        Engine::FrameBudget budget;
        budget.beginFrame({1000.0f, true});
        work.drain(budget);
        const Engine::StreamingLiveHaloSnapshot snapshot = live.snapshot();
        ctx.expect(promoteCount == 1, "promote callback ran once");
        ctx.expect(snapshot.diagnostics.livePayloadCount == 1, "one live payload");
        ctx.expect(snapshot.diagnostics.actualChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::LiveActive)] == 1, "payload became live");
        ctx.expect(snapshot.diagnostics.liveResources.terrainRenderHandles == 1, "live resource counts copied from callback");
        ctx.expect(snapshot.diagnostics.estimatedResidentBytes == 64, "estimated live bytes counted");
        ctx.expect(snapshot.diagnostics.mainThreadPromoteItemsRun == 1, "promote run counted");
    }

    void LiveHaloDemotesLivePayloadUnderBudget(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0),
            bounds(0.0f, 0.0f, 1.0f, 1.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainChunk));
        Engine::OpenWorldStreamingCacheHalo cache = warmCacheForManifest(
            manifest,
            Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"cached"}});

        uint32_t promoteCount = 0;
        uint32_t demoteCount = 0;
        Engine::MainThreadWorkQueue work;
        Engine::OpenWorldStreamingLiveHalo live;
        live.update(work, cachePlanForRecords(manifest), cache, fakeLiveCallbacks(promoteCount, demoteCount));
        Engine::FrameBudget budget;
        budget.beginFrame({1000.0f, true});
        work.drain(budget);

        const Engine::StreamingHaloPlan cachedPlan = Engine::planStreamingHalo(
            manifest,
            {30.0f, 0.0f, 0.0f},
            live.snapshot().residency,
            plannerSettings(20.0f, 40.0f));
        live.update(work, cachedPlan, cache, fakeLiveCallbacks(promoteCount, demoteCount));
        ctx.expect(live.snapshot().diagnostics.pendingDemoteCount == 1, "demotion pending before budget drain");
        budget.beginFrame({1000.0f, true});
        work.drain(budget);

        const Engine::StreamingLiveHaloSnapshot snapshot = live.snapshot();
        ctx.expect(demoteCount == 1, "demote callback ran once");
        ctx.expect(snapshot.diagnostics.livePayloadCount == 0, "live payload released");
        ctx.expect(snapshot.diagnostics.actualChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::CachedCpu)] == 1, "payload returned to cached CPU");
        ctx.expect(snapshot.diagnostics.mainThreadDemoteItemsRun == 1, "demote run counted");
    }

    void LiveHaloAvoidsDuplicateQueuedWork(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0),
            bounds(0.0f, 0.0f, 1.0f, 1.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainChunk));
        Engine::OpenWorldStreamingCacheHalo cache = warmCacheForManifest(
            manifest,
            Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"cached"}});

        uint32_t promoteCount = 0;
        uint32_t demoteCount = 0;
        Engine::MainThreadWorkQueue work;
        Engine::OpenWorldStreamingLiveHalo live;
        const Engine::StreamingHaloPlan plan = cachePlanForRecords(manifest);
        live.update(work, plan, cache, fakeLiveCallbacks(promoteCount, demoteCount));
        live.update(work, plan, cache, fakeLiveCallbacks(promoteCount, demoteCount));
        ctx.expect(live.snapshot().diagnostics.pendingPromoteCount == 1, "only one pending promote");
        ctx.expect(work.pendingCount(Engine::BudgetCategory::StreamingCommit) == 1, "duplicate update does not enqueue another work item");
    }

    void LiveHaloMissingPayloadAndCallbackFailureAreDiagnosed(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0),
            bounds(0.0f, 0.0f, 1.0f, 1.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainChunk));

        uint32_t promoteCount = 0;
        uint32_t demoteCount = 0;
        Engine::MainThreadWorkQueue work;
        Engine::OpenWorldStreamingCacheHalo emptyCache;
        Engine::OpenWorldStreamingLiveHalo missingLive;
        missingLive.update(work, cachePlanForRecords(manifest), emptyCache, fakeLiveCallbacks(promoteCount, demoteCount));
        ctx.expect(missingLive.snapshot().diagnostics.failedPromotionCount == 1, "missing cached payload counted as failed promotion");
        ctx.expect(missingLive.snapshot().diagnostics.lastFailure.hasFailure, "missing payload records last failure");

        Engine::OpenWorldStreamingCacheHalo cache = warmCacheForManifest(
            manifest,
            Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"cached"}});
        Engine::OpenWorldStreamingLiveHalo failingLive;
        failingLive.update(
            work,
            cachePlanForRecords(manifest),
            cache,
            fakeLiveCallbacks(
                promoteCount,
                demoteCount,
                Engine::StreamingPromotionStatus::CallbackFailed));
        Engine::FrameBudget budget;
        budget.beginFrame({1000.0f, true});
        work.drain(budget);
        ctx.expect(failingLive.snapshot().diagnostics.failedPromotionCount == 1, "callback failure counted");
        ctx.expect(failingLive.snapshot().diagnostics.actualChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::Failed)] == 1, "callback failure leaves record failed");
    }

    void LiveHaloQueueCapsAreDeterministic(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        for (int32_t index = 0; index < 3; ++index) {
            manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
                terrainId(index, 0),
                bounds(static_cast<float>(index), 0.0f, static_cast<float>(index + 1), 1.0f),
                1,
                1,
                1,
                Engine::StreamingPayloadKind::TerrainChunk));
        }
        Engine::OpenWorldStreamingCacheHalo cache = warmCacheForManifest(
            manifest,
            Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"cached"}});

        Engine::StreamingPromotionSettings settings;
        settings.maxPromotesQueuedPerUpdate = 1;
        uint32_t promoteCount = 0;
        uint32_t demoteCount = 0;
        Engine::MainThreadWorkQueue work;
        Engine::OpenWorldStreamingLiveHalo live{settings};
        live.update(work, cachePlanForRecords(manifest), cache, fakeLiveCallbacks(promoteCount, demoteCount));
        const Engine::OpenWorldStreamingDiagnostics diagnostics = live.snapshot().diagnostics;
        ctx.expect(diagnostics.pendingPromoteCount == 1, "promotion cap queues one item");
        ctx.expect(diagnostics.mainThreadPromoteItemsDeferred == 2, "promotion cap defers the rest");
        ctx.expect(work.pendingCount(Engine::BudgetCategory::StreamingCommit) == 1, "queue contains one capped item");
    }

    void LiveHaloStaleQueuedWorkIsIgnored(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        manifest.records.push_back(Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0),
            bounds(0.0f, 0.0f, 1.0f, 1.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainChunk));
        Engine::OpenWorldStreamingCacheHalo cache = warmCacheForManifest(
            manifest,
            Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"cached"}});

        uint32_t promoteCount = 0;
        uint32_t demoteCount = 0;
        Engine::MainThreadWorkQueue work;
        Engine::OpenWorldStreamingLiveHalo live;
        live.update(work, cachePlanForRecords(manifest), cache, fakeLiveCallbacks(promoteCount, demoteCount));
        const Engine::StreamingHaloPlan coldPlan = Engine::planStreamingHalo(
            manifest,
            {200.0f, 0.0f, 0.0f},
            live.snapshot().residency,
            plannerSettings(20.0f, 40.0f));
        live.update(work, coldPlan, cache, fakeLiveCallbacks(promoteCount, demoteCount));

        Engine::FrameBudget budget;
        budget.beginFrame({1000.0f, true});
        work.drain(budget);
        ctx.expect(promoteCount == 0, "stale promote callback not invoked");
        ctx.expect(live.snapshot().diagnostics.stalePromotionCompletionCount == 1, "stale queued promote counted");
        ctx.expect(live.snapshot().diagnostics.actualChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::ColdOnDisk)] == 1, "record remains cold");
    }

    Engine::StreamingAssetDependencyDescriptor assetDescriptor(
        Engine::AssetId id,
        Engine::AssetType type,
        std::string_view suffix,
        bool required = true)
    {
        Engine::StreamingAssetDependencyDescriptor descriptor;
        descriptor.asset = id;
        descriptor.type = type;
        descriptor.sourcePath = tempPath(suffix);
        descriptor.importSettings = {"streaming_asset", "s5", std::string{suffix}};
        descriptor.estimatedBytes = 128;
        descriptor.required = required;
        descriptor.debugName = std::string{suffix};
        if (type == Engine::AssetType::Texture) {
            Renderer::TextureDescriptor texture;
            texture.slot = Renderer::TextureSlot::BaseColor;
            texture.colorSpace = Renderer::TextureColorSpace::Srgb;
            texture.generateMips = true;
            descriptor.textureDescriptor = texture;
        }
        return descriptor;
    }

    void AssetDependencyRecordsUseDurableIdentity(TestContext& ctx)
    {
        const Engine::StreamingAssetDependencyDescriptor first =
            assetDescriptor({7101}, Engine::AssetType::Texture, "asset_a");
        Engine::StreamingAssetDependencyDescriptor second = first;
        second.importSettings.optionsHash = "different";

        const Engine::StreamingAssetDependencyBuildResult firstRecord =
            Engine::makeAssetDependencyStreamingRecord(first, bounds(0.0f, 0.0f, 1.0f, 1.0f));
        const Engine::StreamingAssetDependencyBuildResult secondRecord =
            Engine::makeAssetDependencyStreamingRecord(second, bounds(0.0f, 0.0f, 1.0f, 1.0f));

        ctx.expect(firstRecord.success && secondRecord.success, "asset dependency records built");
        ctx.expect(firstRecord.record.key.kind == Engine::StreamingChunkKeyKind::AssetDependency, "asset dependency key kind");
        ctx.expect(firstRecord.record.key.asset == first.asset, "asset id preserved in key");
        ctx.expect(firstRecord.record.payload == Engine::StreamingPayloadKind::AssetDependency, "asset dependency payload kind");
        ctx.expect(firstRecord.record.key != secondRecord.record.key, "import settings distinguish asset dependency keys");
        ctx.expect(firstRecord.readDescriptor.descriptor.kind == Engine::StreamingReadDescriptorKind::Fake, "asset metadata read descriptor is metadata/fake hit");

        const std::string header = readSourceFile("src/Engine/OpenWorldStreamingAssets.hpp");
        ctx.expect(header.find("AssetHandle") == std::string::npos, "asset streaming header does not expose AssetHandle");
        ctx.expect(header.find("CachedTexture") != std::string::npos, "asset streaming adapter owns runtime cache acquisitions");
    }

    void AssetMetadataCacheWarmsWithoutPromotion(TestContext& ctx)
    {
        const Engine::StreamingAssetDependencyDescriptor descriptor =
            assetDescriptor({7201}, Engine::AssetType::StaticMesh, "asset_metadata");
        const Engine::StreamingAssetDependencyBuildResult dependency =
            Engine::makeAssetDependencyStreamingRecord(descriptor, bounds(0.0f, 0.0f, 1.0f, 1.0f));
        Engine::StreamingChunkManifest manifest;
        Engine::StreamingReadDescriptorTable descriptors;
        Engine::addAssetDependencyStreamingRecord(manifest, descriptors, dependency);

        Engine::AsyncWorkQueue queue{1};
        Engine::OpenWorldStreamingCacheHalo cache;
        cache.update(queue, cachePlanForRecords(manifest), descriptors);
        cache.mergeCompleted(waitCompleted(queue));
        queue.shutdown();

        const Engine::StreamingCacheHaloSnapshot snapshot = cache.snapshot();
        ctx.expect(snapshot.diagnostics.cachedCpuPayloadCount == 1, "asset metadata became cached CPU payload");
        ctx.expect(snapshot.diagnostics.payloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::AssetDependency)].hits == 1, "asset metadata hit counted");
        const std::optional<Engine::StreamingCachedPayload> payload =
            cache.cachedPayload(dependency.record.key, Engine::StreamingPayloadKind::AssetDependency);
        ctx.expect(payload && std::holds_alternative<Engine::StreamingMetadataPayload>(*payload), "asset dependency stores metadata payload only");
    }

    void AssetStreamingPromotesAndDemotesThroughCallbacks(TestContext& ctx)
    {
        const Engine::StreamingAssetDependencyDescriptor descriptor =
            assetDescriptor({7301}, Engine::AssetType::Texture, "asset_live");
        const Engine::StreamingAssetDependencyBuildResult dependency =
            Engine::makeAssetDependencyStreamingRecord(descriptor, bounds(0.0f, 0.0f, 1.0f, 1.0f));
        Engine::StreamingChunkManifest manifest;
        Engine::StreamingReadDescriptorTable descriptors;
        Engine::addAssetDependencyStreamingRecord(manifest, descriptors, dependency);
        Engine::OpenWorldStreamingCacheHalo cache = warmCacheForManifest(
            manifest,
            Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"asset"}});

        uint32_t acquireTextureCount = 0;
        uint32_t releaseTextureCount = 0;
        Engine::StreamingAssetCacheCallbacks callbacks;
        callbacks.acquireTexture = [&acquireTextureCount](const std::filesystem::path&, const Renderer::TextureDescriptor&) {
            ++acquireTextureCount;
            return Engine::CachedTexture{acquireTextureCount, Renderer::createSolidTexture(255, 255, 255, 255)};
        };
        callbacks.releaseTexture = [&releaseTextureCount](Engine::CachedTexture) {
            ++releaseTextureCount;
        };

        Engine::OpenWorldStreamingAssetResidency assets{callbacks};
        ctx.expect(assets.registerDependency(descriptor), "registered texture dependency");
        Engine::OpenWorldStreamingLiveHalo live;
        Engine::MainThreadWorkQueue work;
        live.update(work, cachePlanForRecords(manifest), cache, Engine::makeAssetStreamingPromotionCallbacks(assets));
        Engine::FrameBudget budget;
        budget.beginFrame({1000.0f, true});
        work.drain(budget);
        ctx.expect(acquireTextureCount == 1, "texture acquired once during promotion");
        ctx.expect(live.snapshot().diagnostics.liveResources.textureHandles == 1, "live halo copied texture count");
        ctx.expect(assets.diagnostics().liveTextureCount == 1, "asset residency counted live texture");

        const Engine::StreamingHaloPlan coldPlan = Engine::planStreamingHalo(
            manifest,
            {500.0f, 0.0f, 0.0f},
            live.snapshot().residency,
            plannerSettings(1.0f, 2.0f));
        live.update(work, coldPlan, cache, Engine::makeAssetStreamingPromotionCallbacks(assets));
        budget.beginFrame({1000.0f, true});
        work.drain(budget);
        ctx.expect(releaseTextureCount == 1, "texture released once during demotion");
        ctx.expect(assets.diagnostics().liveTextureCount == 0, "asset residency cleared live texture count");
    }

    void AssetStreamingHandlesSharedMissingAndUnsupported(TestContext& ctx)
    {
        uint32_t acquireMeshCount = 0;
        uint32_t releaseMeshCount = 0;
        Engine::StreamingAssetCacheCallbacks callbacks;
        callbacks.acquireStaticMesh = [&acquireMeshCount](const std::filesystem::path&) {
            ++acquireMeshCount;
            return Engine::CachedStaticMesh{acquireMeshCount, Renderer::StaticMeshHandle{acquireMeshCount}};
        };
        callbacks.releaseStaticMesh = [&releaseMeshCount](Engine::CachedStaticMesh) {
            ++releaseMeshCount;
        };
        Engine::OpenWorldStreamingAssetResidency assets{callbacks};
        const Engine::StreamingAssetDependencyDescriptor mesh =
            assetDescriptor({7401}, Engine::AssetType::StaticMesh, "asset_shared");
        ctx.expect(assets.registerDependency(mesh), "registered shared mesh dependency");
        const Engine::StreamingAssetDependencyBuildResult meshRecord =
            Engine::makeAssetDependencyStreamingRecord(mesh, bounds(0.0f, 0.0f, 1.0f, 1.0f));
        Engine::StreamingPromotionRequest request;
        request.key = meshRecord.record.key;
        request.payload = Engine::StreamingPayloadKind::AssetDependency;
        const Engine::StreamingCachedPayload metadata{Engine::StreamingMetadataPayload{"asset"}};
        const Engine::StreamingPromotionResult first = assets.promotionCallbacks().promote(request, metadata);
        const Engine::StreamingPromotionResult second = assets.promotionCallbacks().promote(request, metadata);
        ctx.expect(first.status == Engine::StreamingPromotionStatus::Success, "first shared asset promotion succeeds");
        ctx.expect(second.status == Engine::StreamingPromotionStatus::Success, "second shared asset promotion succeeds");
        ctx.expect(acquireMeshCount == 1, "shared asset acquired only once");
        ctx.expect(assets.diagnostics().sharedReferenceCount == 1, "shared reference counted");
        Engine::StreamingDemotionRequest demote;
        demote.key = meshRecord.record.key;
        demote.payload = Engine::StreamingPayloadKind::AssetDependency;
        assets.promotionCallbacks().demote(demote, second.liveToken);
        ctx.expect(releaseMeshCount == 0, "first demote of shared asset keeps resource alive");
        assets.promotionCallbacks().demote(demote, first.liveToken);
        ctx.expect(releaseMeshCount == 1, "final shared demote releases resource");

        Engine::StreamingAssetCacheCallbacks missingCallbacks;
        missingCallbacks.acquireTexture = [](const std::filesystem::path&, const Renderer::TextureDescriptor&) {
            return Engine::CachedTexture{};
        };
        Engine::OpenWorldStreamingAssetResidency missing{missingCallbacks};
        const Engine::StreamingAssetDependencyDescriptor required =
            assetDescriptor({7402}, Engine::AssetType::Texture, "asset_required_missing", true);
        Engine::StreamingAssetDependencyDescriptor optional =
            assetDescriptor({7403}, Engine::AssetType::Texture, "asset_optional_missing", false);
        missing.registerDependency(required);
        missing.registerDependency(optional);
        const Engine::StreamingPromotionResult missingRequired = missing.promotionCallbacks().promote(
            {Engine::makeAssetDependencyStreamingRecord(required).record.key, Engine::StreamingPayloadKind::AssetDependency},
            metadata);
        const Engine::StreamingPromotionResult missingOptional = missing.promotionCallbacks().promote(
            {Engine::makeAssetDependencyStreamingRecord(optional).record.key, Engine::StreamingPayloadKind::AssetDependency},
            metadata);
        ctx.expect(missingRequired.status != Engine::StreamingPromotionStatus::Success, "missing required asset fails promotion");
        ctx.expect(missingOptional.status == Engine::StreamingPromotionStatus::Success, "missing optional asset promotes metadata only");
        ctx.expect(missing.diagnostics().missingRequiredCount == 1, "missing required counted");
        ctx.expect(missing.diagnostics().missingOptionalCount == 1, "missing optional counted");

        Engine::OpenWorldStreamingAssetResidency unsupported{callbacks};
        const Engine::StreamingAssetDependencyDescriptor material =
            assetDescriptor({7404}, Engine::AssetType::Material, "asset_material");
        unsupported.registerDependency(material);
        const Engine::StreamingPromotionResult unsupportedResult = unsupported.promotionCallbacks().promote(
            {Engine::makeAssetDependencyStreamingRecord(material).record.key, Engine::StreamingPayloadKind::AssetDependency},
            metadata);
        ctx.expect(unsupportedResult.status == Engine::StreamingPromotionStatus::UnsupportedPayload, "unsupported asset type fails explicitly");
        ctx.expect(unsupported.diagnostics().unsupportedTypeCount == 1, "unsupported asset type counted");
    }

    Engine::OpenWorldStreamingBakeSettings bakeSettings(std::string_view suffix)
    {
        Engine::OpenWorldStreamingBakeSettings settings;
        settings.heightmap.sourcePath = writeGrayPng8(
            suffix,
            5,
            5,
            {
                64, 64, 64, 64, 64,
                64, 64, 64, 64, 64,
                64, 64, 64, 64, 64,
                64, 64, 64, 64, 64,
                64, 64, 64, 64, 64,
            });
        settings.heightmap.sourceIdOverride = Engine::AssetId{9001};
        settings.heightmap.sampleSpacing = 8.0f;
        settings.heightmap.heightScale = 2.0f;
        settings.heightmap.chunkWorldSize = 16.0f;
        settings.heightmap.chunkResolution = 3;
        settings.terrainCache.rootPath = tempPath(std::string{suffix} + "_terrain_cache");
        settings.navigationCache.rootPath = tempPath(std::string{suffix} + "_nav_cache");
        settings.navigationCache.worldId = "streaming_s4_test";
        settings.navigationResolution = 3;
        settings.renderLods.push_back({0, 3, 0.25f});
        settings.physicsColliderResolution = 3;
        return settings;
    }

    void FullHeightmapBakeWritesAllPayloads(TestContext& ctx)
    {
        Engine::OpenWorldStreamingBakeSettings settings = bakeSettings("s4_full_bake.png");
        const Engine::OpenWorldStreamingBakeManifest bake = Engine::bakeOpenWorldHeightmap(settings);
        ctx.expect(bake.diagnostics.success, "full bake succeeds: " + bake.diagnostics.message);
        ctx.expect(bake.diagnostics.importedChunkCount == 4, "5x5 heightmap with 3x3 chunks imports four chunks, got " + std::to_string(bake.diagnostics.importedChunkCount));
        ctx.expect(bake.diagnostics.terrainChunkWrites == 4, "one terrain chunk payload per imported chunk, got " + std::to_string(bake.diagnostics.terrainChunkWrites));
        ctx.expect(bake.diagnostics.renderLodWrites == 4, "one render LOD payload per imported chunk, got " + std::to_string(bake.diagnostics.renderLodWrites));
        ctx.expect(bake.diagnostics.navigationTileWrites == 4, "one nav tile payload per imported chunk, got " + std::to_string(bake.diagnostics.navigationTileWrites));
        ctx.expect(bake.diagnostics.physicsColliderWrites == 4, "one physics collider payload per imported chunk, got " + std::to_string(bake.diagnostics.physicsColliderWrites));
        ctx.expect(bake.streamingManifest.records.size() == 16, "four payload records per chunk");
        ctx.expect(bake.readDescriptors.entries.size() == 16, "read descriptor for each baked payload");
        ctx.expect(!bake.sourceHash.empty() && bake.sourceHash != "missing", "source hash recorded");

        const Engine::OpenWorldStreamingBakeManifest second = Engine::bakeOpenWorldHeightmap(settings);
        ctx.expect(second.diagnostics.success, "repeat bake succeeds");
        ctx.expect(second.chunks.size() == bake.chunks.size(), "repeat bake chunk count stable");
        ctx.expect(!second.chunks.empty() &&
                second.chunks.front().terrainChunkManifest.identityHash ==
                    bake.chunks.front().terrainChunkManifest.identityHash,
            "repeat bake terrain identity stable");
    }

    void BakedPayloadsReadThroughCacheHalo(TestContext& ctx)
    {
        Engine::OpenWorldStreamingBakeSettings settings = bakeSettings("s4_cache_halo.png");
        const Engine::OpenWorldStreamingBakeManifest bake = Engine::bakeOpenWorldHeightmap(settings);
        ctx.expect(bake.diagnostics.success, "bake succeeds before readback: " + bake.diagnostics.message);

        Engine::StreamingHaloPlannerSettings planner = plannerSettings(100.0f, 120.0f);
        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(
            bake.streamingManifest,
            {0.0f, 0.0f, 0.0f},
            {},
            planner);
        Engine::AsyncWorkQueue queue{2};
        Engine::OpenWorldStreamingCacheHalo halo;
        halo.update(queue, plan, bake.readDescriptors);
        halo.mergeCompleted(waitCompleted(queue, static_cast<uint32_t>(bake.readDescriptors.entries.size())));
        queue.shutdown();

        const Engine::StreamingCacheHaloSnapshot snapshot = halo.snapshot();
        ctx.expect(snapshot.diagnostics.cachedCpuPayloadCount == bake.readDescriptors.entries.size(), "all baked payloads entered CPU cache");
        ctx.expect(snapshot.diagnostics.payloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)].hits == 4, "terrain chunk hits counted");
        ctx.expect(snapshot.diagnostics.payloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainRenderLod)].hits == 4, "LOD hits counted");
        ctx.expect(snapshot.diagnostics.payloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::NavigationTile)].hits == 4, "navigation hits counted");
        ctx.expect(snapshot.diagnostics.payloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::PhysicsCollider)].hits == 4, "physics collider hits counted");

        bool sawLodPayload = false;
        bool sawPhysicsPayload = false;
        for (const Engine::StreamingReadDescriptorEntry& entry : bake.readDescriptors.entries) {
            const std::optional<Engine::StreamingCachedPayload> payload = halo.cachedPayload(entry.key, entry.payload);
            sawLodPayload |= payload && std::holds_alternative<Engine::StreamingTerrainLodMeshPayload>(*payload);
            sawPhysicsPayload |= payload && std::holds_alternative<Engine::StreamingTerrainPhysicsColliderPayload>(*payload);
        }
        ctx.expect(sawLodPayload, "S2 reads baked render LOD payloads");
        ctx.expect(sawPhysicsPayload, "S2 reads baked physics collider payloads");
    }

    void BakeIdentityChangesWithSettings(TestContext& ctx)
    {
        Engine::OpenWorldStreamingBakeSettings base = bakeSettings("s4_identity.png");
        Engine::OpenWorldStreamingBakeSettings changed = base;
        changed.heightmap.chunkResolution = 5;
        changed.renderLods.front().renderResolution = 5;
        changed.physicsColliderResolution = 5;
        changed.sceneGeometryMaxSlopeDegrees = 30.0f;

        const Engine::OpenWorldStreamingBakeManifest first = Engine::bakeOpenWorldHeightmap(base);
        const Engine::OpenWorldStreamingBakeManifest second = Engine::bakeOpenWorldHeightmap(changed);
        ctx.expect(first.diagnostics.success, "base bake succeeds");
        ctx.expect(second.diagnostics.success, "changed bake succeeds");
        ctx.expect(!first.chunks.empty() && !second.chunks.empty(), "both bakes produced chunks");
        ctx.expect(first.chunks.front().terrainChunkManifest.identityHash !=
                second.chunks.front().terrainChunkManifest.identityHash,
            "chunk resolution changes terrain cache identity");
        ctx.expect(first.chunks.front().renderLodManifests.front().identityHash !=
                second.chunks.front().renderLodManifests.front().identityHash,
            "render LOD settings change LOD cache identity");
        ctx.expect(first.chunks.front().navigationManifest.identityHash !=
                second.chunks.front().navigationManifest.identityHash,
            "scene slope/nav settings change navigation cache identity");
        ctx.expect(first.chunks.front().physicsColliderManifest.identityHash !=
                second.chunks.front().physicsColliderManifest.identityHash,
            "physics collider settings change physics cache identity");
    }

    void VariantKeysSeparatePayloadInstances(TestContext& ctx)
    {
        Engine::StreamingChunkManifestRecord low = Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0),
            bounds(0.0f, 0.0f, 8.0f, 8.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainRenderLod,
            "low");
        low.key.variantId = "terrain_lod_0";
        low.haloProfile = Engine::StreamingHaloProfile::LowDetailLive;
        low.detailLevel = 0;

        Engine::StreamingChunkManifestRecord high = low;
        high.key.variantId = "terrain_lod_2";
        high.haloProfile = Engine::StreamingHaloProfile::HighDetailLive;
        high.detailLevel = 2;
        high.debugName = "high";

        ctx.expect(low.key != high.key, "variant IDs distinguish otherwise identical keys");
        ctx.expect(Engine::hashStreamingManifestRecord(low) != Engine::hashStreamingManifestRecord(high), "variant IDs affect manifest hash");
        ctx.expect(Engine::stableStreamingChunkKeyString(low.key).find("terrain_lod_0") != std::string::npos, "variant appears in stable key string");

        Engine::StreamingReadDescriptorTable descriptors;
        Engine::setStreamingReadDescriptor(
            descriptors,
            low.key,
            low.payload,
            Engine::hashStreamingManifestRecord(low),
            Engine::fakeStreamingReadDescriptor(
                Engine::StreamingReadStatus::Hit,
                Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"low"}}));
        Engine::setStreamingReadDescriptor(
            descriptors,
            high.key,
            high.payload,
            Engine::hashStreamingManifestRecord(high),
            Engine::fakeStreamingReadDescriptor(
                Engine::StreamingReadStatus::Hit,
                Engine::StreamingCachedPayload{Engine::StreamingMetadataPayload{"high"}}));

        ctx.expect(Engine::findStreamingReadDescriptor(descriptors, low.key, low.payload) != nullptr, "low variant descriptor found");
        ctx.expect(Engine::findStreamingReadDescriptor(descriptors, high.key, high.payload) != nullptr, "high variant descriptor found");
    }

    void LodProfilesProduceIndependentDecisions(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        Engine::StreamingChunkManifestRecord low = Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0),
            bounds(70.0f, 0.0f, 78.0f, 8.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainRenderLod);
        low.key.variantId = "terrain_lod_low";
        low.haloProfile = Engine::StreamingHaloProfile::LowDetailLive;
        manifest.records.push_back(low);

        Engine::StreamingChunkManifestRecord high = low;
        high.key.variantId = "terrain_lod_high";
        high.haloProfile = Engine::StreamingHaloProfile::HighDetailLive;
        high.detailLevel = 2;
        manifest.records.push_back(high);

        Engine::StreamingHaloPlannerSettings settings = Engine::defaultStreamingHaloPlannerSettings();
        settings.profilePolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainRenderLod)]
            [static_cast<uint32_t>(Engine::StreamingHaloProfile::LowDetailLive)].activeRadius = 96.0f;
        settings.profilePolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainRenderLod)]
            [static_cast<uint32_t>(Engine::StreamingHaloProfile::LowDetailLive)].cacheRadius = 128.0f;
        settings.profilePolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainRenderLod)]
            [static_cast<uint32_t>(Engine::StreamingHaloProfile::HighDetailLive)].activeRadius = 24.0f;
        settings.profilePolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainRenderLod)]
            [static_cast<uint32_t>(Engine::StreamingHaloProfile::HighDetailLive)].cacheRadius = 48.0f;

        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(
            manifest,
            {0.0f, 0.0f, 0.0f},
            {},
            settings);

        bool lowLive = false;
        bool highCold = false;
        for (const Engine::StreamingChunkResidencyDecision& decision : plan.decisions) {
            lowLive |= decision.key.variantId == "terrain_lod_low" &&
                decision.desired == Engine::StreamingResidencyState::LiveActive;
            highCold |= decision.key.variantId == "terrain_lod_high" &&
                decision.desired == Engine::StreamingResidencyState::ColdOnDisk;
        }
        ctx.expect(lowLive, "low-detail LOD live at broader radius");
        ctx.expect(highCold, "high-detail LOD remains cold outside inner halo");
        ctx.expect(plan.diagnostics.variantRecordCount == 2, "two variants counted");
        ctx.expect(plan.diagnostics.highDetailCandidateCount == 1, "high-detail candidate counted");
    }

    void FarMetadataAndCacheOnlyProfilesStayCached(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        Engine::StreamingChunkManifestRecord metadata = Engine::makeSceneStreamingManifestRecord(
            "sector/far",
            bounds(220.0f, 0.0f, 230.0f, 8.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::SceneChunk);
        metadata.key.variantId = "scene_metadata";
        metadata.haloProfile = Engine::StreamingHaloProfile::FarMetadata;
        manifest.records.push_back(metadata);

        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(
            manifest,
            {0.0f, 0.0f, 0.0f},
            {},
            Engine::defaultStreamingHaloPlannerSettings());

        ctx.expect(plan.decisions.size() == 1, "metadata decision emitted");
        ctx.expect(plan.decisions.front().desired == Engine::StreamingResidencyState::CachedCpu, "far metadata caches without live promotion");
        ctx.expect(plan.diagnostics.desiredChunksByProfile[static_cast<uint32_t>(Engine::StreamingHaloProfile::FarMetadata)] == 1, "far metadata profile counted");
    }

    void ProfileTransitionCapsDoNotBlockStandardLive(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        for (int index = 0; index < 2; ++index) {
            Engine::StreamingChunkManifestRecord high = Engine::makeTerrainStreamingManifestRecord(
                terrainId(index, 0),
                bounds(static_cast<float>(index * 2), 0.0f, static_cast<float>(index * 2 + 1), 1.0f),
                1,
                1,
                1,
                Engine::StreamingPayloadKind::TerrainRenderLod);
            high.key.variantId = "terrain_lod_high_" + std::to_string(index);
            high.haloProfile = Engine::StreamingHaloProfile::HighDetailLive;
            manifest.records.push_back(high);
        }
        Engine::StreamingChunkManifestRecord standard = Engine::makeTerrainStreamingManifestRecord(
            terrainId(99, 0),
            bounds(0.0f, 4.0f, 1.0f, 5.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainRenderLod);
        standard.key.variantId = "terrain_lod_standard";
        standard.haloProfile = Engine::StreamingHaloProfile::StandardLive;
        manifest.records.push_back(standard);

        Engine::StreamingHaloPlannerSettings settings = Engine::defaultStreamingHaloPlannerSettings();
        auto& highPolicy = settings.profilePolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainRenderLod)]
            [static_cast<uint32_t>(Engine::StreamingHaloProfile::HighDetailLive)];
        highPolicy.activeRadius = 20.0f;
        highPolicy.cacheRadius = 30.0f;
        highPolicy.maxTransitionsPerFrame = 1;
        settings.profilePolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainRenderLod)]
            [static_cast<uint32_t>(Engine::StreamingHaloProfile::StandardLive)].activeRadius = 20.0f;

        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(
            manifest,
            {0.0f, 0.0f, 0.0f},
            {},
            settings);

        ctx.expect(plan.diagnostics.transitionLimitedByProfile[static_cast<uint32_t>(Engine::StreamingHaloProfile::HighDetailLive)] == 1, "only high-detail profile is capped");
        bool standardLive = false;
        for (const Engine::StreamingChunkResidencyDecision& decision : plan.decisions) {
            standardLive |= decision.key.variantId == "terrain_lod_standard" &&
                decision.desired == Engine::StreamingResidencyState::LiveActive &&
                !decision.transitionLimited;
        }
        ctx.expect(standardLive, "standard live transition not blocked by high-detail cap");
    }

    void PredictivePrefetchIsCacheOnlyAndLowerPriority(TestContext& ctx)
    {
        Engine::StreamingChunkManifest manifest;
        Engine::StreamingChunkManifestRecord active = Engine::makeTerrainStreamingManifestRecord(
            terrainId(0, 0),
            bounds(0.0f, 0.0f, 4.0f, 4.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainChunk);
        manifest.records.push_back(active);
        Engine::StreamingChunkManifestRecord predicted = Engine::makeTerrainStreamingManifestRecord(
            terrainId(1, 0),
            bounds(80.0f, 0.0f, 84.0f, 4.0f),
            1,
            1,
            1,
            Engine::StreamingPayloadKind::TerrainChunk);
        manifest.records.push_back(predicted);

        Engine::StreamingHaloPlannerSettings settings = plannerSettings(8.0f, 16.0f);
        settings.payloadPolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)].cacheRadius = 48.0f;
        settings.payloadPolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::TerrainChunk)].maxTransitionsPerFrame = 1;

        Engine::StreamingFocusInput focus;
        focus.position = {0.0f, 0.0f, 0.0f};
        focus.velocity = glm::vec3{40.0f, 0.0f, 0.0f};
        focus.predictionSeconds = 2.0f;
        focus.maxPredictionDistance = 96.0f;
        focus.goalFocusPoints.push_back({82.0f, 0.0f, 2.0f});

        const Engine::StreamingHaloPlan plan = Engine::planStreamingHalo(manifest, focus, {}, settings);
        ctx.expect(plan.diagnostics.activeFocusCandidateCount == 1, "active focus candidate counted");
        ctx.expect(plan.diagnostics.predictiveCandidateCount == 1, "predictive candidate counted");
        ctx.expect(plan.diagnostics.predictivePrefetchCount == 1, "predictive prefetch counted");
        ctx.expect(plan.decisions.front().desired == Engine::StreamingResidencyState::LiveActive, "active live decision sorted first");
        bool predictedLimitedToCold = false;
        for (const Engine::StreamingChunkResidencyDecision& decision : plan.decisions) {
            predictedLimitedToCold |= decision.key.terrainChunk.coord.x == 1 &&
                decision.predictiveCandidate &&
                decision.transitionLimited &&
                decision.desired == Engine::StreamingResidencyState::ColdOnDisk;
        }
        ctx.expect(predictedLimitedToCold, "predictive cache transition is capped after active live work");
    }

    void DebugUiHeaderHasStreamingPlaceholder(TestContext& ctx)
    {
        const std::string header = readSourceFile("src/Renderer/DebugUi.hpp");
        ctx.expect(!header.empty(), "debug UI header readable");
        ctx.expect(header.find("OpenWorldStreamingDebugStats") != std::string::npos, "streaming debug stats exposed");
        ctx.expect(header.find("OpenWorldStreamingDebugStats streaming") != std::string::npos, "modern state carries streaming stats");
        ctx.expect(header.find("manifestRecordCount") != std::string::npos, "streaming manifest stats exposed");
        ctx.expect(header.find("transitionLimitedCount") != std::string::npos, "streaming planner stats exposed");
        ctx.expect(header.find("pendingReadCount") != std::string::npos, "streaming read stats exposed");
        ctx.expect(header.find("cachedCpuPayloadCount") != std::string::npos, "streaming cache stats exposed");
        ctx.expect(header.find("pendingPromoteCount") != std::string::npos, "streaming promote stats exposed");
        ctx.expect(header.find("pendingDemoteCount") != std::string::npos, "streaming demote stats exposed");
        ctx.expect(header.find("livePayloadCount") != std::string::npos, "streaming live stats exposed");
        ctx.expect(header.find("bakePayloadWriteCount") != std::string::npos, "streaming bake stats exposed");
        ctx.expect(header.find("generationQueuedCount") != std::string::npos, "streaming generation stats exposed");
        ctx.expect(header.find("assetDependencyManifestCount") != std::string::npos, "streaming asset dependency stats exposed");
        ctx.expect(header.find("liveAssetTextureCount") != std::string::npos, "streaming live asset texture stats exposed");
        ctx.expect(header.find("desiredChunksByProfile") != std::string::npos, "streaming profile stats exposed");
        ctx.expect(header.find("predictivePrefetchCount") != std::string::npos, "streaming predictive prefetch stats exposed");
        ctx.expect(header.find("WorldSave") == std::string::npos, "legacy world save debug UI absent");
        ctx.expect(header.find("DebugPicking") == std::string::npos, "legacy picking debug UI absent");
        ctx.expect(header.find("InteractionDebug") == std::string::npos, "legacy interaction debug UI absent");
        ctx.expect(header.find("struct NavigationDebugControls") == std::string::npos, "legacy nav controls absent");
    }

    using TestFn = void (*)(TestContext&);

    struct TestCase {
        const char* name;
        TestFn fn;
    };
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<TestCase> tests{
        {"EnumNamesAreStable", EnumNamesAreStable},
        {"DefaultDiagnosticsAreZeroed", DefaultDiagnosticsAreZeroed},
        {"DirtyFlagsCombineAndTest", DirtyFlagsCombineAndTest},
        {"DiagnosticsArePlainEngineData", DiagnosticsArePlainEngineData},
        {"ManifestRecordsUseDurableIdentity", ManifestRecordsUseDurableIdentity},
        {"PlannerSelectsActiveCachedAndCold", PlannerSelectsActiveCachedAndCold},
        {"HysteresisPreventsBoundaryChurn", HysteresisPreventsBoundaryChurn},
        {"TransitionCapsLimitDeterministically", TransitionCapsLimitDeterministically},
        {"IndependentPayloadPolicies", IndependentPayloadPolicies},
        {"InvalidInputsAreSkipped", InvalidInputsAreSkipped},
        {"LargeManifestScanIsMetadataOnly", LargeManifestScanIsMetadataOnly},
        {"CacheHaloQueuesWarmDecisions", CacheHaloQueuesWarmDecisions},
        {"CompletedFakeHitBecomesCached", CompletedFakeHitBecomesCached},
        {"CacheReadFailuresAreDiagnosed", CacheReadFailuresAreDiagnosed},
        {"GenerationOnMissIsExplicitPolicy", GenerationOnMissIsExplicitPolicy},
        {"LeavingCacheHaloCancelsAndEvicts", LeavingCacheHaloCancelsAndEvicts},
        {"QueueAndMergeCapsAreDeterministic", QueueAndMergeCapsAreDeterministic},
        {"DescriptorHelpersExposeCacheKinds", DescriptorHelpersExposeCacheKinds},
        {"LiveHaloPromotesCachedPayloadUnderBudget", LiveHaloPromotesCachedPayloadUnderBudget},
        {"LiveHaloDemotesLivePayloadUnderBudget", LiveHaloDemotesLivePayloadUnderBudget},
        {"LiveHaloAvoidsDuplicateQueuedWork", LiveHaloAvoidsDuplicateQueuedWork},
        {"LiveHaloMissingPayloadAndCallbackFailureAreDiagnosed", LiveHaloMissingPayloadAndCallbackFailureAreDiagnosed},
        {"LiveHaloQueueCapsAreDeterministic", LiveHaloQueueCapsAreDeterministic},
        {"LiveHaloStaleQueuedWorkIsIgnored", LiveHaloStaleQueuedWorkIsIgnored},
        {"AssetDependencyRecordsUseDurableIdentity", AssetDependencyRecordsUseDurableIdentity},
        {"AssetMetadataCacheWarmsWithoutPromotion", AssetMetadataCacheWarmsWithoutPromotion},
        {"AssetStreamingPromotesAndDemotesThroughCallbacks", AssetStreamingPromotesAndDemotesThroughCallbacks},
        {"AssetStreamingHandlesSharedMissingAndUnsupported", AssetStreamingHandlesSharedMissingAndUnsupported},
        {"FullHeightmapBakeWritesAllPayloads", FullHeightmapBakeWritesAllPayloads},
        {"BakedPayloadsReadThroughCacheHalo", BakedPayloadsReadThroughCacheHalo},
        {"BakeIdentityChangesWithSettings", BakeIdentityChangesWithSettings},
        {"VariantKeysSeparatePayloadInstances", VariantKeysSeparatePayloadInstances},
        {"LodProfilesProduceIndependentDecisions", LodProfilesProduceIndependentDecisions},
        {"FarMetadataAndCacheOnlyProfilesStayCached", FarMetadataAndCacheOnlyProfilesStayCached},
        {"ProfileTransitionCapsDoNotBlockStandardLive", ProfileTransitionCapsDoNotBlockStandardLive},
        {"PredictivePrefetchIsCacheOnlyAndLowerPriority", PredictivePrefetchIsCacheOnlyAndLowerPriority},
        {"DebugUiHeaderHasStreamingPlaceholder", DebugUiHeaderHasStreamingPlaceholder},
    };

    for (const TestCase& test : tests) {
        TestContext context{test.name, failures};
        test.fn(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "OpenWorldStreaming tests passed (" << tests.size() << " cases)\n";
    return 0;
}
