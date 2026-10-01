/**
 * swr - a software rasterizer
 *
 * general render context and SDL render context implementation.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <format>

#include <SDL3/SDL_pixels.h>

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
thread_local render_context* global_context = nullptr;

/*
 * render context implementation.
 */

bool render_context::is_framebuffer_complete(
  std::uint32_t id)
{
    const auto slot = framebuffer_id_to_slot(id);
    if(!framebuffer_objects.contains(slot))
    {
        last_error = error::invalid_value;
        return false;
    }

    auto& fbo = framebuffer_objects[slot];

    const bool has_color_attachment = fbo.color_attachment_count != 0;
    const bool has_depth_attachment = fbo.has_depth_binding();
    if(!has_color_attachment
       && !has_depth_attachment)
    {
        return false;
    }

    // attachment completeness.
    for(auto& it: fbo.color_bindings)
    {
        if(it
           && (it->info.width == 0
               || it->info.height == 0
               || !it->is_valid()))    // FIXME validates storage, but should likely be done here.
        {
            return false;
        }
    }

    if(has_depth_attachment
       && !fbo.has_valid_depth_binding())    // FIXME validates storage, but should likely be done here.
    {
        return false;
    }

    return true;
}

framebuffer_draw_target* render_context::resolve_draw_target(
  std::uint32_t id)
{
    if(id == default_framebuffer_id)
    {
        return &framebuffer;
    }

    const auto slot = framebuffer_id_to_slot(id);
    if(!framebuffer_objects.contains(slot))
    {
        throw std::invalid_argument{"resolve_draw_target"};
    }

    return &framebuffer_objects[slot];
}

bool render_context::has_pending_deletion(
  resource_type type,
  std::uint32_t id) const
{
    for(const auto& key: pending_resource_deletions)
    {
        if(key.type == type && key.id == id)
        {
            return true;
        }
    }

    return false;
}

void render_context::process_pending_deletions()
{
    for(auto& key: pending_resource_deletions)
    {
        switch(key.type)
        {
        case resource_type::none:
            // `none` is only used to default-initialize the resource key.
            assert(0);
            break;
        case resource_type::index_buffer:
            if(index_buffers.contains(key.id))
            {
                index_buffers[key.id].clear();
                index_buffers.erase(key.id);
            }

            break;
        case resource_type::attribute_buffer:
            if(vertex_attribute_buffers.contains(key.id))
            {
                vertex_attribute_buffers[key.id].data.clear();
                vertex_attribute_buffers.erase(key.id);
            }

            break;
        case resource_type::texture:
            if(texture_2d_storage.contains(key.id))
            {
                texture_2d_storage[key.id].reset();
                texture_2d_storage.erase(key.id);
            }

            break;
        case resource_type::depth_attachment:
        {
            if(depth_attachments.contains(key.id))
            {
                depth_attachments.erase(key.id);
            }

            break;
        }
        case resource_type::framebuffer_object:
        {
            auto slot = framebuffer_id_to_slot(key.id);
            if(framebuffer_objects.contains(slot))
            {
                framebuffer_objects[key.id].reset();
                framebuffer_objects.erase(key.id);
            }

            break;
        }
        }
    }

    pending_resource_deletions.reset();
}

void render_context::create_rasterizer()
{
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
        throw std::runtime_error(std::format("sdl_render_context: bad_alloc on allocating sweep_rasterizer: {}", e.what()));
    }
#else
    try
    {
        rasterizer = std::make_unique<rast::sweep_rasterizer>(nullptr, &framebuffer);
    }
    catch(std::bad_alloc& e)
    {
        throw std::runtime_error(std::format("sdl_render_context: bad_alloc on allocating sweep_rasterizer: {}", e.what()));
    }
#endif
}

