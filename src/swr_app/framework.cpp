/* C++ headers. */
#include <mutex>

/* other library headers */
#ifndef __linux__
#    ifdef __APPLE__
#        include <SDL.h>
#    else
#        include "SDL.h"
#    endif
#else
#    include <SDL2/SDL.h>
#endif

/* platform code. */
#include "../common/platform/platform.h"

/* software rasterizer. */
#include "swr/swr.h"

/* application framework */
#include "framework.h"

namespace swr_app
{

/*
 * renderwindow.
 */

void renderwindow::free_resources()
{
    if(sdl_renderer)
    {
        SDL_DestroyRenderer(sdl_renderer);
        sdl_renderer = nullptr;
    }

    if(sdl_window)
    {
        SDL_DestroyWindow(sdl_window);
        sdl_window = nullptr;
    }
}

bool renderwindow::create()
{
    if(sdl_window || sdl_renderer)
    {
        // either the window is already created or something went very wrong.
        return false;
    }

    sdl_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_SHOWN);
    if(!sdl_window)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Window creation failed: %s\n", SDL_GetError());
        return false;
    }

    auto surface = SDL_GetWindowSurface(sdl_window);
    sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    if(!sdl_renderer)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Render creation for surface failed: %s\n", SDL_GetError());

        SDL_DestroyWindow(sdl_window);
        sdl_window = nullptr;

        return false;
    }

    /* Clear the rendering surface with the specified color */
    SDL_SetRenderDrawColor(sdl_renderer, 0xff, 0xff, 0xff, 0xff);
    SDL_RenderClear(sdl_renderer);

    return true;
}

bool renderwindow::get_surface_buffer_rgba32(std::vector<uint32_t>& contents) const
{
    if(!sdl_window)
    {
        return false;
    }

    auto surface = SDL_GetWindowSurface(sdl_window);
    contents.resize(0);
    contents.reserve(surface->w * surface->h * 4); /* 4 bytes per pixel; RGBA */

    if(surface->format->BytesPerPixel < 1 || surface->format->BytesPerPixel > 4)
    {
        throw std::runtime_error(fmt::format("cannot handle pixel format with {} bytes per pixel", surface->format->BytesPerPixel));
    }

    /* read and convert pixels. */
    for(int y = 0; y < surface->h; ++y)
    {
        for(int x = 0; x < surface->w; ++x)
        {
            Uint8* p = (Uint8*)surface->pixels + y * surface->pitch + x * surface->format->BytesPerPixel;
            Uint32 pixel{0};

            // (if speed was a concern, the branching should happen outside the for-loops)
            if(surface->format->BytesPerPixel == 1)
            {
                pixel = *p;
            }
            else if(surface->format->BytesPerPixel == 2)
            {
                pixel = *reinterpret_cast<Uint16*>(p);
            }
            else if(surface->format->BytesPerPixel == 3)
            {
                //!!todo: this needs to be tested.
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                pixel = p[0] << 16 | p[1] << 8 | p[2];
#else
                pixel = p[0] | p[1] << 8 | p[2] << 16;
#endif
            }
            else if(surface->format->BytesPerPixel == 4)
            {
                pixel = *reinterpret_cast<Uint32*>(p);
            }

            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, surface->format, &r, &g, &b, &a);
            contents.push_back((r << 24) | (g << 16) | (b << 8) | a);
        }
    }

    return true;
}

/*
 * application.
 */

/* singleton interface. */
application* application::global_app = nullptr;
std::mutex application::global_app_mtx;

void application::initialize_instance(int argc, char* argv[])
{
    assert(global_app != nullptr);

    // process command-line arguments.
    global_app->process_cmdline(argc, argv);

    // platform initialization with log disabled.
    platform::global_initialize();

    if(SDL_WasInit(SDL_INIT_VIDEO) == 0)
    {
        /* Enable standard application logging */
        if(SDL_LogGetPriority(SDL_LOG_CATEGORY_APPLICATION) != SDL_LOG_PRIORITY_INFO)
        {
            SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);
        }

        /* Initialize SDL */
        if(SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init fail : %s\n", SDL_GetError());
            throw std::runtime_error("SDL initialization failed.");
        }
    }
}

void application::shutdown_instance()
{
    // thread-safe shutdown. this prevents multiple threads accessing the singleton at the same time.
    const std::scoped_lock lock{global_app_mtx};

    // shut down SDL
    if(SDL_WasInit(SDL_INIT_EVERYTHING) != 0)
    {
        SDL_Quit();
    }

    // shut down other platform services.
    platform::global_shutdown();
}

bool application::process_cmdline(int argc, char* argv[])
{
    if(argc < 0 || argv == nullptr)
    {
        return false;
    }

    // skip the first argument, since it only contains the program's name
    cmd_args = {argv + 1, argv + argc};

    return true;
}

} /* namespace swr_app */