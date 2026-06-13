#pragma once

#include <cstdint>
#include <filesystem>

#include <bgfx/bgfx.h>

namespace Renderer {
    struct TextureHandle {
        uint32_t id = UINT32_MAX;
    };

    bool isValid(TextureHandle handle);
    TextureHandle loadTexture(const std::filesystem::path& path);
    TextureHandle createSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void destroyTexture(TextureHandle handle);
    bgfx::TextureHandle getNativeTexture(TextureHandle handle);
    void destroyTextures();
}
