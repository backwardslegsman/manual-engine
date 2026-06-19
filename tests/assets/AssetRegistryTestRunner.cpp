#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Assets/Assimp/Importer.hpp"
#include "Engine/AssetRegistry.hpp"

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

    std::filesystem::path fixturePath()
    {
        const std::filesystem::path sourceRelative = "tests/assets/fixtures/authored_scene_fixture.gltf";
        if (std::filesystem::exists(sourceRelative)) {
            return sourceRelative;
        }

        const std::filesystem::path buildRelative = "../../tests/assets/fixtures/authored_scene_fixture.gltf";
        if (std::filesystem::exists(buildRelative)) {
            return buildRelative;
        }

        return sourceRelative;
    }

    std::filesystem::path tempPath(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / ("manual_engine_asset_registry_" + std::string{name});
    }

    void writeTextFile(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
    }

    Engine::AssetDescriptor fixtureDescriptor()
    {
        Engine::AssetDescriptor descriptor;
        descriptor.sourcePath = fixturePath();
        descriptor.type = Engine::AssetType::AuthoredScene;
        descriptor.settings = {"import_scene", "1", "default"};
        return descriptor;
    }

    void registerAssetCreatesStableMetadata(TestContext& ctx)
    {
        Engine::AssetRegistry registry;
        const Engine::AssetHandle handle = registry.registerAsset(fixtureDescriptor());
        const std::optional<Engine::AssetMetadata> metadata = registry.metadata(handle);

        ctx.expect(Engine::isValid(handle), "registered asset handle was invalid");
        ctx.expect(registry.contains(handle), "registry did not contain registered asset");
        ctx.expect(metadata.has_value(), "registered asset metadata was missing");
        if (!metadata) {
            return;
        }

        ctx.expect(Engine::isValid(metadata->id), "metadata asset ID was invalid");
        ctx.expect(metadata->handle == handle, "metadata handle did not match runtime handle");
        ctx.expect(metadata->type == Engine::AssetType::AuthoredScene, "metadata type was wrong");
        ctx.expect(metadata->status == Engine::AssetStatus::Registered, "existing fixture did not register as available");
        ctx.expect(metadata->sourceFormat == Engine::AssetSourceFormat::Gltf, "fixture source format was not detected as glTF");
        ctx.expect(!metadata->contentHash.empty(), "existing fixture content hash was empty");
    }

    void duplicateRegistrationReturnsSameHandle(TestContext& ctx)
    {
        Engine::AssetRegistry registry;
        const Engine::AssetDescriptor descriptor = fixtureDescriptor();
        const Engine::AssetHandle first = registry.registerAsset(descriptor);
        const Engine::AssetHandle second = registry.registerAsset(descriptor);

        ctx.expect(first == second, "duplicate registration did not return same handle");
        ctx.expect(registry.assets().size() == 1, "duplicate registration created another live asset");
        ctx.expect(registry.diagnostics().duplicateRegistrationCount == 1, "duplicate diagnostic count was wrong");
    }

    void differentSettingsCreateDistinctAsset(TestContext& ctx)
    {
        Engine::AssetRegistry registry;
        Engine::AssetDescriptor firstDescriptor = fixtureDescriptor();
        Engine::AssetDescriptor secondDescriptor = fixtureDescriptor();
        secondDescriptor.settings.optionsHash = "other";

        const Engine::AssetHandle first = registry.registerAsset(firstDescriptor);
        const Engine::AssetHandle second = registry.registerAsset(secondDescriptor);
        const auto firstMetadata = registry.metadata(first);
        const auto secondMetadata = registry.metadata(second);

        ctx.expect(first != second, "different settings reused the same handle");
        ctx.expect(firstMetadata && secondMetadata, "distinct assets were missing metadata");
        if (firstMetadata && secondMetadata) {
            ctx.expect(firstMetadata->id != secondMetadata->id, "different settings did not create a distinct stable ID");
        }
    }

    void unregisterInvalidatesHandle(TestContext& ctx)
    {
        Engine::AssetRegistry registry;
        const Engine::AssetHandle first = registry.registerAsset(fixtureDescriptor());
        const Engine::AssetId id = registry.metadata(first)->id;

        ctx.expect(registry.unregisterAsset(first), "unregister failed for live asset");
        ctx.expect(!registry.contains(first), "stale asset handle still validated");
        ctx.expect(!registry.metadata(id).has_value(), "unregistered asset still resolved by stable ID");

        const Engine::AssetHandle second = registry.registerAsset(fixtureDescriptor());
        ctx.expect(first.index == second.index, "asset slot was not reused deterministically");
        ctx.expect(first.generation != second.generation, "asset slot reuse did not increment generation");
    }

    void missingAssetReportsMissing(TestContext& ctx)
    {
        Engine::AssetRegistry registry;
        Engine::AssetDescriptor descriptor;
        descriptor.sourcePath = tempPath("missing_asset.gltf");
        descriptor.type = Engine::AssetType::AuthoredScene;

        const Engine::AssetHandle handle = registry.registerAsset(descriptor);
        const auto metadata = registry.metadata(handle);

        ctx.expect(metadata.has_value(), "missing asset metadata was absent");
        if (metadata) {
            ctx.expect(metadata->status == Engine::AssetStatus::Missing, "missing asset did not report missing status");
            ctx.expect(!metadata->warnings.empty(), "missing asset did not record a warning");
        }
        ctx.expect(registry.diagnostics().missingCount == 1, "missing diagnostic count was wrong");
    }

    void refreshDetectsStaleAndMissing(TestContext& ctx)
    {
        Engine::AssetRegistry registry;
        const std::filesystem::path path = tempPath("refresh.txt");
        writeTextFile(path, "first");

        Engine::AssetDescriptor descriptor;
        descriptor.sourcePath = path;
        descriptor.type = Engine::AssetType::Unknown;
        const Engine::AssetHandle handle = registry.registerAsset(descriptor);
        const std::string originalHash = registry.metadata(handle)->contentHash;

        writeTextFile(path, "second");
        ctx.expect(registry.refresh(handle), "refresh failed for changed file");
        auto metadata = registry.metadata(handle);
        ctx.expect(metadata && metadata->status == Engine::AssetStatus::Stale, "changed file was not marked stale");
        ctx.expect(metadata && metadata->contentHash != originalHash, "changed file hash did not update");

        std::filesystem::remove(path);
        ctx.expect(registry.refresh(handle), "refresh failed for removed file");
        metadata = registry.metadata(handle);
        ctx.expect(metadata && metadata->status == Engine::AssetStatus::Missing, "removed file was not marked missing");
    }

    void dependencyEdgesAreDeterministic(TestContext& ctx)
    {
        Engine::AssetRegistry registry;
        const Engine::AssetHandle scene = registry.registerAsset(fixtureDescriptor());

        Engine::AssetDescriptor firstTexture;
        firstTexture.sourcePath = tempPath("first.png");
        firstTexture.type = Engine::AssetType::Texture;
        Engine::AssetDescriptor secondTexture;
        secondTexture.sourcePath = tempPath("second.png");
        secondTexture.type = Engine::AssetType::Texture;
        const Engine::AssetHandle first = registry.registerAsset(firstTexture);
        const Engine::AssetHandle second = registry.registerAsset(secondTexture);

        ctx.expect(registry.addDependency(scene, second), "first dependency add failed");
        ctx.expect(registry.addDependency(scene, first), "second dependency add failed");
        ctx.expect(!registry.addDependency(scene, first), "duplicate dependency add succeeded");
        ctx.expect(!registry.addDependency(scene, scene), "self dependency add succeeded");

        const std::vector<Engine::AssetHandle> dependencies = registry.dependencies(scene);
        ctx.expect(dependencies == std::vector<Engine::AssetHandle>{second, first}, "dependency order was not insertion order");
        ctx.expect(registry.dependents(first) == std::vector<Engine::AssetHandle>{scene}, "dependent lookup was wrong");
        ctx.expect(registry.diagnostics().dependencyEdgeCount == 2, "dependency edge diagnostic count was wrong");
    }

    void findByIdAndPath(TestContext& ctx)
    {
        Engine::AssetRegistry registry;
        const Engine::AssetDescriptor descriptor = fixtureDescriptor();
        const Engine::AssetHandle handle = registry.registerAsset(descriptor);
        const Engine::AssetMetadata metadata = *registry.metadata(handle);

        ctx.expect(registry.findById(metadata.id) == handle, "findById did not return registered handle");
        ctx.expect(
            registry.findByCanonicalPath(descriptor.sourcePath, descriptor.type, descriptor.settings) == handle,
            "findByCanonicalPath did not return registered handle");
        ctx.expect(!registry.findById({}).has_value(), "invalid stable ID resolved to a handle");
    }

    void importedSceneRegistersDependencies(TestContext& ctx)
    {
        const Assets::Assimp::ImportedScene importedScene = Assets::Assimp::importScene(fixturePath());
        ctx.expect(importedScene.success, "fixture scene import failed: " + importedScene.error);
        if (!importedScene.success) {
            return;
        }

        Engine::AssetRegistry registry;
        const Engine::AssetHandle scene = registry.registerAsset(fixtureDescriptor());
        const Engine::ImportedSceneAssetDependencyResult result =
            Engine::registerImportedSceneTextureDependencies(registry, scene, fixturePath(), importedScene);

        ctx.expect(result.registeredTextureCount > 0, "imported scene did not register texture dependencies");
        ctx.expect(result.dependencyCount == result.registeredTextureCount, "dependency count did not match texture count");
        ctx.expect(registry.dependencies(scene).size() == result.dependencyCount, "scene dependency vector size was wrong");
        for (Engine::AssetHandle dependency : registry.dependencies(scene)) {
            const auto metadata = registry.metadata(dependency);
            ctx.expect(metadata && metadata->type == Engine::AssetType::Texture, "imported dependency was not a texture asset");
        }
    }

    void diagnosticsCountsByTypeAndStatus(TestContext& ctx)
    {
        Engine::AssetRegistry registry;
        const Engine::AssetHandle scene = registry.registerAsset(fixtureDescriptor());

        Engine::AssetDescriptor missingTexture;
        missingTexture.sourcePath = tempPath("diagnostic_missing.png");
        missingTexture.type = Engine::AssetType::Texture;
        const Engine::AssetHandle texture = registry.registerAsset(missingTexture);
        registry.addDependency(scene, texture);

        const Engine::AssetRegistryDiagnostics diagnostics = registry.diagnostics();
        const size_t sceneTypeIndex = static_cast<size_t>(Engine::AssetType::AuthoredScene);
        const size_t textureTypeIndex = static_cast<size_t>(Engine::AssetType::Texture);
        ctx.expect(diagnostics.totalRegisteredAssets == 2, "registered asset diagnostic count was wrong");
        ctx.expect(diagnostics.liveAssetCountByType.size() > sceneTypeIndex, "diagnostic type vector missing scene slot");
        ctx.expect(diagnostics.liveAssetCountByType.size() > textureTypeIndex, "diagnostic type vector missing texture slot");
        if (diagnostics.liveAssetCountByType.size() > textureTypeIndex) {
            ctx.expect(diagnostics.liveAssetCountByType[sceneTypeIndex] == 1, "scene type diagnostic count was wrong");
            ctx.expect(diagnostics.liveAssetCountByType[textureTypeIndex] == 1, "texture type diagnostic count was wrong");
        }
        ctx.expect(diagnostics.missingCount == 1, "missing diagnostic count was wrong");
        ctx.expect(diagnostics.dependencyEdgeCount == 1, "dependency diagnostic count was wrong");
    }
}

