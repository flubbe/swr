/**
 * swr - a software rasterizer
 *
 * texture management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{

namespace impl
{

static std::unique_ptr<texture_2d> make_texture_for_format(
  std::uint32_t id,
  pixel_format format)
{
    if(format == pixel_format::depth32f)
    {
        return std::make_unique<texture_depth_2d>(id);
    }

    return std::make_unique<texture_color_2d>(id);
}

/** convenience macro to record failing function calls. */
#define CHECK_AND_SET_LAST_ERROR(expr)      \
    if(auto ret = expr; ret != error::none) \
    {                                       \
        context->last_error = ret;          \
    }

/*
 * texture_2d.
 */

void texture_2d::set_filter_mag(
  texture_filter filter_mag)
{
    sampler->set_filter_mag(filter_mag);
}

void texture_2d::set_filter_min(
  texture_filter filter_min)
{
    sampler->set_filter_min(filter_min);
}

void texture_2d::initialize_sampler()
{
    if(!sampler)
    {
        sampler = std::make_unique<sampler_2d_impl>(this);
    }

    sampler->refresh_texture_state();
}

error texture_2d::set_wrap_s(
  wrap_mode s)
{
    if(s != wrap_mode::repeat
       && s != wrap_mode::mirrored_repeat
       && s != wrap_mode::clamp_to_edge)
    {
        /* invalid warp mode. */
        return error::invalid_value;
    }

    sampler->set_wrap_s(s);
    return error::none;
}

error texture_2d::set_wrap_t(
  wrap_mode t)
{
    if(t != wrap_mode::repeat
       && t != wrap_mode::mirrored_repeat
       && t != wrap_mode::clamp_to_edge)
    {
        /* invalid warp mode. */
        return error::invalid_value;
    }

    sampler->set_wrap_t(t);
    return error::none;
}

void texture_2d::clear()
{
    width = height = 0;
    id = impl::default_tex_id;
    format = pixel_format::unsupported;
    sampler->refresh_texture_state();
}

/*
 * texture_color_2d.
 */

error texture_color_2d::allocate(
  std::uint32_t in_level,
  std::uint32_t in_width,
  std::uint32_t in_height,
  pixel_format in_format)
{
    if(in_format == pixel_format::depth32f)
    {
        return error::invalid_operation;
    }

    if(in_width == 0
       || in_height == 0)
    {
        width = in_width;
        height = in_height;
        format = in_format;
        data.clear();
        sampler->refresh_texture_state();
        return error::none;
    }

    if(!utils::is_power_of_two(in_width)
       || !utils::is_power_of_two(in_height))
    {
        return error::invalid_value;
    }

    const auto u_level = static_cast<std::size_t>(in_level);
    if(u_level == 0)
    {
        if(width != in_width
           || height != in_height
           || format != in_format)
        {
            data.allocate(in_width, in_height);
            width = in_width;
            height = in_height;
            format = in_format;
            sampler->refresh_texture_state();
        }
    }
    else
    {
        if(format != in_format)
        {
            return error::invalid_value;
        }

        if(in_width != width >> u_level
           || in_height != height >> u_level)
        {
            return error::invalid_value;
        }
    }

    return error::none;
}

error texture_color_2d::set_data(
  std::uint32_t level,
  std::uint32_t in_width,
  std::uint32_t in_height,
  pixel_format in_format,
  const std::vector<std::uint8_t>& in_data)
{
    constexpr auto component_size = sizeof(std::uint32_t);

    auto ret = allocate(level, in_width, in_height, in_format);
    if(ret != error::none)
    {
        return ret;
    }

    if(in_width == 0 || in_height == 0)
    {
        return error::none;
    }

    if(in_data.empty())
    {
        return error::none;
    }

    assert(in_width * in_height * component_size <= in_data.size());

    if(static_cast<std::size_t>(level) >= mip_level_count())
    {
        return error::invalid_value;
    }

    auto data_ptr = data.data_ptrs[level];
#ifndef SWR_USE_MORTON_CODES
    const auto pitch = data.pitches[level];
#endif

    pixel_format_converter pfc{
      pixel_format_descriptor::named_format(in_format)};
    for(std::uint32_t y = 0; y < in_height; ++y)
    {
        for(std::uint32_t x = 0; x < in_width; ++x)
        {
            const std::uint8_t* buf_ptr = &in_data[(y * in_width + x) * component_size];
            std::uint32_t color =
              (*buf_ptr) << 24
              | (*(buf_ptr + 1)) << 16
              | (*(buf_ptr + 2)) << 8
              | (*(buf_ptr + 3));
#ifdef SWR_USE_MORTON_CODES
            data_ptr[libmorton::morton2D_32_encode(x, y)] = pfc.to_color(color);
#else
            data_ptr[y * pitch + x] = pfc.to_color(color);
#endif
        }
    }

    return error::none;
}

