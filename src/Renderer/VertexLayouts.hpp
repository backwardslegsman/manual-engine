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

    struct MeshVertex {
        float px;
        float py;
        float pz;
        float nx;
        float ny;
        float nz;
        float tx;
        float ty;
        float tz;
        float u;
        float v;

        static bgfx::VertexLayout layout;
    };

    void configureVertexLayouts();
}
