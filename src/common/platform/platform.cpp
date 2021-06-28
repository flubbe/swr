/**
 * swr - a software rasterizer
 * 
 * Global platform initialization and shutdown code. Rewrite if using different platforms/components. 
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <mutex>
#include <thread>

/* SDL */
#ifndef __linux__
#    ifdef __APPLE__
#        include <SDL.h>
#    else
#        include "SDL.h"
#    endif
#else
#    include <SDL2/SDL.h>
#endif

#include "platform.h"

namespace platform
{

/** default SDL log device (set in global_initialize and used in global_shutdown) */
static SDL_LogOutputFunction default_sdl_log{nullptr};

/** map SDL output to log device. */
void sdl_log([[maybe_unused]] void* userdata, [[maybe_unused]] int category, [[maybe_unused]] SDL_LogPriority priority, const char* message)
{
    logf("{}", message);
}

/**
 * initialize subsystems:
 *
 * 1) (early) filesystem
 * 2) (early) logging
 * 3) cpu info
 *
 * early subsystems may later be handed over to other systems
 */
void global_initialize(log_device* in_log)
{
    if(in_log)
    {
        log_device::set(in_log);
    }
    logf("logging enabled");

    get_cpu_info();
    logf("std::thread::hardware_concurrency: {0}", std::thread::hardware_concurrency());

    /* map SDL output to log. */
    SDL_LogGetOutputFunction(&default_sdl_log, nullptr);
    SDL_LogSetOutputFunction(&sdl_log, nullptr);

    logf("platform initialized");
}

void global_shutdown()
{
    /* reset SDL log. */
    if(default_sdl_log != nullptr)
    {
        SDL_LogSetOutputFunction(default_sdl_log, nullptr);
        default_sdl_log = nullptr;
    }

    logf("platform shut down");
    log_device::cleanup();
}

void set_log(log_device* in_log)
{
    // reset log device to null_log.
    log_device::cleanup();

    if(in_log)
    {
        log_device::set(in_log);
    }
}

} /* namespace platform. */