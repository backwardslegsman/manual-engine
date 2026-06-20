#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "Engine/AsyncWorkQueue.hpp"
#include "Engine/FrameBudget.hpp"
#include "Engine/OpenWorldStreamingSceneChunks.hpp"
#include "Engine/Reflection.hpp"

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
        return std::filesystem::temp_directory_path() / ("manual_engine_scene_chunk_streaming_" + std::string{name});
    }

    std::string readSourceFile(const char* relativePath)
    {
        std::ifstream input{std::string{MANUAL_ENGINE_SOURCE_DIR} + "/" + relativePath};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::vector<Engine::AsyncCompletedJob> waitCompleted(Engine::AsyncWorkQueue& queue, uint32_t minimum = 1)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::vector<Engine::AsyncCompletedJob> completed;
        while (std::chrono::steady_clock::now() < deadline) {
            std::vector<Engine::AsyncCompletedJob> polled = queue.pollCompleted();
            completed.insert(completed.end(), std::make_move_iterator(polled.begin()), std::make_move_iterator(polled.end()));
            if (completed.size() >= minimum) {
                return completed;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return completed;
    }

    Engine::StreamingWorldBounds bounds()
    {
        return {{0.0f, 0.0f, 0.0f}, {8.0f, 8.0f, 8.0f}};
    }

    Engine::StreamingHaloPlannerSettings plannerSettings()
    {
        Engine::StreamingHaloPlannerSettings settings = Engine::defaultStreamingHaloPlannerSettings();
        settings.payloadPolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::SceneChunk)].activeRadius = 16.0f;
        settings.payloadPolicies[static_cast<uint32_t>(Engine::StreamingPayloadKind::SceneChunk)].cacheRadius = 32.0f;
        return settings;
    }

    Engine::SceneSerializedScene simpleSerializedScene(uint64_t rootId = 100, uint64_t childId = 101)
    {
        Engine::SceneSerializedScene scene;
        Engine::SceneSerializedActorRecord root;
        root.id = Engine::SceneObjectId{rootId};
        root.localTransform.translation = {1.0f, 2.0f, 3.0f};
        root.order = 0;
        scene.actors.push_back(root);

        Engine::SceneSerializedActorRecord child;
        child.id = Engine::SceneObjectId{childId};
        child.parent = root.id;
        child.localTransform.translation = {0.0f, 4.0f, 0.0f};
        child.order = 1;
        scene.actors.push_back(child);

        scene.components.push_back({root.id, Engine::SceneComponentTypeId{77}, 0});
        return scene;
    }

    Engine::StreamingSceneChunkDescriptor descriptor(
        std::string stableId,
        Engine::StreamingSceneChunkActorOwnership ownership =
            Engine::StreamingSceneChunkActorOwnership::ChunkOwnedStatic)
    {
        Engine::StreamingSceneChunkDescriptor result;
        result.stableChunkId = std::move(stableId);
        result.bounds = bounds();
        result.sourceHash = 10;
        result.settingsHash = 20;
        result.estimatedBytes = 128;
        result.ownership = ownership;
        result.debugName = result.stableChunkId;
        return result;
    }

    std::filesystem::path writeSceneBinaryFixture(std::string_view name, const Engine::SceneSerializedScene& scene)
    {
        const std::filesystem::path path = tempPath(name);
        std::filesystem::create_directories(path.parent_path());
        const Engine::SceneSerializationWriteResult write =
            Engine::writeSceneBinary(path, scene, Engine::SceneSerializationSettings{});
        if (write.status != Engine::SceneSerializationStatus::Success) {
            std::cerr << "Failed to write scene binary fixture: " << write.message << "\n";
        }
        return path;
    }

    Engine::OpenWorldStreamingCacheHalo readChunkIntoCache(
        const Engine::StreamingSceneChunkDescriptor& desc,
        const std::filesystem::path& path)
    {
        Engine::StreamingChunkManifest manifest;
        Engine::StreamingReadDescriptorTable descriptors;
        const Engine::StreamingSceneChunkBuildResult build =
            Engine::makeSceneChunkStreamingRecord(desc, path);
        Engine::addSceneChunkStreamingRecord(manifest, descriptors, build);
        const Engine::StreamingHaloPlan plan =
            Engine::planStreamingHalo(manifest, {0.0f, 0.0f, 0.0f}, {}, plannerSettings());

        Engine::AsyncWorkQueue queue{1};
        Engine::OpenWorldStreamingCacheHalo halo;
        halo.update(queue, plan, descriptors);
        halo.mergeCompleted(waitCompleted(queue));
        queue.shutdown();
        return halo;
    }

    void SceneChunkManifestUsesStableIdentity(TestContext& ctx)
    {
        Engine::StreamingSceneChunkDescriptor desc = descriptor("scene/chunk/a");
        desc.assetDependencies = {Engine::AssetId{42}};
        const Engine::StreamingSceneChunkBuildResult build =
            Engine::makeSceneChunkStreamingRecord(desc, tempPath("identity.meshunk"));
        ctx.expect(build.success, "scene chunk build result succeeds");
        ctx.expect(build.record.key.kind == Engine::StreamingChunkKeyKind::SceneChunk, "scene chunk key kind used");
        ctx.expect(build.record.key.stableId == "scene/chunk/a", "stable scene chunk ID stored");
        ctx.expect(build.record.payload == Engine::StreamingPayloadKind::SceneChunk, "scene chunk payload kind stored");

        const std::string header = readSourceFile("src/Engine/OpenWorldStreamingSceneChunks.hpp");
        ctx.expect(header.find("SceneActorHandle") != std::string::npos, "adapter owns runtime scene actor handles");
        ctx.expect(header.find("Renderer::") == std::string::npos, "scene chunk streaming header does not expose renderer types");
        ctx.expect(header.find("JPH") == std::string::npos, "scene chunk streaming header does not expose Jolt types");
        ctx.expect(header.find("dtNav") == std::string::npos, "scene chunk streaming header does not expose Detour types");
    }

    void SceneBinaryChunkReadsIntoCache(TestContext& ctx)
    {
        const Engine::StreamingSceneChunkDescriptor desc = descriptor("scene/chunk/read");
        const std::filesystem::path path = writeSceneBinaryFixture("read.meshunk", simpleSerializedScene());
        Engine::OpenWorldStreamingCacheHalo halo = readChunkIntoCache(desc, path);

        const Engine::StreamingChunkKey key{Engine::StreamingChunkKeyKind::SceneChunk, {}, {}, "scene/chunk/read"};
        const std::optional<Engine::StreamingCachedPayload> payload =
            halo.cachedPayload(key, Engine::StreamingPayloadKind::SceneChunk);
        ctx.expect(payload.has_value(), "scene chunk cached payload exists");
        ctx.expect(payload && std::holds_alternative<Engine::StreamingSceneChunkPayload>(*payload), "scene chunk payload type stored");
        ctx.expect(halo.snapshot().diagnostics.cachedSceneChunkPayloadCount == 1, "cached scene chunk count updated");
    }

    void CorruptSceneBinaryFailsBeforePromotion(TestContext& ctx)
    {
        const std::filesystem::path path = tempPath("corrupt.meshunk");
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output << "not a scene";
        }
        const Engine::StreamingSceneChunkDescriptor desc = descriptor("scene/chunk/corrupt");
        Engine::OpenWorldStreamingCacheHalo halo = readChunkIntoCache(desc, path);
        const Engine::OpenWorldStreamingDiagnostics diagnostics = halo.snapshot().diagnostics;
        ctx.expect(diagnostics.actualChunksByState[static_cast<uint32_t>(Engine::StreamingResidencyState::Failed)] == 1, "corrupt scene chunk enters failed state");
        ctx.expect(diagnostics.payloads[static_cast<uint32_t>(Engine::StreamingPayloadKind::SceneChunk)].corrupt == 1, "corrupt scene chunk counted");
    }

    void PromotionAndDemotionRoundTrip(TestContext& ctx)
    {
        const Engine::StreamingSceneChunkDescriptor desc = descriptor("scene/chunk/live");
        const std::filesystem::path path = writeSceneBinaryFixture("live.meshunk", simpleSerializedScene());
        Engine::OpenWorldStreamingCacheHalo cache = readChunkIntoCache(desc, path);

        Engine::StreamingChunkManifest manifest;
        Engine::StreamingReadDescriptorTable descriptors;
        Engine::addSceneChunkStreamingRecord(manifest, descriptors, Engine::makeSceneChunkStreamingRecord(desc, path));
        const Engine::StreamingHaloPlan livePlan =
            Engine::planStreamingHalo(manifest, {0.0f, 0.0f, 0.0f}, cache.snapshot().residency, plannerSettings());

        Engine::Scene scene;
        Engine::ReflectionRegistry registry;
        Engine::OpenWorldStreamingSceneChunkResidency residency{scene, registry};
        ctx.expect(residency.registerChunk(desc), "scene chunk descriptor registered");
        Engine::OpenWorldStreamingLiveHalo live;
        Engine::MainThreadWorkQueue work;
        live.update(work, livePlan, cache, Engine::makeSceneChunkStreamingPromotionCallbacks(residency));
        Engine::FrameBudget budget;
        budget.beginFrame({10.0f, true});
        work.drain(budget);

        ctx.expect(live.snapshot().diagnostics.liveResources.sceneActors == 2, "two scene actors live");
        ctx.expect(residency.diagnostics().componentsCreated == 1, "component restored");
        ctx.expect(scene.roots().size() == 1, "root restored");
        const std::vector<Engine::SceneActorHandle> roots = scene.roots();
        ctx.expect(!roots.empty() && scene.children(roots.front()).size() == 1, "hierarchy restored");

        const Engine::StreamingHaloPlan coldPlan =
            Engine::planStreamingHalo(manifest, {1000.0f, 0.0f, 1000.0f}, live.snapshot().residency, plannerSettings());
        live.update(work, coldPlan, cache, Engine::makeSceneChunkStreamingPromotionCallbacks(residency));
        budget.beginFrame({10.0f, true});
        work.drain(budget);
        ctx.expect(residency.diagnostics().demotedChunkCount == 1, "scene chunk demoted");
        ctx.expect(residency.diagnostics().actorsDestroyed == 2, "demotion destroyed created actors");

        live.update(work, livePlan, cache, Engine::makeSceneChunkStreamingPromotionCallbacks(residency));
        budget.beginFrame({10.0f, true});
        work.drain(budget);
        ctx.expect(residency.diagnostics().promotedChunkCount == 2, "scene chunk can be promoted again");
        ctx.expect(live.snapshot().diagnostics.liveResources.sceneActors == 2, "reloaded actors are live");
    }

    void DuplicateChunkOwnedActorRejected(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle existing = scene.createActor(Engine::SceneObjectId{100});
        ctx.expect(Engine::isValid(existing), "existing actor created");

        const Engine::StreamingSceneChunkDescriptor desc = descriptor("scene/chunk/duplicate");
        const std::filesystem::path path = writeSceneBinaryFixture("duplicate.meshunk", simpleSerializedScene());
        Engine::OpenWorldStreamingCacheHalo cache = readChunkIntoCache(desc, path);
        Engine::StreamingChunkManifest manifest;
        Engine::StreamingReadDescriptorTable descriptors;
        Engine::addSceneChunkStreamingRecord(manifest, descriptors, Engine::makeSceneChunkStreamingRecord(desc, path));
        const Engine::StreamingHaloPlan plan =
            Engine::planStreamingHalo(manifest, {0.0f, 0.0f, 0.0f}, cache.snapshot().residency, plannerSettings());

        Engine::ReflectionRegistry registry;
        Engine::OpenWorldStreamingSceneChunkResidency residency{scene, registry};
        residency.registerChunk(desc);
        Engine::OpenWorldStreamingLiveHalo live;
        Engine::MainThreadWorkQueue work;
        live.update(work, plan, cache, Engine::makeSceneChunkStreamingPromotionCallbacks(residency));
        Engine::FrameBudget budget;
        budget.beginFrame({10.0f, true});
        work.drain(budget);

        ctx.expect(residency.diagnostics().duplicateStableIdCount == 1, "duplicate stable ID diagnosed");
        ctx.expect(live.snapshot().diagnostics.failedPromotionCount == 1, "duplicate promotion failed");
    }

    void GlobalAndMigratoryPoliciesPreventDuplicates(TestContext& ctx)
    {
        Engine::Scene scene;
        const Engine::SceneActorHandle globalRoot = scene.createActor(Engine::SceneObjectId{100});
        const Engine::SceneActorHandle globalChild = scene.createActor(Engine::SceneObjectId{101});
        ctx.expect(Engine::isValid(globalRoot) && Engine::isValid(globalChild), "global actors created");
        Engine::ReflectionRegistry registry;

        const Engine::StreamingSceneChunkDescriptor global =
            descriptor("scene/chunk/global", Engine::StreamingSceneChunkActorOwnership::Global);
        const std::filesystem::path globalPath = writeSceneBinaryFixture("global.meshunk", simpleSerializedScene());
        Engine::OpenWorldStreamingCacheHalo globalCache = readChunkIntoCache(global, globalPath);
        Engine::StreamingChunkManifest manifest;
        Engine::StreamingReadDescriptorTable descriptors;
        Engine::addSceneChunkStreamingRecord(manifest, descriptors, Engine::makeSceneChunkStreamingRecord(global, globalPath));
        Engine::StreamingHaloPlan plan =
            Engine::planStreamingHalo(manifest, {0.0f, 0.0f, 0.0f}, globalCache.snapshot().residency, plannerSettings());

        Engine::OpenWorldStreamingSceneChunkResidency residency{scene, registry};
        residency.registerChunk(global);
        Engine::OpenWorldStreamingLiveHalo live;
        Engine::MainThreadWorkQueue work;
        live.update(work, plan, globalCache, Engine::makeSceneChunkStreamingPromotionCallbacks(residency));
        Engine::FrameBudget budget;
        budget.beginFrame({10.0f, true});
        work.drain(budget);
        ctx.expect(live.snapshot().diagnostics.liveResources.sceneActors == 0, "global promotion does not duplicate actors");

        const Engine::StreamingSceneChunkDescriptor migratoryA =
            descriptor("scene/chunk/migratory-a", Engine::StreamingSceneChunkActorOwnership::Migratory);
        const Engine::StreamingSceneChunkDescriptor migratoryB =
            descriptor("scene/chunk/migratory-b", Engine::StreamingSceneChunkActorOwnership::Migratory);
        residency.registerChunk(migratoryA);
        residency.registerChunk(migratoryB);
        ctx.expect(residency.claimMigratoryActor(Engine::SceneObjectId{100}, "scene/chunk/migratory-a"), "migratory actor claimed");

        Engine::StreamingCachedPayload payload{Engine::StreamingSceneChunkPayload{
            "scene/chunk/migratory-b",
            2,
            1,
            {},
            std::make_shared<Engine::SceneSerializedScene>(simpleSerializedScene())}};
        const Engine::StreamingPromotionResult result =
            Engine::makeSceneChunkStreamingPromotionCallbacks(residency).promote(
                {Engine::StreamingChunkKey{Engine::StreamingChunkKeyKind::SceneChunk, {}, {}, "scene/chunk/migratory-b"},
                    Engine::StreamingPayloadKind::SceneChunk,
                    0,
                    1,
                    Engine::StreamingResidencyState::LiveActive,
                    "migratory-b"},
                payload);
        ctx.expect(result.status == Engine::StreamingPromotionStatus::CallbackFailed, "claimed migratory actor is not duplicated");
        ctx.expect(residency.diagnostics().duplicateStableIdCount == 1, "migratory duplicate diagnosed");
    }

    void InvalidReferencesFailBeforeMutation(TestContext& ctx)
    {
        Engine::Scene scene;
        Engine::ReflectionRegistry registry;
        Engine::OpenWorldStreamingSceneChunkResidency residency{scene, registry};

        Engine::SceneSerializedScene invalidParent = simpleSerializedScene();
        invalidParent.actors[1].parent = Engine::SceneObjectId{999};
        const Engine::StreamingSceneChunkDescriptor desc = descriptor("scene/chunk/invalid-parent");
        residency.registerChunk(desc);
        Engine::StreamingCachedPayload parentPayload{Engine::StreamingSceneChunkPayload{
            desc.stableChunkId,
            2,
            1,
            {},
            std::make_shared<Engine::SceneSerializedScene>(invalidParent)}};
        Engine::StreamingPromotionResult parentResult =
            Engine::makeSceneChunkStreamingPromotionCallbacks(residency).promote(
                {Engine::StreamingChunkKey{Engine::StreamingChunkKeyKind::SceneChunk, {}, {}, desc.stableChunkId},
                    Engine::StreamingPayloadKind::SceneChunk},
                parentPayload);
        ctx.expect(parentResult.status == Engine::StreamingPromotionStatus::CallbackFailed, "invalid parent is rejected before mutation");
        ctx.expect(scene.roots().empty(), "invalid parent did not leave live actors");

        Engine::SceneSerializedScene invalidComponent = simpleSerializedScene();
        invalidComponent.components[0].owner = Engine::SceneObjectId{999};
        const Engine::StreamingSceneChunkDescriptor componentDesc = descriptor("scene/chunk/invalid-component");
        residency.registerChunk(componentDesc);
        Engine::StreamingCachedPayload componentPayload{Engine::StreamingSceneChunkPayload{
            componentDesc.stableChunkId,
            2,
            1,
            {},
            std::make_shared<Engine::SceneSerializedScene>(invalidComponent)}};
        Engine::StreamingPromotionResult componentResult =
            Engine::makeSceneChunkStreamingPromotionCallbacks(residency).promote(
                {Engine::StreamingChunkKey{Engine::StreamingChunkKeyKind::SceneChunk, {}, {}, componentDesc.stableChunkId},
                    Engine::StreamingPayloadKind::SceneChunk},
                componentPayload);
        ctx.expect(componentResult.status == Engine::StreamingPromotionStatus::CallbackFailed, "invalid component owner is rejected before mutation");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string, void (*)(TestContext&)>> tests{
        {"SceneChunkManifestUsesStableIdentity", SceneChunkManifestUsesStableIdentity},
        {"SceneBinaryChunkReadsIntoCache", SceneBinaryChunkReadsIntoCache},
        {"CorruptSceneBinaryFailsBeforePromotion", CorruptSceneBinaryFailsBeforePromotion},
        {"PromotionAndDemotionRoundTrip", PromotionAndDemotionRoundTrip},
        {"DuplicateChunkOwnedActorRejected", DuplicateChunkOwnedActorRejected},
        {"GlobalAndMigratoryPoliciesPreventDuplicates", GlobalAndMigratoryPoliciesPreventDuplicates},
        {"InvalidReferencesFailBeforeMutation", InvalidReferencesFailBeforeMutation},
    };

    for (const auto& [name, test] : tests) {
        TestContext context{name, failures};
        test(context);
    }

    if (!failures.empty()) {
        for (const TestFailure& failure : failures) {
            std::cerr << failure.testName << ": " << failure.message << "\n";
        }
        return 1;
    }

    std::cout << "Open world scene chunk streaming tests passed (" << tests.size() << " cases)\n";
    return 0;
}