error texture_color_2d::set_sub_data(
  std::uint32_t level,
  std::uint32_t in_x,
  std::uint32_t in_y,
  std::uint32_t in_width,
  std::uint32_t in_height,
  pixel_format in_format,
  const std::vector<std::uint8_t>& in_data)
{
    ASSERT_INTERNAL_CONTEXT;
    constexpr auto component_size = sizeof(std::uint32_t);

    if(in_width == 0 || in_height == 0)
    {
        return error::none;
    }

    if(in_data.empty())
    {
        return error::invalid_value;
    }
    assert(in_width * in_height * component_size == in_data.size());

    if(level >= mip_level_count())
    {
        return error::invalid_value;
    }

    if(in_format != format)
    {
        return error::invalid_value;
    }

    if(in_x >= width
       || in_y >= height)
    {
        return error::invalid_value;
    }

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

    auto data_ptr = data.data_ptrs[level];
#ifndef SWR_USE_MORTON_CODES
    const auto pitch = data.pitches[level];
#endif

    pixel_format_converter pfc{
      pixel_format_descriptor::named_format(in_format)};
    for(std::size_t y = 0; y < max_height; ++y)
    {
        for(std::size_t x = 0; x < max_width; ++x)
        {
            const std::uint8_t* buf_ptr = &in_data[(y * in_width + x) * component_size];
            std::uint32_t color =
              (*buf_ptr) << 24
              | (*(buf_ptr + 1)) << 16
              | (*(buf_ptr + 2)) << 8
              | (*(buf_ptr + 3));
#ifdef SWR_USE_MORTON_CODES
            data_ptr[libmorton::morton2D_32_encode(in_x + x, in_y + y)] = pfc.to_color(color);
#else
            data_ptr[(in_y + y) * pitch + (in_x + x)] = pfc.to_color(color);
#endif
        }
    }

    return error::none;
}

void texture_color_2d::clear()
{
    texture_2d::clear();
    data.clear();
}

/*
 * texture_depth_2d.
 */

error texture_depth_2d::allocate(
  std::uint32_t in_level,
  std::uint32_t in_width,
  std::uint32_t in_height,
  pixel_format in_format)
{
    if(in_format != pixel_format::depth32f)
    {
        return error::invalid_operation;
    }

    if(in_width == 0
       || in_height == 0)
    {
        width = in_width;
        height = in_height;
        format = in_format;
        data.clear();
        sampler->refresh_texture_state();
        return error::none;
    }

    if(!utils::is_power_of_two(in_width)
       || !utils::is_power_of_two(in_height))
    {
        return error::invalid_value;
    }

    const auto u_level = static_cast<std::size_t>(in_level);
    if(u_level == 0)
    {
        if(width != in_width
           || height != in_height
           || format != in_format)
        {
            data.allocate(in_width, in_height);
            width = in_width;
            height = in_height;
            format = in_format;
            sampler->refresh_texture_state();
        }
    }
    else
    {
        if(format != in_format)
        {
            return error::invalid_value;
        }

        if(in_width != width >> u_level
           || in_height != height >> u_level)
        {
            return error::invalid_value;
        }
    }

    return error::none;
}

