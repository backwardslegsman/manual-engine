#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/FrameBudget.hpp"
#include "Engine/OpenWorldStreamingRuntime.hpp"

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

    std::filesystem::path tempPath(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / ("manual_engine_streaming_runtime_" + std::string{name});
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
            data.insert(
                data.end(),
                raw.begin() + static_cast<std::ptrdiff_t>(offset),
                raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
            offset += blockSize;
        }
        writeU32BE(data, adler32(raw));
        return data;
    }

    std::filesystem::path writeGrayPng8(std::string_view name)
    {
        const uint32_t width = 5;
        const uint32_t height = 5;
        const std::vector<uint8_t> samples{
            0, 8, 16, 24, 32,
            8, 16, 24, 32, 40,
            16, 24, 32, 40, 48,
            24, 32, 40, 48, 56,
            32, 40, 48, 56, 64,
        };
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

    Engine::OpenWorldStreamingRuntimeSettings runtimeSettings(std::string_view suffix)
    {
        const std::filesystem::path root = tempPath(suffix);
        std::filesystem::remove_all(root);
        Engine::OpenWorldStreamingRuntimeSettings settings;
        settings.savedBuildManifestPath = root / "streaming" / "manifest.yaml";
        settings.bake.heightmap.sourcePath = writeGrayPng8(std::string{suffix} + ".png");
        settings.bake.heightmap.sourceIdOverride = Engine::AssetId{0x51510000ull + static_cast<uint64_t>(suffix.size())};
        settings.bake.heightmap.sampleSpacing = 1.0f;
        settings.bake.heightmap.heightScale = 8.0f;
        settings.bake.heightmap.sourceOrigin = {0.0f, 0.0f, 4.0f};
        settings.bake.heightmap.chunkWorldSize = 4.0f;
        settings.bake.heightmap.chunkResolution = 5;
        settings.bake.terrainCache.rootPath = root / "terrain";
        settings.bake.terrainCache.policy = Engine::TerrainDerivedCachePolicy::ReadOnly;
        settings.bake.navigationCache.rootPath = root / "navigation";
        settings.bake.navigationCache.worldId = "runtime_test";
        settings.bake.navigationResolution = 5;
        settings.bake.bakeNavigationTiles = false;
        settings.bake.physicsColliderResolution = 5;
        settings.bake.renderLods = {{0, 5, 0.5f}};
        settings.cache.maxReadJobsQueuedPerUpdate = 16;
        settings.cache.maxCompletedJobsMergedPerUpdate = 16;
        settings.promotion.maxPromotesQueuedPerUpdate = 16;
        settings.promotion.maxDemotesQueuedPerUpdate = 16;
        for (Engine::StreamingPayloadResidencyPolicy& policy : settings.planner.payloadPolicies) {
            policy.activeRadius = 32.0f;
            policy.cacheRadius = 64.0f;
            policy.hysteresis = 4.0f;
            policy.maxTransitionsPerFrame = 16;
        }
        for (auto& payloadProfiles : settings.planner.profilePolicies) {
            for (Engine::StreamingPayloadResidencyPolicy& policy : payloadProfiles) {
                policy.activeRadius = 32.0f;
                policy.cacheRadius = 64.0f;
                policy.hysteresis = 4.0f;
                policy.maxTransitionsPerFrame = 16;
            }
        }
        return settings;
    }

    void waitForAsync(Engine::OpenWorldStreamingRuntime& runtime, Engine::AsyncWorkQueue& queue)
    {
        for (uint32_t attempt = 0; attempt < 200; ++attempt) {
            runtime.pollCompleted(queue.pollCompleted());
            if (queue.pendingCount() == 0) {
                runtime.pollCompleted(queue.pollCompleted());
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    Engine::StreamingPromotionCallbacks fakeCallbacks(uint32_t& promoted, uint32_t& demoted)
    {
        Engine::StreamingPromotionCallbacks callbacks;
        callbacks.promote = [&](const Engine::StreamingPromotionRequest&, const Engine::StreamingCachedPayload&) {
            Engine::StreamingPromotionResult result;
            result.status = Engine::StreamingPromotionStatus::Success;
            result.liveToken = {++promoted};
            result.liveResources.terrainRenderHandles = promoted;
            return result;
        };
        callbacks.demote = [&](const Engine::StreamingDemotionRequest&, Engine::StreamingRuntimeToken) {
            ++demoted;
            Engine::StreamingPromotionResult result;
            result.status = Engine::StreamingPromotionStatus::Success;
            return result;
        };
        return callbacks;
    }

    void MissingSavedBuildRebuilds(TestContext& ctx)
    {
        Engine::OpenWorldStreamingRuntimeSettings settings = runtimeSettings("missing_rebuild");
        Engine::OpenWorldStreamingRuntime runtime{settings};
        const Engine::OpenWorldStreamingBuildResult& result = runtime.initializeFromSavedBuild();
        ctx.expect(result.success, "initial rebuild succeeds");
        ctx.expect(result.rebuilt, "missing build triggers rebuild");
        ctx.expect(!runtime.manifest().records.empty(), "runtime manifest populated");
        ctx.expect(!runtime.readDescriptors().entries.empty(), "read descriptors populated");
        ctx.expect(std::filesystem::is_regular_file(settings.savedBuildManifestPath), "saved manifest exists");
    }

    void UnchangedSavedBuildIsReused(TestContext& ctx)
    {
        Engine::OpenWorldStreamingRuntimeSettings settings = runtimeSettings("reuse");
        Engine::OpenWorldStreamingRuntime first{settings};
        const Engine::OpenWorldStreamingBuildResult firstResult = first.initializeFromSavedBuild();
        Engine::OpenWorldStreamingRuntime second{settings};
        const Engine::OpenWorldStreamingBuildResult secondResult = second.initializeFromSavedBuild();
        ctx.expect(firstResult.rebuilt, "first run rebuilds");
        ctx.expect(secondResult.success, "second run succeeds");
        ctx.expect(secondResult.reusedSavedBuild, "second run reuses saved build");
        ctx.expect(second.manifest().records.size() == first.manifest().records.size(), "reused manifest record count matches");
    }

    void VersionOrSettingsChangeInvalidates(TestContext& ctx)
    {
        Engine::OpenWorldStreamingRuntimeSettings base = runtimeSettings("invalidate");
        Engine::OpenWorldStreamingRuntime first{base};
        (void)first.initializeFromSavedBuild();
        Engine::OpenWorldStreamingRuntimeSettings changed = base;
        changed.bake.renderLods[0].renderResolution = 3;
        Engine::OpenWorldStreamingRuntime second{changed};
        const Engine::OpenWorldStreamingBuildResult result = second.initializeFromSavedBuild();
        ctx.expect(result.success, "changed settings rebuild succeeds");
        ctx.expect(result.rebuilt, "changed settings invalidates saved build");
        ctx.expect(!result.reusedSavedBuild, "changed settings not reused");
    }

    void RuntimePlansReadsAndPromotes(TestContext& ctx)
    {
        Engine::OpenWorldStreamingRuntime runtime{runtimeSettings("runtime_flow")};
        ctx.expect(runtime.initializeFromSavedBuild().success, "runtime initialized");

        Engine::AsyncWorkQueue queue{2};
        Engine::MainThreadWorkQueue work;
        uint32_t promoted = 0;
        uint32_t demoted = 0;
        runtime.update(glm::vec3{2.0f, 0.0f, 2.0f}, queue, work, fakeCallbacks(promoted, demoted));
        ctx.expect(runtime.diagnostics().pendingReadCount > 0, "first update queues reads");
        waitForAsync(runtime, queue);
        runtime.update(glm::vec3{2.0f, 0.0f, 2.0f}, queue, work, fakeCallbacks(promoted, demoted));
        Engine::FrameBudget budget;
        budget.beginFrame({20.0f, true});
        work.drain(budget);
        ctx.expect(promoted > 0, "cached payloads promote through callbacks");
        ctx.expect(runtime.diagnostics().cachedCpuPayloadCount > 0, "cached cpu payload count populated");

        runtime.update(glm::vec3{500.0f, 0.0f, 500.0f}, queue, work, fakeCallbacks(promoted, demoted));
        budget.beginFrame({20.0f, true});
        work.drain(budget);
        ctx.expect(demoted > 0, "leaving active halo demotes through callbacks");
        queue.shutdown();
    }

    void SavedManifestIsHandleFree(TestContext& ctx)
    {
        Engine::OpenWorldStreamingRuntimeSettings settings = runtimeSettings("handle_free");
        Engine::OpenWorldStreamingRuntime runtime{settings};
        ctx.expect(runtime.initializeFromSavedBuild().success, "runtime initialized");
        std::ifstream input(settings.savedBuildManifestPath);
        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        ctx.expect(text.find("Handle") == std::string::npos, "saved manifest does not contain handle type names");
        ctx.expect(text.find("renderer") == std::string::npos, "saved manifest does not contain renderer handles");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests{
        {"MissingSavedBuildRebuilds", MissingSavedBuildRebuilds},
        {"UnchangedSavedBuildIsReused", UnchangedSavedBuildIsReused},
        {"VersionOrSettingsChangeInvalidates", VersionOrSettingsChangeInvalidates},
        {"RuntimePlansReadsAndPromotes", RuntimePlansReadsAndPromotes},
        {"SavedManifestIsHandleFree", SavedManifestIsHandleFree},
    };

    for (const auto& [name, test] : tests) {
        TestContext ctx{name, failures};
        test(ctx);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << failure.testName << ": " << failure.message << '\n';
        }
        return 1;
    }

    std::cout << "Open world streaming runtime tests passed (" << tests.size() << " cases).\n";
    return 0;
}
