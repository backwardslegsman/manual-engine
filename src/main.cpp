#include <bgfx/bgfx.h>
#include <SDL3/SDL.h>

#include <iostream>

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    std::cout << "SDL3 and bgfx are available.\n";

    SDL_Quit();
    return 0;
}
