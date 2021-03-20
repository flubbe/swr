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
#include "rasterizer/sweep_st.h"

namespace swr
{
namespace impl
{

/** global render context. */
thread_local render_device_context* global_context = nullptr;

/*
 * render context implementation.
 */

void render_device_context::AllocateDepthBuffer()
{
    // allocate depth buffer.
    DepthBuffer.data.resize(ColorBuffer.width * ColorBuffer.height);
    DepthBuffer.data_ptr = &DepthBuffer.data[0];

    // set dimensions.
    DepthBuffer.width = ColorBuffer.width;
    DepthBuffer.height = ColorBuffer.height;
    DepthBuffer.pitch = DepthBuffer.width * sizeof(ml::fixed_32_t);
}

void render_device_context::Shutdown()
{
    // empty draw list.
    DrawList.clear();

    /*
     * Clean up all slot maps.
     */

    // delete all geometry data.
    vertex_buffers.clear();
    vertex_attribute_buffers.clear();
    index_buffers.clear();

    // delete shaders.
    ShaderObjectHash.clear();

    // free texture memory.
    for(auto it: Texture2dHash.data)
    {
        delete it;
    }
    Texture2dHash.clear();
}

void render_device_context::ClearColorBuffer()
{
    // buffer clearing respects scissoring.
    if(RenderStates.scissor_test_enabled
       && (RenderStates.scissor_box.x_min != 0 || RenderStates.scissor_box.x_max != ColorBuffer.width
           || RenderStates.scissor_box.y_min != 0 || RenderStates.scissor_box.y_max != ColorBuffer.height))
    {
        int x_min = std::min(std::max(0, RenderStates.scissor_box.x_min), ColorBuffer.width);
        int x_max = std::max(0, std::min(RenderStates.scissor_box.x_max, ColorBuffer.width));
        int y_min = std::min(std::max(ColorBuffer.height - RenderStates.scissor_box.y_max, 0), ColorBuffer.height);
        int y_max = std::max(0, std::min(ColorBuffer.height - RenderStates.scissor_box.y_min, ColorBuffer.height));

        const auto row_size = (x_max - x_min) * sizeof(uint32_t);

        if(ColorBuffer.data_ptr)
        {
            auto ptr = reinterpret_cast<uint8_t*>(ColorBuffer.data_ptr) + y_min * ColorBuffer.pitch + x_min * sizeof(uint32_t);
            for(int y = y_min; y < y_max; ++y)
            {
                utils::memset32(ptr, row_size, ClearColor);
                ptr += ColorBuffer.pitch;
            }
        }
    }
    else
    {
        ColorBuffer.clear(ClearColor);
    }
}

void render_device_context::ClearDepthBuffer()
{
    // buffer clearing respects scissoring.
    if(RenderStates.scissor_test_enabled
       && (RenderStates.scissor_box.x_min != 0 || RenderStates.scissor_box.x_max != DepthBuffer.width
           || RenderStates.scissor_box.y_min != 0 || RenderStates.scissor_box.y_max != DepthBuffer.height))
    {
        int x_min = std::min(std::max(0, RenderStates.scissor_box.x_min), DepthBuffer.width);
        int x_max = std::max(0, std::min(RenderStates.scissor_box.x_max, DepthBuffer.width));
        int y_min = std::min(std::max(DepthBuffer.height - RenderStates.scissor_box.y_max, 0), DepthBuffer.height);
        int y_max = std::max(0, std::min(DepthBuffer.height - RenderStates.scissor_box.y_min, DepthBuffer.height));

        const auto row_size = (x_max - x_min) * sizeof(ml::fixed_32_t);

        if(DepthBuffer.data_ptr)
        {
            auto ptr = reinterpret_cast<uint8_t*>(DepthBuffer.data_ptr) + y_min * DepthBuffer.pitch + x_min * sizeof(ml::fixed_32_t);
            for(int y = y_min; y < y_max; ++y)
            {
                utils::memset32(ptr, row_size, ml::unwrap(ClearDepth));
                ptr += DepthBuffer.pitch;
            }
        }
    }
    else
    {
        DepthBuffer.clear(ClearDepth);
    }
}

/*
 * SDL render context implementation.
 */

void sdl_render_context::Initialize(SDL_Window* InWindow, SDL_Renderer* InRenderer, int width, int height)
{
    if(InWindow == nullptr || InRenderer == nullptr || width <= 0 || height <= 0)
    {
        return;
    }

    Window = InWindow;
    Renderer = InRenderer;

    // initialize viewport transform.
    RenderStates.x = 0;
    RenderStates.y = 0;
    RenderStates.width = width;
    RenderStates.height = height;
    RenderStates.z_near = 0.f;
    RenderStates.z_far = 1.f;

    // initialze scissor box.
    RenderStates.scissor_box.x_min = 0;
    RenderStates.scissor_box.x_max = width;
    RenderStates.scissor_box.y_min = 0;
    RenderStates.scissor_box.y_max = height;

    // set depth func.
    RenderStates.depth_test_enabled = false;
    RenderStates.depth_func = comparison_func::less;

    // set blend func.
    RenderStates.blend_src = blend_func::one;
    RenderStates.blend_dst = blend_func::zero;

    // set color buffer width, height, and update the buffer.
    ColorBuffer.width = upper_align_on_block_size(width);
    ColorBuffer.height = upper_align_on_block_size(height);
    UpdateBuffers();

    // write dimensions for the blitting rectangle.
    ContextDimensions.x = 0;
    ContextDimensions.y = 0;
    ContextDimensions.w = width;
    ContextDimensions.h = height;

    // create default texture.
    create_default_texture(this);

    // create default shader.
    create_default_shader(this);

    // create triangle rasterizer.
    try
    {
        Rasterizer = std::unique_ptr<rast::sweep_rasterizer_single_threaded>(new rast::sweep_rasterizer_single_threaded(&ColorBuffer, &DepthBuffer));
    }
    catch(std::bad_alloc& e)
    {
        throw std::runtime_error(fmt::format("sdl_render_context: bad_alloc on allocating sweep_rasterizer_single_threaded: {}", e.what()));
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

    if(Buffer)
    {
        SDL_DestroyTexture(Buffer);
        Buffer = nullptr;
    }

    DepthBuffer.data.clear();
    DepthBuffer.width = DepthBuffer.height = DepthBuffer.pitch = 0;

    ColorBuffer.width = ColorBuffer.height = ColorBuffer.pitch = 0;

    Renderer = nullptr;
    Window = nullptr;

    render_device_context::Shutdown();
}

void sdl_render_context::UpdateBuffers()
{
    // set the (global) pixel format.
    Uint32 WindowPixelFormat = SDL_GetWindowPixelFormat(Window);

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

    if(Buffer)
    {
        SDL_DestroyTexture(Buffer);
        Buffer = nullptr;
    }
    Buffer = SDL_CreateTexture(Renderer, NativePixelFormat, SDL_TEXTUREACCESS_STREAMING, ColorBuffer.width, ColorBuffer.height);
    if(Buffer == nullptr)
    {
        return;
    }

    AllocateDepthBuffer();
}

void sdl_render_context::CopyDefaultColorBuffer()
{
    if(Buffer != nullptr && Renderer != nullptr && Window != nullptr)
    {
        SDL_RenderCopy(Renderer, Buffer, &ContextDimensions, nullptr);
        SDL_RenderPresent(Renderer);
        SDL_UpdateWindowSurface(Window);
    }
}

bool sdl_render_context::Lock()
{
    // lock texture.
    if(ColorBuffer.data_ptr != nullptr)
    {
        return false;
    }

    if(SDL_LockTexture(Buffer, nullptr, reinterpret_cast<void**>(&ColorBuffer.data_ptr), &ColorBuffer.pitch) != 0)
    {
        return false;
    }

    return ColorBuffer.data_ptr != nullptr;
}

void sdl_render_context::Unlock()
{
    if(ColorBuffer.data_ptr != nullptr)
    {
        SDL_UnlockTexture(Buffer);
        ColorBuffer.data_ptr = nullptr;
        ColorBuffer.pitch = 0;
    }
}

void sdl_render_context::DisplayDepthBuffer()
{
    if(ColorBuffer.data_ptr != nullptr)
    {
        for(int x = 0; x < ColorBuffer.width; ++x)
        {
            for(int y = 0; y < ColorBuffer.height; ++y)
            {
                const auto Depth = DepthBuffer.data_ptr[y * DepthBuffer.width + x];
                const ml::vec4 c = ml::vec4(1.f, 1.f, 1.f, 1.f) * ml::to_float(Depth);

                uint8_t* ColorBufferBytePtr = reinterpret_cast<uint8_t*>(ColorBuffer.data_ptr);
                ColorBufferBytePtr += y * ColorBuffer.pitch + x * sizeof(uint32_t);

                *reinterpret_cast<uint32_t*>(ColorBufferBytePtr) = ColorBuffer.pf_conv.to_pixel(c);
            }
        }
    }
}

}    // namespace impl

/*
 * context interface.
 */

context_handle CreateSDLContext(SDL_Window* Window, SDL_Renderer* Renderer)
{
    int Width = 0, Height = 0;
    SDL_GetWindowSize(Window, &Width, &Height);
    impl::sdl_render_context* InternalContext = new impl::sdl_render_context();
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
