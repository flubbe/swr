/**
 * swr - a software rasterizer
 *
 * texture management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{

namespace impl
{

/** convenience macro to record failing function calls. */
#define CHECK_AND_SET_LAST_ERROR(expr) \
    {                                  \
        auto ret = expr;               \
        if(ret != error::none)         \
        {                              \
            context->last_error = ret; \
        }                              \
    }

/*
 * texture objects.
 */

void texture_2d::set_filter_mag(texture_filter filter_mag)
{
    sampler->set_filter_mag(filter_mag);
}

void texture_2d::set_filter_min(texture_filter filter_min)
{
    sampler->set_filter_min(filter_min);
}

void texture_2d::initialize_sampler()
{
    if(!sampler)
    {
        sampler = std::make_unique<sampler_2d_impl>(this);
    }
}

error texture_2d::set_wrap_s(wrap_mode s)
{
    if(s != wrap_mode::repeat && s != wrap_mode::mirrored_repeat && s != wrap_mode::clamp_to_edge)
    {
        /* invalid warp mode. */
        return error::invalid_value;
    }

    sampler->set_wrap_s(s);
    return error::none;
}

error texture_2d::set_wrap_t(wrap_mode t)
{
    if(t != wrap_mode::repeat && t != wrap_mode::mirrored_repeat && t != wrap_mode::clamp_to_edge)
    {
        /* invalid warp mode. */
        return error::invalid_value;
    }

    sampler->set_wrap_t(t);
    return error::none;
}

error texture_2d::allocate(std::uint32_t in_level, std::uint32_t in_width, std::uint32_t in_height)
{
    if(in_width == 0 || in_height == 0)
    {
        // this texture has no size, but we set width and height anyway.
        width = in_width;
        height = in_height;
        data.clear();

        return error::none;
    }

    if(!utils::is_power_of_two(in_width) || !utils::is_power_of_two(height))
    {
        return error::invalid_value;
    }

    auto uLevel = static_cast<std::size_t>(in_level);
    if(uLevel == 0)
    {
        if(width != in_width || height != in_height)
        {
            data.allocate(in_width, in_height);

            width = in_width;
            height = in_height;
        }
    }
    else
    {
        // assert the correct sizes.
        if(in_width != width >> uLevel
           || in_height != height >> uLevel)
        {
            return error::invalid_value;
        }
    }

    return error::none;
}

error texture_2d::set_data(
  std::uint32_t level,
  std::uint32_t width,
  std::uint32_t height,
  pixel_format format,
  const std::vector<std::uint8_t>& in_data)
{
    constexpr auto component_size = sizeof(std::uint32_t);

    // allocate the texture. this verifies that level is non-negative, and also sets width and height.
    auto ret = allocate(level, width, height);
    if(ret != error::none)
    {
        return ret;
    }

    // if no data was supplied, we act as just allocate was called.
    if(in_data.size() == 0)
    {
        return error::none;
    }

    // the data is allowed to be larger than what we really need.
    assert(width * height * component_size <= in_data.size());

    // check the upper bound for the mipmap level.
    if(static_cast<std::size_t>(level) >= data.data_ptrs.size())
    {
        return error::invalid_value;
    }

    auto data_ptr = data.data_ptrs[level];
#ifndef SWR_USE_MORTON_CODES
    auto pitch = width + (width >> 1);
#endif

    pixel_format_converter pfc{pixel_format_descriptor::named_format(format)};
    for(std::uint32_t y = 0; y < height; ++y)
    {
        for(std::uint32_t x = 0; x < width; ++x)
        {
            const std::uint8_t* buf_ptr = &in_data[(y * width + x) * component_size];
            std::uint32_t color = (*buf_ptr) << 24 | (*(buf_ptr + 1)) << 16 | (*(buf_ptr + 2)) << 8 | (*(buf_ptr + 3));
#ifdef SWR_USE_MORTON_CODES
            data_ptr[libmorton::morton2D_32_encode(x, y)] = pfc.to_color(color);
#else
            data_ptr[y * pitch + x] = pfc.to_color(color);
#endif
        }
    }

    return error::none;
}

