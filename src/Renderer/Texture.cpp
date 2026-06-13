#include "Renderer/Texture.hpp"

#include <string>
#include <vector>

#include <SDL3/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {
    struct TextureResource {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        std::filesystem::path sourcePath;
    };

    std::vector<TextureResource> g_textures;

    std::filesystem::path resolveAssetPath(const std::filesystem::path& path)
    {
        if (std::filesystem::exists(path)) {
            return path;
        }

        const std::filesystem::path currentRelative = std::filesystem::current_path() / path;
        if (std::filesystem::exists(currentRelative)) {
            return currentRelative;
        }

        const char* basePath = SDL_GetBasePath();
        if (basePath) {
            const std::filesystem::path executablePath = basePath;
            const std::filesystem::path projectRootFromDebugOutput = executablePath / ".." / ".." / "..";
            const std::filesystem::path executableRelativePath = projectRootFromDebugOutput / path;
            if (std::filesystem::exists(executableRelativePath)) {
                return executableRelativePath;
            }
        }

        return path;
    }
}

namespace Renderer {
    bool isValid(TextureHandle handle)
    {
        return handle.id < g_textures.size() && bgfx::isValid(g_textures[handle.id].handle);
    }

    TextureHandle loadTexture(const std::filesystem::path& path)
    {
        const std::filesystem::path resolvedPath = resolveAssetPath(path);
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(resolvedPath.string().c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            SDL_Log("Failed to load texture %s: %s", resolvedPath.string().c_str(), stbi_failure_reason());
            return {};
        }

        const uint32_t byteCount = static_cast<uint32_t>(width * height * 4);
        const bgfx::Memory* memory = bgfx::copy(pixels, byteCount);
        stbi_image_free(pixels);

        const bgfx::TextureHandle texture = bgfx::createTexture2D(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            memory
        );

        if (!bgfx::isValid(texture)) {
            SDL_Log("bgfx failed to create texture %s", resolvedPath.string().c_str());
            return {};
        }

        const uint32_t id = static_cast<uint32_t>(g_textures.size());
        g_textures.push_back({texture, resolvedPath});
        return {id};
    }

    TextureHandle createSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const uint8_t pixel[4] = {r, g, b, a};
        const bgfx::Memory* memory = bgfx::copy(pixel, sizeof(pixel));
        const bgfx::TextureHandle texture = bgfx::createTexture2D(
            1,
            1,
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            memory
        );

        if (!bgfx::isValid(texture)) {
            return {};
        }

        const uint32_t id = static_cast<uint32_t>(g_textures.size());
        g_textures.push_back({texture, {}});
        return {id};
    }

    void destroyTexture(TextureHandle handle)
    {
        if (!isValid(handle)) {
            return;
        }

        bgfx::destroy(g_textures[handle.id].handle);
        g_textures[handle.id] = {};
    }

    bgfx::TextureHandle getNativeTexture(TextureHandle handle)
    {
        if (!isValid(handle)) {
            return BGFX_INVALID_HANDLE;
        }

        return g_textures[handle.id].handle;
    }

    void destroyTextures()
    {
        for (TextureResource& texture : g_textures) {
            if (bgfx::isValid(texture.handle)) {
                bgfx::destroy(texture.handle);
                texture.handle = BGFX_INVALID_HANDLE;
            }
        }
        g_textures.clear();
    }
}
