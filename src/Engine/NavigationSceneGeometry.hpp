#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/ChunkTypes.hpp"
#include "Engine/Navigation.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Renderer/Scene.hpp"

namespace Engine {
    struct SceneNavigationSourceHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    enum class SceneNavigationSourceType {
        StaticMesh,
        Terrain,
        Collider,
        Generated,
    };

    enum class SceneNavigationSourceRole {
        Walkable,
        Blocker,
    };

    enum class SceneNavigationDirtyReason : uint32_t {
        None = 0,
        Transform = 1u << 0u,
        Geometry = 1u << 1u,
        Enabled = 1u << 2u,
        Removed = 1u << 3u,
    };

    struct SceneNavigationSourceDescriptor {
        std::optional<SceneActorHandle> actor;
        SceneNavigationSourceType type = SceneNavigationSourceType::StaticMesh;
        SceneNavigationSourceRole role = SceneNavigationSourceRole::Walkable;
        bool enabled = true;
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;
        std::optional<Renderer::Aabb> localBounds;
        std::string debugName;
    };

    struct SceneNavigationBuildRequest {
        ChunkCoord coord{};
        Renderer::Aabb bounds{};
        bool captureDebug = false;
    };

    enum class SceneNavigationDebugRequestType {
        SourceBounds,
        IncludedTriangles,
        IgnoredSource,
        DirtyChunk,
        BuildBounds,
    };

    struct SceneNavigationDebugRequest {
        SceneNavigationDebugRequestType type = SceneNavigationDebugRequestType::IgnoredSource;
        SceneNavigationSourceHandle source;
        ChunkCoord coord{};
        Renderer::Aabb bounds{};
        std::vector<glm::vec3> points;
        std::string message;
    };

    struct SceneNavigationGeometryDiagnostics {
        uint32_t registeredSourceCount = 0;
        uint32_t enabledSourceCount = 0;
        uint32_t walkableSourceCount = 0;
        uint32_t blockerSourceCount = 0;
        uint32_t includedSourceCount = 0;
        uint32_t skippedSourceCount = 0;
        uint32_t invalidActorCount = 0;
        uint32_t invalidGeometryCount = 0;
        uint32_t nonFiniteGeometryCount = 0;
        uint32_t outOfBoundsSourceCount = 0;
        uint32_t disabledSourceCount = 0;
        uint32_t walkableVertexCount = 0;
        uint32_t walkableTriangleCount = 0;
        uint32_t blockerVertexCount = 0;
        uint32_t blockerTriangleCount = 0;
        uint32_t dirtySourceCount = 0;
        uint32_t dirtyChunkCount = 0;
        uint32_t debugRequestCount = 0;
        std::vector<std::string> warnings;
    };

    [[nodiscard]] constexpr bool isValid(SceneNavigationSourceHandle handle)
    {
        return handle.index != UINT32_MAX && handle.generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(SceneNavigationSourceHandle lhs, SceneNavigationSourceHandle rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    [[nodiscard]] constexpr bool operator!=(SceneNavigationSourceHandle lhs, SceneNavigationSourceHandle rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] constexpr SceneNavigationDirtyReason operator|(
        SceneNavigationDirtyReason lhs,
        SceneNavigationDirtyReason rhs)
    {
        return static_cast<SceneNavigationDirtyReason>(
            static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    [[nodiscard]] constexpr bool hasDirtyReason(
        SceneNavigationDirtyReason value,
        SceneNavigationDirtyReason reason)
    {
        return (static_cast<uint32_t>(value) & static_cast<uint32_t>(reason)) != 0u;
    }

    class SceneNavigationGeometryRegistry {
    public:
        [[nodiscard]] SceneNavigationSourceHandle registerSource(SceneNavigationSourceDescriptor descriptor);
        bool unregisterSource(SceneNavigationSourceHandle source);
        [[nodiscard]] bool contains(SceneNavigationSourceHandle source) const;
        [[nodiscard]] std::optional<SceneNavigationSourceDescriptor> descriptor(SceneNavigationSourceHandle source) const;
        bool setSourceEnabled(SceneNavigationSourceHandle source, bool enabled);
        bool setSourceGeometry(
            SceneNavigationSourceHandle source,
            std::vector<glm::vec3> vertices,
            std::vector<uint32_t> indices,
            std::optional<Renderer::Aabb> localBounds = std::nullopt);
        bool markSourceDirty(
            SceneNavigationSourceHandle source,
            SceneNavigationDirtyReason reason = SceneNavigationDirtyReason::Geometry);
        void refreshDirtySources(Scene& scene, float chunkSize);
        void clearDirty();
        [[nodiscard]] std::vector<ChunkCoord> dirtyChunks() const;

        [[nodiscard]] std::optional<NavigationTerrainBuildData> buildNavigationData(
            Scene& scene,
            const SceneNavigationBuildRequest& request,
            const NavigationTerrainBuildData* terrainBase = nullptr);

        [[nodiscard]] SceneNavigationGeometryDiagnostics diagnostics() const;
        [[nodiscard]] std::vector<SceneNavigationDebugRequest> debugRequests() const;
        void clearDebugRequests();

    private:
        struct SourceRecord {
            uint32_t generation = 0;
            bool occupied = false;
            SceneNavigationSourceDescriptor descriptor;
            std::optional<Renderer::Aabb> lastWorldBounds;
            SceneNavigationDirtyReason dirtyReason = SceneNavigationDirtyReason::None;
        };

        [[nodiscard]] SourceRecord* record(SceneNavigationSourceHandle source);
        [[nodiscard]] const SourceRecord* record(SceneNavigationSourceHandle source) const;
        [[nodiscard]] uint32_t nextGeneration(uint32_t generation) const;
        [[nodiscard]] std::optional<Renderer::Aabb> localBounds(const SourceRecord& source) const;
        [[nodiscard]] std::optional<Renderer::Aabb> worldBounds(Scene& scene, SourceRecord& source);
        void markChunksForBounds(const Renderer::Aabb& bounds, float chunkSize);
        void refreshDiagnostics();
        void appendDebug(SceneNavigationDebugRequest request);

        std::vector<SourceRecord> sources_;
        std::vector<uint32_t> freeSources_;
        std::vector<Renderer::Aabb> removedDirtyBounds_;
        std::vector<ChunkCoord> dirtyChunks_;
        SceneNavigationGeometryDiagnostics diagnostics_;
        std::vector<SceneNavigationDebugRequest> debugRequests_;
    };
}