error texture_2d::set_sub_data(
  std::uint32_t level,
  std::uint32_t in_x,
  std::uint32_t in_y,
  std::uint32_t in_width,
  std::uint32_t in_height,
  pixel_format format,
  const std::vector<std::uint8_t>& in_data)
{
    ASSERT_INTERNAL_CONTEXT;
    constexpr auto component_size = sizeof(std::uint32_t);

    if(in_width == 0 || in_height == 0 || in_data.size() == 0)
    {
        return error::invalid_value;
    }
    assert(in_width * in_height * component_size == in_data.size());

    if(level >= data.data_ptrs.size())
    {
        return error::invalid_value;
    }

    // ensure the dimensions are set up correctly.
    if(in_x >= width || in_y >= height)
    {
        return error::invalid_value;
    }

    auto data_ptr = data.data_ptrs[level];
#ifndef SWR_USE_MORTON_CODES
    auto pitch = width + (width >> 1);
#endif

    auto img_w = width >> level;
    auto img_h = height >> level;

    std::uint32_t max_width = 0;
    if(in_x < img_w)
    {
        std::uint32_t end = in_x + in_width;
        max_width = (end >= img_w ? img_w : end) - in_x;
    }

    std::uint32_t max_height = 0;
    if(in_y < img_h)
    {
        std::uint32_t end = in_y + in_height;
        max_height = (end >= img_h ? img_h : end) - in_y;
    }

    pixel_format_converter pfc(pixel_format_descriptor::named_format(format));
    for(std::size_t y = 0; y < max_height; ++y)
    {
        for(std::size_t x = 0; x < max_width; ++x)
        {
            const std::uint8_t* buf_ptr = &in_data[(y * in_width + x) * component_size];
            std::uint32_t color = (*buf_ptr) << 24 | (*(buf_ptr + 1)) << 16 | (*(buf_ptr + 2)) << 8 | (*(buf_ptr + 3));
#ifdef SWR_USE_MORTON_CODES
            data_ptr[libmorton::morton2D_32_encode(in_x + x, in_y + y)] = pfc.to_color(color);
#else
            data_ptr[(in_y + y) * pitch + (in_x + x)] = pfc.to_color(color);
#endif
        }
    }

    return error::none;
}

void texture_2d::clear()
{
    width = height = 0;
    id = impl::default_tex_id;

    data.clear();
}

/*
 * texture binding.
 */

/**
 * bind a 2d texture to the context's texture pointer. sets global_context->last_error to error::invalid_operation
 * if binding to the defaul texture failed. sets global_context->last_error to error::invalid_value, if
 * the supplied id is invalid.
 *
 * \param id the id of the texture to be bound to the 2d texture pointer
 * \return true if the bind was successful and false otherwise. In the latter case, global_context->last_error is set.
 */
bool bind_texture_pointer(texture_target target, std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;

    if(target != texture_target::texture_2d)
    {
        global_context->last_error = error::unimplemented;
        return false;
    }

    // if needed, increase unit array size.
    auto unit = global_context->states.texture_2d_active_unit;
    if(unit >= global_context->states.texture_2d_units.size())
    {
        if(unit > geom::limits::max::texture_units)
        {
            global_context->last_error = error::invalid_value;
            return false;
        }

        global_context->states.texture_2d_units.resize(unit + 1);
    }
    if(unit >= global_context->states.texture_2d_samplers.size())
    {
        if(unit > geom::limits::max::texture_units)
        {
            global_context->last_error = error::invalid_value;
            return false;
        }

        global_context->states.texture_2d_samplers.resize(unit + 1);
    }

    auto texture_2d = &global_context->states.texture_2d_units[unit];
    auto sampler_2d = &global_context->states.texture_2d_samplers[unit];

    if(id == default_tex_id)
    {
        *texture_2d = global_context->default_texture_2d;
        if(!(*texture_2d))
        {
            // this can only happen if the context was in an invalid state in the first place.
            global_context->last_error = error::invalid_operation;
            return false;
        }

        *sampler_2d = (*texture_2d)->sampler.get();
        return true;
    }

    if(*texture_2d == nullptr || (*texture_2d)->id != id)
    {
        if(id < global_context->texture_2d_storage.size())
        {
            *texture_2d = global_context->texture_2d_storage[id].get();
        }
        else
        {
            global_context->last_error = error::invalid_value;
            return false;
        }

        if(!(*texture_2d) || (*texture_2d)->id != id)
        {
            // this can only happen if the context was in an invalid state in the first place.
            global_context->last_error = error::invalid_operation;
            return false;
        }

        *sampler_2d = (*texture_2d)->sampler.get();
        return true;
    }

    // here the texture was already set.
    return true;
}

