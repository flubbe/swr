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

void render_device_context::Shutdown()
{
    // empty command list.
    render_command_list.clear();

    /*
     * Clean up all slot maps.
     */

    // delete all geometry data.
    vertex_buffers.clear();
    vertex_attribute_buffers.clear();
    index_buffers.clear();

    // delete shaders.
    programs.clear();

    // free texture memory.
    texture_2d_storage.clear();
}

template<typename T>
static void scissor_clear_buffer(T clear_value, render_buffer<T>& buffer, const utils::rect& scissor_box)
{
    static_assert(sizeof(T) == sizeof(uint32_t), "Types sizes must match for utils::memset32 to work correctly.");

    int x_min = std::min(std::max(0, scissor_box.x_min), buffer.width);
    int x_max = std::max(0, std::min(scissor_box.x_max, buffer.width));
    int y_min = std::min(std::max(buffer.height - scissor_box.y_max, 0), buffer.height);
    int y_max = std::max(0, std::min(buffer.height - scissor_box.y_min, buffer.height));

    const auto row_size = (x_max - x_min) * sizeof(T);

    auto ptr = reinterpret_cast<uint8_t*>(buffer.data_ptr) + y_min * buffer.pitch + x_min * sizeof(T);
    for(int y = y_min; y < y_max; ++y)
    {
        utils::memset32(ptr, row_size, *reinterpret_cast<uint32_t*>(&clear_value));
        ptr += buffer.pitch;
    }
}

void render_device_context::ClearColorBuffer()
{
    // buffer clearing respects scissoring.
    if(states.scissor_test_enabled
       && (states.scissor_box.x_min != 0 || states.scissor_box.x_max != ColorBuffer.width
           || states.scissor_box.y_min != 0 || states.scissor_box.y_max != ColorBuffer.height))
    {
        scissor_clear_buffer(states.clear_color, ColorBuffer, states.scissor_box);
    }
    else
    {
        ColorBuffer.clear(states.clear_color);
    }
}

void render_device_context::ClearDepthBuffer()
{
    // buffer clearing respects scissoring.
    if(states.scissor_test_enabled
       && (states.scissor_box.x_min != 0 || states.scissor_box.x_max != DepthBuffer.width
           || states.scissor_box.y_min != 0 || states.scissor_box.y_max != DepthBuffer.height))
    {
        scissor_clear_buffer(states.clear_depth, DepthBuffer, states.scissor_box);
    }
    else
    {
        DepthBuffer.clear(states.clear_depth);
    }
}

/*
 * SDL render context implementation.
 */

void sdl_render_context::Initialize(SDL_Window* window, SDL_Renderer* renderer, int width, int height)
{
    if(window == nullptr || renderer == nullptr || width <= 0 || height <= 0)
    {
        return;
    }

    sdl_window = window;
    sdl_renderer = renderer;

    // initialize viewport transform.
    states.x = 0;
    states.y = 0;
    states.width = width;
    states.height = height;
    states.z_near = 0.f;
    states.z_far = 1.f;

    // initialze scissor box.
    states.scissor_box = utils::rect{0, width, 0, height};

    // set depth func.
    states.depth_test_enabled = false;
    states.depth_func = comparison_func::less;

    // set blend func.
    states.blend_src = blend_func::one;
    states.blend_dst = blend_func::zero;

    // set color buffer width, height, and update the buffer.
    ColorBuffer.width = upper_align_on_block_size(width);
    ColorBuffer.height = upper_align_on_block_size(height);
    UpdateBuffers();

    // write dimensions for the blitting rectangle.
    sdl_viewport_dimensions = {0, 0, width, height};

    // create default texture.
    create_default_texture(this);

    // create default shader.
    create_default_shader(this);

    // create triangle rasterizer using rasterizer_thread_pool_size threads. we don't use more threads than reported by std::thread::hardware_concurrency
    // and default to half of it.
    if(rasterizer_thread_pool_size == 0 || rasterizer_thread_pool_size > std::thread::hardware_concurrency())
    {
        rasterizer_thread_pool_size = (std::thread::hardware_concurrency() > 1) ? (std::thread::hardware_concurrency() / 2) : 1;
    }

    try
    {
        rasterizer = std::unique_ptr<rast::sweep_rasterizer>(new rast::sweep_rasterizer(rasterizer_thread_pool_size, &ColorBuffer, &DepthBuffer));
    }
    catch(std::bad_alloc& e)
    {
        throw std::runtime_error(fmt::format("sdl_render_context: bad_alloc on allocating sweep_rasterizer: {}", e.what()));
    }
}