error texture_depth_2d::set_data(
  std::uint32_t level,
  std::uint32_t in_width,
  std::uint32_t in_height,
  pixel_format in_format,
  const std::vector<std::uint8_t>& in_data)
{
    constexpr auto component_size = sizeof(float);

    auto ret = allocate(level, in_width, in_height, in_format);
    if(ret != error::none)
    {
        return ret;
    }

    if(in_width == 0 || in_height == 0)
    {
        return error::none;
    }

    if(in_data.empty())
    {
        return error::none;
    }

    assert(in_width * in_height * component_size <= in_data.size());

    if(static_cast<std::size_t>(level) >= mip_level_count())
    {
        return error::invalid_value;
    }

#ifndef SWR_USE_MORTON_CODES
    const auto pitch = data.pitches[level];
#endif
    auto data_ptr = data.data_ptrs[level];
    for(std::uint32_t y = 0; y < in_height; ++y)
    {
        for(std::uint32_t x = 0; x < in_width; ++x)
        {
            float depth = 0.0f;
            std::memcpy(
              &depth,
              &in_data[(y * in_width + x) * component_size],
              sizeof(depth));
            const ml::fixed_32_t depth_value{
              std::clamp(depth, 0.0f, 1.0f)};
#ifdef SWR_USE_MORTON_CODES
            data_ptr[libmorton::morton2D_32_encode(x, y)] = depth_value;
#else
            data_ptr[y * pitch + x] = depth_value;
#endif
        }
    }

    return error::none;
}

error texture_depth_2d::set_sub_data(
  std::uint32_t level,
  std::uint32_t in_x,
  std::uint32_t in_y,
  std::uint32_t in_width,
  std::uint32_t in_height,
  pixel_format in_format,
  const std::vector<std::uint8_t>& in_data)
{
    ASSERT_INTERNAL_CONTEXT;
    constexpr auto component_size = sizeof(float);

    if(in_width == 0 || in_height == 0)
    {
        return error::none;
    }

    if(in_data.empty())
    {
        return error::invalid_value;
    }
    assert(in_width * in_height * component_size == in_data.size());

    if(level >= mip_level_count())
    {
        return error::invalid_value;
    }

    if(in_format != format)
    {
        return error::invalid_value;
    }

    if(in_x >= width
       || in_y >= height)
    {
        return error::invalid_value;
    }

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

#ifndef SWR_USE_MORTON_CODES
    const auto pitch = data.pitches[level];
#endif
    auto data_ptr = data.data_ptrs[level];
    for(std::size_t y = 0; y < max_height; ++y)
    {
        for(std::size_t x = 0; x < max_width; ++x)
        {
            float depth = 0.0f;
            std::memcpy(
              &depth,
              &in_data[(y * in_width + x) * component_size],
              sizeof(depth));
            const ml::fixed_32_t depth_value{
              std::clamp(depth, 0.0f, 1.0f)};
#ifdef SWR_USE_MORTON_CODES
            data_ptr[libmorton::morton2D_32_encode(in_x + x, in_y + y)] = depth_value;
#else
            data_ptr[(in_y + y) * pitch + (in_x + x)] = depth_value;
#endif
        }
    }

    return error::none;
}

void texture_depth_2d::clear()
{
    texture_2d::clear();
    data.clear();
    compare_mode = texture_compare_mode::none;
    compare_func = comparison_func::less_equal;
}

error texture_depth_2d::set_compare_mode(
  texture_compare_mode mode)
{
    if(mode != texture_compare_mode::none
       && mode != texture_compare_mode::ref_to_texture)
    {
        return error::invalid_value;
    }

    compare_mode = mode;
    return error::none;
}

error texture_depth_2d::set_compare_func(
  comparison_func func)
{
    compare_func = func;
    return error::none;
}

/*
 * texture binding.
 */

/**
 * bind a 2d texture to a context's texture target pointer. sets global_context->last_error to error::invalid_operation
 * if binding to the defaul texture failed. sets global_context->last_error to error::invalid_value, if
 * the supplied id is invalid.
 *
 * @param target texture target
 * @param id the id of the texture to be bound to the 2d texture pointer
 * @return true if the bind was successful and false otherwise. In the latter case, global_context->last_error is set.
 */