void create_default_texture(render_device_context* context)
{
    assert(context);

    // the default texture is stored in slot 0. check if it is already taken.
    if(context->texture_2d_storage.size() != 0)
    {
        context->last_error = error::invalid_operation;
        return;
    }

    const std::vector<std::uint8_t> default_texture_data = {
      0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, /* RGBA RGBA */
      0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff  /* RGBA RGBA */
    };

    // the memory allocated here is freed in render_device_context::shutdown.
    context->texture_2d_storage.push(std::make_unique<texture_2d>(default_tex_id));
    context->default_texture_2d = context->texture_2d_storage[default_tex_id].get();
    assert(context->default_texture_2d->id == default_tex_id);

#define CHECK(expr)                    \
    {                                  \
        auto ret = expr;               \
        if(ret != error::none)         \
        {                              \
            context->last_error = ret; \
            return;                    \
        }                              \
    }

    CHECK(context->default_texture_2d->set_data(0, 2, 2, pixel_format::rgba8888, default_texture_data));
    CHECK(context->default_texture_2d->set_wrap_s(wrap_mode::repeat));
    CHECK(context->default_texture_2d->set_wrap_t(wrap_mode::repeat));

#undef CHECK

    context->default_texture_2d->set_filter_mag(texture_filter::nearest);
    context->default_texture_2d->set_filter_min(texture_filter::nearest);
}

} /* namespace impl */

/*
 * texture interface.
 */

std::uint32_t CreateTexture()
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    // set up a new texture.
    auto slot = context->texture_2d_storage.push(std::make_unique<impl::texture_2d>());

    impl::texture_2d* new_texture = context->texture_2d_storage[slot].get();
    new_texture->id = slot;

    // TODO this should set the last used texture filters
    new_texture->set_filter_mag(texture_filter::nearest);
    new_texture->set_filter_min(texture_filter::nearest);

#define CHECK(expr)                    \
    {                                  \
        auto ret = expr;               \
        if(ret != error::none)         \
        {                              \
            context->last_error = ret; \
            return 0;                  \
        }                              \
    }

    // TODO this should set the last used wrap modes.
    CHECK(new_texture->set_wrap_s(wrap_mode::repeat));
    CHECK(new_texture->set_wrap_t(wrap_mode::repeat));

#undef CHECK

    return slot;
}

void ReleaseTexture(std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(id < context->texture_2d_storage.size())
    {
        auto texture_2d = &context->states.texture_2d_units[context->states.texture_2d_active_unit];
        auto sampler_2d = &context->states.texture_2d_samplers[context->states.texture_2d_active_unit];

        // see if this was the last texture used.
        if((*texture_2d) && (*texture_2d)->id == context->texture_2d_storage[id]->id)
        {
            // reset to the default texture.
            *texture_2d = context->default_texture_2d;
            if(*texture_2d)
            {
                *sampler_2d = (*texture_2d)->sampler.get();
            }
        }

        // free texture memory.
        context->texture_2d_storage[id].reset();
        context->texture_2d_storage.free(id);
    }
}

void ActiveTexture(std::uint32_t unit)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(unit >= geom::limits::max::texture_units)
    {
        context->last_error = error::invalid_value;
        return;
    }

    if(unit >= context->states.texture_2d_samplers.size())
    {
        if(unit > geom::limits::max::texture_units)
        {
            context->last_error = error::invalid_value;
            return;
        }

        context->states.texture_2d_samplers.resize(unit + 1);
    }
    context->states.texture_2d_active_unit = unit;
}

void BindTexture(texture_target target, std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(target != texture_target::texture_2d)
    {
        context->last_error = error::unimplemented;
        return;
    }

    impl::bind_texture_pointer(target, id);
}

void AllocateImage(std::uint32_t texture_id, std::size_t width, std::size_t height)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(texture_id == impl::default_tex_id)
    {
        context->last_error = error::invalid_value;
        return;
    }

    if(texture_id >= context->texture_2d_storage.size())
    {
        context->last_error = error::invalid_value;
        return;
    }

    impl::texture_2d* texture_2d = context->texture_2d_storage[texture_id].get();
    CHECK_AND_SET_LAST_ERROR(texture_2d->allocate(0, width, height));
}

