#include "Engine/TerrainRenderLodAdapter.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string_view>
#include <utility>

namespace Engine {
    namespace {
        constexpr AssetId LegacyProceduralTerrainSourceId{0x7465727261696e01ull};

        std::string cacheStatusLabel(TerrainDerivedCacheStatus status)
        {
            switch (status) {
                case TerrainDerivedCacheStatus::Hit: return "hit";
                case TerrainDerivedCacheStatus::Miss: return "miss";
                case TerrainDerivedCacheStatus::Stale: return "stale";
                case TerrainDerivedCacheStatus::Corrupt: return "corrupt";
                case TerrainDerivedCacheStatus::WriteSuccess: return "write-success";
                case TerrainDerivedCacheStatus::WriteFailed: return "write-failed";
                case TerrainDerivedCacheStatus::Cancelled: return "cancelled";
            }
            return "unknown";
        }

        void countCacheStatus(TerrainRenderLodBuildDiagnostics& diagnostics)
        {
            switch (diagnostics.cacheStatus) {
                case TerrainDerivedCacheStatus::Hit:
                    ++diagnostics.cacheHitCount;
                    break;
                case TerrainDerivedCacheStatus::Miss:
                    ++diagnostics.cacheMissCount;
                    break;
                case TerrainDerivedCacheStatus::Stale:
                    ++diagnostics.cacheStaleCount;
                    break;
                case TerrainDerivedCacheStatus::Corrupt:
                    ++diagnostics.cacheCorruptCount;
                    break;
                case TerrainDerivedCacheStatus::WriteSuccess:
                case TerrainDerivedCacheStatus::WriteFailed:
                case TerrainDerivedCacheStatus::Cancelled:
                    break;
            }
        }

        Renderer::MeshVertex toRendererVertex(const TerrainCpuMeshVertex& vertex)
        {
            Renderer::MeshVertex result{};
            result.px = vertex.position.x;
            result.py = vertex.position.y;
            result.pz = vertex.position.z;
            result.nx = vertex.normal.x;
            result.ny = vertex.normal.y;
            result.nz = vertex.normal.z;
            result.tx = vertex.tangent.x;
            result.ty = vertex.tangent.y;
            result.tz = vertex.tangent.z;
            result.tw = vertex.tangent.w;
            result.u = vertex.uv0.x;
            result.v = vertex.uv0.y;
            result.u1 = vertex.uv1.x;
            result.v1 = vertex.uv1.y;
            result.abgr = vertex.color;
            return result;
        }

        TerrainCpuMeshVertex toCpuVertex(const Renderer::MeshVertex& vertex)
        {
            TerrainCpuMeshVertex result;
            result.position = {vertex.px, vertex.py, vertex.pz};
            result.normal = {vertex.nx, vertex.ny, vertex.nz};
            result.tangent = {vertex.tx, vertex.ty, vertex.tz, vertex.tw};
            result.uv0 = {vertex.u, vertex.v};
            result.uv1 = {vertex.u1, vertex.v1};
            result.color = vertex.abgr;
            return result;
        }

        TerrainRenderMeshBuildResult renderMeshBuildResultFromCachedLod(
            const TerrainCachedLodMeshPayload& payload,
            const TerrainRenderLodBuildRequest& request)
        {
            TerrainRenderMeshBuildResult result;
            result.success = !payload.vertices.empty() && !payload.indices.empty();
            result.mesh = renderMeshDataFromCachedLodPayload(payload, request);
            result.message = result.success ? "Terrain render LOD loaded from cache." : "Cached terrain render LOD payload was empty.";
            return result;
        }
    }

