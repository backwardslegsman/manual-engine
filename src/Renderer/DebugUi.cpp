#include "Renderer/DebugUi.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include "Renderer/core.hpp"

namespace {
    bgfx::VertexLayout g_imguiVertexLayout;
    bgfx::ProgramHandle g_imguiProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle g_imguiTextureSampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle g_fontTexture = BGFX_INVALID_HANDLE;
    bool g_initialized = false;

    void destroyBgfxResources()
    {
        if (bgfx::isValid(g_fontTexture)) {
            bgfx::destroy(g_fontTexture);
            g_fontTexture = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(g_imguiTextureSampler)) {
            bgfx::destroy(g_imguiTextureSampler);
            g_imguiTextureSampler = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(g_imguiProgram)) {
            bgfx::destroy(g_imguiProgram);
            g_imguiProgram = BGFX_INVALID_HANDLE;
        }
    }

    bool createFontTexture()
    {
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        if (!pixels || width <= 0 || height <= 0) {
            return false;
        }

        const bgfx::Memory* memory = bgfx::copy(pixels, static_cast<uint32_t>(width * height * 4));
        g_fontTexture = bgfx::createTexture2D(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            memory
        );

        return bgfx::isValid(g_fontTexture);
    }
}

namespace Renderer::DebugUi {
    bool init(SDL_Window* window)
    {
        if (g_initialized) {
            return true;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        if (!ImGui_ImplSDL3_InitForOther(window)) {
            ImGui::DestroyContext();
            return false;
        }

        g_imguiVertexLayout
            .begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();

        const bgfx::ShaderHandle vertexShader = loadShader("vs_imgui.bin");
        const bgfx::ShaderHandle fragmentShader = loadShader("fs_imgui.bin");
        if (!bgfx::isValid(vertexShader) || !bgfx::isValid(fragmentShader)) {
            if (bgfx::isValid(vertexShader)) {
                bgfx::destroy(vertexShader);
            }
            if (bgfx::isValid(fragmentShader)) {
                bgfx::destroy(fragmentShader);
            }
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return false;
        }

        g_imguiProgram = bgfx::createProgram(vertexShader, fragmentShader, true);
        g_imguiTextureSampler = bgfx::createUniform("s_imgui", bgfx::UniformType::Sampler);

        if (!bgfx::isValid(g_imguiProgram) ||
            !bgfx::isValid(g_imguiTextureSampler) ||
            !createFontTexture()) {
            destroyBgfxResources();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return false;
        }

        g_initialized = true;
        return true;
    }

    void shutdown()
    {
        if (!g_initialized) {
            return;
        }

        destroyBgfxResources();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        g_initialized = false;
    }

    void processEvent(const SDL_Event& event)
    {
        if (!g_initialized) {
            return;
        }

        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    void beginFrame()
    {
        if (!g_initialized) {
            return;
        }

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    void showRendererStats(const SceneDrawStats& stats)
    {
        if (!g_initialized) {
            return;
        }

        ImGui::Begin("Renderer Debug");
        ImGui::Text("Mesh instances");
        ImGui::Text("Live: %u", stats.liveMeshInstances);
        ImGui::Text("Visible: %u", stats.visibleMeshInstances);
        ImGui::Text("Submitted: %u", stats.submittedMeshInstances);
        ImGui::Separator();
        ImGui::Text("Terrain tiles");
        ImGui::Text("Live: %u", stats.liveTerrainTiles);
        ImGui::Text("Visible: %u", stats.visibleTerrainTiles);
        ImGui::Text("Submitted: %u", stats.submittedTerrainTiles);
        ImGui::End();
    }

    void render(bgfx::ViewId viewId, uint16_t width, uint16_t height)
    {
        if (!g_initialized || width == 0 || height == 0) {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        if (!drawData || drawData->TotalVtxCount == 0) {
            return;
        }

        const glm::mat4 projection = glm::ortho(
            0.0f,
            io.DisplaySize.x,
            io.DisplaySize.y,
            0.0f,
            0.0f,
            1000.0f
        );

        bgfx::setViewName(viewId, "Dear ImGui");
        bgfx::setViewRect(viewId, 0, 0, width, height);
        bgfx::setViewTransform(viewId, nullptr, &projection[0][0]);
        bgfx::touch(viewId);

        const ImVec2 clipOffset = drawData->DisplayPos;
        for (int commandListIndex = 0; commandListIndex < drawData->CmdListsCount; ++commandListIndex) {
            const ImDrawList* commandList = drawData->CmdLists[commandListIndex];
            bgfx::TransientVertexBuffer vertexBuffer;
            bgfx::TransientIndexBuffer indexBuffer;

            const uint32_t vertexCount = static_cast<uint32_t>(commandList->VtxBuffer.Size);
            const uint32_t indexCount = static_cast<uint32_t>(commandList->IdxBuffer.Size);
            if (!bgfx::getAvailTransientVertexBuffer(vertexCount, g_imguiVertexLayout) ||
                !bgfx::getAvailTransientIndexBuffer(indexCount)) {
                break;
            }

            bgfx::allocTransientVertexBuffer(&vertexBuffer, vertexCount, g_imguiVertexLayout);
            bgfx::allocTransientIndexBuffer(&indexBuffer, indexCount, sizeof(ImDrawIdx) == 4);
            std::memcpy(vertexBuffer.data, commandList->VtxBuffer.Data, vertexCount * sizeof(ImDrawVert));
            std::memcpy(indexBuffer.data, commandList->IdxBuffer.Data, indexCount * sizeof(ImDrawIdx));

            uint32_t indexOffset = 0;
            for (const ImDrawCmd& command : commandList->CmdBuffer) {
                if (command.UserCallback) {
                    command.UserCallback(commandList, &command);
                    indexOffset += command.ElemCount;
                    continue;
                }

                const uint16_t clipX = static_cast<uint16_t>(std::max(command.ClipRect.x - clipOffset.x, 0.0f));
                const uint16_t clipY = static_cast<uint16_t>(std::max(command.ClipRect.y - clipOffset.y, 0.0f));
                const uint16_t clipWidth = static_cast<uint16_t>(std::max(command.ClipRect.z - command.ClipRect.x, 0.0f));
                const uint16_t clipHeight = static_cast<uint16_t>(std::max(command.ClipRect.w - command.ClipRect.y, 0.0f));

                bgfx::setScissor(clipX, clipY, clipWidth, clipHeight);
                bgfx::setTexture(0, g_imguiTextureSampler, g_fontTexture);
                bgfx::setVertexBuffer(0, &vertexBuffer, command.VtxOffset, vertexCount);
                bgfx::setIndexBuffer(&indexBuffer, indexOffset + command.IdxOffset, command.ElemCount);
                bgfx::setState(
                    BGFX_STATE_WRITE_RGB |
                    BGFX_STATE_WRITE_A |
                    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA)
                );
                bgfx::submit(viewId, g_imguiProgram);
                indexOffset += command.ElemCount;
            }
        }
    }
}