void render_context::shutdown()
{
    // release command list, render objects, state snapshots, and vertex data.
    command_list.release();
    render_objects.release();
    index_buffer_pool.release();
    resolved_draws.release();
    state_snapshots.release();
    vec4_data.release();

    /*
     * Clean up all slot maps.
     */

    // framebuffers.
    framebuffer_objects.clear();
    framebuffer_objects.shrink_to_fit();

    depth_attachments.clear();
    depth_attachments.shrink_to_fit();

    // delete all geometry data.
    vertex_attribute_buffers.clear();
    vertex_attribute_buffers.shrink_to_fit();

    index_buffers.clear();
    index_buffers.shrink_to_fit();

    // delete shaders.
#ifdef SWR_ENABLE_MULTI_THREADING
    program_instances.clear();
    program_instances.shrink_to_fit();

    program_storage.clear();
    program_storage.release_if_empty();
#endif /* SWR_ENABLE_MULTI_THREADING */

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

void render_context::create_clear_command(
  clear_kind kind)
{
    const std::size_t snapshot_idx = capture_state();
    command_list.emplace_back(
      clear_command{
        .kind = kind,
        .state_snapshot_index = snapshot_idx});
}

/*
 * SDL render context implementation.
 */

pixel_format sdl_render_context::get_window_pixel_format(SDL_PixelFormat* out_sdl_pixel_format) const
{
    switch(SDL_GetWindowPixelFormat(sdl_window))
    {
    case SDL_PIXELFORMAT_XRGB8888:
    case SDL_PIXELFORMAT_ARGB8888:
        if(out_sdl_pixel_format)
        {
            *out_sdl_pixel_format = SDL_PIXELFORMAT_XRGB8888;
        }
        return pixel_format::argb8888;

    case SDL_PIXELFORMAT_RGBX8888:
    case SDL_PIXELFORMAT_RGBA8888:
        if(out_sdl_pixel_format)
        {
            *out_sdl_pixel_format = SDL_PIXELFORMAT_RGBX8888;
        }
        return pixel_format::rgba8888;
    default: /* fall through */;
    }

    // this is the default case, but it is a guess.
    // FIXME log a warning?
    if(out_sdl_pixel_format)
    {
        *out_sdl_pixel_format = SDL_PIXELFORMAT_XRGB8888;
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
    states.reset();

    // set viewport dimensions.
    states.set_viewport(0, 0, width, height);

    // set scissor box.
    states.set_scissor_box(0, width, 0, height);

    // Update buffers with the given width and height.
    update_buffers(width, height);

    // write dimensions for the blitting rectangle.
    int rw, rh;
    SDL_GetRenderOutputSize(sdl_renderer, &rw, &rh);
    sdl_viewport_dimensions = {0.f, 0.f, static_cast<float>(rw), static_cast<float>(rh)};

    // create default texture.
    create_default_texture(this);

    // create rasterizer.
    create_rasterizer();

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

    render_context::shutdown();
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
    SDL_PixelFormat native_pixel_format;
    auto swr_pixel_format = get_window_pixel_format(&native_pixel_format);

    SDL_PropertiesID p = SDL_CreateProperties();
    if(p == 0)
    {
        throw std::runtime_error(std::format("sdl_render_context: could not create properties: {}", SDL_GetError()));
    }
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, native_pixel_format);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);

    sdl_color_buffer = SDL_CreateTextureWithProperties(sdl_renderer, p);
    SDL_DestroyProperties(p);

    if(sdl_color_buffer == nullptr)
    {
        throw std::runtime_error(std::format("sdl_render_context: could not create color buffer: {}", SDL_GetError()));
    }

    if(!SDL_SetTextureBlendMode(sdl_color_buffer, SDL_BLENDMODE_NONE))
    {
        throw std::runtime_error(std::format("sdl_render_context: could not set blend mode: {}", SDL_GetError()));
    }

    framebuffer.setup(
      width,
      height,
      width * sizeof(std::uint32_t),    // FIXME This depends on the pixel format, but we only support 4-byte pf's.
      swr_pixel_format,
      nullptr);

    // clamp viewport.
    int x0 = states.x;
    int y0 = states.y;
    int x1 = states.x + static_cast<int>(states.width);
    int y1 = states.y + static_cast<int>(states.height);

    // clip to framebuffer
    x0 = std::clamp(x0, 0, width);
    y0 = std::clamp(y0, 0, height);
    x1 = std::clamp(x1, 0, width);
    y1 = std::clamp(y1, 0, height);

    if(x1 < x0)
    {
        x1 = x0;
    }
    if(y1 < y0)
    {
        y1 = y0;
    }

    states.set_viewport(x0, y0, x1 - x0, y1 - y0);

    // clamp scissor box.
    int x_min = std::clamp(states.scissor_box.x_min, 0, width);
    int x_max = std::clamp(states.scissor_box.x_max, 0, width);
    int y_min = std::clamp(states.scissor_box.y_min, 0, height);
    int y_max = std::clamp(states.scissor_box.y_max, 0, height);

    if(x_max < x_min)
    {
        x_max = x_min;
    }
    if(y_max < y_min)
    {
        y_max = y_min;
    }

    states.set_scissor_box(x_min, x_max, y_min, y_max);
}

