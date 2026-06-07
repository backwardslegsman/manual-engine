#pragma once
#include <cstdint>
#include <bgfx/bgfx.h>

namespace Renderer {
    struct PosColorVertex {
            float x;
            float y;
            float z;
            uint32_t abgr;

            static bgfx::VertexLayout layout;
    };

    void configureVertexLayouts();
}