bool bind_texture_pointer(
  texture_target target,
  std::uint32_t id)
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
        if(unit > swr::limits::max::texture_units)
        {
            global_context->last_error = error::invalid_value;
            return false;
        }

        global_context->states.texture_2d_units.resize(unit + 1);
    }
    if(unit >= global_context->states.texture_2d_samplers.size())
    {
        if(unit > swr::limits::max::texture_units)
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

        *sampler_2d = static_cast<swr::sampler_2d*>((*texture_2d)->sampler.get());
        return true;
    }

    if(*texture_2d == nullptr
       || (*texture_2d)->id != id)
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

        if(!(*texture_2d)
           || (*texture_2d)->id != id)
        {
            // this can only happen if the context was in an invalid state in the first place.
            global_context->last_error = error::invalid_operation;
            return false;
        }

        *sampler_2d = static_cast<swr::sampler_2d*>((*texture_2d)->sampler.get());
        return true;
    }

    // here the texture was already set.
    return true;
}

void create_default_texture(
  render_context* context)
{
    assert(context);

    // the default texture is stored in slot 0. check if it is already taken.
    if(!context->texture_2d_storage.empty())
    {
        context->last_error = error::invalid_operation;
        return;
    }

    const std::vector<std::uint8_t> default_texture_data = {
      0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, /* RGBA RGBA */
      0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff  /* RGBA RGBA */
    };

    // the memory allocated here is freed in render_device_context::shutdown.
    context->texture_2d_storage.push(std::make_unique<texture_color_2d>(default_tex_id));
    context->default_texture_2d = context->texture_2d_storage[default_tex_id].get();
    assert(context->default_texture_2d->id == default_tex_id);

#define CHECK(expr)                         \
    if(auto ret = expr; ret != error::none) \
    {                                       \
        context->last_error = ret;          \
        return;                             \
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
    impl::render_context* context = impl::global_context;

    // set up a new texture.
    auto slot = context->texture_2d_storage.push(
      std::make_unique<impl::texture_color_2d>());

    impl::texture_2d* new_texture = context->texture_2d_storage[slot].get();
    new_texture->id = slot;

    /*
     * TODO Set the initial value of min to nearest_mipmap_linear
     *      (see e.g. https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexParameter.xhtml)
     */
    new_texture->set_filter_mag(texture_filter::linear);
    new_texture->set_filter_min(texture_filter::nearest);

#define CHECK(expr)                         \
    if(auto ret = expr; ret != error::none) \
    {                                       \
        context->last_error = ret;          \
        return 0;                           \
    }

    /*
     * Initial values: wrap_mode::repeat
     * (see e.g. https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexParameter.xhtml)
     */
    CHECK(new_texture->set_wrap_s(wrap_mode::repeat));
    CHECK(new_texture->set_wrap_t(wrap_mode::repeat));

#undef CHECK

    return slot;
}

void ReleaseTexture(
  std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(id < context->texture_2d_storage.size()
       && !context->texture_2d_storage.is_free(id))
    {
        const auto active_unit = context->states.texture_2d_active_unit;

        // see if this was the last texture used on the active unit, and if so reset to the default texture.
        if(active_unit < context->states.texture_2d_units.size())
        {
            auto texture_2d = &context->states.texture_2d_units[active_unit];
            auto sampler_2d = &context->states.texture_2d_samplers[active_unit];

            if((*texture_2d)
               && (*texture_2d)->id == context->texture_2d_storage[id]->id)
            {
                // reset to the default texture.
                *texture_2d = context->default_texture_2d;
                if(*texture_2d)
                {
                    *sampler_2d = static_cast<swr::sampler_2d*>((*texture_2d)->sampler.get());
                }
            }
        }

        // free texture memory.
        context->texture_2d_storage[id].reset();
        context->texture_2d_storage.free(id);
    }
}

void ActiveTexture(
  std::uint32_t unit)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(unit >= swr::limits::max::texture_units)
    {
        context->last_error = error::invalid_value;
        return;
    }

    if(unit >= context->states.texture_2d_samplers.size())
    {
        if(unit > swr::limits::max::texture_units)
        {
            context->last_error = error::invalid_value;
            return;
        }

        context->states.texture_2d_samplers.resize(unit + 1);
    }
    context->states.texture_2d_active_unit = unit;
}

