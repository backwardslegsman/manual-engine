#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>

#include "Renderer/Texture.hpp"
#include "Renderer/core.hpp"

namespace {
    struct Failure {
        std::string message;
    };

    void expect(std::vector<Failure>& failures, bool condition, std::string message)
    {
        if (!condition) {
            failures.push_back({std::move(message)});
        }
    }

    void writeU16(std::ofstream& output, uint16_t value)
    {
        output.put(static_cast<char>(value & 0xffu));
        output.put(static_cast<char>((value >> 8u) & 0xffu));
    }

    void writeU32(std::ofstream& output, uint32_t value)
    {
        output.put(static_cast<char>(value & 0xffu));
        output.put(static_cast<char>((value >> 8u) & 0xffu));
        output.put(static_cast<char>((value >> 16u) & 0xffu));
        output.put(static_cast<char>((value >> 24u) & 0xffu));
    }

    void writeI32(std::ofstream& output, int32_t value)
    {
        writeU32(output, static_cast<uint32_t>(value));
    }

    std::filesystem::path writeTestBmp()
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / "manual_engine_texture_test.bmp";
        std::ofstream output(path, std::ios::binary);

        constexpr uint32_t width = 4;
        constexpr uint32_t height = 4;
        constexpr uint32_t bytesPerPixel = 4;
        constexpr uint32_t pixelBytes = width * height * bytesPerPixel;
        constexpr uint32_t headerBytes = 14 + 40;

        output.put('B');
        output.put('M');
        writeU32(output, headerBytes + pixelBytes);
        writeU16(output, 0);
        writeU16(output, 0);
        writeU32(output, headerBytes);

        writeU32(output, 40);
        writeI32(output, width);
        writeI32(output, -static_cast<int32_t>(height));
        writeU16(output, 1);
        writeU16(output, 32);
        writeU32(output, 0);
        writeU32(output, pixelBytes);
        writeI32(output, 2835);
        writeI32(output, 2835);
        writeU32(output, 0);
        writeU32(output, 0);

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                output.put(static_cast<char>(x * 64)); // B
                output.put(static_cast<char>(y * 64)); // G
                output.put(static_cast<char>(255 - x * 32)); // R
                output.put(static_cast<char>(255)); // A
            }
        }

        return path;
    }

    SDL_Window* createHiddenWindow()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
            return nullptr;
        }

        SDL_Window* window = SDL_CreateWindow(
            "ManualEngine Texture Tests",
            64,
            64,
            SDL_WINDOW_HIDDEN
        );
        if (!window) {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
            SDL_Quit();
        }
        return window;
    }
}

int main()
{
    std::vector<Failure> failures;
    SDL_Window* window = createHiddenWindow();
    if (!window) {
        return 1;
    }

    if (Renderer::init_bgfx(window) != 0) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const std::filesystem::path texturePath = writeTestBmp();

    Renderer::TextureDescriptor mipmapped;
    mipmapped.slot = Renderer::TextureSlot::BaseColor;
    mipmapped.colorSpace = Renderer::TextureColorSpace::Srgb;
    mipmapped.wrapU = Renderer::TextureWrap::Repeat;
    mipmapped.wrapV = Renderer::TextureWrap::Repeat;
    mipmapped.generateMips = true;
    mipmapped.debugName = "texture.test.mipmapped";
    const Renderer::TextureHandle mipmappedTexture = Renderer::loadTexture(texturePath, mipmapped);
    const Renderer::TextureInfo mipmappedInfo = Renderer::textureInfo(mipmappedTexture);
    expect(failures, Renderer::isValid(mipmappedTexture), "mipmapped texture did not load");
    expect(failures, mipmappedInfo.valid, "mipmapped texture info was invalid");
    expect(failures, mipmappedInfo.width == 4 && mipmappedInfo.height == 4, "mipmapped texture dimensions were wrong");
    expect(failures, mipmappedInfo.mipCount == 3, "mipmapped texture mip count was wrong");
    expect(failures, mipmappedInfo.estimatedBytes == 84, "mipmapped texture byte estimate was wrong");
    expect(failures, mipmappedInfo.srgbRequested, "mipmapped texture did not record sRGB request");
    expect(failures, mipmappedInfo.srgbApplied || mipmappedInfo.srgbFallback, "mipmapped texture did not record sRGB outcome");
    expect(failures, mipmappedInfo.descriptor.wrapU == Renderer::TextureWrap::Repeat, "mipmapped texture wrap U was wrong");

    Renderer::TextureDescriptor singleLevel;
    singleLevel.slot = Renderer::TextureSlot::Normal;
    singleLevel.colorSpace = Renderer::TextureColorSpace::Linear;
    singleLevel.wrapU = Renderer::TextureWrap::ClampToEdge;
    singleLevel.wrapV = Renderer::TextureWrap::ClampToEdge;
    singleLevel.minFilter = Renderer::TextureFilter::Nearest;
    singleLevel.magFilter = Renderer::TextureFilter::Nearest;
    singleLevel.mipFilter = Renderer::TextureFilter::Nearest;
    singleLevel.generateMips = false;
    const Renderer::TextureHandle singleLevelTexture = Renderer::loadTexture(texturePath, singleLevel);
    const Renderer::TextureInfo singleLevelInfo = Renderer::textureInfo(singleLevelTexture);
    expect(failures, Renderer::isValid(singleLevelTexture), "single-level texture did not load");
    expect(failures, singleLevelInfo.mipCount == 1, "single-level texture mip count was wrong");
    expect(failures, singleLevelInfo.estimatedBytes == 64, "single-level texture byte estimate was wrong");
    expect(failures, !singleLevelInfo.srgbRequested, "single-level texture unexpectedly requested sRGB");

    Renderer::destroyTexture(mipmappedTexture);
    Renderer::destroyTexture(singleLevelTexture);
    Renderer::destroyTextures();
    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures.empty()) {
        std::cout << "Texture tests passed\n";
        return 0;
    }

    std::cerr << "Texture tests failed: " << failures.size() << '\n';
    for (const Failure& failure : failures) {
        std::cerr << "  " << failure.message << '\n';
    }
    return 1;
}
