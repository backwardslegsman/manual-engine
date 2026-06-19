#include "Engine/TerrainDerivedCache.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <string_view>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace Engine {
    namespace {
        constexpr std::array<char, 4> ChunkMagic{'M', 'T', 'C', '1'};
        constexpr std::array<char, 4> LodMagic{'M', 'T', 'L', '1'};
        constexpr uint64_t FnvOffset = 14695981039346656037ull;
        constexpr uint64_t FnvPrime = 1099511628211ull;

        uint64_t fnv1a(std::string_view text, uint64_t hash = FnvOffset)
        {
            for (unsigned char value : text) {
                hash ^= value;
                hash *= FnvPrime;
            }
            return hash;
        }

        std::string hexHash(uint64_t hash)
        {
            std::ostringstream stream;
            stream << std::hex << std::setfill('0') << std::setw(16) << hash;
            return stream.str();
        }

        void appendCommonIdentity(std::ostringstream& stream, const TerrainDerivedCacheManifest& manifest)
        {
            stream << manifest.settings.formatVersion << '|'
                   << manifest.settings.terrainImportVersion << '|'
                   << manifest.chunkId.source.value << '|'
                   << manifest.chunkId.coord.x << '|'
                   << manifest.chunkId.coord.z << '|'
                   << manifest.importSettings.pipeline << '|'
                   << manifest.importSettings.version << '|'
                   << manifest.importSettings.optionsHash << '|'
                   << manifest.sourceHash << '|'
                   << static_cast<uint32_t>(manifest.sourceType) << '|'
                   << manifest.chunkSize << '|'
                   << manifest.chunkResolution << '|'
                   << manifest.payloadVersion << '|';
        }

        std::string fileName(const TerrainDerivedCacheManifest& manifest)
        {
            const char* prefix = manifest.kind == TerrainDerivedKind::ChunkHeights ? "chunk" : "lod";
            return std::string{prefix} + "_" +
                std::to_string(manifest.chunkId.coord.x) + "_" +
                std::to_string(manifest.chunkId.coord.z) + ".bin";
        }

        std::filesystem::path payloadPath(const TerrainDerivedCacheManifest& manifest)
        {
            return TerrainDerivedCache::cacheRoot(manifest) / manifest.payloadFileName;
        }

        void writeString(std::ofstream& file, const std::string& value)
        {
            const uint32_t size = static_cast<uint32_t>(value.size());
            file.write(reinterpret_cast<const char*>(&size), sizeof(size));
            file.write(value.data(), size);
        }

        bool readString(std::ifstream& file, std::string& value)
        {
            uint32_t size = 0;
            file.read(reinterpret_cast<char*>(&size), sizeof(size));
            if (!file || size > 1024 * 1024) {
                return false;
            }
            value.resize(size);
            file.read(value.data(), size);
            return static_cast<bool>(file);
        }

        void writeVec3(std::ofstream& file, const glm::vec3& value)
        {
            file.write(reinterpret_cast<const char*>(&value.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&value.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&value.z), sizeof(float));
        }

        bool readVec3(std::ifstream& file, glm::vec3& value)
        {
            file.read(reinterpret_cast<char*>(&value.x), sizeof(float));
            file.read(reinterpret_cast<char*>(&value.y), sizeof(float));
            file.read(reinterpret_cast<char*>(&value.z), sizeof(float));
            return static_cast<bool>(file);
        }

        void writeVec2(std::ofstream& file, const glm::vec2& value)
        {
            file.write(reinterpret_cast<const char*>(&value.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&value.y), sizeof(float));
        }

        bool readVec2(std::ifstream& file, glm::vec2& value)
        {
            file.read(reinterpret_cast<char*>(&value.x), sizeof(float));
            file.read(reinterpret_cast<char*>(&value.y), sizeof(float));
            return static_cast<bool>(file);
        }

        void writeSettings(std::ofstream& file, const AssetImportSettingsKey& settings)
        {
            writeString(file, settings.pipeline);
            writeString(file, settings.version);
            writeString(file, settings.optionsHash);
        }

        bool readSettings(std::ifstream& file, AssetImportSettingsKey& settings)
        {
            return readString(file, settings.pipeline) &&
                readString(file, settings.version) &&
                readString(file, settings.optionsHash);
        }

        bool settingsMatch(const AssetImportSettingsKey& lhs, const AssetImportSettingsKey& rhs)
        {
            return lhs == rhs;
        }

        YAML::Node manifestNode(const TerrainDerivedCacheManifest& manifest)
        {
            YAML::Node node;
            node["identity_hash"] = manifest.identityHash;
            node["format_version"] = manifest.settings.formatVersion;
            node["terrain_import_version"] = manifest.settings.terrainImportVersion;
            node["kind"] = manifest.kind == TerrainDerivedKind::ChunkHeights ? "chunk" : "lod";
            node["source_id"] = manifest.chunkId.source.value;
            node["chunk"]["x"] = manifest.chunkId.coord.x;
            node["chunk"]["z"] = manifest.chunkId.coord.z;
            node["source_hash"] = manifest.sourceHash;
            node["source_type"] = static_cast<uint32_t>(manifest.sourceType);
            node["chunk_size"] = manifest.chunkSize;
            node["chunk_resolution"] = manifest.chunkResolution;
            node["lod_index"] = manifest.lodIndex;
            node["render_resolution"] = manifest.renderResolution;
            node["payload_version"] = manifest.payloadVersion;
            node["payload_file"] = manifest.payloadFileName;
            node["payload_hash"] = manifest.payloadHash;
            node["settings"]["pipeline"] = manifest.importSettings.pipeline;
            node["settings"]["version"] = manifest.importSettings.version;
            node["settings"]["options_hash"] = manifest.importSettings.optionsHash;
            return node;
        }

        bool validateManifestFile(const TerrainDerivedCacheManifest& expected, TerrainDerivedCacheOperationResult& result)
        {
            const std::filesystem::path path = TerrainDerivedCache::cacheRoot(expected) / "manifest.yaml";
            if (!std::filesystem::is_regular_file(path)) {
                result.status = TerrainDerivedCacheStatus::Miss;
                result.message = "Terrain cache miss.";
                return false;
            }
            try {
                const YAML::Node node = YAML::LoadFile(path.string());
                if (node["identity_hash"].as<std::string>("") != expected.identityHash ||
                    node["payload_file"].as<std::string>("") != expected.payloadFileName ||
                    node["payload_version"].as<std::string>("") != expected.payloadVersion) {
                    result.status = TerrainDerivedCacheStatus::Stale;
                    result.message = "Terrain cache manifest identity mismatch.";
                    return false;
                }
                const std::string expectedPayloadHash = node["payload_hash"].as<std::string>("");
                if (!expectedPayloadHash.empty() && TerrainDerivedCache::hashFile(payloadPath(expected)) != expectedPayloadHash) {
                    result.status = TerrainDerivedCacheStatus::Stale;
                    result.message = "Terrain cache payload hash mismatch.";
                    return false;
                }
            } catch (const std::exception& ex) {
                result.status = TerrainDerivedCacheStatus::Corrupt;
                result.message = ex.what();
                return false;
            }
            return true;
        }

        TerrainDerivedCacheWriteResult writeManifestAndFinalize(
            TerrainDerivedCacheManifest manifest,
            const std::filesystem::path& payload,
            TerrainDerivedKind kind)
        {
            TerrainDerivedCacheWriteResult result;
            result.kind = kind;
            result.path = payload;
            try {
                manifest.payloadHash = TerrainDerivedCache::hashFile(payload);
                std::ofstream manifestFile(TerrainDerivedCache::cacheRoot(manifest) / "manifest.yaml");
                if (!manifestFile) {
                    result.status = TerrainDerivedCacheStatus::WriteFailed;
                    result.message = "Failed to write terrain cache manifest.";
                    return result;
                }
                manifestFile << manifestNode(manifest);
                result.status = TerrainDerivedCacheStatus::WriteSuccess;
                result.message = "Wrote terrain cache payload.";
                result.bytes = std::filesystem::file_size(payload);
            } catch (const std::exception& ex) {
                result.status = TerrainDerivedCacheStatus::WriteFailed;
                result.message = ex.what();
            }
            return result;
        }

        float sampleHeight(const TerrainCachedChunkPayload& chunk, float worldX, float worldZ)
        {
            const float localX = std::clamp(worldX - chunk.origin.x, 0.0f, chunk.size);
            const float localZ = std::clamp(worldZ - chunk.origin.z, 0.0f, chunk.size);
            const float normalizedX = localX / chunk.size * static_cast<float>(chunk.resolution - 1u);
            const float normalizedZ = localZ / chunk.size * static_cast<float>(chunk.resolution - 1u);
            const uint32_t x0 = std::min(static_cast<uint32_t>(std::floor(normalizedX)), chunk.resolution - 1u);
            const uint32_t z0 = std::min(static_cast<uint32_t>(std::floor(normalizedZ)), chunk.resolution - 1u);
            const uint32_t x1 = std::min(x0 + 1u, chunk.resolution - 1u);
            const uint32_t z1 = std::min(z0 + 1u, chunk.resolution - 1u);
            const float tx = normalizedX - static_cast<float>(x0);
            const float tz = normalizedZ - static_cast<float>(z0);
            const auto heightAt = [&](uint32_t x, uint32_t z) {
                return chunk.heights[static_cast<size_t>(z) * chunk.resolution + x];
            };
            const float h00 = heightAt(x0, z0);
            const float h10 = heightAt(x1, z0);
            const float h01 = heightAt(x0, z1);
            const float h11 = heightAt(x1, z1);
            const float hx0 = h00 + (h10 - h00) * tx;
            const float hx1 = h01 + (h11 - h01) * tx;
            return hx0 + (hx1 - hx0) * tz;
        }

        glm::vec3 normalFromHeights(float left, float right, float down, float up, float spacing)
        {
            return glm::normalize(glm::vec3{left - right, 2.0f * spacing, down - up});
        }

        void pushQuad(std::vector<uint32_t>& indices, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br)
        {
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    TerrainDerivedCache::TerrainDerivedCache(TerrainDerivedCacheSettings settings)
        : settings_(std::move(settings))
    {
    }

    TerrainDerivedCacheManifest TerrainDerivedCache::buildChunkManifest(
        TerrainDerivedCacheSettings settings,
        const TerrainCachedChunkPayload& payload,
        std::string sourceHash)
    {
        TerrainDerivedCacheManifest manifest;
        manifest.settings = std::move(settings);
        manifest.kind = TerrainDerivedKind::ChunkHeights;
        manifest.chunkId = payload.chunkId;
        manifest.importSettings = payload.importSettings;
        manifest.sourceHash = std::move(sourceHash);
        manifest.sourceType = payload.sourceType;
        manifest.chunkSize = payload.size;
        manifest.chunkResolution = payload.resolution;
        manifest.payloadVersion = manifest.settings.chunkPayloadVersion;
        manifest.payloadFileName = fileName(manifest);
        std::ostringstream identity;
        appendCommonIdentity(identity, manifest);
        identity << "chunk|";
        manifest.identityHash = hexHash(fnv1a(identity.str()));
        return manifest;
    }

    TerrainDerivedCacheManifest TerrainDerivedCache::buildLodMeshManifest(
        TerrainDerivedCacheSettings settings,
        const TerrainCachedChunkPayload& sourceChunk,
        const TerrainLodMeshBuildSettings& lod,
        std::string sourceHash)
    {
        TerrainDerivedCacheManifest manifest;
        manifest.settings = std::move(settings);
        manifest.kind = TerrainDerivedKind::LodMesh;
        manifest.chunkId = sourceChunk.chunkId;
        manifest.importSettings = sourceChunk.importSettings;
        manifest.sourceHash = std::move(sourceHash);
        manifest.sourceType = sourceChunk.sourceType;
        manifest.chunkSize = sourceChunk.size;
        manifest.chunkResolution = sourceChunk.resolution;
        manifest.lodIndex = lod.lodIndex;
        manifest.renderResolution = std::max(lod.renderResolution, 2u);
        manifest.payloadVersion = manifest.settings.lodMeshPayloadVersion;
        manifest.payloadFileName = fileName(manifest);
        std::ostringstream identity;
        appendCommonIdentity(identity, manifest);
        identity << "lod|" << manifest.lodIndex << '|' << manifest.renderResolution << '|';
        manifest.identityHash = hexHash(fnv1a(identity.str()));
        return manifest;
    }

    std::filesystem::path TerrainDerivedCache::cacheRoot(const TerrainDerivedCacheManifest& manifest)
    {
        return manifest.settings.rootPath / manifest.identityHash;
    }

    std::string TerrainDerivedCache::hashFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return "missing";
        }
        uint64_t hash = FnvOffset;
        std::array<char, 4096> buffer{};
        while (file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = file.gcount();
            hash = fnv1a(std::string_view{buffer.data(), static_cast<size_t>(count)}, hash);
        }
        return hexHash(hash);
    }

    TerrainDerivedCacheChunkReadResult TerrainDerivedCache::readChunk(const TerrainDerivedCacheManifest& manifest)
    {
        TerrainDerivedCacheChunkReadResult result;
        result.kind = TerrainDerivedKind::ChunkHeights;
        result.path = payloadPath(manifest);
        if (!validateManifestFile(manifest, result)) {
            return result;
        }

        std::ifstream file(result.path, std::ios::binary);
        if (!file) {
            result.status = TerrainDerivedCacheStatus::Miss;
            result.message = "Terrain chunk cache miss.";
            return result;
        }
        std::array<char, 4> magic{};
        file.read(magic.data(), magic.size());
        if (magic != ChunkMagic) {
            result.status = TerrainDerivedCacheStatus::Stale;
            result.message = "Terrain chunk cache magic mismatch.";
            return result;
        }
        TerrainCachedChunkPayload payload;
        file.read(reinterpret_cast<char*>(&payload.chunkId.source.value), sizeof(payload.chunkId.source.value));
        file.read(reinterpret_cast<char*>(&payload.chunkId.coord.x), sizeof(payload.chunkId.coord.x));
        file.read(reinterpret_cast<char*>(&payload.chunkId.coord.z), sizeof(payload.chunkId.coord.z));
        readVec3(file, payload.origin);
        file.read(reinterpret_cast<char*>(&payload.size), sizeof(payload.size));
        file.read(reinterpret_cast<char*>(&payload.resolution), sizeof(payload.resolution));
        uint32_t sourceType = 0;
        file.read(reinterpret_cast<char*>(&sourceType), sizeof(sourceType));
        payload.sourceType = static_cast<TerrainDatasetSourceType>(sourceType);
        if (!readSettings(file, payload.importSettings)) {
            result.status = TerrainDerivedCacheStatus::Corrupt;
            result.message = "Terrain chunk cache settings are truncated.";
            return result;
        }
        uint32_t heightCount = 0;
        file.read(reinterpret_cast<char*>(&heightCount), sizeof(heightCount));
        if (!file || heightCount != payload.resolution * payload.resolution || heightCount > 16u * 1024u * 1024u) {
            result.status = TerrainDerivedCacheStatus::Stale;
            result.message = "Terrain chunk cache header is invalid.";
            return result;
        }
        payload.heights.resize(heightCount);
        file.read(reinterpret_cast<char*>(payload.heights.data()), static_cast<std::streamsize>(payload.heights.size() * sizeof(float)));
        if (!file) {
            result.status = TerrainDerivedCacheStatus::Corrupt;
            result.message = "Terrain chunk cache payload is truncated.";
            return result;
        }
        uint32_t warningCount = 0;
        file.read(reinterpret_cast<char*>(&warningCount), sizeof(warningCount));
        if (!file || warningCount > 4096u) {
            result.status = TerrainDerivedCacheStatus::Corrupt;
            result.message = "Terrain chunk cache warnings are truncated.";
            return result;
        }
        payload.warnings.resize(warningCount);
        for (std::string& warning : payload.warnings) {
            if (!readString(file, warning)) {
                result.status = TerrainDerivedCacheStatus::Corrupt;
                result.message = "Terrain chunk cache warning string is truncated.";
                return result;
            }
        }
        if (payload.chunkId != manifest.chunkId || !settingsMatch(payload.importSettings, manifest.importSettings)) {
            result.status = TerrainDerivedCacheStatus::Stale;
            result.message = "Terrain chunk cache identity mismatch.";
            return result;
        }
        result.status = TerrainDerivedCacheStatus::Hit;
        result.message = "Loaded terrain chunk cache.";
        result.bytes = std::filesystem::file_size(result.path);
        result.payload = std::move(payload);
        return result;
    }

    TerrainDerivedCacheWriteResult TerrainDerivedCache::writeChunk(
        TerrainDerivedCacheManifest manifest,
        const TerrainCachedChunkPayload& payload)
    {
        TerrainDerivedCacheWriteResult result;
        result.kind = TerrainDerivedKind::ChunkHeights;
        try {
            const std::filesystem::path root = cacheRoot(manifest);
            std::filesystem::create_directories(root);
            const std::filesystem::path path = root / manifest.payloadFileName;
            std::ofstream file(path, std::ios::binary);
            if (!file) {
                result.status = TerrainDerivedCacheStatus::WriteFailed;
                result.message = "Failed to open terrain chunk cache for writing.";
                return result;
            }
            file.write(ChunkMagic.data(), ChunkMagic.size());
            file.write(reinterpret_cast<const char*>(&payload.chunkId.source.value), sizeof(payload.chunkId.source.value));
            file.write(reinterpret_cast<const char*>(&payload.chunkId.coord.x), sizeof(payload.chunkId.coord.x));
            file.write(reinterpret_cast<const char*>(&payload.chunkId.coord.z), sizeof(payload.chunkId.coord.z));
            writeVec3(file, payload.origin);
            file.write(reinterpret_cast<const char*>(&payload.size), sizeof(payload.size));
            file.write(reinterpret_cast<const char*>(&payload.resolution), sizeof(payload.resolution));
            const uint32_t sourceType = static_cast<uint32_t>(payload.sourceType);
            file.write(reinterpret_cast<const char*>(&sourceType), sizeof(sourceType));
            writeSettings(file, payload.importSettings);
            const uint32_t heightCount = static_cast<uint32_t>(payload.heights.size());
            file.write(reinterpret_cast<const char*>(&heightCount), sizeof(heightCount));
            file.write(reinterpret_cast<const char*>(payload.heights.data()), static_cast<std::streamsize>(payload.heights.size() * sizeof(float)));
            const uint32_t warningCount = static_cast<uint32_t>(payload.warnings.size());
            file.write(reinterpret_cast<const char*>(&warningCount), sizeof(warningCount));
            for (const std::string& warning : payload.warnings) {
                writeString(file, warning);
            }
            if (!file) {
                result.status = TerrainDerivedCacheStatus::WriteFailed;
                result.message = "Failed to write terrain chunk cache payload.";
                return result;
            }
            file.close();
            return writeManifestAndFinalize(std::move(manifest), path, TerrainDerivedKind::ChunkHeights);
        } catch (const std::exception& ex) {
            result.status = TerrainDerivedCacheStatus::WriteFailed;
            result.message = ex.what();
            return result;
        }
    }

    TerrainDerivedCacheLodMeshReadResult TerrainDerivedCache::readLodMesh(const TerrainDerivedCacheManifest& manifest)
    {
        TerrainDerivedCacheLodMeshReadResult result;
        result.kind = TerrainDerivedKind::LodMesh;
        result.path = payloadPath(manifest);
        if (!validateManifestFile(manifest, result)) {
            return result;
        }
        std::ifstream file(result.path, std::ios::binary);
        if (!file) {
            result.status = TerrainDerivedCacheStatus::Miss;
            result.message = "Terrain LOD mesh cache miss.";
            return result;
        }
        std::array<char, 4> magic{};
        file.read(magic.data(), magic.size());
        if (magic != LodMagic) {
            result.status = TerrainDerivedCacheStatus::Stale;
            result.message = "Terrain LOD mesh cache magic mismatch.";
            return result;
        }
        TerrainCachedLodMeshPayload payload;
        file.read(reinterpret_cast<char*>(&payload.chunkId.source.value), sizeof(payload.chunkId.source.value));
        file.read(reinterpret_cast<char*>(&payload.chunkId.coord.x), sizeof(payload.chunkId.coord.x));
        file.read(reinterpret_cast<char*>(&payload.chunkId.coord.z), sizeof(payload.chunkId.coord.z));
        file.read(reinterpret_cast<char*>(&payload.lodIndex), sizeof(payload.lodIndex));
        file.read(reinterpret_cast<char*>(&payload.renderResolution), sizeof(payload.renderResolution));
        readVec3(file, payload.bounds.min);
        readVec3(file, payload.bounds.max);
        if (!readSettings(file, payload.importSettings)) {
            result.status = TerrainDerivedCacheStatus::Corrupt;
            result.message = "Terrain LOD mesh settings are truncated.";
            return result;
        }
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        file.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
        file.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));
        if (!file || vertexCount == 0 || indexCount == 0 || vertexCount > 16u * 1024u * 1024u || indexCount > 64u * 1024u * 1024u) {
            result.status = TerrainDerivedCacheStatus::Stale;
            result.message = "Terrain LOD mesh cache header is invalid.";
            return result;
        }
        payload.vertices.resize(vertexCount);
        for (TerrainCpuMeshVertex& vertex : payload.vertices) {
            readVec3(file, vertex.position);
            readVec3(file, vertex.normal);
            file.read(reinterpret_cast<char*>(&vertex.tangent.x), sizeof(float));
            file.read(reinterpret_cast<char*>(&vertex.tangent.y), sizeof(float));
            file.read(reinterpret_cast<char*>(&vertex.tangent.z), sizeof(float));
            file.read(reinterpret_cast<char*>(&vertex.tangent.w), sizeof(float));
            readVec2(file, vertex.uv0);
            readVec2(file, vertex.uv1);
            file.read(reinterpret_cast<char*>(&vertex.color), sizeof(vertex.color));
        }
        payload.indices.resize(indexCount);
        file.read(reinterpret_cast<char*>(payload.indices.data()), static_cast<std::streamsize>(payload.indices.size() * sizeof(uint32_t)));
        if (!file) {
            result.status = TerrainDerivedCacheStatus::Corrupt;
            result.message = "Terrain LOD mesh cache payload is truncated.";
            return result;
        }
        if (payload.chunkId != manifest.chunkId || payload.lodIndex != manifest.lodIndex ||
            payload.renderResolution != manifest.renderResolution || !settingsMatch(payload.importSettings, manifest.importSettings)) {
            result.status = TerrainDerivedCacheStatus::Stale;
            result.message = "Terrain LOD mesh cache identity mismatch.";
            return result;
        }
        result.status = TerrainDerivedCacheStatus::Hit;
        result.message = "Loaded terrain LOD mesh cache.";
        result.bytes = std::filesystem::file_size(result.path);
        result.payload = std::move(payload);
        return result;
    }

    TerrainDerivedCacheWriteResult TerrainDerivedCache::writeLodMesh(
        TerrainDerivedCacheManifest manifest,
        const TerrainCachedLodMeshPayload& payload)
    {
        TerrainDerivedCacheWriteResult result;
        result.kind = TerrainDerivedKind::LodMesh;
        try {
            const std::filesystem::path root = cacheRoot(manifest);
            std::filesystem::create_directories(root);
            const std::filesystem::path path = root / manifest.payloadFileName;
            std::ofstream file(path, std::ios::binary);
            if (!file) {
                result.status = TerrainDerivedCacheStatus::WriteFailed;
                result.message = "Failed to open terrain LOD mesh cache for writing.";
                return result;
            }
            file.write(LodMagic.data(), LodMagic.size());
            file.write(reinterpret_cast<const char*>(&payload.chunkId.source.value), sizeof(payload.chunkId.source.value));
            file.write(reinterpret_cast<const char*>(&payload.chunkId.coord.x), sizeof(payload.chunkId.coord.x));
            file.write(reinterpret_cast<const char*>(&payload.chunkId.coord.z), sizeof(payload.chunkId.coord.z));
            file.write(reinterpret_cast<const char*>(&payload.lodIndex), sizeof(payload.lodIndex));
            file.write(reinterpret_cast<const char*>(&payload.renderResolution), sizeof(payload.renderResolution));
            writeVec3(file, payload.bounds.min);
            writeVec3(file, payload.bounds.max);
            writeSettings(file, payload.importSettings);
            const uint32_t vertexCount = static_cast<uint32_t>(payload.vertices.size());
            const uint32_t indexCount = static_cast<uint32_t>(payload.indices.size());
            file.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
            file.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));
            for (const TerrainCpuMeshVertex& vertex : payload.vertices) {
                writeVec3(file, vertex.position);
                writeVec3(file, vertex.normal);
                file.write(reinterpret_cast<const char*>(&vertex.tangent.x), sizeof(float));
                file.write(reinterpret_cast<const char*>(&vertex.tangent.y), sizeof(float));
                file.write(reinterpret_cast<const char*>(&vertex.tangent.z), sizeof(float));
                file.write(reinterpret_cast<const char*>(&vertex.tangent.w), sizeof(float));
                writeVec2(file, vertex.uv0);
                writeVec2(file, vertex.uv1);
                file.write(reinterpret_cast<const char*>(&vertex.color), sizeof(vertex.color));
            }
            file.write(reinterpret_cast<const char*>(payload.indices.data()), static_cast<std::streamsize>(payload.indices.size() * sizeof(uint32_t)));
            if (!file) {
                result.status = TerrainDerivedCacheStatus::WriteFailed;
                result.message = "Failed to write terrain LOD mesh cache payload.";
                return result;
            }
            file.close();
            return writeManifestAndFinalize(std::move(manifest), path, TerrainDerivedKind::LodMesh);
        } catch (const std::exception& ex) {
            result.status = TerrainDerivedCacheStatus::WriteFailed;
            result.message = ex.what();
            return result;
        }
    }

    void TerrainDerivedCache::recordResult(const TerrainDerivedCacheOperationResult& result)
    {
        stats_.lastPath = result.path;
        stats_.lastMessage = result.message;
        switch (result.status) {
            case TerrainDerivedCacheStatus::Hit:
                ++stats_.hits;
                stats_.bytesRead += result.bytes;
                break;
            case TerrainDerivedCacheStatus::Miss:
                ++stats_.misses;
                break;
            case TerrainDerivedCacheStatus::Stale:
                ++stats_.stale;
                break;
            case TerrainDerivedCacheStatus::Corrupt:
                ++stats_.corrupt;
                break;
            case TerrainDerivedCacheStatus::WriteSuccess:
                ++stats_.writes;
                stats_.bytesWritten += result.bytes;
                break;
            case TerrainDerivedCacheStatus::Cancelled:
                ++stats_.cancelled;
                break;
            case TerrainDerivedCacheStatus::WriteFailed:
                break;
        }
    }

    const TerrainDerivedCacheStats& TerrainDerivedCache::stats() const
    {
        return stats_;
    }

    void TerrainDerivedCache::clearStats()
    {
        stats_ = {};
    }

    TerrainCachedChunkPayload terrainCachedChunkPayloadFromChunk(
        const TerrainChunkData& chunk,
        const AssetImportSettingsKey& settings)
    {
        TerrainCachedChunkPayload payload;
        payload.chunkId = chunk.id;
        payload.origin = chunk.origin;
        payload.size = chunk.size;
        payload.resolution = chunk.resolution;
        payload.heights = chunk.heights;
        payload.sourceType = chunk.sourceType;
        payload.importSettings = settings;
        payload.warnings = chunk.warnings;
        return payload;
    }

    TerrainCachedChunkPayload terrainCachedChunkPayloadFromImported(
        const TerrainImportedChunk& chunk,
        TerrainDatasetSourceType sourceType,
        const AssetImportSettingsKey& settings)
    {
        TerrainCachedChunkPayload payload;
        payload.chunkId = chunk.id;
        payload.origin = chunk.origin;
        payload.size = chunk.size;
        payload.resolution = chunk.resolution;
        payload.heights = chunk.heights;
        payload.sourceType = sourceType;
        payload.importSettings = settings;
        payload.warnings = chunk.warnings;
        return payload;
    }

    TerrainCachedLodMeshPayload buildTerrainCachedLodMesh(
        const TerrainCachedChunkPayload& chunk,
        const TerrainLodMeshBuildSettings& settings)
    {
        TerrainCachedLodMeshPayload payload;
        payload.chunkId = chunk.chunkId;
        payload.lodIndex = settings.lodIndex;
        payload.renderResolution = std::max(settings.renderResolution, 2u);
        payload.importSettings = chunk.importSettings;
        payload.bounds.min = {chunk.origin.x, std::numeric_limits<float>::max(), chunk.origin.z};
        payload.bounds.max = {chunk.origin.x + chunk.size, std::numeric_limits<float>::lowest(), chunk.origin.z + chunk.size};
        if (chunk.resolution < 2 || chunk.size <= 0.0f || chunk.heights.size() != static_cast<size_t>(chunk.resolution) * chunk.resolution) {
            return payload;
        }

        const uint32_t resolution = payload.renderResolution;
        const float spacing = chunk.size / static_cast<float>(resolution - 1u);
        payload.vertices.reserve(static_cast<size_t>(resolution) * resolution + static_cast<size_t>(resolution) * 8u);
        for (uint32_t z = 0; z < resolution; ++z) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const float worldX = chunk.origin.x + static_cast<float>(x) * spacing;
                const float worldZ = chunk.origin.z + static_cast<float>(z) * spacing;
                const float height = sampleHeight(chunk, worldX, worldZ);
                const float left = sampleHeight(chunk, worldX - spacing, worldZ);
                const float right = sampleHeight(chunk, worldX + spacing, worldZ);
                const float down = sampleHeight(chunk, worldX, worldZ - spacing);
                const float up = sampleHeight(chunk, worldX, worldZ + spacing);
                TerrainCpuMeshVertex vertex;
                vertex.position = {worldX, height, worldZ};
                vertex.normal = normalFromHeights(left, right, down, up, spacing);
                vertex.uv0 = {
                    static_cast<float>(x) / static_cast<float>(resolution - 1u),
                    static_cast<float>(z) / static_cast<float>(resolution - 1u),
                };
                vertex.uv1 = vertex.uv0;
                payload.bounds.min.y = std::min(payload.bounds.min.y, height - settings.skirtDepth);
                payload.bounds.max.y = std::max(payload.bounds.max.y, height);
                payload.vertices.push_back(vertex);
            }
        }
        for (uint32_t z = 0; z + 1u < resolution; ++z) {
            for (uint32_t x = 0; x + 1u < resolution; ++x) {
                const uint32_t tl = z * resolution + x;
                const uint32_t tr = tl + 1u;
                const uint32_t bl = (z + 1u) * resolution + x;
                const uint32_t br = bl + 1u;
                pushQuad(payload.indices, tl, tr, bl, br);
            }
        }
        const auto addSkirtVertex = [&](uint32_t sourceIndex) {
            TerrainCpuMeshVertex vertex = payload.vertices[sourceIndex];
            vertex.position.y -= settings.skirtDepth;
            payload.vertices.push_back(vertex);
            return static_cast<uint32_t>(payload.vertices.size() - 1u);
        };
        if (settings.skirtDepth > 0.0f) {
            for (uint32_t x = 0; x + 1u < resolution; ++x) {
                const uint32_t topA = x;
                const uint32_t topB = x + 1u;
                pushQuad(payload.indices, topA, topB, addSkirtVertex(topA), addSkirtVertex(topB));
                const uint32_t bottomA = (resolution - 1u) * resolution + x;
                const uint32_t bottomB = bottomA + 1u;
                const uint32_t bottomSkirtB = addSkirtVertex(bottomB);
                const uint32_t bottomSkirtA = addSkirtVertex(bottomA);
                pushQuad(payload.indices, bottomB, bottomA, bottomSkirtB, bottomSkirtA);
            }
            for (uint32_t z = 0; z + 1u < resolution; ++z) {
                const uint32_t leftA = z * resolution;
                const uint32_t leftB = (z + 1u) * resolution;
                const uint32_t leftSkirtB = addSkirtVertex(leftB);
                const uint32_t leftSkirtA = addSkirtVertex(leftA);
                pushQuad(payload.indices, leftB, leftA, leftSkirtB, leftSkirtA);
                const uint32_t rightA = z * resolution + (resolution - 1u);
                const uint32_t rightB = (z + 1u) * resolution + (resolution - 1u);
                pushQuad(payload.indices, rightA, rightB, addSkirtVertex(rightA), addSkirtVertex(rightB));
            }
        }
        return payload;
    }
}
