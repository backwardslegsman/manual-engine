#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/core.hpp"
#include "Renderer/VertexLayouts.hpp"

/*
* Test data
*/

struct PosColorVertex
{
    float x;
    float y;
    float z;
    uint32_t abgr;
};

static PosColorVertex cubeVertices[] =
{
    {-1.0f,  1.0f,  1.0f, 0xff000000 },
    { 1.0f,  1.0f,  1.0f, 0xff0000ff },
    {-1.0f, -1.0f,  1.0f, 0xff00ff00 },
    { 1.0f, -1.0f,  1.0f, 0xff00ffff },
    {-1.0f,  1.0f, -1.0f, 0xffff0000 },
    { 1.0f,  1.0f, -1.0f, 0xffff00ff },
    {-1.0f, -1.0f, -1.0f, 0xffffff00 },
    { 1.0f, -1.0f, -1.0f, 0xffffffff },
};

static const uint16_t cubeTriList[] =
{
    0, 1, 2,
    1, 3, 2,
    4, 6, 5,
    5, 6, 7,
    0, 2, 4,
    4, 2, 6,
    1, 5, 3,
    5, 7, 3,
    0, 4, 1,
    4, 5, 1,
    2, 3, 6,
    6, 3, 7,
};

/*Test data end*/

int main(int, char**)
{
    SDL_Window* window = nullptr;
    if (Renderer::initWindow(window) != 0) {
        return 1;
    }

    if (Renderer::init_bgfx(window) != 0) {
        return 1;
    }

    Renderer::configureVertexLayouts();

    // Set view clear state and viewport for the 0th view
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x443355FF, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, 1280, 720);

    // Load the shader
    bgfx::ShaderHandle vsh = Renderer::loadShader("vs_cube.bin");
    bgfx::ShaderHandle fsh = Renderer::loadShader("fs_cube.bin");
    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bgfx::ProgramHandle program = bgfx::createProgram(vsh, fsh, true);

    // Create handles
    bgfx::VertexBufferHandle vbh = bgfx::createVertexBuffer(bgfx::makeRef(cubeVertices, sizeof(cubeVertices)), Renderer::PosColorVertex::layout);
    bgfx::IndexBufferHandle ibh = bgfx::createIndexBuffer(bgfx::makeRef(cubeTriList, sizeof(cubeTriList)));

    // Main loop
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        
        // Clear viewport to a solid color and submit an empty primitive for rendering
        bgfx::setViewClear(
            0,
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            0x443355FF,
            1.0f,
            0
        );

        // Set view rectangle for 0th viewport
        bgfx::setViewRect(0, 0, 0, 1280, 720);
        
        // Define vectors for render
        const glm::vec3 at = {0.0f, 0.0f,  0.0f};
        const glm::vec3 eye = {0.0f, 0.0f, -5.0f};

        // Set view and projection matrix for the 0th view
        glm::mat4 view = glm::lookAt(eye, at, {0.0f, 1.0f, 0.0f});
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1280.0f / 720.0f, 0.1f, 100.0f);
        bgfx::setViewTransform(0, &view, &proj);
        
        // Set model matrix for rendering
        float time = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        glm::mat4 model = glm::rotate(
        glm::mat4(1.0f),
        time,
        glm::vec3(0.0f, 1.0f, 0.0f)
        );

        bgfx::setTransform(&model[0][0]);
        bgfx::setVertexBuffer(0, vbh);
        bgfx::setIndexBuffer(ibh);

        bgfx::submit(0, program);
        bgfx::touch(0);
        bgfx::frame();
    }

    bgfx::destroy(program);
    bgfx::destroy(vbh);
    bgfx::destroy(ibh);

    bgfx::shutdown();

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
