#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>

#include <array>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ActorController.hpp"
#include "Engine/AssetCache.hpp"
#include "Engine/Biome.hpp"
#include "Engine/BlockingCollision.hpp"
#include "Engine/ChunkStreamer.hpp"
#include "Engine/EventQueue.hpp"
#include "Engine/FixedStepLoop.hpp"
#include "Engine/InputMapping.hpp"
#include "Engine/InteractionHandlerSystem.hpp"
#include "Engine/InteractionSystem.hpp"
#include "Engine/ObjectArchetype.hpp"
#include "Engine/OrbitCamera.hpp"
#include "Engine/PersistentObjectEditor.hpp"
#include "Engine/Picking.hpp"
#include "Engine/ProceduralChunkContent.hpp"
#include "Engine/SpatialRegistry.hpp"
#include "Engine/Terrain.hpp"
#include "Engine/World.hpp"
#include "Engine/WorldObjectOverrides.hpp"
#include "Engine/WorldState.hpp"
#include "Engine/input.hpp"
#include "Renderer/DebugUi.hpp"
#include "Renderer/Scene.hpp"
#include "Renderer/VertexLayouts.hpp"
#include "Renderer/core.hpp"

namespace {
    constexpr uint32_t WindowResetFlags = BGFX_RESET_VSYNC;
    constexpr float DebugPickMaxDistance = 500.0f;
    constexpr float DebugPickTerrainStep = 1.0f;
    constexpr float DebugPickObjectQueryMargin = 2.0f;

    struct RuntimeObjectArchetypeVisual {
        std::string id;
        Engine::CachedStaticMesh mesh;
        Engine::CachedTexture texture;
        Renderer::MaterialHandle material;
    };

    struct RuntimeBiomeTerrainMaterial {
        std::string biomeId;
        std::array<uint8_t, 4> color{};
        Engine::CachedTexture texture;
        Renderer::MaterialHandle material;
    };

    uint32_t clearColorFromLinearRgba(const glm::vec4& color)
    {
        const auto channel = [](float value) {
            return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
        };
        return (channel(color.r) << 24) |
            (channel(color.g) << 16) |
            (channel(color.b) << 8) |
            channel(color.a);
    }

    uint16_t viewportExtent(int value)
    {
        return static_cast<uint16_t>(std::max(value, 1));
    }

    Renderer::MaterialHandle createMaterial(
        const std::string& name,
        Renderer::TextureHandle baseColorTexture,
        const glm::vec4& baseColorFactor = glm::vec4{1.0f})
    {
        Renderer::MaterialDescriptor descriptor;
        descriptor.name = name;
        descriptor.baseColorTexture = baseColorTexture;
        descriptor.baseColorFactor = baseColorFactor;
        descriptor.metallicFactor = 0.0f;
        descriptor.roughnessFactor = 1.0f;
        return Renderer::createMaterial(descriptor);
    }

    const RuntimeObjectArchetypeVisual* findVisual(
        const std::vector<RuntimeObjectArchetypeVisual>& visuals,
        std::string_view archetypeId)
    {
        const auto visualIt = std::ranges::find_if(visuals, [archetypeId](const RuntimeObjectArchetypeVisual& visual) {
            return visual.id == archetypeId;
        });
        return visualIt == visuals.end() ? nullptr : &*visualIt;
    }

    const RuntimeBiomeTerrainMaterial* findTerrainMaterial(
        const std::vector<RuntimeBiomeTerrainMaterial>& materials,
        std::string_view biomeId)
    {
        const auto materialIt = std::ranges::find_if(materials, [biomeId](const RuntimeBiomeTerrainMaterial& material) {
            return material.biomeId == biomeId;
        });
        return materialIt == materials.end() ? nullptr : &*materialIt;
    }

    std::string joinTags(const std::vector<std::string>& tags)
    {
        std::string result;
        for (const std::string& tag : tags) {
            if (!result.empty()) {
                result += ", ";
            }
            result += tag;
        }
        return result;
    }

    bool pressedAction(const Engine::EventQueue& events, std::string_view actionName)
    {
        return std::ranges::any_of(events.inputActions(), [actionName](const Engine::InputActionEvent& event) {
            return event.action == actionName &&
                event.payloadType == Engine::InputActionPayloadType::Digital &&
                event.phase == Engine::InputActionPhase::Pressed;
        });
    }

    const char* cameraModeName(Engine::CameraMode mode)
    {
        switch (mode) {
            case Engine::CameraMode::Free:
                return "free";
            case Engine::CameraMode::FollowTarget:
                return "follow_target";
        }
        return "free";
    }

    void recenterCameraOnPlayer(Engine::OrbitCameraController& camera, const glm::vec3& playerPosition)
    {
        Engine::CameraState state = camera.state();
        state.pivot = playerPosition + state.followOffset;
        camera.setFollowTarget(playerPosition);
        camera.setState(state);
    }

    Renderer::DebugUi::CameraDebugStats makeCameraDebugStats(const Engine::OrbitCameraController& camera)
    {
        Renderer::DebugUi::CameraDebugStats stats;
        stats.followMode = camera.mode() == Engine::CameraMode::FollowTarget;
        stats.hasTarget = camera.followState().hasTarget;
        stats.pivot = camera.state().pivot;
        stats.targetPosition = camera.followState().targetPosition;
        stats.followOffset = camera.followState().offset;
        stats.followSmoothing = camera.followSettings().followSmoothing;
        stats.maxFollowLag = camera.followSettings().maxFollowLag;
        return stats;
    }

