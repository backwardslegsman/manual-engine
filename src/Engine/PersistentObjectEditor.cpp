#include "Engine/PersistentObjectEditor.hpp"

#include <algorithm>
#include <cmath>

namespace Engine {
    PersistentObjectEditor::PersistentObjectEditor(PersistentObjectEditorContext context)
        : context_(context)
    {
        context_.chunkSize = std::max(context_.chunkSize, 1.0f);
    }

    std::optional<SelectedPersistentObject> PersistentObjectEditor::selectedObject(const DebugSelectionState& selection) const
    {
        if (!validContext() || !selection.selectedObject || !context_.world->isValid(selection.selectedObject->object)) {
            return std::nullopt;
        }

        const WorldObjectHandle object = selection.selectedObject->object;
        const std::optional<ObjectId> selectedId = context_.world->objectId(object);
        const std::optional<Transform> transform = context_.world->transform(object);
        if (!selectedId || !selectedId->isValid() || !transform || *selectedId == ObjectId::player()) {
            return std::nullopt;
        }

        SelectedPersistentObject selected;
        selected.object = object;
        selected.objectId = *selectedId;
        selected.archetypeId = archetypeIdFromObjectId(*selectedId, "camp_marker");
        selected.chunk = chunkForPosition(transform->position);
        selected.transform = *transform;
        selected.isCustom = isCustomObjectId(*selectedId);
        selected.hasPersistentOverride = context_.overrides->persistentObject(*selectedId).has_value();
        return selected;
    }

    PersistentObjectEditResult PersistentObjectEditor::removeObject(
        WorldObjectHandle object,
        ObjectId fallbackId,
        const glm::vec3& fallbackPosition)
    {
        if (!validContext() || !context_.world->isValid(object)) {
            return {"Remove failed: no selected object."};
        }

        const std::optional<ObjectId> selectedId = context_.world->objectId(object);
        const ObjectId objectId = selectedId.value_or(fallbackId);
        if (!objectId.isValid() || objectId == ObjectId::player()) {
            return {"Remove failed: selected object has no removable stable id."};
        }

        if (isCustomObjectId(objectId)) {
            context_.overrides->removePersistent(objectId);
            return {"Removed selected custom persistent object.", true, true};
        }

        const glm::vec3 position = context_.world->position(object).value_or(fallbackPosition);
        context_.overrides->markRemoved(objectId, archetypeIdFromObjectId(objectId), chunkForPosition(position));
        return {"Removed selected procedural object.", true, true};
    }

    PersistentObjectEditResult PersistentObjectEditor::persistSelectedTransform(const DebugSelectionState& selection)
    {
        const std::optional<SelectedPersistentObject> selected = selectedObject(selection);
        if (!selected) {
            return {"Persist failed: no editable selected object."};
        }

        context_.overrides->upsertPersistent(makePersistentRecord(*selected, selected->transform));
        return {"Persisted selected object transform."};
    }

    PersistentObjectEditResult PersistentObjectEditor::resetSelectedOverride(const DebugSelectionState& selection)
    {
        const std::optional<SelectedPersistentObject> selected = selectedObject(selection);
        if (!selected) {
            return {"Reset failed: no editable selected object."};
        }

        if (selected->isCustom) {
            context_.overrides->removePersistent(selected->objectId);
            return {"Removed selected custom persistent object.", true, true};
        }

        if (!selected->hasPersistentOverride) {
            return {"Reset skipped: selected object has no persistent override."};
        }

        context_.overrides->removePersistent(selected->objectId);
        return {"Cleared selected procedural override.", true, true};
    }