void sdl_render_context::copy_default_color_buffer()
{
    if(sdl_color_buffer != nullptr && sdl_renderer != nullptr && sdl_window != nullptr)
    {
        SDL_SetRenderTarget(sdl_renderer, nullptr);

        if(!SDL_RenderTexture(sdl_renderer, sdl_color_buffer, &sdl_viewport_dimensions, nullptr))
        {
            throw std::runtime_error(std::format("sdl_render_context: cound not render texture: {}", SDL_GetError()));
        }
        if(!SDL_RenderPresent(sdl_renderer))
        {
            throw std::runtime_error(std::format("sdl_render_context: cound not present: {}", SDL_GetError()));
        }
    }
}

bool sdl_render_context::lock()
{
    if(!framebuffer.is_color_weakly_attached())
    {
        std::uint32_t* data_ptr{nullptr};
        int pitch{0};

        if(!SDL_LockTexture(sdl_color_buffer, nullptr, reinterpret_cast<void**>(&data_ptr), &pitch))
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
        framebuffer.color_buffer.detach();
        SDL_UnlockTexture(sdl_color_buffer);
    }
}

/*
 * Offscreen render context implementation.
 */

void offscreen_render_context::initialize(
  int width,
  int height)
{
    if(width <= 0 || height <= 0)
    {
        return;
    }

    // reset states to default values.
    states.reset();

    // set viewport dimensions.
    states.set_viewport(0, 0, width, height);

    // set scissor box.
    states.set_scissor_box(0, width, 0, height);

    // Update buffers with the given width and height.
    if(!update_buffers(width, height))
    {
        throw std::runtime_error(
          "offscreen_render_context: initial buffer update failed.");
    }

    // create default texture.
    create_default_texture(this);

    // create rasterizer.
    create_rasterizer();

    // create default shader. this needs to happen after the thread pool
    // is set up, since we create one shader per thread.
    create_default_shader(this);
}

void offscreen_render_context::shutdown()
{
    if(framebuffer.is_color_weakly_attached())
    {
        // Unlock resets ColorBuffer.data_ptr.
        unlock();
    }

    framebuffer.reset();

    render_context::shutdown();
}