    Renderer::DebugUi::BiomeDebugStats makeBiomeDebugStats(
        const Engine::BiomeSystem& biomes,
        const Engine::TerrainSystem& terrain,
        const std::vector<RuntimeBiomeTerrainMaterial>& terrainMaterials,
        const std::array<uint8_t, 4>& fallbackTerrainColor,
        const Engine::OrbitCameraController& camera,
        const Engine::ActorController& actors,
        Engine::ActorHandle playerActor,
        const Engine::World& world,
        const Engine::DebugSelectionState& selection)
    {
        Renderer::DebugUi::BiomeDebugStats stats;
        stats.valid = true;

        const Engine::ChunkCoord cameraChunk = terrain.coordForWorldPosition(camera.state().pivot.x, camera.state().pivot.z);
        const Engine::BiomeSample cameraSample = biomes.sampleChunk(cameraChunk, terrain.settings().chunkSize);
        stats.cameraBiomeId = cameraSample.id;
        stats.cameraChunkX = cameraChunk.x;
        stats.cameraChunkZ = cameraChunk.z;
        if (const Engine::BiomeDescriptor* biome = biomes.descriptor(cameraSample.id)) {
            stats.cameraBiomeDisplayName = biome->displayName;
        }
        if (const RuntimeBiomeTerrainMaterial* material = findTerrainMaterial(terrainMaterials, cameraSample.id)) {
            stats.cameraTerrainMaterialBiomeId = material->biomeId;
            stats.cameraTerrainColor = material->color;
            stats.cameraTerrainUsesFallback = false;
        } else {
            stats.cameraTerrainMaterialBiomeId = cameraSample.id;
            stats.cameraTerrainColor = fallbackTerrainColor;
            stats.cameraTerrainUsesFallback = true;
        }

        if (const std::optional<glm::vec3> playerPosition = actors.position(playerActor, world)) {
            const Engine::ChunkCoord playerChunk = terrain.coordForWorldPosition(playerPosition->x, playerPosition->z);
            stats.hasPlayerBiome = true;
            stats.playerBiomeId = biomes.sampleChunk(playerChunk, terrain.settings().chunkSize).id;
            stats.playerChunkX = playerChunk.x;
            stats.playerChunkZ = playerChunk.z;
        }

        if (selection.hoveredObject) {
            stats.hasHoveredBiome = true;
            stats.hoveredBiomeId = biomes.sampleChunk(selection.hoveredObject->cell, terrain.settings().chunkSize).id;
            stats.hoveredChunkX = selection.hoveredObject->cell.x;
            stats.hoveredChunkZ = selection.hoveredObject->cell.z;
        }

        if (selection.terrainHit) {
            const Engine::BiomeSample hitSample =
                biomes.sample(selection.terrainHit->position.x, selection.terrainHit->position.z);
            stats.hasTerrainHitBiome = true;
            stats.terrainHitBiomeId = hitSample.id;
            stats.terrainHitChunkX = selection.terrainHit->chunk.x;
            stats.terrainHitChunkZ = selection.terrainHit->chunk.z;
            stats.moisture = hitSample.moisture;
            stats.roughness = hitSample.roughness;
            stats.elevation = hitSample.elevation;
        }

        return stats;
    }

    const char* interactionActionName(Engine::InteractionAction action)
    {
        switch (action) {
            case Engine::InteractionAction::Select:
                return "select";
            case Engine::InteractionAction::Interact:
                return "interact";
            case Engine::InteractionAction::RemoveObject:
                return "remove_object";
            case Engine::InteractionAction::PlaceMarker:
                return "place_marker";
        }
        return "unknown";
    }

    const char* interactionTargetName(Engine::InteractionTargetType target)
    {
        switch (target) {
            case Engine::InteractionTargetType::None:
                return "none";
            case Engine::InteractionTargetType::Object:
                return "object";
            case Engine::InteractionTargetType::Terrain:
                return "terrain";
        }
        return "unknown";
    }

    const char* interactionOutcomeName(Engine::InteractionOutcomeType outcome)
    {
        switch (outcome) {
            case Engine::InteractionOutcomeType::None:
                return "none";
            case Engine::InteractionOutcomeType::SelectObject:
                return "select_object";
            case Engine::InteractionOutcomeType::ClearSelection:
                return "clear_selection";
            case Engine::InteractionOutcomeType::Inspect:
                return "inspect";
            case Engine::InteractionOutcomeType::RemoveObject:
                return "remove_object";
            case Engine::InteractionOutcomeType::HarvestResource:
                return "harvest_resource";
            case Engine::InteractionOutcomeType::PlaceMarker:
                return "place_marker";
            case Engine::InteractionOutcomeType::Rejected:
                return "rejected";
        }
        return "unknown";
    }

    Renderer::DebugUi::InteractionDebugStats makeInteractionDebugStats(
        const Engine::InteractionOutcome& outcome)
    {
        const Engine::InteractionEvent& event = outcome.event;
        Renderer::DebugUi::InteractionDebugStats stats;
        stats.hasLastInteraction = true;
        stats.action = interactionActionName(event.action);
        stats.target = interactionTargetName(event.target);
        stats.outcome = interactionOutcomeName(outcome.type);
        stats.stableId = outcome.objectId.toString();
        stats.archetypeId = outcome.archetypeId;
        stats.archetypeDisplayName = outcome.archetypeDisplayName;
        stats.archetypeTags = outcome.archetypeTags;
        stats.chunkX = event.chunk.x;
        stats.chunkZ = event.chunk.z;
        stats.position = outcome.position;
        stats.distance = event.distance;
        stats.resourceId = outcome.resourceId;
        stats.resourceAmount = outcome.resourceAmount;
        stats.status = outcome.status;
        return stats;
    }

    Renderer::DebugUi::DebugPickingStats makeDebugPickingStats(
        const Engine::DebugSelectionState& selection,
        const Engine::World& world,
        const Engine::ObjectArchetypeCatalog& objectArchetypes,
        const Engine::WorldObjectOverrides& objectOverrides,
        const Engine::ChunkStreamer& chunkStreamer)
    {
        Renderer::DebugUi::DebugPickingStats stats;
        stats.mousePosition = selection.mousePosition;
        stats.rayOrigin = selection.ray.origin;
        stats.rayDirection = selection.ray.direction;

        if (selection.hoveredObject) {
            stats.hasHoveredObject = true;
            stats.hoveredObjectId = selection.hoveredObject->object.id;
            stats.hoveredStableId = selection.hoveredObject->objectId.toString();
            stats.hoveredObjectPosition = selection.hoveredObject->position;
            stats.hoveredObjectDistance = selection.hoveredObject->distance;
            stats.hoveredObjectCellX = selection.hoveredObject->cell.x;
            stats.hoveredObjectCellZ = selection.hoveredObject->cell.z;
        }

        if (selection.selectedObject && world.isValid(selection.selectedObject->object)) {
            stats.hasSelectedObject = true;
            stats.selectedObjectId = selection.selectedObject->object.id;
            stats.selectedStableId = world.objectId(selection.selectedObject->object)
                .value_or(selection.selectedObject->objectId)
                .toString();
            Engine::ObjectId selectedStableId = Engine::ObjectId::fromString(stats.selectedStableId);
            stats.selectedIsProcedural = Engine::parseProceduralObjectId(
                selectedStableId,
                stats.selectedArchetypeId,
                stats.selectedLocalSlot
            );
            if (stats.selectedArchetypeId.empty()) {
                stats.selectedArchetypeId = Engine::archetypeIdFromObjectId(selectedStableId);
            }
            if (const Engine::ObjectArchetypeDescriptor* archetype = objectArchetypes.find(stats.selectedArchetypeId)) {
                stats.selectedArchetypeDisplayName = archetype->displayName;
                stats.selectedArchetypeTags = joinTags(archetype->tags);
            }
            if (const std::optional<Engine::Transform> transform = world.transform(selection.selectedObject->object)) {
                stats.selectedObjectPosition = transform->position;
                stats.selectedObjectRotation = transform->rotation;
                stats.selectedObjectScale = transform->scale;
                const Engine::ChunkCoord chunk = chunkStreamer.coordForWorldPosition(transform->position);
                stats.selectedOwnerChunkX = chunk.x;
                stats.selectedOwnerChunkZ = chunk.z;
            } else {
                stats.selectedObjectPosition = world.position(selection.selectedObject->object).value_or(selection.selectedObject->position);
            }
            stats.selectedIsCustom = Engine::isCustomObjectId(selectedStableId);
            stats.selectedHasPersistentOverride = objectOverrides.persistentObject(selectedStableId).has_value();
            stats.selectedEditable = selectedStableId.isValid() && selectedStableId != Engine::ObjectId::player();
            stats.selectedCanReset = stats.selectedEditable && (stats.selectedIsCustom || stats.selectedHasPersistentOverride);
        }

        if (selection.terrainHit) {
            stats.hasTerrainHit = true;
            stats.terrainHitPosition = selection.terrainHit->position;
            stats.terrainHitDistance = selection.terrainHit->distance;
            stats.terrainHitChunkX = selection.terrainHit->chunk.x;
            stats.terrainHitChunkZ = selection.terrainHit->chunk.z;
        }

        return stats;
    }

