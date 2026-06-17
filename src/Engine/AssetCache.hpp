#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Renderer/Scene.hpp"
#include "Renderer/Texture.hpp"

namespace Engine {
    struct CachedStaticMesh {
        uint32_t id = UINT32_MAX;
        Renderer::StaticMeshHandle handle;
    };

    struct CachedTexture {
        uint32_t id = UINT32_MAX;
        Renderer::TextureHandle handle;
    };

    // Synchronous cache for reusable renderer assets. Callers must release every
    // successful acquire before renderer shutdown.
    class AssetCache {
    public:
        CachedStaticMesh acquireStaticMesh(const std::filesystem::path& path);
        CachedStaticMesh acquireFallbackCubeMesh();
        void release(CachedStaticMesh mesh);

        CachedTexture acquireTexture(const std::filesystem::path& path);
        CachedTexture acquireTexture(const std::filesystem::path& path, const Renderer::TextureDescriptor& descriptor);
        CachedTexture acquireSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        void release(CachedTexture texture);

        void shutdown();

    private:
        struct StaticMeshEntry {
            bool alive = false;
            std::string key;
            uint32_t refCount = 0;
            Renderer::StaticMeshHandle handle;
        };

        struct TextureEntry {
            bool alive = false;
            std::string key;
            uint32_t refCount = 0;
            Renderer::TextureHandle handle;
        };

        CachedStaticMesh acquireMeshByKey(const std::string& key, Renderer::StaticMeshHandle handle);
        CachedTexture acquireTextureByKey(const std::string& key, Renderer::TextureHandle handle);

        std::vector<StaticMeshEntry> meshes_;
        std::vector<TextureEntry> textures_;
    };
}
