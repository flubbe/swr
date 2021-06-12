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

/** default texture id. */
constexpr int default_tex_id = 0;

/*
 * texture storage.
 */

void texture_storage::allocate(size_t width, size_t height, bool mipmapping)
{
    // check that width and height are a power of two (this is probably not strictly necessary, but the other texture code has that restriction).
    assert((width & (width - 1)) == 0);
    assert((height & (height - 1)) == 0);

    if(!mipmapping)
    {
        // just allocate the base texture. in this case, data_ptrs only holds a single element.
        buffer.resize(width * height);
        data_ptrs.push_back(buffer.data());

        return;
    }

    /*
     * allocate a texture buffer of size 1.5*width*height. Seen as a rectangle, the left width*height part stores the
     * base image. 
     * 
     *  *) the first mipmap level of size (width/2)x(height/2) starts at coordinate (width,0) with pitch 1.5*width.
     *  *) the second mipmap level of size (width/4)x(height/4) starts at coordinate (width,height/2) with pitch 1.5*width
     * 
     * in general, the n-th mipmap level of size (width/2^n)x(height/2^n) starts at coordinate (width,(1-1/2^(n-1))*height) with pitch 1.5*width.
     */
    buffer.resize(width * height + ((width * height) >> 1));

    // base image.
    data_ptrs.push_back(buffer.data());

    // mipmaps.
    auto pitch = width + (width >> 1);
    size_t h_offs = 0;
    for(size_t h = height >> 1; h > 0; h >>= 1)
    {
        data_ptrs.push_back(buffer.data() + h_offs * pitch + width);
        h_offs += h;
    }
}

/*
 * texture objects.
 */

void texture_2d::set_filter_mag(texture_filter filter_mag)
{
    sampler->filter_mag = filter_mag;
}

void texture_2d::set_filter_min(texture_filter filter_min)
{
    sampler->filter_min = filter_min;
}

void texture_2d::initialize_sampler()
{
    if(!sampler)
    {
        sampler = std::make_unique<sampler_2d_impl>(this);
    }
}

void texture_2d::set_wrap_s(wrap_mode s)
{
    if(s == wrap_mode::repeat || s == wrap_mode::mirrored_repeat || s == wrap_mode::clamp_to_edge)
    {
        sampler->set_wrap_s(s);
    }
    else
    {
        impl::global_context->last_error = error::invalid_value;
    }
}

void texture_2d::set_wrap_t(wrap_mode t)
{
    if(t == wrap_mode::repeat || t == wrap_mode::mirrored_repeat || t == wrap_mode::clamp_to_edge)
    {
        sampler->set_wrap_t(t);
    }
    else
    {
        impl::global_context->last_error = error::invalid_value;
    }
}

//!!fixme: set appropriate error codes (note: this function is called before impl::global_context is set).
void texture_2d::set_data(int level, int in_width, int in_height, pixel_format format, wrap_mode wrap_s, wrap_mode wrap_t, const std::vector<uint8_t>& in_data)
{
    constexpr auto component_size = sizeof(uint32_t);

    if(in_width == 0 || in_height == 0 || in_data.size() == 0)
    {
        return;
    }
    assert(in_width * in_height * component_size == in_data.size());

    // check that we have a valid mipmap level. this only checks the lower
    // bound, since we may need to allocate the texture in the first place.
    if(level < 0)
    {
        return;
    }

    auto uLevel = static_cast<size_t>(level);
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
            return;
        }
    }

    // check the upper bound for the mipmap level.
    if(static_cast<std::size_t>(level) >= data.data_ptrs.size())
    {
        return;
    }

    set_wrap_s(wrap_s);
    set_wrap_t(wrap_t);

    auto data_ptr = data.data_ptrs[uLevel];
    auto pitch = width + (width >> 1);

    pixel_format_converter pfc(pixel_format_descriptor::named_format(format));
    for(int y = 0; y < in_height; ++y)
    {
        for(int x = 0; x < in_width; ++x)
        {
            const uint8_t* buf_ptr = &in_data[(y * in_width + x) * component_size];
            uint32_t color = (*buf_ptr) << 24 | (*(buf_ptr + 1)) << 16 | (*(buf_ptr + 2)) << 8 | (*(buf_ptr + 3));
            data_ptr[y * pitch + x] = pfc.to_color(color);
        }
    }
}