    TerrainRenderLodSourceIdentity legacyProceduralTerrainRenderLodIdentity(const TerrainSettings& settings)
    {
        TerrainRenderLodSourceIdentity identity;
        identity.sourceId = LegacyProceduralTerrainSourceId;
        identity.sourceType = TerrainDatasetSourceType::Procedural;
        identity.importSettings = {"legacy_procedural_terrain", "t4", "lod_adapter"};

        std::ostringstream stream;
        stream << "chunk=" << settings.chunkSize
               << ";resolution=" << settings.resolution
               << ";heightScale=" << settings.heightScale
               << ";skirt=" << settings.skirtDepth
               << ";navResolution=" << settings.navigationResolution;
        for (uint32_t index = 0; index < settings.lodLevels.size(); ++index) {
            stream << ";lod" << index << "="
                   << settings.lodLevels[index].startDistance << ','
                   << settings.lodLevels[index].resolution;
        }
        identity.sourceHash = stream.str();
        return identity;
    }

    std::optional<TerrainRenderLodBuildRequest> renderLodRequestFromTerrainSystemInput(
        const TerrainRenderMeshBuildInput& input,
        TerrainRenderLodSourceIdentity identity,
        TerrainDerivedCacheSettings cacheSettings)
    {
        if (input.cpuResolution < 2 ||
            input.size <= 0.0f ||
            input.heights.size() != static_cast<size_t>(input.cpuResolution) * input.cpuResolution) {
            return std::nullopt;
        }

        TerrainRenderLodBuildRequest request;
        request.tile = input.tile;
        request.chunkId = {identity.sourceId, {input.coord.x, input.coord.z}};
        request.coord = input.coord;
        request.generation = input.generation;
        request.lodIndex = input.lodIndex;
        request.renderResolution = std::max(input.renderResolution, 2u);
        request.origin = input.origin;
        request.size = input.size;
        request.cpuResolution = input.cpuResolution;
        request.heights = input.heights;
        request.skirtDepth = input.skirtDepth;
        request.identity = std::move(identity);
        request.cacheSettings = std::move(cacheSettings);
        return request;
    }

    std::optional<TerrainRenderLodBuildRequest> renderLodRequestFromDatasetChunk(
        const TerrainDataset& dataset,
        TerrainChunkHandle chunk,
        const TerrainLodMeshBuildSettings& lod,
        TerrainRenderLodSourceIdentity identity,
        TerrainTileHandle tile,
        uint64_t generation,
        TerrainDerivedCacheSettings cacheSettings)
    {
        const std::optional<TerrainChunkData> data = dataset.chunk(chunk);
        if (!data ||
            data->resolution < 2 ||
            data->size <= 0.0f ||
            data->heights.size() != static_cast<size_t>(data->resolution) * data->resolution) {
            return std::nullopt;
        }

        TerrainRenderLodBuildRequest request;
        request.tile = tile;
        request.chunkId = data->id;
        request.coord = {data->coord.x, data->coord.z};
        request.generation = generation;
        request.lodIndex = lod.lodIndex;
        request.renderResolution = std::max(lod.renderResolution, 2u);
        request.origin = data->origin;
        request.size = data->size;
        request.cpuResolution = data->resolution;
        request.heights = data->heights;
        request.skirtDepth = lod.skirtDepth;
        request.identity = std::move(identity);
        request.identity.sourceType = data->sourceType;
        request.cacheSettings = std::move(cacheSettings);
        return request;
    }

    TerrainCachedChunkPayload cachedChunkPayloadFromRenderLodRequest(const TerrainRenderLodBuildRequest& request)
    {
        TerrainCachedChunkPayload payload;
        payload.chunkId = request.chunkId;
        payload.origin = request.origin;
        payload.size = request.size;
        payload.resolution = request.cpuResolution;
        payload.heights = request.heights;
        payload.sourceType = request.identity.sourceType;
        payload.importSettings = request.identity.importSettings;
        return payload;
    }

    TerrainCachedLodMeshPayload cachedLodPayloadFromRenderMesh(
        const TerrainRenderMeshData& mesh,
        const TerrainRenderLodBuildRequest& request)
    {
        TerrainCachedLodMeshPayload payload;
        payload.chunkId = request.chunkId;
        payload.lodIndex = mesh.lodIndex;
        payload.renderResolution = mesh.renderResolution;
        payload.bounds.min = mesh.bounds.min;
        payload.bounds.max = mesh.bounds.max;
        payload.importSettings = request.identity.importSettings;
        payload.vertices.reserve(mesh.vertices.size());
        for (const Renderer::MeshVertex& vertex : mesh.vertices) {
            payload.vertices.push_back(toCpuVertex(vertex));
        }
        payload.indices = mesh.indices;
        return payload;
    }