    Engine::WorldStateSnapshot makeWorldStateSnapshot(
        const Engine::ActorController& actors,
        Engine::ActorHandle playerActor,
        const Engine::World& world,
        const Engine::OrbitCameraController& camera,
        const Engine::ChunkStreamer& chunkStreamer,
        const Engine::TerrainSystem& terrain,
        const Engine::WorldObjectOverrides& objectOverrides)
    {
        Engine::WorldStateSnapshot snapshot;
        snapshot.settings.seed = 0;
        snapshot.settings.chunkSize = chunkStreamer.settings().chunkSize;
        snapshot.settings.loadRadiusChunks = chunkStreamer.settings().loadRadiusChunks;
        snapshot.settings.terrainHeightScale = terrain.settings().heightScale;
        snapshot.player.position = actors.position(playerActor, world).value_or(camera.state().pivot);
        snapshot.camera = camera.state();
        objectOverrides.writeToSnapshot(snapshot);
        return snapshot;
    }

    Renderer::DebugUi::PlayerActorDebugStats makePlayerActorDebugStats(
        const Engine::ActorController& actors,
        Engine::ActorHandle playerActor,
        const Engine::World& world,
        const Engine::TerrainSystem& terrain)
    {
        Renderer::DebugUi::PlayerActorDebugStats stats;
        const std::optional<Engine::ActorState> state = actors.state(playerActor);
        if (!state || !world.isValid(state->object)) {
            return stats;
        }

        stats.valid = true;
        stats.worldObjectId = state->object.id;
        stats.stableId = world.objectId(state->object).value_or(Engine::ObjectId{}).toString();
        stats.position = world.position(state->object).value_or(glm::vec3{});
        stats.velocity = state->velocity;
        stats.facingRadians = state->facingRadians;
        stats.collisionEnabled = state->collisionEnabled;
        stats.collisionRadius = state->collisionRadius;
        stats.collisionHeight = state->collisionHeight;
        stats.blockedX = state->blockedX;
        stats.blockedZ = state->blockedZ;
        stats.collisionHitCount = state->collisionHitCount;
        stats.firstBlockingObjectId = state->firstBlockingObject.id;
        stats.firstBlockingStableId = state->firstBlockingObjectId.toString();
        if (const std::optional<float> groundHeight = terrain.sampleHeight(stats.position.x, stats.position.z)) {
            stats.hasGroundHeight = true;
            stats.groundHeight = *groundHeight;
        }
        return stats;
    }
}