void sdl_render_context::Shutdown()
{
    if(ColorBuffer.data_ptr != nullptr)
    {
        // Unlock resets ColorBuffer.data_ptr.
        Unlock();
        assert(ColorBuffer.data_ptr == nullptr);
    }

    if(sdl_color_buffer)
    {
        SDL_DestroyTexture(sdl_color_buffer);
        sdl_color_buffer = nullptr;
    }

    DepthBuffer.data.clear();
    DepthBuffer.width = DepthBuffer.height = DepthBuffer.pitch = 0;

    ColorBuffer.width = ColorBuffer.height = ColorBuffer.pitch = 0;

    sdl_renderer = nullptr;
    sdl_window = nullptr;

    render_device_context::Shutdown();
}

void sdl_render_context::UpdateBuffers()
{
    // set the (global) pixel format.
    Uint32 WindowPixelFormat = SDL_GetWindowPixelFormat(sdl_window);

    Uint32 NativePixelFormat = 0;
    switch(WindowPixelFormat)
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
        NativePixelFormat = SDL_PIXELFORMAT_ARGB8888;
        ColorBuffer.pf_conv.set_pixel_format(pixel_format_descriptor::named_format(pixel_format::argb8888));
        break;

    case SDL_PIXELFORMAT_RGBA8888:
        NativePixelFormat = SDL_PIXELFORMAT_RGBA8888;
        ColorBuffer.pf_conv.set_pixel_format(pixel_format_descriptor::named_format(pixel_format::rgba8888));
        break;

    default:
        NativePixelFormat = SDL_PIXELFORMAT_ARGB8888;
        ColorBuffer.pf_conv.set_pixel_format(pixel_format_descriptor::named_format(pixel_format::argb8888));
    }

    if(sdl_color_buffer)
    {
        SDL_DestroyTexture(sdl_color_buffer);
        sdl_color_buffer = nullptr;
    }
    sdl_color_buffer = SDL_CreateTexture(sdl_renderer, NativePixelFormat, SDL_TEXTUREACCESS_STREAMING, ColorBuffer.width, ColorBuffer.height);
    if(sdl_color_buffer == nullptr)
    {
        return;
    }

    DepthBuffer.allocate(ColorBuffer.width, ColorBuffer.height);
}

void sdl_render_context::CopyDefaultColorBuffer()
{
    if(sdl_color_buffer != nullptr && sdl_renderer != nullptr && sdl_window != nullptr)
    {
        SDL_RenderCopy(sdl_renderer, sdl_color_buffer, &sdl_viewport_dimensions, nullptr);
        SDL_RenderPresent(sdl_renderer);
        SDL_UpdateWindowSurface(sdl_window);
    }
}

bool sdl_render_context::Lock()
{
    // lock texture.
    if(ColorBuffer.data_ptr != nullptr)
    {
        return false;
    }

    if(SDL_LockTexture(sdl_color_buffer, nullptr, reinterpret_cast<void**>(&ColorBuffer.data_ptr), &ColorBuffer.pitch) != 0)
    {
        return false;
    }

    return ColorBuffer.data_ptr != nullptr;
}

void sdl_render_context::Unlock()
{
    if(ColorBuffer.data_ptr != nullptr)
    {
        SDL_UnlockTexture(sdl_color_buffer);
        ColorBuffer.data_ptr = nullptr;
        ColorBuffer.pitch = 0;
    }
}

}    // namespace impl

/*
 * context interface.
 */

context_handle CreateSDLContext(SDL_Window* Window, SDL_Renderer* Renderer, uint32_t thread_hint)
{
    int Width = 0, Height = 0;
    SDL_GetWindowSize(Window, &Width, &Height);
    impl::sdl_render_context* InternalContext = new impl::sdl_render_context(thread_hint);
    InternalContext->Initialize(Window, Renderer, Width, Height);
    return InternalContext;
}

void DestroyContext(context_handle Context)
{
    if(Context)
    {
        auto ctx = static_cast<impl::render_device_context*>(Context);

        if(impl::global_context == ctx)
        {
            MakeContextCurrent(nullptr);
        }

        delete ctx;
    }
}

bool MakeContextCurrent(context_handle Context)
{
    if(!Context)
    {
        // make no context the current one.
        if(impl::global_context)
        {
            impl::global_context->Unlock();
            impl::global_context = nullptr;
        }

        return true;
    }
    else
    {
        assert(!impl::global_context);
        impl::global_context = static_cast<impl::render_device_context*>(Context);
        return impl::global_context->Lock();
    }
}

void CopyDefaultColorBuffer(context_handle Context)
{
    assert(Context);

    swr::impl::render_device_context* InternalContext = static_cast<swr::impl::render_device_context*>(Context);

    InternalContext->Unlock();
    InternalContext->CopyDefaultColorBuffer();

    // check results in debug builds.
#ifdef DEBUG
    bool LockSucceeded = InternalContext->Lock();
    assert(LockSucceeded);
#else
    InternalContext->Lock();
#endif
}

} /* namespace swr */
