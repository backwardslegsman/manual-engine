#include "core.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace Renderer {
    int initWindow(SDL_Window*& window) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return 1;
        }

        window = SDL_CreateWindow(
            "Hello Triangle - SDL3 + bgfx",
            1280,
            720,
            SDL_WINDOW_RESIZABLE
        );

        if (!window) {
            SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        return 0;
    }

    int init_bgfx(SDL_Window* window) {
        // Native window is part of SDL_Properties, so we need to get the properties first and then query for the native window handle.
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        void* nativeWindowHandle = SDL_GetPointerProperty(
            props,
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            nullptr
        );

        if (!nativeWindowHandle) {
            SDL_Log("Failed to get native window handle: %s", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        bgfx::PlatformData pd{};
        pd.nwh = nativeWindowHandle;

        bgfx::Init init{};
        init.type = bgfx::RendererType::Count;
        init.platformData = pd;
        init.resolution.width = 1280;
        init.resolution.height = 720;
        init.resolution.reset = BGFX_RESET_VSYNC;

        if (!bgfx::init(init)) {
            SDL_Log("bgfx::init failed");
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        return 0;
    }

    bgfx::ShaderHandle loadShader(const char *FILENAME) {
        const char* shaderPath = nullptr;

        switch(bgfx::getRendererType()) {
            case bgfx::RendererType::Noop:
            case bgfx::RendererType::Direct3D11: shaderPath = "assets/shaders/dx11/";  break;
            //case bgfx::RendererType::Direct3D12: shaderPath = "assets/shaders/dx11/";  break;
            //case bgfx::RendererType::Gnm:        shaderPath = "assets/shaders/pssl/";  break;
            //case bgfx::RendererType::Metal:      shaderPath = "assets/shaders/metal/"; break;
            //case bgfx::RendererType::OpenGL:     shaderPath = "assets/shaders/glsl/";  break;
            //case bgfx::RendererType::OpenGLES:   shaderPath = "assets/shaders/essl/";  break;
            //case bgfx::RendererType::Vulkan:     shaderPath = "assets/shaders/spirv/"; break;
            default: break;
        }

        if (!shaderPath) {
            SDL_Log("No shader path configured for bgfx renderer type %d", bgfx::getRendererType());
            return BGFX_INVALID_HANDLE;
        }

        const std::filesystem::path relativePath = std::filesystem::path(shaderPath) / FILENAME;
        std::filesystem::path filePath = relativePath;

        if (!std::filesystem::exists(filePath)) {
            filePath = std::filesystem::current_path() / relativePath;
        }

        if (!std::filesystem::exists(filePath)) {
            const char* basePath = SDL_GetBasePath();
            if (basePath) {
                const std::filesystem::path executablePath = basePath;
                const std::filesystem::path projectRootFromDebugOutput =
                    executablePath / ".." / ".." / "..";
                const std::filesystem::path executableRelativePath =
                    projectRootFromDebugOutput / relativePath;

                if (std::filesystem::exists(executableRelativePath)) {
                    filePath = executableRelativePath;
                }
            }
        }

        FILE *file = std::fopen(filePath.string().c_str(), "rb");
        if (!file) {
            SDL_Log("Failed to open shader: %s", filePath.string().c_str());
            return BGFX_INVALID_HANDLE;
        }

        std::fseek(file, 0, SEEK_END);
        long fileSize = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);

        const bgfx::Memory *mem = bgfx::alloc(fileSize + 1);
        std::fread(mem->data, 1, fileSize, file);
        mem->data[mem->size - 1] = '\0';
        std::fclose(file);

        return bgfx::createShader(mem);
    }
}