void texture_2d::set_sub_data(int level, int in_x, int in_y, int in_width, int in_height, pixel_format format, const std::vector<uint8_t>& in_data)
{
    ASSERT_INTERNAL_CONTEXT;
    constexpr auto component_size = sizeof(uint32_t);

    if(in_width == 0 || in_height == 0 || in_data.size() == 0)
    {
        return;
    }
    assert(static_cast<std::size_t>(in_width * in_height * component_size) == in_data.size());

    if(level < 0 || static_cast<std::size_t>(level) >= data.data_ptrs.size())
    {
        impl::global_context->last_error = error::invalid_value;
        return;
    }

    // ensure the dimensions are set up correctly.
    if(in_x < 0 || in_y < 0 || in_x >= width || in_y >= height)
    {
        impl::global_context->last_error = error::invalid_value;
        return;
    }

    auto data_ptr = data.data_ptrs[level];
    auto pitch = width + (width >> 1);

    uint32_t max_width = std::max(std::min(in_x + in_width, width >> level) - in_x, 0);
    uint32_t max_height = std::max(std::min(in_y + in_height, height >> level) - in_y, 0);

    pixel_format_converter pfc(pixel_format_descriptor::named_format(format));
    for(size_t y = 0; y < max_height; ++y)
    {
        for(size_t x = 0; x < max_width; ++x)
        {
            const uint8_t* buf_ptr = &in_data[(y * in_width + x) * component_size];
            uint32_t color = (*buf_ptr) << 24 | (*(buf_ptr + 1)) << 16 | (*(buf_ptr + 2)) << 8 | (*(buf_ptr + 3));
            data_ptr[(in_y + y) * pitch + (in_x + x)] = pfc.to_color(color);
        }
    }
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
bool bind_texture_pointer(texture_target target, uint32_t id)
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

    const std::vector<uint8_t> DefaultTexture = {
      0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, /* RGBA RGBA */
      0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff  /* RGBA RGBA */
    };

    // the memory allocated here is freed in render_device_context::Shutdown.
    context->texture_2d_storage.push(std::make_unique<texture_2d>(default_tex_id));
    context->default_texture_2d = context->texture_2d_storage[default_tex_id].get();
    assert(context->default_texture_2d->id == default_tex_id);

    context->default_texture_2d->set_data(0, 2, 2, pixel_format::rgba8888, wrap_mode::repeat, wrap_mode::repeat, DefaultTexture);
    context->default_texture_2d->set_filter_mag(texture_filter::nearest);
    context->default_texture_2d->set_filter_min(texture_filter::nearest);
    context->default_texture_2d->initialize_sampler();
}

} /* namespace impl */

/*
 * texture interface.
 */

uint32_t CreateTexture(size_t Width, size_t Height, pixel_format Format, wrap_mode WrapS, wrap_mode WrapT, const std::vector<uint8_t>& Data)
{
    ASSERT_INTERNAL_CONTEXT;

    // roughly verify data.
    if(Width * Height * 4 > Data.size())
    {
        impl::global_context->last_error = error::invalid_value;
        return 0;
    }

    if(Width <= 0 || Height <= 0)
    {
        impl::global_context->last_error = error::invalid_value;
        return 0;
    }

    // check dimensions are power-of-two
    if((Width & (Width - 1)) != 0 || (Height & (Height - 1)) != 0)
    {
        impl::global_context->last_error = error::invalid_value;
        return 0;
    }

    // set up and upload new texture.
    auto slot = impl::global_context->texture_2d_storage.push(std::make_unique<impl::texture_2d>());

    impl::texture_2d* NewTexture = impl::global_context->texture_2d_storage[slot].get();
    NewTexture->set_filter_mag(texture_filter::nearest);
    NewTexture->set_filter_min(texture_filter::nearest);
    NewTexture->set_data(0, Width, Height, Format, WrapS, WrapT, Data);
    NewTexture->id = slot;

    return slot;
}

void ReleaseTexture(uint32_t id)
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

void ActiveTexture(uint32_t unit)
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

void BindTexture(texture_target target, uint32_t id)
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

void SetSubImage(uint32_t texture_id, uint32_t level, size_t offset_x, size_t offset_y, size_t width, size_t height, pixel_format format, const std::vector<uint8_t>& data)
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
    texture_2d->set_sub_data(level, offset_x, offset_y, width, height, format, data);
}

void SetTextureWrapMode(uint32_t id, wrap_mode s, wrap_mode t)
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

void GetTextureWrapMode(uint32_t id, wrap_mode* s, wrap_mode* t)
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
        texture_filter min, mag /* unused */;
        sampler->get_texture_filters(mag, min);
        return min;
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
        texture_filter min /* unused */, mag;
        sampler->get_texture_filters(mag, min);
        return mag;
    }
    else
    {
        context->last_error = error::invalid_operation;
    }
    return texture_filter::nearest;
}

} /* namespace swr */
