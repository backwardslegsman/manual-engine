#include "Renderer/Texture.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {
    constexpr uint32_t BytesPerPixel = 4;

    struct TextureResource {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        Renderer::TextureInfo info;
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

    uint64_t wrapFlags(Renderer::TextureWrap wrap, bool uAxis)
    {
        switch (wrap) {
            case Renderer::TextureWrap::ClampToEdge:
                return uAxis ? BGFX_SAMPLER_U_CLAMP : BGFX_SAMPLER_V_CLAMP;
            case Renderer::TextureWrap::MirroredRepeat:
                return uAxis ? BGFX_SAMPLER_U_MIRROR : BGFX_SAMPLER_V_MIRROR;
            case Renderer::TextureWrap::Repeat:
            default:
                break;
        }
        return 0;
    }

    uint64_t filterFlags(const Renderer::TextureDescriptor& descriptor)
    {
        uint64_t flags = 0;
        if (descriptor.minFilter == Renderer::TextureFilter::Nearest) {
            flags |= BGFX_SAMPLER_MIN_POINT;
        }
        if (descriptor.magFilter == Renderer::TextureFilter::Nearest) {
            flags |= BGFX_SAMPLER_MAG_POINT;
        }
        if (descriptor.mipFilter == Renderer::TextureFilter::Nearest) {
            flags |= BGFX_SAMPLER_MIP_POINT;
        }
        return flags;
    }

    uint64_t textureFlags(const Renderer::TextureDescriptor& descriptor, bool& srgbApplied, bool& srgbFallback)
    {
        uint64_t flags = wrapFlags(descriptor.wrapU, true) |
            wrapFlags(descriptor.wrapV, false) |
            filterFlags(descriptor);

        srgbApplied = false;
        srgbFallback = false;
        if (descriptor.colorSpace == Renderer::TextureColorSpace::Srgb) {
            const bgfx::Caps* caps = bgfx::getCaps();
            const bool supported = caps &&
                (caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB) != 0;
            if (supported) {
                flags |= BGFX_TEXTURE_SRGB;
                srgbApplied = true;
            } else {
                srgbFallback = true;
            }
        }

        return flags;
    }

    uint8_t mipCountFor(uint32_t width, uint32_t height, bool generateMips)
    {
        if (!generateMips) {
            return 1;
        }

        uint8_t count = 1;
        while (width > 1 || height > 1) {
            width = std::max(1u, width / 2u);
            height = std::max(1u, height / 2u);
            ++count;
        }
        return count;
    }

    void appendMipLevel(std::vector<uint8_t>& chain, const std::vector<uint8_t>& level)
    {
        chain.insert(chain.end(), level.begin(), level.end());
    }

    std::vector<uint8_t> downsample2x(const std::vector<uint8_t>& source, uint32_t width, uint32_t height)
    {
        const uint32_t nextWidth = std::max(1u, width / 2u);
        const uint32_t nextHeight = std::max(1u, height / 2u);
        std::vector<uint8_t> result(static_cast<size_t>(nextWidth) * nextHeight * BytesPerPixel);

        for (uint32_t y = 0; y < nextHeight; ++y) {
            for (uint32_t x = 0; x < nextWidth; ++x) {
                uint32_t accum[4]{};
                uint32_t samples = 0;
                for (uint32_t oy = 0; oy < 2; ++oy) {
                    for (uint32_t ox = 0; ox < 2; ++ox) {
                        const uint32_t sx = std::min(width - 1u, x * 2u + ox);
                        const uint32_t sy = std::min(height - 1u, y * 2u + oy);
                        const size_t sourceOffset = (static_cast<size_t>(sy) * width + sx) * BytesPerPixel;
                        for (uint32_t channel = 0; channel < 4; ++channel) {
                            accum[channel] += source[sourceOffset + channel];
                        }
                        ++samples;
                    }
                }

                const size_t targetOffset = (static_cast<size_t>(y) * nextWidth + x) * BytesPerPixel;
                for (uint32_t channel = 0; channel < 4; ++channel) {
                    result[targetOffset + channel] = static_cast<uint8_t>((accum[channel] + samples / 2u) / samples);
                }
            }
        }

        return result;
    }

    std::vector<uint8_t> buildMipChain(const uint8_t* pixels, uint32_t width, uint32_t height, bool generateMips)
    {
        std::vector<uint8_t> current(pixels, pixels + static_cast<size_t>(width) * height * BytesPerPixel);
        std::vector<uint8_t> chain;
        chain.reserve(current.size());
        appendMipLevel(chain, current);

        while (generateMips && (width > 1 || height > 1)) {
            current = downsample2x(current, width, height);
            width = std::max(1u, width / 2u);
            height = std::max(1u, height / 2u);
            appendMipLevel(chain, current);
        }

        return chain;
    }
}

