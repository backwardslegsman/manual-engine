/*
 * core.hpp
 * 
*/

#pragma once
#include <filesystem>
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

namespace Renderer {
    // initWindow needs to dereference the window pointer to assign the created window, so it takes a reference to a pointer. 
    int initWindow(SDL_Window*& window);

    // init_bgfx only needs to read the window pointer, so it takes a pointer directly.
    int init_bgfx(SDL_Window* window);

    // Loads a shader from the "assets/shaders" directory based on the current renderer type. The shader filename is passed as an argument, and the function returns a bgfx::ShaderHandle.
    bgfx::ShaderHandle loadShader(const char *FILENAME);
}
