/**
 * swr - a software rasterizer
 *
 * general render context and SDL render context implementation.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* format library */
#include "fmt/format.h"

/* user headers. */
#include "swr_internal.h"

#include "rasterizer/interpolators.h"
#include "rasterizer/fragment.h"
#include "rasterizer/sweep.h"

namespace swr
{
namespace impl
{

/** global render context. */
thread_local render_device_context* global_context = nullptr;

/*
 * render context implementation.
 */

void render_device_context::shutdown()
{
    // empty command list.
    render_object_list.clear();

    /*
     * Clean up all slot maps.
     */

    // framebuffers.
    framebuffer_objects.clear();
    framebuffer_objects.shrink_to_fit();

    depth_attachments.clear();
    depth_attachments.shrink_to_fit();

    // delete all geometry data.
    vertex_buffers.clear();
    vertex_buffers.shrink_to_fit();

    vertex_attribute_buffers.clear();
    vertex_attribute_buffers.shrink_to_fit();

    index_buffers.clear();
    index_buffers.shrink_to_fit();

    // delete shaders.
    program_instances.clear();
    program_instances.shrink_to_fit();

    program_storage.clear();
    program_storage.shrink_to_fit();

    programs.clear();
    programs.shrink_to_fit();

    // free texture memory.
    texture_2d_storage.clear();
    texture_2d_storage.shrink_to_fit();

    /*
     * reset default framebuffer.
     */
    framebuffer.reset();
}

void render_device_context::clear_color_buffer()
{
    // buffer clearing respects scissoring.
    if(states.scissor_test_enabled
       && (states.scissor_box.x_min != 0 || states.scissor_box.x_max != framebuffer.color_buffer.info.width
           || states.scissor_box.y_min != 0 || states.scissor_box.y_max != framebuffer.color_buffer.info.height))
    {
        states.draw_target->clear_color(0, states.clear_color, states.scissor_box);
    }
    else
    {
        states.draw_target->clear_color(0, states.clear_color);
    }
}

void render_device_context::clear_depth_buffer()
{
    // buffer clearing respects scissoring.
    if(states.scissor_test_enabled
       && (states.scissor_box.x_min != 0 || states.scissor_box.x_max != framebuffer.color_buffer.info.width
           || states.scissor_box.y_min != 0 || states.scissor_box.y_max != framebuffer.color_buffer.info.height))
    {
        states.draw_target->clear_depth(states.clear_depth, states.scissor_box);
    }
    else
    {
        states.draw_target->clear_depth(states.clear_depth);
    }
}

/*
 * SDL render context implementation.
 */

pixel_format sdl_render_context::get_window_pixel_format(Uint32* out_sdl_pixel_format) const
{
    switch(SDL_GetWindowPixelFormat(sdl_window))
    {
#if defined(_WIN32)
    case SDL_PIXELFORMAT_RGB888:
#elif defined(__linux__)
    case SDL_PIXELFORMAT_RGB888:
#elif defined(__APPLE__)
    case SDL_PIXELFORMAT_RGB888:
#else
#    error Check the systems default pixel format.
#endif
    case SDL_PIXELFORMAT_ARGB8888:
        if(out_sdl_pixel_format)
        {
            *out_sdl_pixel_format = SDL_PIXELFORMAT_ARGB8888;
        }
        return pixel_format::argb8888;

    case SDL_PIXELFORMAT_RGBA8888:
        if(out_sdl_pixel_format)
        {
            *out_sdl_pixel_format = SDL_PIXELFORMAT_RGBA8888;
        }
        return pixel_format::rgba8888;
    }

    // this is the default case, but it is a guess.
    // FIXME log a warning?
    if(out_sdl_pixel_format)
    {
        *out_sdl_pixel_format = SDL_PIXELFORMAT_ARGB8888;
    }
    return pixel_format::argb8888;
}

void sdl_render_context::initialize(SDL_Window* window, SDL_Renderer* renderer, int width, int height)
{
    if(window == nullptr || renderer == nullptr || width <= 0 || height <= 0)
    {
        return;
    }

    sdl_window = window;
    sdl_renderer = renderer;

    // reset states to default values.
    states.reset(&framebuffer);

    // set viewport dimensions.
    states.set_viewport(0, 0, width, height);

    // set scissor box.
    states.set_scissor_box(0, width, 0, height);

    // Update buffers with the given width and height.
    update_buffers(width, height);

    // write dimensions for the blitting rectangle.
    sdl_viewport_dimensions = {0, 0, width, height};

    // create default texture.
    create_default_texture(this);

#ifdef SWR_ENABLE_MULTI_THREADING
    // create thread pool
    // we don't use more threads than reported by std::thread::hardware_concurrence and default to half of it.
    if(thread_pool_size == 0 || thread_pool_size > std::thread::hardware_concurrency())
    {
        thread_pool_size = (std::thread::hardware_concurrency() > 1) ? (std::thread::hardware_concurrency() / 2) : 1;
    }
    thread_pool.reset(thread_pool_size);

    try
    {
        rasterizer = std::make_unique<rast::sweep_rasterizer>(&thread_pool, &framebuffer);
    }
    catch(std::bad_alloc& e)
    {
        throw std::runtime_error(fmt::format("sdl_render_context: bad_alloc on allocating sweep_rasterizer: {}", e.what()));
    }
#else
    try
    {
        rasterizer = std::make_unique<rast::sweep_rasterizer>(nullptr, &framebuffer);
    }
    catch(std::bad_alloc& e)
    {
        throw std::runtime_error(fmt::format("sdl_render_context: bad_alloc on allocating sweep_rasterizer: {}", e.what()));
    }
#endif

    // create default shader. this needs to happen after the thread pool
    // is set up, since we create one shader per thread.
    create_default_shader(this);
}

void sdl_render_context::shutdown()
{
    if(framebuffer.is_color_weakly_attached())
    {
        // Unlock resets ColorBuffer.data_ptr.
        unlock();
    }

    if(sdl_color_buffer)
    {
        SDL_DestroyTexture(sdl_color_buffer);
        sdl_color_buffer = nullptr;
    }

    framebuffer.reset();

    sdl_renderer = nullptr;
    sdl_window = nullptr;

    render_device_context::shutdown();
}

void sdl_render_context::update_buffers(int width, int height)
{
    if(width <= 0 || height <= 0)
    {
        framebuffer.setup(0, 0, 0, pixel_format::unsupported, nullptr);
        return;
    }

    if(sdl_color_buffer)
    {
        SDL_DestroyTexture(sdl_color_buffer);
        sdl_color_buffer = nullptr;
    }

    // get pixel format.
    Uint32 native_pixel_format{0};
    auto swr_pixel_format = get_window_pixel_format(&native_pixel_format);

    // create SDL color buffer in native pixel format.
    sdl_color_buffer = SDL_CreateTexture(sdl_renderer, native_pixel_format, SDL_TEXTUREACCESS_STREAMING, width, height);
    if(sdl_color_buffer == nullptr)
    {
        return;
    }

    framebuffer.setup(width, height, 0, swr_pixel_format, nullptr);
}

void sdl_render_context::copy_default_color_buffer()
{
    if(sdl_color_buffer != nullptr && sdl_renderer != nullptr && sdl_window != nullptr)
    {
        SDL_RenderCopy(sdl_renderer, sdl_color_buffer, &sdl_viewport_dimensions, nullptr);
        SDL_RenderPresent(sdl_renderer);
        SDL_UpdateWindowSurface(sdl_window);
    }
}

bool sdl_render_context::lock()
{
    if(!framebuffer.is_color_weakly_attached())
    {
        uint32_t* data_ptr{nullptr};
        int pitch{0};

        if(SDL_LockTexture(sdl_color_buffer, nullptr, reinterpret_cast<void**>(&data_ptr), &pitch) != 0)
        {
            return false;
        }

        framebuffer.color_buffer.attach(sdl_viewport_dimensions.w, sdl_viewport_dimensions.h, pitch, data_ptr);
    }

    return framebuffer.is_color_attached();
}

void sdl_render_context::unlock()
{
    if(framebuffer.is_color_weakly_attached())
    {
        SDL_UnlockTexture(sdl_color_buffer);
        framebuffer.color_buffer.detach();
    }
}

} /* namespace impl */

/*
 * context interface.
 */

context_handle CreateSDLContext(SDL_Window* window, SDL_Renderer* renderer, uint32_t thread_hint)
{
    if(!window || !renderer)
    {
        return nullptr;
    }

    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    auto* context = new impl::sdl_render_context(thread_hint);
    context->initialize(window, renderer, width, height);
    return context;
}

void DestroyContext(context_handle context)
{
    if(context)
    {
        auto ctx = static_cast<impl::render_device_context*>(context);

        if(impl::global_context == ctx)
        {
            MakeContextCurrent(nullptr);
        }

        delete ctx;
    }
}

bool MakeContextCurrent(context_handle context)
{
    if(!context)
    {
        // make no context the current one.
        if(impl::global_context)
        {
            impl::global_context->unlock();
            impl::global_context = nullptr;
        }

        return true;
    }

    assert(!impl::global_context);
    impl::global_context = static_cast<impl::render_device_context*>(context);
    return impl::global_context->lock();
}

void CopyDefaultColorBuffer(context_handle context)
{
    assert(context);

    swr::impl::render_device_context* internal_context = static_cast<swr::impl::render_device_context*>(context);

    internal_context->unlock();
    internal_context->copy_default_color_buffer();

    // check results in debug builds.
#ifndef NDEBUG
    bool locked = internal_context->lock();
    assert(locked);
#else
    internal_context->lock();
#endif
}

} /* namespace swr */
