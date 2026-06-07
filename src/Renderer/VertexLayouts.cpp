#include "Renderer/VertexLayouts.hpp"

namespace Renderer {

    // Vertex layout definitions
    bgfx::VertexLayout PosColorVertex::layout;

    // Configures all vertex layouts used in the application. Must be called before using any vertex layout.
    void configureVertexLayouts()
    {
        PosColorVertex::layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    }

}