bool offscreen_render_context::update_buffers(int width, int height)
{
    if(width <= 0 || height <= 0)
    {
        return false;
    }

    if(locked)
    {
        return false;
    }

    rgba_buffer.resize(width * height);
    framebuffer.setup(
      width,
      height,
      width * sizeof(std::uint32_t),
      swr::pixel_format::argb8888,
      nullptr);

    this->width = width;
    this->height = height;

    // clamp viewport.
    int x0 = states.x;
    int y0 = states.y;
    int x1 = states.x + static_cast<int>(states.width);
    int y1 = states.y + static_cast<int>(states.height);

    // clip to framebuffer
    x0 = std::clamp(x0, 0, width);
    y0 = std::clamp(y0, 0, height);
    x1 = std::clamp(x1, 0, width);
    y1 = std::clamp(y1, 0, height);

    if(x1 < x0)
    {
        x1 = x0;
    }
    if(y1 < y0)
    {
        y1 = y0;
    }

    states.set_viewport(x0, y0, x1 - x0, y1 - y0);

    // clamp scissor box.
    int x_min = std::clamp(states.scissor_box.x_min, 0, width);
    int x_max = std::clamp(states.scissor_box.x_max, 0, width);
    int y_min = std::clamp(states.scissor_box.y_min, 0, height);
    int y_max = std::clamp(states.scissor_box.y_max, 0, height);

    if(x_max < x_min)
    {
        x_max = x_min;
    }
    if(y_max < y_min)
    {
        y_max = y_min;
    }

    states.set_scissor_box(x_min, x_max, y_min, y_max);

    return true;
}

bool offscreen_render_context::lock()
{
    if(locked)
    {
        return false;
    }

    locked = true;

    if(!framebuffer.is_color_weakly_attached())
    {
        framebuffer.color_buffer.attach(
          width,
          height,
          width * sizeof(std::uint32_t),
          static_cast<std::uint32_t*>(rgba_buffer.data()));
    }

    return framebuffer.is_color_attached();
}

void offscreen_render_context::unlock()
{
    if(locked)
    {
        if(framebuffer.is_color_weakly_attached())
        {
            framebuffer.color_buffer.detach();
        }

        locked = false;
    }
}

} /* namespace impl */

/*
 * context interface.
 */

context_handle CreateSDLContext(
  SDL_Window* window,
  SDL_Renderer* renderer,
  std::uint32_t thread_hint)
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

context_handle CreateOffscreenContext(
  std::uint32_t width,
  std::uint32_t height,
  std::uint32_t thread_hint)
{
    auto* context = new impl::offscreen_render_context(thread_hint);
    context->initialize(width, height);
    return context;
}

bool ResizeOffscreenContext(
  context_handle context,
  std::uint32_t width,
  std::uint32_t height)
{
    if(context == nullptr)
    {
        return false;
    }

    auto* internal_context = static_cast<swr::impl::render_context*>(context);
    if(internal_context->type != impl::context_type::offscreen)
    {
        return false;
    }

    auto* offscreen_context = static_cast<swr::impl::offscreen_render_context*>(internal_context);
    if(!offscreen_context->update_buffers(width, height))
    {
        return false;
    }

    offscreen_context->create_rasterizer();

    return true;
}

void DestroyContext(context_handle context)
{
    if(context != nullptr)
    {
        auto ctx = static_cast<impl::render_context*>(context);

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
    impl::global_context = static_cast<impl::render_context*>(context);
    return impl::global_context->lock();
}

void CopyDefaultColorBuffer(context_handle context)
{
    assert(context);

    swr::impl::render_context* internal_context = static_cast<swr::impl::render_context*>(context);
    if(internal_context->type != impl::context_type::sdl)
    {
        // Copying the default buffer is only valid for SDL-backed contexts.
        internal_context->last_error = error::invalid_operation;
        return;
    }

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

void GetContextInfo(
  context_handle context,
  void** data,
  int* width,
  int* height,
  int* components)
{
    swr::impl::render_context* internal_context = static_cast<swr::impl::render_context*>(context);
    *data = internal_context->framebuffer.color_buffer.info.data_ptr;
    if(width != nullptr)
    {
        *width = internal_context->framebuffer.color_buffer.info.width;
    }
    if(height != nullptr)
    {
        *height = internal_context->framebuffer.color_buffer.info.height;
    }
    if(components != nullptr)
    {
        *components = 4;
    }
}

} /* namespace swr */
