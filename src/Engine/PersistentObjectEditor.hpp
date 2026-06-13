#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "Engine/ObjectArchetype.hpp"
#include "Engine/Picking.hpp"
#include "Engine/SpatialRegistry.hpp"
#include "Engine/World.hpp"
#include "Engine/WorldObjectOverrides.hpp"

namespace Engine {
    struct SelectedPersistentObject {
        WorldObjectHandle object;
        ObjectId objectId;
        std::string archetypeId;
        ChunkCoord chunk;
        Transform transform;
        bool isCustom = false;
        bool hasPersistentOverride = false;
    };

    struct PersistentObjectEditResult {
        std::string status;
        bool reloadChunks = false;
        bool clearSelection = false;
    };

    struct PersistentObjectEditorContext {
        World* world = nullptr;
        SpatialRegistry* spatialRegistry = nullptr;
        WorldObjectOverrides* overrides = nullptr;
        const ObjectArchetypeCatalog* archetypes = nullptr;
        float chunkSize = 16.0f;
    };

    // Applies debug/editor object mutations through WorldObjectOverrides so
    // procedural regeneration and save/load observe the same stable ObjectIds.
    class PersistentObjectEditor {
    public:
        explicit PersistentObjectEditor(PersistentObjectEditorContext context);

        std::optional<SelectedPersistentObject> selectedObject(const DebugSelectionState& selection) const;
        PersistentObjectEditResult removeObject(WorldObjectHandle object, ObjectId fallbackId, const glm::vec3& fallbackPosition);
        PersistentObjectEditResult persistSelectedTransform(const DebugSelectionState& selection);
        PersistentObjectEditResult resetSelectedOverride(const DebugSelectionState& selection);
        PersistentObjectEditResult applySelectedTransform(DebugSelectionState& selection, const Transform& transform);
        PersistentObjectEditResult nudgeSelected(DebugSelectionState& selection, const glm::vec3& delta);
        PersistentObjectEditResult rotateSelectedYaw(DebugSelectionState& selection, float degrees);
        PersistentObjectEditResult moveSelectedToTerrain(DebugSelectionState& selection);
        PersistentObjectEditResult placeArchetype(const TerrainPickHit& terrainHit, std::string_view archetypeId);

    private:
        ChunkCoord chunkForPosition(const glm::vec3& position) const;
        SavedPersistentObject makePersistentRecord(const SelectedPersistentObject& selected, const Transform& transform) const;
        bool validContext() const;

        PersistentObjectEditorContext context_;
    };
}