    TerrainRenderMeshData renderMeshDataFromCachedLodPayload(
        const TerrainCachedLodMeshPayload& payload,
        const TerrainRenderLodBuildRequest& request)
    {
        TerrainRenderMeshData mesh;
        mesh.tile = request.tile;
        mesh.coord = request.coord;
        mesh.generation = request.generation;
        mesh.lodIndex = payload.lodIndex;
        mesh.renderResolution = payload.renderResolution;
        mesh.bounds.min = payload.bounds.min;
        mesh.bounds.max = payload.bounds.max;
        mesh.vertices.reserve(payload.vertices.size());
        for (const TerrainCpuMeshVertex& vertex : payload.vertices) {
            mesh.vertices.push_back(toRendererVertex(vertex));
        }
        mesh.indices = payload.indices;
        return mesh;
    }

    TerrainRenderLodBuildResult buildTerrainRenderLod(const TerrainRenderLodBuildRequest& request)
    {
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();

        TerrainRenderLodBuildResult result;
        TerrainLodMeshBuildSettings lod;
        lod.lodIndex = request.lodIndex;
        lod.renderResolution = request.renderResolution;
        lod.skirtDepth = request.skirtDepth;
        const TerrainCachedChunkPayload sourcePayload = cachedChunkPayloadFromRenderLodRequest(request);
        const TerrainDerivedCacheManifest manifest =
            TerrainDerivedCache::buildLodMeshManifest(request.cacheSettings, sourcePayload, lod, request.identity.sourceHash);

        if (request.cacheSettings.policy != TerrainDerivedCachePolicy::Disabled &&
            request.cacheSettings.policy != TerrainDerivedCachePolicy::Refresh) {
            const TerrainDerivedCacheLodMeshReadResult read = TerrainDerivedCache::readLodMesh(manifest);
            result.diagnostics.cacheStatus = read.status;
            countCacheStatus(result.diagnostics);
            if (read.status == TerrainDerivedCacheStatus::Hit && read.payload) {
                result.build = renderMeshBuildResultFromCachedLod(*read.payload, request);
                result.success = result.build.success;
                result.diagnostics.usedCache = result.success;
                result.diagnostics.vertexCount = static_cast<uint32_t>(result.build.mesh.vertices.size());
                result.diagnostics.indexCount = static_cast<uint32_t>(result.build.mesh.indices.size());
                result.diagnostics.buildMs = std::chrono::duration<float, std::milli>(Clock::now() - started).count();
                result.build.buildMs = result.diagnostics.buildMs;
                result.diagnostics.message = result.build.message;
                return result;
            }
        } else {
            result.diagnostics.cacheStatus = TerrainDerivedCacheStatus::Miss;
        }

        const TerrainCachedLodMeshPayload payload = buildTerrainCachedLodMesh(sourcePayload, lod);
        result.build = renderMeshBuildResultFromCachedLod(payload, request);
        result.build.message = "Terrain render LOD generated.";
        result.success = result.build.success;
        result.diagnostics.generated = true;
        result.diagnostics.generatedCount = result.success ? 1u : 0u;
        result.diagnostics.vertexCount = static_cast<uint32_t>(result.build.mesh.vertices.size());
        result.diagnostics.indexCount = static_cast<uint32_t>(result.build.mesh.indices.size());
        result.diagnostics.buildMs = std::chrono::duration<float, std::milli>(Clock::now() - started).count();
        result.diagnostics.message = result.build.message + " cache=" + cacheStatusLabel(result.diagnostics.cacheStatus);
        result.build.buildMs = result.diagnostics.buildMs;
        result.build.message = result.diagnostics.message;
        return result;
    }
}
