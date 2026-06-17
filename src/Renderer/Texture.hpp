#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <bgfx/bgfx.h>

namespace Renderer {
    struct TextureHandle {
        uint32_t id = UINT32_MAX;
    };

    enum class TextureSlot {
        Unknown,
        BaseColor,
        Normal,
        Metallic,
        Roughness,
        MetallicRoughness,
        Occlusion,
        Emissive,
    };

    enum class TextureColorSpace {
        Linear,
        Srgb,
    };

    enum class TextureWrap {
        Repeat,
        ClampToEdge,
        MirroredRepeat,
    };

    enum class TextureFilter {
        Nearest,
        Linear,
    };

    struct TextureDescriptor {
        TextureSlot slot = TextureSlot::Unknown;
        TextureColorSpace colorSpace = TextureColorSpace::Linear;
        TextureWrap wrapU = TextureWrap::ClampToEdge;
        TextureWrap wrapV = TextureWrap::ClampToEdge;
        TextureFilter minFilter = TextureFilter::Linear;
        TextureFilter magFilter = TextureFilter::Linear;
        TextureFilter mipFilter = TextureFilter::Linear;
        bool generateMips = false;
        std::string debugName;
    };

    struct TextureInfo {
        bool valid = false;
        std::filesystem::path sourcePath;
        uint16_t width = 0;
        uint16_t height = 0;
        uint8_t mipCount = 0;
        uint64_t estimatedBytes = 0;
        uint64_t bgfxFlags = 0;
        bool srgbRequested = false;
        bool srgbApplied = false;
        bool srgbFallback = false;
        TextureDescriptor descriptor;
    };

    bool isValid(TextureHandle handle);
    TextureHandle loadTexture(const std::filesystem::path& path);
    TextureHandle loadTexture(const std::filesystem::path& path, const TextureDescriptor& descriptor);
    TextureHandle createSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    TextureInfo textureInfo(TextureHandle handle);
    void destroyTexture(TextureHandle handle);
    bgfx::TextureHandle getNativeTexture(TextureHandle handle);
    void destroyTextures();
}
