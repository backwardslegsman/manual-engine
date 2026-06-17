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
        float tw;
        float u;
        float v;
        float u1;
        float v1;
        uint32_t abgr = 0xffffffff;

        static bgfx::VertexLayout layout;
    };

    void configureVertexLayouts();
}
