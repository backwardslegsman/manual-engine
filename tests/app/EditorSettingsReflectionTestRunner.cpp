#include "App/EditorSettingsReflection.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
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

    using ObjectId = ManualEngine::App::EditorSettingsReflectedObjectId;
    using PropertyId = ManualEngine::App::EditorSettingsReflectedPropertyId;

    const Engine::ReflectedPropertyDescriptor* property(
        const Engine::ReflectionRegistry& registry,
        ObjectId object,
        PropertyId property)
    {
        return registry.property(static_cast<uint32_t>(object), static_cast<uint32_t>(property));
    }

    void descriptorRegistration(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry;
        ManualEngine::App::registerEditorSettingsReflectionDescriptors(registry);
        const auto objects = registry.objects();
        ctx.expect(objects.size() == 13, "unexpected editor settings object count");
        ctx.expect(objects.front().id == static_cast<uint32_t>(ObjectId::Project), "objects are sorted by id");
        ctx.expect(registry.object("EditorTerrainImportSettings") != nullptr, "terrain import descriptor missing");
        ctx.expect(registry.object("EditorCameraSettings") != nullptr, "camera descriptor missing");
        ctx.expect(registry.diagnostics().duplicateObjectCount == 0, "descriptor registration reported duplicate objects");
    }

    void metadataFlags(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry;
        ManualEngine::App::registerEditorSettingsReflectionDescriptors(registry);
        for (const Engine::ReflectedObjectDescriptor& object : registry.objects()) {
            ctx.expect(!object.displayName.empty(), "object display name missing");
            ctx.expect(!object.category.empty(), "object category missing");
            for (const Engine::ReflectedPropertyDescriptor& prop : object.properties) {
                ctx.expect(!prop.displayName.empty(), "property display name missing");
                ctx.expect(!prop.category.empty(), "property category missing");
                ctx.expect(
                    Engine::hasFlag(prop.flags, Engine::ReflectedPropertyFlag::EditorVisible),
                    "property missing editor-visible flag");
                ctx.expect(
                    Engine::hasFlag(prop.flags, Engine::ReflectedPropertyFlag::ScriptVisible),
                    "editor setting should be script-visible after E7");
                ctx.expect(
                    prop.type != Engine::ReflectedValueType::OpaqueHandle,
                    "editor settings reflection should not expose opaque handles");
            }
        }

        const auto* terrainSource = property(registry, ObjectId::TerrainImport, PropertyId::SourcePath);
        ctx.expect(terrainSource != nullptr, "terrain source path metadata missing");
        ctx.expect(
            Engine::hasFlag(terrainSource->flags, Engine::ReflectedPropertyFlag::RequiresExplicitApply),
            "terrain source path should require explicit apply");
        ctx.expect(terrainSource->type == Engine::ReflectedValueType::String, "source path type should be string");

        const auto* rendererDistance = property(registry, ObjectId::Renderer, PropertyId::PropMaxDrawDistance);
        ctx.expect(rendererDistance != nullptr, "renderer distance metadata missing");
        ctx.expect(
            !Engine::hasFlag(rendererDistance->flags, Engine::ReflectedPropertyFlag::RequiresExplicitApply),
            "renderer max draw distance should be lightweight");
        ctx.expect(rendererDistance->minimum.has_value(), "renderer max draw distance needs a minimum");

        const auto* channel = property(registry, ObjectId::TerrainImport, PropertyId::HeightmapChannel);
        ctx.expect(channel != nullptr && channel->enumLabels.size() == 5, "heightmap channel enum labels missing");

        const auto* navVerts = property(registry, ObjectId::NavigationBuild, PropertyId::MaxVertsPerPoly);
        ctx.expect(navVerts != nullptr && navVerts->minimum && navVerts->maximum, "nav verts min/max missing");
        ctx.expect(Engine::hasFlag(navVerts->flags, Engine::ReflectedPropertyFlag::Advanced), "detailed nav settings should be advanced");
    }

    void enumerateTargets(TestContext& ctx)
    {
        ManualEngine::App::EditorProjectSettings settings =
            ManualEngine::App::defaultEditorProjectSettings();
        const auto targets = ManualEngine::App::enumerateEditorSettingsTargets(settings);
        ctx.expect(targets.size() == 14, "expected singleton targets plus two LOD targets");
        uint32_t lodTargets = 0;
        for (const auto& target : targets) {
            if (target.object == ObjectId::TerrainRenderLod) {
                ++lodTargets;
            }
        }
        ctx.expect(lodTargets == settings.streaming.bake.renderLods.size(), "LOD target count did not match profile");
    }

    void getRepresentativeValues(TestContext& ctx)
    {
        ManualEngine::App::EditorProjectSettings settings =
            ManualEngine::App::defaultEditorProjectSettings();
        ManualEngine::App::EditorSettingsReflectionContext context{&settings};

        const Engine::ReflectionResult source = ManualEngine::App::getEditorSetting(
            context,
            {ObjectId::TerrainImport, 0},
            PropertyId::SourcePath);
        ctx.expect(source.status == Engine::ReflectionStatus::Success, "source path read failed");
        ctx.expect(std::get<std::string>(source.value).find("heightmaps") != std::string::npos, "source path value wrong");

        const Engine::ReflectionResult lod = ManualEngine::App::getEditorSetting(
            context,
            {ObjectId::TerrainRenderLod, 1},
            PropertyId::RenderResolution);
        ctx.expect(std::get<uint64_t>(lod.value) == 17, "indexed LOD read returned wrong value");

        const Engine::ReflectionResult nav = ManualEngine::App::getEditorSetting(
            context,
            {ObjectId::NavigationAgent, 0},
            PropertyId::AgentRadius);
        ctx.expect(std::get<float>(nav.value) == settings.streaming.bake.navAgent.radius, "nav agent value wrong");

        const Engine::ReflectionResult debug = ManualEngine::App::getEditorSetting(
            context,
            {ObjectId::DebugDraw, 0},
            PropertyId::ColliderShapes);
        ctx.expect(std::get<bool>(debug.value) == settings.debugDraw.colliderShapes, "debug draw value wrong");
    }

    void setRepresentativeValues(TestContext& ctx)
    {
        ManualEngine::App::EditorProjectSettings settings =
            ManualEngine::App::defaultEditorProjectSettings();
        ManualEngine::App::EditorSettingsReflectionContext context{&settings};

        Engine::ReflectionResult set = ManualEngine::App::setEditorSetting(
            context,
            {ObjectId::Renderer, 0},
            PropertyId::PropMaxDrawDistance,
            321.0f);
        ctx.expect(set.status == Engine::ReflectionStatus::Success && set.changed, "renderer set failed");
        ctx.expect(settings.renderer.propMaxDrawDistance == 321.0f, "renderer setting did not commit");

        set = ManualEngine::App::setEditorSetting(
            context,
            {ObjectId::TerrainRenderLod, 0},
            PropertyId::RenderResolution,
            uint64_t{21});
        ctx.expect(set.status == Engine::ReflectionStatus::Success, "LOD set failed");
        ctx.expect(settings.streaming.bake.renderLods[0].renderResolution == 21, "LOD setting did not commit");

        set = ManualEngine::App::setEditorSetting(
            context,
            {ObjectId::TerrainImport, 0},
            PropertyId::SampleSpacing,
            std::string{"bad"});
        ctx.expect(set.status == Engine::ReflectionStatus::TypeMismatch, "wrong type was not rejected");

        const float previousSpacing = settings.streaming.bake.heightmap.sampleSpacing;
        set = ManualEngine::App::setEditorSetting(
            context,
            {ObjectId::TerrainImport, 0},
            PropertyId::SampleSpacing,
            -1.0f);
        ctx.expect(set.status == Engine::ReflectionStatus::ValidationFailed, "invalid range was not rejected");
        ctx.expect(settings.streaming.bake.heightmap.sampleSpacing == previousSpacing, "invalid write mutated settings");

        set = ManualEngine::App::setEditorSetting(
            context,
            {ObjectId::TerrainRenderLod, 99},
            PropertyId::RenderResolution,
            uint64_t{9});
        ctx.expect(set.status == Engine::ReflectionStatus::InvalidHandle, "bad LOD index was not rejected");

        set = ManualEngine::App::setEditorSetting(
            context,
            {ObjectId::Project, 0},
            PropertyId::SchemaVersion,
            uint64_t{2});
        ctx.expect(set.status == Engine::ReflectionStatus::ReadOnly, "read-only schema was not rejected");
    }

    void descriptorDefaultsMatchProfile(TestContext& ctx)
    {
        Engine::ReflectionRegistry registry;
        ManualEngine::App::registerEditorSettingsReflectionDescriptors(registry);
        const ManualEngine::App::EditorProjectSettings defaults =
            ManualEngine::App::defaultEditorProjectSettings();

        const auto* source = property(registry, ObjectId::TerrainImport, PropertyId::SourcePath);
        ctx.expect(
            source && std::get<std::string>(source->defaultValue) ==
                defaults.streaming.bake.heightmap.sourcePath.generic_string(),
            "source path default does not match profile");

        const auto* cameraDistance = property(registry, ObjectId::Camera, PropertyId::CameraDistance);
        ctx.expect(
            cameraDistance && std::get<float>(cameraDistance->defaultValue) == defaults.camera.distance,
            "camera distance default does not match profile");
    }

    const std::vector<std::pair<std::string, void (*)(TestContext&)>> Tests = {
        {"DescriptorRegistration", descriptorRegistration},
        {"MetadataFlags", metadataFlags},
        {"EnumerateTargets", enumerateTargets},
        {"GetRepresentativeValues", getRepresentativeValues},
        {"SetRepresentativeValues", setRepresentativeValues},
        {"DescriptorDefaultsMatchProfile", descriptorDefaultsMatchProfile},
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
        std::cerr << ctx.failures << " editor settings reflection test failure(s)\n";
        return 1;
    }
    std::cout << Tests.size() << " editor settings reflection tests passed\n";
    return 0;
}