int main()
{
    std::vector<TestFailure> failures;
    const std::vector<std::pair<std::string_view, std::function<void(TestContext&)>>> tests = {
        {"RegisterAssetCreatesStableMetadata", registerAssetCreatesStableMetadata},
        {"DuplicateRegistrationReturnsSameHandle", duplicateRegistrationReturnsSameHandle},
        {"DifferentSettingsCreateDistinctAsset", differentSettingsCreateDistinctAsset},
        {"UnregisterInvalidatesHandle", unregisterInvalidatesHandle},
        {"MissingAssetReportsMissing", missingAssetReportsMissing},
        {"RefreshDetectsStaleAndMissing", refreshDetectsStaleAndMissing},
        {"DependencyEdgesAreDeterministic", dependencyEdgesAreDeterministic},
        {"FindByIdAndPath", findByIdAndPath},
        {"ImportedSceneRegistersDependencies", importedSceneRegistersDependencies},
        {"DiagnosticsCountsByTypeAndStatus", diagnosticsCountsByTypeAndStatus},
    };

    for (const auto& [name, test] : tests) {
        TestContext ctx{std::string{name}, failures};
        test(ctx);
    }

    if (failures.empty()) {
        std::cout << "Asset registry tests passed: " << tests.size() << '\n';
        return 0;
    }

    std::cerr << "Asset registry tests failed: " << failures.size() << '\n';
    for (const TestFailure& failure : failures) {
        std::cerr << "  " << failure.testName << ": " << failure.message << '\n';
    }
    return 1;
}