namespace Renderer {
    bool isValid(TextureHandle handle)
    {
        return handle.id < g_textures.size() && bgfx::isValid(g_textures[handle.id].handle);
    }

    TextureHandle loadTexture(const std::filesystem::path& path)
    {
        return loadTexture(path, {});
    }

    TextureHandle loadTexture(const std::filesystem::path& path, const TextureDescriptor& descriptor)
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
        if (width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX) {
            SDL_Log("Texture %s has unsupported dimensions %dx%d", resolvedPath.string().c_str(), width, height);
            stbi_image_free(pixels);
            return {};
        }

        const uint8_t mipCount = mipCountFor(static_cast<uint32_t>(width), static_cast<uint32_t>(height), descriptor.generateMips);
        std::vector<uint8_t> mipChain = buildMipChain(
            pixels,
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            descriptor.generateMips);
        stbi_image_free(pixels);

        const bgfx::Memory* memory = bgfx::copy(mipChain.data(), static_cast<uint32_t>(mipChain.size()));
        bool srgbApplied = false;
        bool srgbFallback = false;
        const uint64_t flags = textureFlags(descriptor, srgbApplied, srgbFallback);

        const bgfx::TextureHandle texture = bgfx::createTexture2D(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            mipCount > 1,
            1,
            bgfx::TextureFormat::RGBA8,
            flags,
            memory
        );

        if (!bgfx::isValid(texture)) {
            SDL_Log("bgfx failed to create texture %s", resolvedPath.string().c_str());
            return {};
        }
        if (srgbFallback) {
            SDL_Log("Texture %s requested sRGB sampling, but RGBA8 sRGB sampling is not supported by this backend.", resolvedPath.string().c_str());
        }

        TextureInfo info;
        info.valid = true;
        info.sourcePath = resolvedPath;
        info.width = static_cast<uint16_t>(width);
        info.height = static_cast<uint16_t>(height);
        info.mipCount = mipCount;
        info.estimatedBytes = static_cast<uint64_t>(mipChain.size());
        info.bgfxFlags = flags;
        info.srgbRequested = descriptor.colorSpace == TextureColorSpace::Srgb;
        info.srgbApplied = srgbApplied;
        info.srgbFallback = srgbFallback;
        info.descriptor = descriptor;

        const uint32_t id = static_cast<uint32_t>(g_textures.size());
        g_textures.push_back({texture, std::move(info)});
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

        TextureDescriptor descriptor;
        descriptor.debugName = "solid";

        TextureInfo info;
        info.valid = true;
        info.width = 1;
        info.height = 1;
        info.mipCount = 1;
        info.estimatedBytes = sizeof(pixel);
        info.bgfxFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        info.descriptor = descriptor;

        const uint32_t id = static_cast<uint32_t>(g_textures.size());
        g_textures.push_back({texture, std::move(info)});
        return {id};
    }

    TextureInfo textureInfo(TextureHandle handle)
    {
        if (!isValid(handle)) {
            return {};
        }
        return g_textures[handle.id].info;
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
