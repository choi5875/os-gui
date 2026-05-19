#include <iostream>
#include <cstdlib>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "app.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    std::filesystem::path home("/");
    const char *home_env = getenv("HOME");
    if (home_env) {
        home = home_env;
    }
    std::cout << "HOME: " << home << std::endl;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        std::cerr << "IMG_Init failed: " << IMG_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    if (TTF_Init() != 0) {
        std::cerr << "TTF_Init failed: " << TTF_GetError() << std::endl;
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = NULL;
    SDL_Window *window = NULL;
    if (SDL_CreateWindowAndRenderer(WIDTH, HEIGHT, 0, &window, &renderer) != 0) {
        std::cerr << "SDL_CreateWindowAndRenderer failed: " << SDL_GetError() << std::endl;
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowTitle(window, "OS GUI - task manager");

    AppData data = {};
    data.current_directory = "/proc";
    initialize(renderer, &data);

    SDL_Event event;
    bool running = true;
    do {
        if (SDL_WaitEventTimeout(&event, 16)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            handleEvent(&event, renderer, &data);
        }

        if (SDL_GetTicks() - data.last_refresh_tick >= REFRESH_MS) {
            clearGProcesses(data.graphic_entries);
            clearProcessEntries(data.process_entries);
            populateProcesses(renderer, &data);
            data.last_refresh_tick = SDL_GetTicks();
        }

        render(renderer, &data);
    } while (running);

    quit(&data);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();

    return 0;
}