void BindTexture(
  texture_target target,
  std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(target != texture_target::texture_2d)
    {
        context->last_error = error::unimplemented;
        return;
    }

    impl::bind_texture_pointer(target, id);
}

void SetImage(
  std::uint32_t texture_id,
  std::uint32_t level,
  std::size_t width,
  std::size_t height,
  pixel_format format,
  const std::vector<std::uint8_t>& data)
{
    // TODO Rebinding code likely needs a rewrite.

    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

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

    if(level == 0)
    {
        auto* existing = context->texture_2d_storage[texture_id].get();
        const bool is_depth_tex = existing->as_texture_depth_2d() != nullptr;
        const bool wants_depth_tex = format == pixel_format::depth32f;
        if(is_depth_tex != wants_depth_tex)
        {
            auto replacement = impl::make_texture_for_format(texture_id, format);

            replacement->set_filter_mag(existing->sampler->get_filter_mag());
            replacement->set_filter_min(existing->sampler->get_filter_min());

            auto ret = replacement->set_wrap_s(existing->sampler->get_wrap_s());
            if(ret != error::none)
            {
                context->last_error = ret;
                return;
            }

            ret = replacement->set_wrap_t(existing->sampler->get_wrap_t());
            if(ret != error::none)
            {
                context->last_error = ret;
                return;
            }

            replacement->sampler = std::move(existing->sampler);
            replacement->sampler->set_associated_texture(replacement.get());
            context->texture_2d_storage[texture_id] = std::move(replacement);

            auto* rebound = context->texture_2d_storage[texture_id].get();
            for(std::size_t i = 0; i < context->states.texture_2d_units.size(); ++i)
            {
                auto* bound_tex = context->states.texture_2d_units[i];
                if(bound_tex != nullptr
                   && bound_tex->id == texture_id)
                {
                    context->states.texture_2d_units[i] = rebound;
                }
            }
        }
    }

    impl::texture_2d* texture_2d = context->texture_2d_storage[texture_id].get();
    CHECK_AND_SET_LAST_ERROR(
      texture_2d->set_data(
        level,
        width,
        height,
        format,
        data));
}

void SetSubImage(
  std::uint32_t texture_id,
  std::uint32_t level,
  std::size_t offset_x,
  std::size_t offset_y,
  std::size_t width,
  std::size_t height,
  pixel_format format,
  const std::vector<std::uint8_t>& data)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

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
    CHECK_AND_SET_LAST_ERROR(
      texture_2d->set_sub_data(
        level,
        offset_x,
        offset_y,
        width,
        height,
        format,
        data));
}

void SetTextureWrapMode(
  std::uint32_t id,
  wrap_mode s,
  wrap_mode t)
{
    if(impl::bind_texture_pointer(texture_target::texture_2d, id))
    {
        ASSERT_INTERNAL_CONTEXT;
        impl::render_context* context = impl::global_context;

        if((s != wrap_mode::repeat
            && s != wrap_mode::mirrored_repeat
            && s != wrap_mode::clamp_to_edge)
           || (t != wrap_mode::repeat
               && t != wrap_mode::mirrored_repeat
               && t != wrap_mode::clamp_to_edge))
        {
            context->last_error = error::invalid_value;
            return;
        }

        auto texture_2d =
          context->states.texture_2d_units[context->states.texture_2d_active_unit];
        if(texture_2d == nullptr)
        {
            context->last_error = error::invalid_operation;
            return;
        }

        CHECK_AND_SET_LAST_ERROR(texture_2d->set_wrap_s(s));
        CHECK_AND_SET_LAST_ERROR(texture_2d->set_wrap_t(t));
    }
}