int main(int, char**)
{
    SDL_Window* window = nullptr;
    if (Renderer::initWindow(window) != 0) {
        return 1;
    }

    if (Renderer::init_bgfx(window) != 0) {
        return 1;
    }

    Renderer::configureVertexLayouts();
    if (!Renderer::initSceneRenderer()) {
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    const bool debugUiEnabled = Renderer::DebugUi::init(window);
    if (!debugUiEnabled) {
        SDL_Log("Dear ImGui debug UI failed to initialize; continuing without debug UI.");
    }

    Renderer::AtmosphereSettings atmosphere = Renderer::atmosphereSettings();
    Renderer::setAtmosphereSettings(atmosphere);

    int currentWidth = 1280;
    int currentHeight = 720;
    SDL_GetWindowSize(window, &currentWidth, &currentHeight);
    currentWidth = std::max(currentWidth, 1);
    currentHeight = std::max(currentHeight, 1);
    bgfx::reset(static_cast<uint32_t>(currentWidth), static_cast<uint32_t>(currentHeight), WindowResetFlags);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColorFromLinearRgba(atmosphere.skyColor), 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, viewportExtent(currentWidth), viewportExtent(currentHeight));

    Engine::AssetCache assetCache;
    const Engine::CachedTexture yellowTexture = assetCache.acquireSolidTexture(220, 190, 70);
    const Engine::CachedTexture cyanTexture = assetCache.acquireSolidTexture(80, 190, 210);
    constexpr std::array<uint8_t, 4> FallbackTerrainColor{80, 190, 210, 255};
    const Renderer::MaterialHandle yellowMaterial = createMaterial("sample.player", yellowTexture.handle);
    const Renderer::MaterialHandle cyanMaterial = createMaterial("sample.terrain", cyanTexture.handle);

    const Engine::ObjectArchetypeLoadResult archetypeLoad =
        Engine::ObjectArchetypeCatalog::loadFromYaml("assets/config/object_archetypes.yaml");
    if (!archetypeLoad.success) {
        SDL_Log("Using default object archetypes: %s", archetypeLoad.error.c_str());
    }
    const Engine::ObjectArchetypeCatalog objectArchetypes = archetypeLoad.catalog;

    const Engine::BiomeLoadResult biomeLoad = Engine::BiomeSystem::loadFromYaml("assets/config/biomes.yaml");
    if (!biomeLoad.success) {
        SDL_Log("Using default biomes: %s", biomeLoad.error.c_str());
    }
    const Engine::BiomeSystem biomes = biomeLoad.system;

    std::vector<RuntimeBiomeTerrainMaterial> biomeTerrainMaterials;
    for (const Engine::BiomeDescriptor* biome : biomes.all()) {
        if (!biome) {
            continue;
        }

        const auto [r, g, b, a] = biome->terrainColor;
        Engine::CachedTexture terrainTexture = assetCache.acquireSolidTexture(r, g, b, a);
        const Renderer::MaterialHandle terrainMaterial =
            createMaterial("biome.terrain." + biome->id, terrainTexture.handle);
        biomeTerrainMaterials.push_back({
            biome->id,
            biome->terrainColor,
            terrainTexture,
            terrainMaterial,
        });
    }

    std::vector<RuntimeObjectArchetypeVisual> archetypeVisuals;
    for (const Engine::ObjectArchetypeDescriptor* archetype : objectArchetypes.all()) {
        if (!archetype) {
            continue;
        }

        Engine::CachedStaticMesh archetypeMesh = archetype->meshPath.empty()
            ? assetCache.acquireFallbackCubeMesh()
            : assetCache.acquireStaticMesh(archetype->meshPath);
        if (archetypeMesh.handle.id == UINT32_MAX) {
            SDL_Log("Falling back to generated cube mesh for archetype '%s'.", archetype->id.c_str());
            archetypeMesh = assetCache.acquireFallbackCubeMesh();
        }

        const auto [r, g, b, a] = archetype->solidColor;
        Engine::CachedTexture archetypeTexture = assetCache.acquireSolidTexture(r, g, b, a);
        const Renderer::MaterialHandle archetypeMaterial =
            createMaterial("archetype." + archetype->id, archetypeTexture.handle);
        archetypeVisuals.push_back({
            archetype->id,
            archetypeMesh,
            archetypeTexture,
            archetypeMaterial,
        });
    }

    Engine::CachedStaticMesh playerMesh = assetCache.acquireFallbackCubeMesh();
    const Renderer::StaticMeshHandle playerStaticMesh = playerMesh.handle;
    Renderer::DebugUi::RendererDebugSettings debugSettings;
    Renderer::DebugUi::WorldSaveDebugControls worldSaveControls;

    Engine::World world;
    Engine::ActorController actors;
    Engine::BlockingCollisionSystem blockingCollision;
    Engine::SpatialRegistry spatialRegistry({16.0f, true});
    Engine::TerrainSettings terrainSettings;
    terrainSettings.chunkSize = 16.0f;
    terrainSettings.resolution = 33;
    terrainSettings.heightScale = 1.25f;
    terrainSettings.biomes = &biomes;
    Engine::TerrainSystem terrain(terrainSettings);
    Engine::ChunkStreamer chunkStreamer({16.0f, 1});
    Engine::WorldObjectOverrides objectOverrides;
    const Engine::ProceduralChunkContentConfig chunkContentConfig =
        Engine::ProceduralChunkContentConfig::sampleOpenWorldConfig(objectArchetypes, &biomes);
    bool debugUiWantsMouse = false;
    bool debugUiWantsKeyboard = false;
    const auto terrainMaterialForBiome = [&](std::string_view biomeId) {
        if (const RuntimeBiomeTerrainMaterial* material = findTerrainMaterial(biomeTerrainMaterials, biomeId)) {
            return material->material;
        }
        return cyanMaterial;
    };
    const auto terrainMaterialForChunk = [&](Engine::ChunkCoord coord) {
        return terrainMaterialForBiome(terrain.sampleChunkBiome(coord).id);
    };
    const auto terrainMaterialForTile = [&](Engine::TerrainTileHandle handle) {
        const std::optional<Engine::BiomeSample> tileBiome = terrain.tileBiome(handle);
        return tileBiome ? terrainMaterialForBiome(tileBiome->id) : cyanMaterial;
    };
    const Engine::ChunkContentFactory chunkFactory =
        [&terrainMaterialForChunk, &debugSettings, &spatialRegistry, &chunkContentConfig, &objectOverrides, &archetypeVisuals](
            Engine::ChunkCoord coord,
            Engine::World& targetWorld,
            Engine::TerrainSystem& targetTerrain) {
            Engine::ChunkContent content;
            Renderer::RenderGroupDescriptor groupDescriptor;
            groupDescriptor.name = "chunk " + std::to_string(coord.x) + "," + std::to_string(coord.z);
            groupDescriptor.hasChunkCoord = true;
            groupDescriptor.chunkX = coord.x;
            groupDescriptor.chunkZ = coord.z;
            content.renderGroup = Renderer::createRenderGroup(groupDescriptor);
            const Renderer::MaterialHandle terrainMaterial = terrainMaterialForChunk(coord);
            content.terrain = targetTerrain.createTile(coord, terrainMaterial);
            Renderer::setTerrainMaterial(targetTerrain.rendererTerrain(content.terrain), terrainMaterial);
            Renderer::setTerrainRenderLayer(targetTerrain.rendererTerrain(content.terrain), Renderer::RenderLayer::Terrain);
            Renderer::setTerrainVisibilityFlags(targetTerrain.rendererTerrain(content.terrain), Renderer::VisibilityFlags::Visible);
            Renderer::setTerrainMaxDrawDistance(targetTerrain.rendererTerrain(content.terrain), debugSettings.terrainMaxDrawDistance);
            Renderer::setTerrainRenderGroup(targetTerrain.rendererTerrain(content.terrain), content.renderGroup);
            const std::vector<Engine::ProceduralPropSpawn> propSpawns = chunkContentConfig.propsForChunk(coord);
            content.objects.reserve(propSpawns.size());

            const float chunkSize = chunkContentConfig.settings().chunkSize;
            const glm::vec3 chunkOrigin{
                static_cast<float>(coord.x) * chunkSize,
                0.0f,
                static_cast<float>(coord.z) * chunkSize,
            };

            std::unordered_set<std::string> baselineObjectIds;
            const auto spawnObject = [&](
                const Engine::ObjectId& objectId,
                const glm::vec3& position,
                const glm::vec3& rotation,
                const glm::vec3& scale,
                const Renderer::Aabb& localBounds,
                const glm::vec3& angularVelocity,
                std::string_view archetypeId) {
                const RuntimeObjectArchetypeVisual* visual = findVisual(archetypeVisuals, archetypeId);
                if (!visual || visual->mesh.handle.id == UINT32_MAX) {
                    return;
                }

                const Engine::WorldObjectHandle object = targetWorld.createObject(objectId);
                const Renderer::MeshInstanceHandle instance = Renderer::createInstance(visual->mesh.handle);

                Renderer::setInstanceMaterial(instance, visual->material);
                Renderer::setInstanceRenderLayer(instance, Renderer::RenderLayer::Props);
                Renderer::setInstanceVisibilityFlags(instance, Renderer::VisibilityFlags::Visible);
                Renderer::setInstanceMaxDrawDistance(instance, debugSettings.propMaxDrawDistance);
                Renderer::setInstanceRenderGroup(instance, content.renderGroup);
                targetWorld.attachRendererInstance(object, instance);
                targetWorld.setPosition(object, position);
                targetWorld.setRotation(object, rotation);
                targetWorld.setScale(object, scale);
                targetWorld.setAngularVelocity(object, angularVelocity);
                targetWorld.setLocalBounds(object, localBounds);
                if (const Engine::ObjectArchetypeDescriptor* archetype = chunkContentConfig.archetypeById(archetypeId)) {
                    targetWorld.setCollisionEnabled(object, Engine::hasTag(*archetype, "blocking"));
                }
                spatialRegistry.insert(object, position);
                content.objects.push_back(object);
            };

            for (const Engine::ProceduralPropSpawn& spawn : propSpawns) {
                baselineObjectIds.insert(spawn.objectId.toString());
                if (objectOverrides.isRemoved(spawn.objectId)) {
                    continue;
                }

                if (const std::optional<Engine::SavedPersistentObject> persistent = objectOverrides.persistentObject(spawn.objectId)) {
                    if (persistent->chunk != coord) {
                        continue;
                    }

                    const Engine::ObjectArchetypeDescriptor* archetype = chunkContentConfig.archetypeById(persistent->archetypeId);
                    spawnObject(
                        persistent->id,
                        persistent->position,
                        persistent->rotation,
                        persistent->scale,
                        archetype ? archetype->localBounds : spawn.localBounds,
                        archetype ? archetype->angularVelocity : spawn.angularVelocity,
                        persistent->archetypeId
                    );
                    continue;
                }

                glm::vec3 position = chunkOrigin + spawn.localPosition;
                position.y = targetTerrain.sampleHeight(position.x, position.z).value_or(0.0f) + spawn.terrainYOffset;
                spawnObject(
                    spawn.objectId,
                    position,
                    {},
                    spawn.scale,
                    spawn.localBounds,
                    spawn.angularVelocity,
                    spawn.archetypeId
                );
            }

            for (const Engine::SavedPersistentObject& persistent : objectOverrides.persistentObjectsForChunk(coord)) {
                if (baselineObjectIds.contains(persistent.id.toString()) || objectOverrides.isRemoved(persistent.id)) {
                    continue;
                }

                const Engine::ObjectArchetypeDescriptor* archetype = chunkContentConfig.archetypeById(persistent.archetypeId);
                if (!archetype) {
                    continue;
                }

                spawnObject(
                    persistent.id,
                    persistent.position,
                    persistent.rotation,
                    persistent.scale,
                    archetype->localBounds,
                    archetype->angularVelocity,
                    persistent.archetypeId
                );
            }

            return content;
        };
    const auto applyDebugVisibilitySettings = [&]() {
        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle terrainTile, const std::vector<Engine::WorldObjectHandle>& objects, Renderer::RenderGroupHandle renderGroup) {
                const Renderer::TerrainHandle rendererTerrain = terrain.rendererTerrain(terrainTile);
                Renderer::setTerrainMaterial(rendererTerrain, terrainMaterialForTile(terrainTile));
                Renderer::setTerrainRenderLayer(rendererTerrain, Renderer::RenderLayer::Terrain);
                Renderer::setTerrainVisibilityFlags(rendererTerrain, Renderer::VisibilityFlags::Visible);
                Renderer::setTerrainMaxDrawDistance(rendererTerrain, debugSettings.terrainMaxDrawDistance);
                Renderer::setTerrainRenderGroup(rendererTerrain, renderGroup);

                for (Engine::WorldObjectHandle object : objects) {
                    const Renderer::MeshInstanceHandle instance = world.rendererInstance(object);
                    Renderer::setInstanceRenderLayer(instance, Renderer::RenderLayer::Props);
                    Renderer::setInstanceVisibilityFlags(instance, Renderer::VisibilityFlags::Visible);
                    Renderer::setInstanceMaxDrawDistance(instance, debugSettings.propMaxDrawDistance);
                    Renderer::setInstanceRenderGroup(instance, renderGroup);
                }
            }
        );
    };
    const auto updateSpatialRegistryForLoadedObjects = [&]() {
        chunkStreamer.forEachLoadedChunkContent(
            [&](Engine::TerrainTileHandle, const std::vector<Engine::WorldObjectHandle>& objects, Renderer::RenderGroupHandle) {
                for (Engine::WorldObjectHandle object : objects) {
                    const std::optional<glm::vec3> position = world.position(object);
                    if (position) {
                        spatialRegistry.update(object, *position);
                    } else {
                        spatialRegistry.remove(object);
                    }
                }
            }
        );
    };

    bool running = true;
    Engine::FixedStepLoop loop;
    Engine::InputState input;
    Engine::EventQueue events;
    Engine::InteractionSystem interactionSystem;
    Engine::InteractionHandlerSystem interactionHandlers;
    Engine::InputMappingLoadResult inputMappingLoad = Engine::InputMapping::loadFromYaml("assets/config/input.yaml");
    if (!inputMappingLoad.success) {
        SDL_Log("Using default input mapping: %s", inputMappingLoad.error.c_str());
    }
    Engine::InputMapping inputMapping = inputMappingLoad.mapping;

    Engine::CameraSettings cameraSettings;
    cameraSettings.minPivotXZ = {-50.0f, -50.0f};
    cameraSettings.maxPivotXZ = {50.0f, 50.0f};
    Engine::CameraState cameraState;
    cameraState.pivot = {0.0f, 0.0f, 0.0f};
    cameraState.yawRadians = 0.0f;
    cameraState.pitchRadians = glm::radians(-40.0f);
    cameraState.distance = 14.0f;
    Engine::OrbitCameraController camera(cameraSettings, cameraState);
    Renderer::DebugUi::CameraDebugControls cameraDebugControls;
    cameraDebugControls.followSmoothing = camera.followSettings().followSmoothing;
    cameraDebugControls.maxFollowLag = camera.followSettings().maxFollowLag;
    Engine::DebugSelectionState debugSelection;
    Renderer::DebugUi::InteractionDebugStats interactionStats;
    chunkStreamer.update(camera.state().pivot, world, terrain, chunkFactory, &spatialRegistry);
    terrain.updateLods(camera.position());
    applyDebugVisibilitySettings();

    const Engine::WorldObjectHandle playerObject = world.createObject(Engine::ObjectId::player());
    const Renderer::MeshInstanceHandle playerInstance = Renderer::createInstance(playerStaticMesh);
    Renderer::setInstanceMaterial(playerInstance, yellowMaterial);
    Renderer::setInstanceRenderLayer(playerInstance, Renderer::RenderLayer::Props);
    Renderer::setInstanceVisibilityFlags(playerInstance, Renderer::VisibilityFlags::Visible);
    Renderer::setInstanceMaxDrawDistance(playerInstance, 0.0f);
    world.attachRendererInstance(playerObject, playerInstance);
    world.setScale(playerObject, {0.55f, 1.15f, 0.55f});
    world.setLocalBounds(playerObject, {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}});
    world.setCollisionEnabled(playerObject, false);
    const Engine::ActorHandle playerActor = actors.createActor(playerObject, {6.0f, 1.15f, 20.0f});
    actors.setPosition(playerActor, {0.0f, 0.0f, 0.0f}, &terrain, &world);
    if (const std::optional<glm::vec3> playerPosition = world.position(playerObject)) {
        spatialRegistry.insert(playerObject, *playerPosition);
    }
    world.syncRenderState();

    const auto reloadLoadedChunks = [&]() {
        chunkStreamer.unloadAll(world, terrain, &spatialRegistry);
        chunkStreamer.update(camera.state().pivot, world, terrain, chunkFactory, &spatialRegistry);
        terrain.updateLods(camera.position());
        applyDebugVisibilitySettings();
        world.syncRenderState();
    };

    Engine::PersistentObjectEditor objectEditor{{
        &world,
        &spatialRegistry,
        &objectOverrides,
        &objectArchetypes,
        chunkStreamer.settings().chunkSize,
    }};

    const auto applyEditResult = [&](const Engine::PersistentObjectEditResult& result) {
        if (result.reloadChunks) {
            reloadLoadedChunks();
        }
        if (result.clearSelection) {
            debugSelection = {};
        }
        return result.status;
    };

    const auto resolveInteractionRequest = [&](const Engine::InteractionEvent& event) {
        Engine::InteractionRequest request;
        request.event = event;
        request.objectId = event.objectId;
        if (event.target == Engine::InteractionTargetType::Object && world.isValid(event.object)) {
            request.objectId = world.objectId(event.object).value_or(event.objectId);
        }

        request.archetypeId = Engine::archetypeIdFromObjectId(request.objectId);
        request.archetype = objectArchetypes.find(request.archetypeId);
        return request;
    };

    while (running) {
        loop.beginFrame();
        input.beginFrame();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (debugUiEnabled) {
                Renderer::DebugUi::processEvent(event);
            }
            bool sendToGameInput = true;
            switch (event.type) {
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                case SDL_EVENT_MOUSE_MOTION:
                case SDL_EVENT_MOUSE_WHEEL:
                    sendToGameInput = !debugUiWantsMouse;
                    break;
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP:
                case SDL_EVENT_TEXT_INPUT:
                    sendToGameInput = !debugUiWantsKeyboard;
                    break;
                default:
                    break;
            }
            if (sendToGameInput) {
                input.processEvent(event);
            }
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        int width = currentWidth;
        int height = currentHeight;
        SDL_GetWindowSize(window, &width, &height);
        width = std::max(width, 1);
        height = std::max(height, 1);
        if (width != currentWidth || height != currentHeight) {
            currentWidth = width;
            currentHeight = height;
            bgfx::reset(static_cast<uint32_t>(currentWidth), static_cast<uint32_t>(currentHeight), WindowResetFlags);
        }
        input.setViewportSize(width, height);
        if (debugUiEnabled) {
            Renderer::DebugUi::beginFrame(viewportExtent(width), viewportExtent(height));
        }
        debugUiWantsMouse = debugUiEnabled && Renderer::DebugUi::wantsMouseCapture();
        debugUiWantsKeyboard = debugUiEnabled && Renderer::DebugUi::wantsKeyboardCapture();
        inputMapping.publishEvents(input, events);
        camera.followSettings().followSmoothing = cameraDebugControls.followSmoothing;
        camera.followSettings().maxFollowLag = cameraDebugControls.maxFollowLag;

        const std::optional<glm::vec3> playerCameraTarget = actors.position(playerActor, world);
        if (pressedAction(events, "camera.toggle_follow")) {
            camera.setMode(camera.mode() == Engine::CameraMode::FollowTarget
                ? Engine::CameraMode::Free
                : Engine::CameraMode::FollowTarget);
            SDL_Log("Camera mode: %s", cameraModeName(camera.mode()));
        }
        if (pressedAction(events, "camera.recenter")) {
            if (camera.mode() == Engine::CameraMode::FollowTarget) {
                camera.resetFollowOffset();
            }
            if (playerCameraTarget) {
                recenterCameraOnPlayer(camera, *playerCameraTarget);
            }
        }
        if (cameraDebugControls.setFreeModeRequested) {
            cameraDebugControls.setFreeModeRequested = false;
            camera.setMode(Engine::CameraMode::Free);
        }
        if (cameraDebugControls.setFollowModeRequested) {
            cameraDebugControls.setFollowModeRequested = false;
            camera.setMode(Engine::CameraMode::FollowTarget);
        }
        if (cameraDebugControls.recenterRequested) {
            cameraDebugControls.recenterRequested = false;
            if (camera.mode() == Engine::CameraMode::FollowTarget) {
                camera.resetFollowOffset();
            }
            if (playerCameraTarget) {
                recenterCameraOnPlayer(camera, *playerCameraTarget);
            }
        }

        camera.update(events, loop.frameDeltaSeconds(), playerCameraTarget);
        chunkStreamer.update(camera.state().pivot, world, terrain, chunkFactory, &spatialRegistry);
        terrain.updateLods(camera.position());
        applyDebugVisibilitySettings();
        Renderer::setAtmosphereSettings(atmosphere);

        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColorFromLinearRgba(atmosphere.skyColor), 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, viewportExtent(width), viewportExtent(height));
        const bgfx::ViewId viewOrder[] = {0, 1};
        bgfx::setViewOrder(0, 2, viewOrder);

        const float aspect = static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
        const Engine::CameraMatrices cameraMatrices = camera.matrices(aspect);
        bgfx::setViewTransform(0, &cameraMatrices.view, &cameraMatrices.projection);
        const Renderer::RenderView renderView{
            0,
            cameraMatrices.view,
            cameraMatrices.projection,
            cameraMatrices.projection * cameraMatrices.view,
            camera.position(),
            viewportExtent(width),
            viewportExtent(height),
            debugSettings.layerMask,
            debugSettings.enableDistanceCulling,
        };

        while (loop.shouldRunFixedUpdate()) {
            world.fixedUpdate(loop.fixedDeltaSeconds());
            actors.fixedUpdate(playerActor, events, terrain, world, spatialRegistry, blockingCollision, loop.fixedDeltaSeconds());
            updateSpatialRegistryForLoadedObjects();
            if (const std::optional<glm::vec3> playerPosition = world.position(playerObject)) {
                spatialRegistry.update(playerObject, *playerPosition);
            }
            loop.consumeFixedUpdate();
        }
        world.syncRenderState();

        debugSelection.mousePosition = input.mousePosition();
        debugSelection.ray = Engine::screenPointToRay(
            debugSelection.mousePosition,
            input.viewportSize(),
            cameraMatrices.view,
            cameraMatrices.projection
        );
        debugSelection.hoveredObject = Engine::pickNearestObject(
            debugSelection.ray,
            spatialRegistry,
            world,
            DebugPickMaxDistance,
            DebugPickObjectQueryMargin
        );
        debugSelection.terrainHit = terrain.raycast(debugSelection.ray, DebugPickMaxDistance, DebugPickTerrainStep);
        if (debugSelection.selectedObject && !world.isValid(debugSelection.selectedObject->object)) {
            debugSelection.selectedObject = std::nullopt;
        }

        interactionSystem.publishEvents(events, debugSelection, events);
        for (const Engine::InteractionEvent& interaction : events.interactionEvents()) {
            Engine::InteractionOutcome outcome = interactionHandlers.handle(resolveInteractionRequest(interaction));
            if (outcome.type == Engine::InteractionOutcomeType::SelectObject) {
                if (outcome.event.target == Engine::InteractionTargetType::Object && world.isValid(outcome.event.object)) {
                    debugSelection.selectedObject = Engine::ObjectPickHit{
                        outcome.event.object,
                        world.objectId(outcome.event.object).value_or(outcome.objectId),
                        outcome.event.objectHitPosition,
                        outcome.event.distance,
                        outcome.event.chunk,
                    };
                }
            } else if (outcome.type == Engine::InteractionOutcomeType::ClearSelection) {
                debugSelection.selectedObject = std::nullopt;
            } else if (outcome.type == Engine::InteractionOutcomeType::RemoveObject) {
                outcome.status = applyEditResult(objectEditor.removeObject(
                    outcome.event.object,
                    outcome.event.objectId,
                    outcome.event.objectHitPosition
                ));
            } else if (outcome.type == Engine::InteractionOutcomeType::PlaceMarker) {
                outcome.status = outcome.event.target == Engine::InteractionTargetType::Terrain
                    ? applyEditResult(objectEditor.placeArchetype(
                        Engine::TerrainPickHit{
                            outcome.event.terrainHitPosition,
                            outcome.event.distance,
                            outcome.event.chunk,
                        },
                        worldSaveControls.placeArchetypeId.data()
                    ))
                    : "Place failed: no terrain hit under cursor.";
            }
            worldSaveControls.status = outcome.status;
            interactionStats = makeInteractionDebugStats(outcome);
        }

        const Renderer::SceneDrawStats drawStats = Renderer::drawScene(renderView);
        if (debugUiEnabled) {
            Renderer::DebugUi::TerrainLodDebugStats terrainLods;
            terrainLods.counts = terrain.lodCounts();
            const float spatialQueryRadius = 24.0f;
            const Engine::ChunkCoord currentSpatialCell = spatialRegistry.cellForPosition(camera.state().pivot);
            Renderer::DebugUi::SpatialRegistryDebugStats spatialStats;
            spatialStats.activeCells = static_cast<uint32_t>(spatialRegistry.activeCellCount());
            spatialStats.registeredObjects = static_cast<uint32_t>(spatialRegistry.registeredObjectCount());
            spatialStats.currentCellX = currentSpatialCell.x;
            spatialStats.currentCellZ = currentSpatialCell.z;
            spatialStats.objectsInCurrentCell = static_cast<uint32_t>(spatialRegistry.objectsInCell(currentSpatialCell).size());
            spatialStats.objectsNearCamera = static_cast<uint32_t>(spatialRegistry.objectsInRadius(camera.state().pivot, spatialQueryRadius).size());
            spatialStats.nearQueryRadius = spatialQueryRadius;
            const Renderer::DebugUi::DebugPickingStats pickingStats =
                makeDebugPickingStats(debugSelection, world, objectArchetypes, objectOverrides, chunkStreamer);
            const Renderer::DebugUi::PlayerActorDebugStats playerStats =
                makePlayerActorDebugStats(actors, playerActor, world, terrain);
            const Renderer::DebugUi::CameraDebugStats cameraStats = makeCameraDebugStats(camera);
            const Renderer::DebugUi::BiomeDebugStats biomeStats =
                makeBiomeDebugStats(
                    biomes,
                    terrain,
                    biomeTerrainMaterials,
                    FallbackTerrainColor,
                    camera,
                    actors,
                    playerActor,
                    world,
                    debugSelection);
            Renderer::DebugUi::showRendererDebug(
                drawStats,
                debugSettings,
                atmosphere,
                terrainLods,
                spatialStats,
                cameraStats,
                &cameraDebugControls,
                biomeStats,
                pickingStats,
                interactionStats,
                &worldSaveControls,
                playerStats
            );
            Renderer::DebugUi::render(1, viewportExtent(width), viewportExtent(height));
        }
        bgfx::touch(0);
        bgfx::frame();
        if (worldSaveControls.saveRequested) {
            worldSaveControls.saveRequested = false;
            std::string error;
            const Engine::WorldStateSnapshot snapshot =
                makeWorldStateSnapshot(actors, playerActor, world, camera, chunkStreamer, terrain, objectOverrides);
            if (Engine::saveWorldState(worldSaveControls.path.data(), snapshot, &error)) {
                worldSaveControls.status = "Saved world state.";
            } else {
                worldSaveControls.status = "Save failed: " + error;
            }
        }
        if (worldSaveControls.loadRequested) {
            worldSaveControls.loadRequested = false;
            const Engine::WorldStateIoResult result = Engine::loadWorldState(worldSaveControls.path.data());
            if (result.success) {
                objectOverrides.replaceFromSnapshot(result.snapshot, result.snapshot.settings.chunkSize);
                camera.setState(result.snapshot.camera);
                chunkStreamer.unloadAll(world, terrain, &spatialRegistry);
                spatialRegistry.clear();
                chunkStreamer.update(camera.state().pivot, world, terrain, chunkFactory, &spatialRegistry);
                terrain.updateLods(camera.position());
                actors.setPosition(playerActor, result.snapshot.player.position, &terrain, &world);
                if (const std::optional<glm::vec3> playerPosition = world.position(playerObject)) {
                    spatialRegistry.insert(playerObject, *playerPosition);
                    if (camera.mode() == Engine::CameraMode::FollowTarget) {
                        recenterCameraOnPlayer(camera, *playerPosition);
                    }
                }
                applyDebugVisibilitySettings();
                world.syncRenderState();
                debugSelection = {};
                worldSaveControls.status = "Loaded world state.";
            } else {
                worldSaveControls.status = "Load failed: " + result.error;
            }
        }
        if (worldSaveControls.removeSelectedRequested) {
            worldSaveControls.removeSelectedRequested = false;
            Engine::InteractionEvent interaction;
            interaction.action = Engine::InteractionAction::RemoveObject;
            if (debugSelection.selectedObject) {
                interaction.target = Engine::InteractionTargetType::Object;
                interaction.object = debugSelection.selectedObject->object;
                interaction.objectId = debugSelection.selectedObject->objectId;
                interaction.objectHitPosition = debugSelection.selectedObject->position;
                interaction.chunk = debugSelection.selectedObject->cell;
                interaction.distance = debugSelection.selectedObject->distance;
            }
            Engine::InteractionOutcome outcome = interactionHandlers.handle(resolveInteractionRequest(interaction));
            if (outcome.type == Engine::InteractionOutcomeType::RemoveObject) {
                outcome.status = applyEditResult(objectEditor.removeObject(
                    outcome.event.object,
                    outcome.event.objectId,
                    outcome.event.objectHitPosition
                ));
            }
            worldSaveControls.status = outcome.status;
            interactionStats = makeInteractionDebugStats(outcome);
        }
        if (worldSaveControls.persistSelectedRequested) {
            worldSaveControls.persistSelectedRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.persistSelectedTransform(debugSelection));
        }
        if (worldSaveControls.resetSelectedOverrideRequested) {
            worldSaveControls.resetSelectedOverrideRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.resetSelectedOverride(debugSelection));
        }
        if (worldSaveControls.nudgeSelectedPositiveXRequested) {
            worldSaveControls.nudgeSelectedPositiveXRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {worldSaveControls.editMoveStep, 0.0f, 0.0f}));
        }
        if (worldSaveControls.nudgeSelectedNegativeXRequested) {
            worldSaveControls.nudgeSelectedNegativeXRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {-worldSaveControls.editMoveStep, 0.0f, 0.0f}));
        }
        if (worldSaveControls.nudgeSelectedPositiveYRequested) {
            worldSaveControls.nudgeSelectedPositiveYRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {0.0f, worldSaveControls.editMoveStep, 0.0f}));
        }
        if (worldSaveControls.nudgeSelectedNegativeYRequested) {
            worldSaveControls.nudgeSelectedNegativeYRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {0.0f, -worldSaveControls.editMoveStep, 0.0f}));
        }
        if (worldSaveControls.nudgeSelectedPositiveZRequested) {
            worldSaveControls.nudgeSelectedPositiveZRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {0.0f, 0.0f, worldSaveControls.editMoveStep}));
        }
        if (worldSaveControls.nudgeSelectedNegativeZRequested) {
            worldSaveControls.nudgeSelectedNegativeZRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.nudgeSelected(debugSelection, {0.0f, 0.0f, -worldSaveControls.editMoveStep}));
        }
        if (worldSaveControls.rotateSelectedPositiveYawRequested) {
            worldSaveControls.rotateSelectedPositiveYawRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.rotateSelectedYaw(debugSelection, worldSaveControls.editRotateStepDegrees));
        }
        if (worldSaveControls.rotateSelectedNegativeYawRequested) {
            worldSaveControls.rotateSelectedNegativeYawRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.rotateSelectedYaw(debugSelection, -worldSaveControls.editRotateStepDegrees));
        }
        if (worldSaveControls.moveSelectedToTerrainRequested) {
            worldSaveControls.moveSelectedToTerrainRequested = false;
            worldSaveControls.status = applyEditResult(objectEditor.moveSelectedToTerrain(debugSelection));
        }
        if (worldSaveControls.placeArchetypeRequested) {
            worldSaveControls.placeArchetypeRequested = false;
            if (!debugSelection.terrainHit) {
                worldSaveControls.status = "Place failed: no terrain hit under cursor.";
            } else {
                Engine::InteractionEvent interaction;
                interaction.action = Engine::InteractionAction::PlaceMarker;
                interaction.target = Engine::InteractionTargetType::Terrain;
                interaction.terrainHitPosition = debugSelection.terrainHit->position;
                interaction.chunk = debugSelection.terrainHit->chunk;
                interaction.distance = debugSelection.terrainHit->distance;
                Engine::InteractionOutcome outcome = interactionHandlers.handle(resolveInteractionRequest(interaction));
                if (outcome.type == Engine::InteractionOutcomeType::PlaceMarker) {
                    outcome.status = applyEditResult(objectEditor.placeArchetype(
                        Engine::TerrainPickHit{
                            outcome.event.terrainHitPosition,
                            outcome.event.distance,
                            outcome.event.chunk,
                        },
                        worldSaveControls.placeArchetypeId.data()
                    ));
                }
                worldSaveControls.status = outcome.status;
                interactionStats = makeInteractionDebugStats(outcome);
            }
        }
        events.clear();
    }

    chunkStreamer.unloadAll(world, terrain, &spatialRegistry);
    spatialRegistry.clear();
    world.syncRenderState();

    Renderer::destroyMaterial(yellowMaterial);
    Renderer::destroyMaterial(cyanMaterial);
    for (const RuntimeBiomeTerrainMaterial& material : biomeTerrainMaterials) {
        Renderer::destroyMaterial(material.material);
    }
    for (const RuntimeObjectArchetypeVisual& visual : archetypeVisuals) {
        Renderer::destroyMaterial(visual.material);
    }

    assetCache.release(yellowTexture);
    assetCache.release(cyanTexture);
    for (const RuntimeBiomeTerrainMaterial& material : biomeTerrainMaterials) {
        assetCache.release(material.texture);
    }
    assetCache.release(playerMesh);
    for (const RuntimeObjectArchetypeVisual& visual : archetypeVisuals) {
        assetCache.release(visual.texture);
        assetCache.release(visual.mesh);
    }
    assetCache.shutdown();

    if (debugUiEnabled) {
        Renderer::DebugUi::shutdown();
    }
    Renderer::shutdownSceneRenderer();
    bgfx::shutdown();

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