void SetImage(std::uint32_t texture_id, std::uint32_t level, std::size_t width, std::size_t height, pixel_format format, const std::vector<std::uint8_t>& data)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(texture_id == impl::default_tex_id)
    {
        context->last_error = error::invalid_value;
        return;
    }

    if(texture_id >= context->texture_2d_storage.size())
    {
        context->last_error = error::invalid_value;
        return;
    }

    impl::texture_2d* texture_2d = context->texture_2d_storage[texture_id].get();
    CHECK_AND_SET_LAST_ERROR(texture_2d->set_data(level, width, height, format, data));
}

void SetSubImage(std::uint32_t texture_id, std::uint32_t level, std::size_t offset_x, std::size_t offset_y, std::size_t width, std::size_t height, pixel_format format, const std::vector<std::uint8_t>& data)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(texture_id == impl::default_tex_id)
    {
        context->last_error = error::invalid_value;
        return;
    }

    if(texture_id >= context->texture_2d_storage.size())
    {
        context->last_error = error::invalid_value;
        return;
    }

    impl::texture_2d* texture_2d = context->texture_2d_storage[texture_id].get();
    CHECK_AND_SET_LAST_ERROR(texture_2d->set_sub_data(level, offset_x, offset_y, width, height, format, data));
}

void SetTextureWrapMode(std::uint32_t id, wrap_mode s, wrap_mode t)
{
    if(impl::bind_texture_pointer(texture_target::texture_2d, id))
    {
        ASSERT_INTERNAL_CONTEXT;
        impl::render_device_context* context = impl::global_context;

        if((s != wrap_mode::repeat && s != wrap_mode::mirrored_repeat && s != wrap_mode::clamp_to_edge)
           || (t != wrap_mode::repeat && t != wrap_mode::mirrored_repeat && t != wrap_mode::clamp_to_edge))
        {
            context->last_error = error::invalid_value;
            return;
        }

        auto sampler = static_cast<impl::sampler_2d_impl*>(context->states.texture_2d_samplers[context->states.texture_2d_active_unit]);
        sampler->set_wrap_s(s);
        sampler->set_wrap_t(t);
    }
}

void GetTextureWrapMode(std::uint32_t id, wrap_mode* s, wrap_mode* t)
{
    if(impl::bind_texture_pointer(texture_target::texture_2d, id))
    {
        ASSERT_INTERNAL_CONTEXT;
        impl::render_device_context* context = impl::global_context;

        auto sampler = static_cast<impl::sampler_2d_impl*>(context->states.texture_2d_samplers[context->states.texture_2d_active_unit]);
        if(s)
        {
            *s = sampler->get_wrap_s();
        }
        if(t)
        {
            *t = sampler->get_wrap_t();
        }
    }
}

void SetTextureMinificationFilter(texture_filter filter)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;
    auto texture_2d = context->states.texture_2d_units[context->states.texture_2d_active_unit];

    if(texture_2d)
    {
        texture_2d->set_filter_min(filter);
    }
    else
    {
        context->last_error = error::invalid_operation;
    }
}

void SetTextureMagnificationFilter(texture_filter filter)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;
    auto texture_2d = context->states.texture_2d_units[context->states.texture_2d_active_unit];

    if(texture_2d)
    {
        texture_2d->set_filter_mag(filter);
    }
    else
    {
        context->last_error = error::invalid_operation;
    }
}

texture_filter GetTextureMinificationFilter()
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;
    auto sampler = static_cast<impl::sampler_2d_impl*>(context->states.texture_2d_samplers[context->states.texture_2d_active_unit]);

    if(sampler)
    {
        return sampler->get_filter_min();
    }
    else
    {
        context->last_error = error::invalid_operation;
    }
    return texture_filter::nearest;
}

texture_filter GetTextureMagnificationFilter()
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;
    auto sampler = static_cast<impl::sampler_2d_impl*>(context->states.texture_2d_samplers[context->states.texture_2d_active_unit]);

    if(sampler)
    {
        return sampler->get_filter_mag();
    }
    else
    {
        context->last_error = error::invalid_operation;
    }
    return texture_filter::nearest;
}

} /* namespace swr */