    PersistentObjectEditResult PersistentObjectEditor::applySelectedTransform(DebugSelectionState& selection, const Transform& transform)
    {
        const std::optional<SelectedPersistentObject> selected = selectedObject(selection);
        if (!selected) {
            return {"Edit failed: no editable selected object."};
        }

        const ChunkCoord oldChunk = selected->chunk;
        const ChunkCoord newChunk = chunkForPosition(transform.position);
        context_.overrides->upsertPersistent(makePersistentRecord(*selected, transform));

        if (oldChunk != newChunk) {
            return {"Edited selected object and moved it to a different chunk.", true, true};
        }

        context_.world->setTransform(selected->object, transform);
        context_.spatialRegistry->update(selected->object, transform.position);
        if (selection.selectedObject) {
            selection.selectedObject->position = transform.position;
            selection.selectedObject->cell = newChunk;
            selection.selectedObject->objectId = selected->objectId;
        }
        context_.world->syncRenderState();
        return {"Edited selected object transform."};
    }

    PersistentObjectEditResult PersistentObjectEditor::nudgeSelected(DebugSelectionState& selection, const glm::vec3& delta)
    {
        const std::optional<SelectedPersistentObject> selected = selectedObject(selection);
        if (!selected) {
            return {"Edit failed: no editable selected object."};
        }

        Transform transform = selected->transform;
        transform.position += delta;
        return applySelectedTransform(selection, transform);
    }

    PersistentObjectEditResult PersistentObjectEditor::rotateSelectedYaw(DebugSelectionState& selection, float degrees)
    {
        const std::optional<SelectedPersistentObject> selected = selectedObject(selection);
        if (!selected) {
            return {"Edit failed: no editable selected object."};
        }

        Transform transform = selected->transform;
        transform.rotation.y += glm::radians(degrees);
        return applySelectedTransform(selection, transform);
    }

    PersistentObjectEditResult PersistentObjectEditor::moveSelectedToTerrain(DebugSelectionState& selection)
    {
        const std::optional<SelectedPersistentObject> selected = selectedObject(selection);
        if (!selected) {
            return {"Move failed: no editable selected object."};
        }
        if (!selection.terrainHit) {
            return {"Move failed: no terrain hit under cursor."};
        }

        Transform transform = selected->transform;
        transform.position = selection.terrainHit->position;
        if (context_.archetypes) {
            if (const ObjectArchetypeDescriptor* archetype = context_.archetypes->find(selected->archetypeId)) {
                transform.position.y += archetype->terrainYOffset;
            }
        }
        return applySelectedTransform(selection, transform);
    }

    PersistentObjectEditResult PersistentObjectEditor::placeArchetype(const TerrainPickHit& terrainHit, std::string_view archetypeId)
    {
        if (!validContext() || !context_.archetypes) {
            return {"Place failed: editor context is incomplete."};
        }

        const ObjectArchetypeDescriptor* archetype = context_.archetypes->find(archetypeId);
        if (!archetype) {
            return {"Place failed: archetype is missing."};
        }

        SavedPersistentObject record;
        record.id = context_.overrides->allocateCustomObjectId(archetypeId);
        record.archetypeId = std::string{archetypeId};
        record.chunk = terrainHit.chunk;
        record.hasChunk = true;
        record.position = terrainHit.position;
        record.position.y += archetype->terrainYOffset;
        record.rotation = {};
        record.scale = archetype->scale;
        context_.overrides->upsertPersistent(record);
        return {"Placed persistent archetype.", true, true};
    }

    ChunkCoord PersistentObjectEditor::chunkForPosition(const glm::vec3& position) const
    {
        const float safeChunkSize = std::max(context_.chunkSize, 1.0f);
        return {
            static_cast<int32_t>(std::floor(position.x / safeChunkSize)),
            static_cast<int32_t>(std::floor(position.z / safeChunkSize)),
        };
    }

    SavedPersistentObject PersistentObjectEditor::makePersistentRecord(
        const SelectedPersistentObject& selected,
        const Transform& transform) const
    {
        SavedPersistentObject record;
        record.id = selected.objectId;
        record.archetypeId = selected.archetypeId;
        record.chunk = chunkForPosition(transform.position);
        record.hasChunk = true;
        record.position = transform.position;
        record.rotation = transform.rotation;
        record.scale = transform.scale;
        return record;
    }

    bool PersistentObjectEditor::validContext() const
    {
        return context_.world && context_.spatialRegistry && context_.overrides;
    }
}