void GetTextureWrapMode(
  std::uint32_t id,
  wrap_mode* s,
  wrap_mode* t)
{
    if(impl::bind_texture_pointer(texture_target::texture_2d, id))
    {
        ASSERT_INTERNAL_CONTEXT;
        impl::render_context* context = impl::global_context;
        auto texture_2d =
          context->states.texture_2d_units[context->states.texture_2d_active_unit];

        if(texture_2d == nullptr)
        {
            context->last_error = error::invalid_operation;
            return;
        }

        if(s)
        {
            *s = texture_2d->get_wrap_s();
        }
        if(t)
        {
            *t = texture_2d->get_wrap_t();
        }
    }
}

void SetTextureMinificationFilter(
  texture_filter filter)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;
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

void SetTextureMagnificationFilter(
  texture_filter filter)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;
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
    impl::render_context* context = impl::global_context;
    auto texture_2d =
      context->states.texture_2d_units[context->states.texture_2d_active_unit];

    if(texture_2d)
    {
        return texture_2d->get_filter_min();
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
    impl::render_context* context = impl::global_context;
    auto texture_2d =
      context->states.texture_2d_units[context->states.texture_2d_active_unit];

    if(texture_2d)
    {
        return texture_2d->get_filter_mag();
    }
    else
    {
        context->last_error = error::invalid_operation;
    }
    return texture_filter::nearest;
}

void SetTextureCompareMode(
  std::uint32_t id,
  texture_compare_mode mode)
{
    if(impl::bind_texture_pointer(texture_target::texture_2d, id))
    {
        ASSERT_INTERNAL_CONTEXT;
        impl::render_context* context = impl::global_context;
        auto texture_2d = context->states.texture_2d_units[context->states.texture_2d_active_unit];

        if(texture_2d)
        {
            if(texture_2d->as_texture_depth_2d() == nullptr)
            {
                context->last_error = error::invalid_operation;
                return;
            }

            CHECK_AND_SET_LAST_ERROR(texture_2d->set_compare_mode(mode));
        }
        else
        {
            context->last_error = error::invalid_operation;
        }
    }
}

texture_compare_mode GetTextureCompareMode(
  std::uint32_t id)
{
    if(impl::bind_texture_pointer(texture_target::texture_2d, id))
    {
        ASSERT_INTERNAL_CONTEXT;
        impl::render_context* context = impl::global_context;
        auto texture_2d = context->states.texture_2d_units[context->states.texture_2d_active_unit];

        if(texture_2d)
        {
            if(texture_2d->as_texture_depth_2d() == nullptr)
            {
                context->last_error = error::invalid_operation;
                return texture_compare_mode::none;
            }

            return texture_2d->get_compare_mode();
        }

        context->last_error = error::invalid_operation;
    }

    return texture_compare_mode::none;
}

void SetTextureCompareFunc(
  std::uint32_t id,
  comparison_func func)
{
    if(impl::bind_texture_pointer(texture_target::texture_2d, id))
    {
        ASSERT_INTERNAL_CONTEXT;
        impl::render_context* context = impl::global_context;
        auto texture_2d = context->states.texture_2d_units[context->states.texture_2d_active_unit];

        if(texture_2d)
        {
            if(texture_2d->as_texture_depth_2d() == nullptr)
            {
                context->last_error = error::invalid_operation;
                return;
            }

            CHECK_AND_SET_LAST_ERROR(texture_2d->set_compare_func(func));
        }
        else
        {
            context->last_error = error::invalid_operation;
        }
    }
}

comparison_func GetTextureCompareFunc(
  std::uint32_t id)
{
    if(impl::bind_texture_pointer(texture_target::texture_2d, id))
    {
        ASSERT_INTERNAL_CONTEXT;
        impl::render_context* context = impl::global_context;
        auto texture_2d = context->states.texture_2d_units[context->states.texture_2d_active_unit];

        if(texture_2d)
        {
            if(texture_2d->as_texture_depth_2d() == nullptr)
            {
                context->last_error = error::invalid_operation;
                return comparison_func::less_equal;
            }

            return texture_2d->get_compare_func();
        }

        context->last_error = error::invalid_operation;
    }

    return comparison_func::less_equal;
}

} /* namespace swr */
