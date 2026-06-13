#pragma once

#include <cstdint>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>

#include "Renderer/Scene.hpp"

namespace Renderer::DebugUi {
    bool init(SDL_Window* window);
    void shutdown();

    void processEvent(const SDL_Event& event);
    void beginFrame();
    void showRendererStats(const SceneDrawStats& stats);
    void render(bgfx::ViewId viewId, uint16_t width, uint16_t height);
}
