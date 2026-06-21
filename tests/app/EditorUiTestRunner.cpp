#include "App/EditorUi.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {
    using ObjectId = ManualEngine::App::EditorSettingsReflectedObjectId;
    using PropertyId = ManualEngine::App::EditorSettingsReflectedPropertyId;

    struct TestContext {
        int failures = 0;

        void expect(bool condition, const std::string& message)
        {
            if (!condition) {
                ++failures;
                std::cerr << "FAIL: " << message << '\n';
            }
        }
    };

    ManualEngine::App::EditorUiState makeState()
    {
        ManualEngine::App::EditorProjectSettings settings =
            ManualEngine::App::defaultEditorProjectSettings();
        ManualEngine::App::EditorUiState state;
        ManualEngine::App::initializeEditorUiState(
            state,
            settings,
            "projects/default.editor.yaml",
            "loaded for test",
            ManualEngine::App::validateEditorProjectSettings(settings));
        return state;
    }

    const ManualEngine::App::EditorUiPropertyRow* findRow(
        const std::vector<ManualEngine::App::EditorUiPropertyRow>& rows,
        ObjectId object,
        PropertyId property,
        uint32_t index = 0)
    {
        for (const ManualEngine::App::EditorUiPropertyRow& row : rows) {
            if (row.target.object == object && row.target.index == index && row.property == property) {
                return &row;
            }
        }
        return nullptr;
    }

    void initializationAndCategories(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState();
        ctx.expect(state.initialized, "editor UI state did not initialize");
        ctx.expect(state.reflectionRegistry.diagnostics().objectCount == 13, "reflection objects not registered");

        const std::vector<std::string> categories = ManualEngine::App::editorUiCategories(state);
        const std::vector<std::string> expected = {
            "Project",
            "Terrain Import",
            "Terrain Cache",
            "Render LODs",
            "Navigation Agent",
            "Navigation Build",
            "Terrain Navigation",
            "Scene Geometry Filtering",
            "Physics Colliders",
            "Streaming",
            "Renderer",
            "Debug Draw",
            "Camera",
        };
        for (const std::string& category : expected) {
            ctx.expect(
                std::find(categories.begin(), categories.end(), category) != categories.end(),
                "missing editor UI category: " + category);
        }
    }

    void rowGenerationDeterministic(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState a = makeState();
        ManualEngine::App::EditorUiState b = makeState();
        const auto rowsA = ManualEngine::App::buildEditorUiPropertyRows(a);
        const auto rowsB = ManualEngine::App::buildEditorUiPropertyRows(b);
        ctx.expect(!rowsA.empty(), "no editor UI rows generated");
        ctx.expect(rowsA.size() == rowsB.size(), "row generation size is not deterministic");
        if (!rowsA.empty() && !rowsB.empty()) {
            ctx.expect(rowsA.front().property == rowsB.front().property, "first row differs between runs");
            ctx.expect(rowsA.back().property == rowsB.back().property, "last row differs between runs");
        }
    }

    void advancedFiltering(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState();
        state.showAdvanced = false;
        const auto hiddenRows = ManualEngine::App::buildEditorUiPropertyRows(state);
        uint32_t hiddenAdvanced = 0;
        for (const auto& row : hiddenRows) {
            if (row.advanced && !row.visible) {
                ++hiddenAdvanced;
            }
        }
        ctx.expect(hiddenAdvanced > 0, "advanced rows were not hidden by default");

        state.showAdvanced = true;
        const auto visibleRows = ManualEngine::App::buildEditorUiPropertyRows(state);
        uint32_t visibleAdvanced = 0;
        for (const auto& row : visibleRows) {
            if (row.advanced && row.visible) {
                ++visibleAdvanced;
            }
        }
        ctx.expect(visibleAdvanced == hiddenAdvanced, "advanced rows did not become visible");
    }

    void dirtyClassification(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState();
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::TerrainImport, 0},
            PropertyId::SourcePath,
            std::string{"assets/heightmaps/edited.png"});
        (void)ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::Renderer, 0},
            PropertyId::PropMaxDrawDistance,
            321.0f);

        const auto rows = ManualEngine::App::buildEditorUiPropertyRows(state);
        const ManualEngine::App::EditorUiDirtySummary summary =
            ManualEngine::App::editorUiDirtySummary(rows);
        ctx.expect(summary.dirtyPropertyCount == 2, "expected two dirty properties");
        ctx.expect(summary.rebuildRequiredCount == 1, "expected one rebuild-affecting dirty property");
        ctx.expect(summary.lightweightPendingCount == 1, "expected one lightweight pending property");

        const auto* source = findRow(rows, ObjectId::TerrainImport, PropertyId::SourcePath);
        const auto* distance = findRow(rows, ObjectId::Renderer, PropertyId::PropMaxDrawDistance);
        ctx.expect(source && source->dirty && source->requiresExplicitApply, "source path dirty metadata wrong");
        ctx.expect(distance && distance->dirty && distance->lightweightPending, "renderer dirty metadata wrong");
    }

    void invalidEditDoesNotMutate(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState();
        const float before = state.settings.streaming.bake.heightmap.sampleSpacing;
        const Engine::ReflectionResult result = ManualEngine::App::setEditorUiValue(
            state,
            {ObjectId::TerrainImport, 0},
            PropertyId::SampleSpacing,
            -1.0f);
        ctx.expect(result.status == Engine::ReflectionStatus::ValidationFailed, "invalid edit was not rejected");
        ctx.expect(state.settings.streaming.bake.heightmap.sampleSpacing == before, "invalid edit mutated settings");
        ctx.expect(state.lastEditResult.status == Engine::ReflectionStatus::ValidationFailed, "last edit result not recorded");
    }

    void indexedRenderLodsAndReadOnlyRows(TestContext& ctx)
    {
        ManualEngine::App::EditorUiState state = makeState();
        const auto rows = ManualEngine::App::buildEditorUiPropertyRows(state);

        uint32_t lodIndexRows = 0;
        for (const auto& row : rows) {
            if (row.target.object == ObjectId::TerrainRenderLod &&
                row.property == PropertyId::LodIndex) {
                ++lodIndexRows;
                ctx.expect(row.readOnly, "LOD index row should be read-only");
            }
        }
        ctx.expect(
            lodIndexRows == state.settings.streaming.bake.renderLods.size(),
            "render LOD indexed rows do not match profile LOD count");

        const auto* schema = findRow(rows, ObjectId::Project, PropertyId::SchemaVersion);
        ctx.expect(schema && schema->readOnly, "schema version row should be read-only");
        ctx.expect(schema && schema->supported, "read-only schema row should still be displayable");
    }

    const std::vector<std::pair<std::string, void (*)(TestContext&)>> Tests = {
        {"InitializationAndCategories", initializationAndCategories},
        {"RowGenerationDeterministic", rowGenerationDeterministic},
        {"AdvancedFiltering", advancedFiltering},
        {"DirtyClassification", dirtyClassification},
        {"InvalidEditDoesNotMutate", invalidEditDoesNotMutate},
        {"IndexedRenderLodsAndReadOnlyRows", indexedRenderLodsAndReadOnlyRows},
    };
}

int main()
{
    TestContext ctx;
    for (const auto& [name, test] : Tests) {
        test(ctx);
        if (ctx.failures == 0) {
            std::cout << "PASS: " << name << '\n';
        }
    }
    if (ctx.failures != 0) {
        std::cerr << ctx.failures << " editor UI test failure(s)\n";
        return 1;
    }
    std::cout << Tests.size() << " editor UI tests passed\n";
    return 0;
}
