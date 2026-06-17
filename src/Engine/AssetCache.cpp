#include "Engine/AssetCache.hpp"

#include <sstream>

namespace {
    std::filesystem::path resolveAssetPath(const std::filesystem::path& path)
    {
        if (std::filesystem::exists(path)) {
            return std::filesystem::weakly_canonical(path);
        }

        const std::filesystem::path currentRelative = std::filesystem::current_path() / path;
        if (std::filesystem::exists(currentRelative)) {
            return std::filesystem::weakly_canonical(currentRelative);
        }

        return std::filesystem::absolute(path).lexically_normal();
    }

    std::string pathKey(const std::filesystem::path& path)
    {
        return resolveAssetPath(path).generic_string();
    }

    template <typename Enum>
    uint32_t enumValue(Enum value)
    {
        return static_cast<uint32_t>(value);
    }

    std::string textureDescriptorKey(const Renderer::TextureDescriptor& descriptor)
    {
        std::ostringstream key;
        key << "slot=" << enumValue(descriptor.slot)
            << ";color=" << enumValue(descriptor.colorSpace)
            << ";wrap=" << enumValue(descriptor.wrapU) << "," << enumValue(descriptor.wrapV)
            << ";filter=" << enumValue(descriptor.minFilter) << "," << enumValue(descriptor.magFilter) << "," << enumValue(descriptor.mipFilter)
            << ";mips=" << (descriptor.generateMips ? 1 : 0);
        return key.str();
    }

    std::string textureKey(const std::filesystem::path& path, const Renderer::TextureDescriptor& descriptor)
    {
        return pathKey(path) + "|" + textureDescriptorKey(descriptor);
    }

    std::string solidTextureKey(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        std::ostringstream key;
        key << "solid:" << static_cast<uint32_t>(r)
            << "," << static_cast<uint32_t>(g)
            << "," << static_cast<uint32_t>(b)
            << "," << static_cast<uint32_t>(a);
        return key.str();
    }
}

namespace Engine {
    CachedStaticMesh AssetCache::acquireStaticMesh(const std::filesystem::path& path)
    {
        const std::string key = pathKey(path);
        for (uint32_t index = 0; index < meshes_.size(); ++index) {
            StaticMeshEntry& entry = meshes_[index];
            if (entry.alive && entry.key == key) {
                ++entry.refCount;
                return {index, entry.handle};
            }
        }

        Renderer::StaticMeshHandle mesh = Renderer::loadStaticMesh(path);
        if (mesh.id == UINT32_MAX) {
            return {};
        }

        return acquireMeshByKey(key, mesh);
    }

    CachedStaticMesh AssetCache::acquireFallbackCubeMesh()
    {
        constexpr const char* key = "generated:fallback_textured_cube";
        for (uint32_t index = 0; index < meshes_.size(); ++index) {
            StaticMeshEntry& entry = meshes_[index];
            if (entry.alive && entry.key == key) {
                ++entry.refCount;
                return {index, entry.handle};
            }
        }

        return acquireMeshByKey(key, Renderer::createTexturedCubeMesh());
    }

    void AssetCache::release(CachedStaticMesh mesh)
    {
        if (mesh.id >= meshes_.size() || !meshes_[mesh.id].alive) {
            return;
        }

        StaticMeshEntry& entry = meshes_[mesh.id];
        if (entry.refCount > 0) {
            --entry.refCount;
        }
        if (entry.refCount == 0) {
            Renderer::destroyStaticMesh(entry.handle);
            entry = {};
        }
    }

    CachedTexture AssetCache::acquireTexture(const std::filesystem::path& path)
    {
        return acquireTexture(path, {});
    }

    CachedTexture AssetCache::acquireTexture(const std::filesystem::path& path, const Renderer::TextureDescriptor& descriptor)
    {
        const std::string key = textureKey(path, descriptor);
        for (uint32_t index = 0; index < textures_.size(); ++index) {
            TextureEntry& entry = textures_[index];
            if (entry.alive && entry.key == key) {
                ++entry.refCount;
                return {index, entry.handle};
            }
        }

        Renderer::TextureHandle texture = Renderer::loadTexture(path, descriptor);
        if (!Renderer::isValid(texture)) {
            return {};
        }

        return acquireTextureByKey(key, texture);
    }

    CachedTexture AssetCache::acquireSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const std::string key = solidTextureKey(r, g, b, a);
        for (uint32_t index = 0; index < textures_.size(); ++index) {
            TextureEntry& entry = textures_[index];
            if (entry.alive && entry.key == key) {
                ++entry.refCount;
                return {index, entry.handle};
            }
        }

        return acquireTextureByKey(key, Renderer::createSolidTexture(r, g, b, a));
    }

    void AssetCache::release(CachedTexture texture)
    {
        if (texture.id >= textures_.size() || !textures_[texture.id].alive) {
            return;
        }

        TextureEntry& entry = textures_[texture.id];
        if (entry.refCount > 0) {
            --entry.refCount;
        }
        if (entry.refCount == 0) {
            Renderer::destroyTexture(entry.handle);
            entry = {};
        }
    }

    void AssetCache::shutdown()
    {
        for (StaticMeshEntry& entry : meshes_) {
            if (entry.alive) {
                Renderer::destroyStaticMesh(entry.handle);
                entry = {};
            }
        }
        for (TextureEntry& entry : textures_) {
            if (entry.alive) {
                Renderer::destroyTexture(entry.handle);
                entry = {};
            }
        }
        meshes_.clear();
        textures_.clear();
    }

    CachedStaticMesh AssetCache::acquireMeshByKey(const std::string& key, Renderer::StaticMeshHandle handle)
    {
        if (handle.id == UINT32_MAX) {
            return {};
        }

        for (uint32_t index = 0; index < meshes_.size(); ++index) {
            if (!meshes_[index].alive) {
                meshes_[index] = {true, key, 1, handle};
                return {index, handle};
            }
        }

        const uint32_t id = static_cast<uint32_t>(meshes_.size());
        meshes_.push_back({true, key, 1, handle});
        return {id, handle};
    }

    CachedTexture AssetCache::acquireTextureByKey(const std::string& key, Renderer::TextureHandle handle)
    {
        if (!Renderer::isValid(handle)) {
            return {};
        }

        for (uint32_t index = 0; index < textures_.size(); ++index) {
            if (!textures_[index].alive) {
                textures_[index] = {true, key, 1, handle};
                return {index, handle};
            }
        }

        const uint32_t id = static_cast<uint32_t>(textures_.size());
        textures_.push_back({true, key, 1, handle});
        return {id, handle};
    }
}
