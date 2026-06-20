#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "Engine/DebugVisualization.hpp"

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

    bool near(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 0.0001f)
    {
        return std::abs(lhs.x - rhs.x) <= epsilon &&
            std::abs(lhs.y - rhs.y) <= epsilon &&
            std::abs(lhs.z - rhs.z) <= epsilon;
    }

    void DisabledCollectorAcceptsNoRequests(TestContext& ctx)
    {
        Engine::DebugVisualizationSettings settings = Engine::defaultDebugVisualizationSettings();
        settings.enabled = false;
        Engine::DebugVisualizationCollector collector{settings};

        ctx.expect(!collector.addLine(Engine::DebugVisualizationCategory::Physics, {0, 0, 0}, {1, 0, 0}), "disabled line rejected");
        ctx.expect(!collector.addLabel(Engine::DebugVisualizationCategory::Physics, {0, 0, 0}, "label"), "disabled label rejected");
        ctx.expect(!collector.addReportRow(Engine::DebugVisualizationCategory::Physics, "physics", "bodies", "1"), "disabled report rejected");

        const auto snapshot = collector.snapshot();
        ctx.expect(snapshot.lines.empty() && snapshot.labels.empty() && snapshot.reportRows.empty(), "disabled collector stores no output");
        ctx.expect(snapshot.diagnostics.skippedPrimitiveCount == 3, "disabled collector reports skipped requests");
    }

    void CategoryTogglesRejectDeterministicSubset(TestContext& ctx)
    {
        Engine::DebugVisualizationSettings settings = Engine::defaultDebugVisualizationSettings();
        settings.categories[static_cast<uint32_t>(Engine::DebugVisualizationCategory::Navigation)].enabled = false;
        Engine::DebugVisualizationCollector collector{settings};

        ctx.expect(!collector.addLine(Engine::DebugVisualizationCategory::Navigation, {0, 0, 0}, {1, 0, 0}), "disabled category rejected");
        ctx.expect(collector.addLine(Engine::DebugVisualizationCategory::Physics, {0, 0, 0}, {1, 0, 0}), "enabled category accepted");

        const auto snapshot = collector.snapshot();
        ctx.expect(snapshot.lines.size() == 1, "only enabled category output stored");
        ctx.expect(snapshot.lines[0].category == Engine::DebugVisualizationCategory::Physics, "stored output preserves category");
        ctx.expect(snapshot.diagnostics.skippedByCategory[static_cast<uint32_t>(Engine::DebugVisualizationCategory::Navigation)] == 1, "disabled category skip counted");
    }

    void PerCategoryAndGlobalBudgetsCapOutput(TestContext& ctx)
    {
        Engine::DebugVisualizationSettings settings = Engine::defaultDebugVisualizationSettings();
        settings.maxPrimitives = 3;
        settings.categories[static_cast<uint32_t>(Engine::DebugVisualizationCategory::Physics)].maxPrimitives = 2;
        Engine::DebugVisualizationCollector collector{settings};

        ctx.expect(collector.addLine(Engine::DebugVisualizationCategory::Physics, {0, 0, 0}, {1, 0, 0}), "physics line 1 accepted");
        ctx.expect(collector.addLine(Engine::DebugVisualizationCategory::Physics, {0, 1, 0}, {1, 1, 0}), "physics line 2 accepted");
        ctx.expect(!collector.addLine(Engine::DebugVisualizationCategory::Physics, {0, 2, 0}, {1, 2, 0}), "physics category cap rejects line 3");
        ctx.expect(collector.addLine(Engine::DebugVisualizationCategory::Terrain, {0, 0, 1}, {1, 0, 1}), "terrain line accepted until global cap");
        ctx.expect(!collector.addLine(Engine::DebugVisualizationCategory::Terrain, {0, 0, 2}, {1, 0, 2}), "global cap rejects line");

        const auto diagnostics = collector.diagnostics();
        ctx.expect(diagnostics.acceptedPrimitiveCount == 3, "accepted count honors budgets");
        ctx.expect(diagnostics.cappedPrimitiveCount == 2, "capped requests counted");
        ctx.expect(diagnostics.lastCappedCategory == Engine::DebugVisualizationCategory::Terrain, "last capped category reported");
    }

    void ClippingRejectsOutOfRangePrimitives(TestContext& ctx)
    {
        Engine::DebugVisualizationSettings settings = Engine::defaultDebugVisualizationSettings();
        settings.distanceClippingEnabled = true;
        settings.clipCenter = {0.0f, 0.0f, 0.0f};
        settings.maxDistance = 2.0f;
        Engine::DebugVisualizationCollector collector{settings};

        ctx.expect(collector.addLine(Engine::DebugVisualizationCategory::SceneTransforms, {0, 0, 0}, {1, 0, 0}), "near primitive accepted");
        ctx.expect(!collector.addLine(Engine::DebugVisualizationCategory::SceneTransforms, {10, 0, 0}, {11, 0, 0}), "far primitive clipped");

        const auto snapshot = collector.snapshot();
        ctx.expect(snapshot.lines.size() == 1, "only unclipped line stored");
        ctx.expect(snapshot.diagnostics.clippedPrimitiveCount == 1, "clipped request counted");
    }

    void DeterministicOrderingIsStable(TestContext& ctx)
    {
        Engine::DebugVisualizationCollector collector;
        (void)collector.addLine(Engine::DebugVisualizationCategory::SceneTransforms, {0, 0, 0}, {1, 0, 0}, 0xff000001, Engine::DebugVisualizationSeverity::Info, "first");
        (void)collector.addLine(Engine::DebugVisualizationCategory::Physics, {0, 1, 0}, {1, 1, 0}, 0xff000002, Engine::DebugVisualizationSeverity::Info, "second");
        (void)collector.addAabb(Engine::DebugVisualizationCategory::Terrain, {{0, 0, 0}, {1, 1, 1}}, 0xff000003, Engine::DebugVisualizationSeverity::Info, "third");

        const auto snapshot = collector.snapshot();
        ctx.expect(snapshot.lines.size() == 2 && snapshot.shapes.size() == 1, "expected primitive counts");
        ctx.expect(snapshot.lines[0].source == "first" && snapshot.lines[1].source == "second", "line insertion order is stable");
        ctx.expect(snapshot.shapes[0].source == "third", "shape insertion order is stable");
    }

    void ReportRowsPreserveSeverityAndLabels(TestContext& ctx)
    {
        Engine::DebugVisualizationCollector collector;
        ctx.expect(collector.addReportRow(
            Engine::DebugVisualizationCategory::Lua,
            "script",
            "budget",
            "exceeded",
            Engine::DebugVisualizationSeverity::Warning,
            "instruction budget exhausted"), "report row accepted");

        const auto snapshot = collector.snapshot();
        ctx.expect(snapshot.reportRows.size() == 1, "report row stored");
        ctx.expect(snapshot.reportRows[0].category == Engine::DebugVisualizationCategory::Lua, "report category preserved");
        ctx.expect(snapshot.reportRows[0].severity == Engine::DebugVisualizationSeverity::Warning, "report severity preserved");
        ctx.expect(snapshot.reportRows[0].source == "script" && snapshot.reportRows[0].name == "budget", "report labels preserved");
        ctx.expect(snapshot.diagnostics.warningCount == 1, "warning count updated");
    }

    void PrimitiveTypesStorePlainDataOnly(TestContext& ctx)
    {
        Engine::DebugVisualizationLine line;
        Engine::DebugVisualizationShape shape;
        Engine::DebugVisualizationLabel label;
        Engine::DebugVisualizationReportRow row;

        ctx.expect(sizeof(line) > 0 && sizeof(shape) > 0 && sizeof(label) > 0 && sizeof(row) > 0, "plain debug request records exist");
        ctx.expect(std::string{Engine::debugVisualizationCategoryName(Engine::DebugVisualizationCategory::Lua)} == "Lua", "category names are stable");
        ctx.expect(std::string{Engine::debugVisualizationSeverityName(Engine::DebugVisualizationSeverity::Error)} == "Error", "severity names are stable");
    }

    void ExpandedLinesCoverPrimitiveShapes(TestContext& ctx)
    {
        Engine::DebugVisualizationCollector collector;
        (void)collector.addLine(Engine::DebugVisualizationCategory::SceneTransforms, {0, 0, 0}, {1, 0, 0}, 0xff0000ff);
        (void)collector.addAabb(Engine::DebugVisualizationCategory::SceneBounds, {{0, 0, 0}, {1, 1, 1}}, 0xff00ff00);
        (void)collector.addSphere(Engine::DebugVisualizationCategory::Physics, {0, 0, 0}, 1.0f, 0xff00ffff);
        (void)collector.addCapsule(Engine::DebugVisualizationCategory::CharacterMovement, {0, 0, 0}, {0, 2, 0}, 0.25f, 0xffff00ff);
        (void)collector.addTransformAxes(Engine::DebugVisualizationCategory::SceneTransforms, glm::mat4{1.0f}, 1.0f);

        const std::vector<Engine::DebugVisualizationExpandedLine> lines =
            Engine::expandDebugVisualizationLines(collector.snapshot());
        ctx.expect(lines.size() == 100, "line, aabb, sphere, capsule, and axes expand deterministically");
        ctx.expect(near(lines[0].a, {0, 0, 0}) && near(lines[0].b, {1, 0, 0}), "direct line converted first");
        ctx.expect(lines[0].color == 0xff0000ff, "direct line color preserved");
        ctx.expect(lines[1].category == Engine::DebugVisualizationCategory::SceneTransforms, "transform axes preserve category");
        ctx.expect(lines[16].category == Engine::DebugVisualizationCategory::Physics, "sphere expanded after aabb");
    }

    void HeaderDoesNotMentionRendererTypes(TestContext& ctx)
    {
        std::ifstream input{std::string{MANUAL_ENGINE_SOURCE_DIR} + "/src/Engine/DebugVisualization.hpp"};
        std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        ctx.expect(!contents.empty(), "debug visualization header readable");
        ctx.expect(contents.find("Renderer::") == std::string::npos, "debug visualization header does not expose Renderer types");
        ctx.expect(contents.find("DebugLinePrimitive") == std::string::npos, "debug visualization header does not expose renderer line primitives");
    }

    void DebugUiHeaderExposesModernOnlyState(TestContext& ctx)
    {
        std::ifstream input{std::string{MANUAL_ENGINE_SOURCE_DIR} + "/src/Renderer/DebugUi.hpp"};
        std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        ctx.expect(!contents.empty(), "debug UI header readable");
        ctx.expect(contents.find("ModernDebugUiState") != std::string::npos, "modern debug UI aggregate is exposed");
        ctx.expect(contents.find("PerformanceDebugStats") != std::string::npos, "modern performance stats are exposed");
        ctx.expect(contents.find("ModernSceneDebugStats") != std::string::npos, "modern scene stats are exposed");
        ctx.expect(contents.find("ModernTerrainDebugStats") != std::string::npos, "modern terrain stats are exposed");
        ctx.expect(contents.find("ModernNavigationDebugStats") != std::string::npos, "modern navigation stats are exposed");
        ctx.expect(contents.find("ModernPhysicsDebugStats") != std::string::npos, "modern physics stats are exposed");
        ctx.expect(contents.find("ModernCharacterDebugStats") != std::string::npos, "modern character stats are exposed");
        ctx.expect(contents.find("DebugVisualizationDebugStats") != std::string::npos, "modern debug visualization stats are exposed");
        ctx.expect(contents.find("OpenWorldStreamingDebugStats") != std::string::npos, "modern streaming stats are exposed");
        ctx.expect(contents.find("OpenWorldStreamingDebugStats streaming") != std::string::npos, "modern aggregate carries streaming stats");
        ctx.expect(contents.find("WorldSave") == std::string::npos, "legacy world save debug UI is not public");
        ctx.expect(contents.find("Biome") == std::string::npos, "legacy biome debug UI is not public");
        ctx.expect(contents.find("DebugPicking") == std::string::npos, "legacy picking debug UI is not public");
        ctx.expect(contents.find("InteractionDebug") == std::string::npos, "legacy interaction debug UI is not public");
        ctx.expect(contents.find("PlayerActor") == std::string::npos, "legacy player actor debug UI is not public");
        ctx.expect(contents.find("SpatialRegistry") == std::string::npos, "legacy spatial registry debug UI is not public");
        ctx.expect(contents.find("struct NavigationDebugControls") == std::string::npos, "legacy navigation controls are not public");
        ctx.expect(contents.find("struct NavigationDebugStats") == std::string::npos, "legacy navigation stats are not public");
        ctx.expect(contents.find("struct TerrainLodDebugStats") == std::string::npos, "legacy terrain LOD stats are not public");
        ctx.expect(contents.find("ActorController") == std::string::npos, "legacy actor controller is not public debug UI");
        ctx.expect(contents.find("ChunkStreamer") == std::string::npos, "legacy chunk streamer is not public debug UI");
        ctx.expect(contents.find("BlockingCollision") == std::string::npos, "legacy blocking collision is not public debug UI");
    }

    void ActiveCodeDoesNotMentionRemovedLegacySystems(TestContext& ctx)
    {
        const std::filesystem::path root{MANUAL_ENGINE_SOURCE_DIR};
        const std::vector<std::string> forbidden{
            "WorldObjectHandle",
            "ActorController",
            "BlockingCollisionSystem",
            "TerrainSystem",
            "ChunkStreamer",
            "WorldNavigationGraph",
            "PartitionedAuthoredScene",
            "SceneWorldMigrationBridge",
            "GeneratedTerrainTileData",
            "TerrainTileHandle",
            "TerrainRenderMeshBuildInput",
            "GameplayRuntimeMode",
            "LegacyProcedural",
            "LegacyAuthored",
        };
        const std::vector<std::filesystem::path> roots{
            root / "src",
            root / "tests",
        };

        auto shouldScan = [](const std::filesystem::path& path) {
            const std::string ext = path.extension().generic_string();
            return ext == ".cpp" || ext == ".hpp" || ext == ".inl";
        };

        for (const std::filesystem::path& scanRoot : roots) {
            for (const std::filesystem::directory_entry& entry :
                std::filesystem::recursive_directory_iterator(scanRoot)) {
                if (!entry.is_regular_file() || !shouldScan(entry.path())) {
                    continue;
                }
                if (entry.path().filename() == "DebugVisualizationTestRunner.cpp") {
                    continue;
                }
                std::ifstream input{entry.path()};
                const std::string contents(
                    (std::istreambuf_iterator<char>(input)),
                    std::istreambuf_iterator<char>());
                for (const std::string& name : forbidden) {
                    ctx.expect(
                        contents.find(name) == std::string::npos,
                        "active code mentions removed legacy symbol " + name + " in " +
                            entry.path().generic_string());
                }
                ctx.expect(
                    contents.find("AnimatedModel") == std::string::npos ||
                        contents.find("SceneAnimatedModelAdapter") != std::string::npos ||
                        contents.find("AnimatedModelPose") != std::string::npos,
                    "active code mentions legacy AnimatedModel owner in " + entry.path().generic_string());
            }
        }

        std::ifstream cmake{root / "CMakeLists.txt"};
        const std::string cmakeContents(
            (std::istreambuf_iterator<char>(cmake)),
            std::istreambuf_iterator<char>());
        for (const std::string& name : forbidden) {
            ctx.expect(cmakeContents.find(name) == std::string::npos, "CMake mentions removed legacy symbol " + name);
        }
    }

    void ClearResetsOutputAndDiagnostics(TestContext& ctx)
    {
        Engine::DebugVisualizationCollector collector;
        (void)collector.addLine(Engine::DebugVisualizationCategory::Physics, {0, 0, 0}, {1, 0, 0});
        (void)collector.addLabel(Engine::DebugVisualizationCategory::Physics, {0, 0, 0}, "label");
        collector.clear();

        const auto snapshot = collector.snapshot();
        ctx.expect(snapshot.lines.empty() && snapshot.labels.empty(), "clear removes output");
        ctx.expect(snapshot.diagnostics.acceptedPrimitiveCount == 0 && snapshot.diagnostics.labelCount == 0, "clear resets diagnostics");
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
        {"DisabledCollectorAcceptsNoRequests", DisabledCollectorAcceptsNoRequests},
        {"CategoryTogglesRejectDeterministicSubset", CategoryTogglesRejectDeterministicSubset},
        {"PerCategoryAndGlobalBudgetsCapOutput", PerCategoryAndGlobalBudgetsCapOutput},
        {"ClippingRejectsOutOfRangePrimitives", ClippingRejectsOutOfRangePrimitives},
        {"DeterministicOrderingIsStable", DeterministicOrderingIsStable},
        {"ReportRowsPreserveSeverityAndLabels", ReportRowsPreserveSeverityAndLabels},
        {"PrimitiveTypesStorePlainDataOnly", PrimitiveTypesStorePlainDataOnly},
        {"ExpandedLinesCoverPrimitiveShapes", ExpandedLinesCoverPrimitiveShapes},
        {"HeaderDoesNotMentionRendererTypes", HeaderDoesNotMentionRendererTypes},
        {"DebugUiHeaderExposesModernOnlyState", DebugUiHeaderExposesModernOnlyState},
        {"ActiveCodeDoesNotMentionRemovedLegacySystems", ActiveCodeDoesNotMentionRemovedLegacySystems},
        {"ClearResetsOutputAndDiagnostics", ClearResetsOutputAndDiagnostics},
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

    std::cout << "DebugVisualization tests passed (" << tests.size() << " cases)\n";
    return 0;
}
