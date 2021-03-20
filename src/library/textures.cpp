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
    assert(width & (width - 1) == 0);
    assert(height & (height - 1) == 0);

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
    buffer.resize(width * height + (width * height >> 1));

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
    if(sampler == nullptr)
    {
        sampler = new sampler_2d_impl(this);
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

void texture_2d::set_data(int level, int in_width, int in_height, pixel_format format, wrap_mode wrap_s, wrap_mode wrap_t, const std::vector<uint8_t>& in_data)
{
    ASSERT_INTERNAL_CONTEXT;
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
        impl::global_context->last_error = error::invalid_value;
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
            impl::global_context->last_error = error::invalid_value;
            return;
        }
    }

    // check the upper bound for the mipmap level.
    if(static_cast<std::size_t>(level) >= data.data_ptrs.size())
    {
        impl::global_context->last_error = error::invalid_value;
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
    assert(in_width * in_height * 4 == in_data.size());

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
bool bind_texture_pointer(uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;

    if(id == default_tex_id)
    {
        global_context->RenderStates.tex_2d = global_context->DefaultTexture2d;
        if(!global_context->RenderStates.tex_2d)
        {
            // this can only happen if the context was in an invalid state in the first place.
            global_context->last_error = error::invalid_operation;
            return false;
        }

        return true;
    }

    if(global_context->RenderStates.tex_2d == nullptr || global_context->RenderStates.tex_2d->id != id)
    {
        if(id < global_context->Texture2dHash.size())
        {
            global_context->RenderStates.tex_2d = global_context->Texture2dHash[id];
        }
        else
        {
            global_context->last_error = error::invalid_value;
            return false;
        }

        if(!global_context->RenderStates.tex_2d || global_context->RenderStates.tex_2d->id != id)
        {
            // this can only happen if the context was in an invalid state in the first place.
            global_context->last_error = error::invalid_operation;
            return false;
        }

        return true;
    }

    // here the texture was already set.
    return true;
}

void create_default_texture(render_device_context* context)
{
    assert(context);

    // the default texture is stored in slot 0. check if it is already taken.
    if(context->Texture2dHash.size() != 0)
    {
        context->last_error = error::invalid_operation;
        return;
    }

    const std::vector<uint8_t> DefaultTexture = {
      0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, /* RGBA RGBA */
      0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff  /* RGBA RGBA */
    };

    // the memory allocated here is freed in render_device_context::Shutdown.
    context->DefaultTexture2d = new texture_2d(default_tex_id);

    context->DefaultTexture2d->set_data(0, 2, 2, pixel_format::rgba8888, wrap_mode::repeat, wrap_mode::repeat, DefaultTexture);
    context->DefaultTexture2d->set_filter_mag(context->TextureFilterMag);
    context->DefaultTexture2d->set_filter_min(context->TextureFilterMin);
    context->DefaultTexture2d->initialize_sampler();

    context->Texture2dHash.push(context->DefaultTexture2d);
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
    impl::texture_2d* NewTexture = new impl::texture_2d();
    NewTexture->set_filter_mag(impl::global_context->TextureFilterMag);
    NewTexture->set_filter_min(impl::global_context->TextureFilterMin);

    NewTexture->set_data(0, Width, Height, Format, WrapS, WrapT, Data);

    // the returned slot id is always positive, since there is at least the default
    // texture stored in the hash.
    int Id = impl::global_context->Texture2dHash.push(NewTexture);
    NewTexture->id = Id;
    return Id;
}

void ReleaseTexture(uint32_t TextureId)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;

    if(TextureId < Context->Texture2dHash.size())
    {
        // see if this was the last texture used.
        if(Context->RenderStates.tex_2d && Context->RenderStates.tex_2d->id == Context->Texture2dHash[TextureId]->id)
        {
            // reset to the default texture.
            Context->RenderStates.tex_2d = Context->DefaultTexture2d;
        }

        // free texture memory.
        delete Context->Texture2dHash[TextureId];
        Context->Texture2dHash[TextureId] = nullptr;

        Context->Texture2dHash.free(TextureId);
    }
}

void BindTexture(uint32_t TextureId)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;

    /*
     * bind_texture_pointer only looks up the texture and (if successful) assigns it
     * to impl::global_context->RenderStates.tex_2d. We also need to copy over the
     * texture's parameters for the Get* and Set* functions to work.
     */
    if(impl::bind_texture_pointer(TextureId))
    {
        Context->RenderStates.tex_2d->sampler->get_texture_filters(Context->TextureFilterMag, Context->TextureFilterMin);
        Context->TextureWrapS = Context->RenderStates.tex_2d->sampler->get_wrap_s();
        Context->TextureWrapT = Context->RenderStates.tex_2d->sampler->get_wrap_t();
    }
}

void SetTextureWrapMode(uint32_t id, wrap_mode s, wrap_mode t)
{
    if(impl::bind_texture_pointer(id))
    {
        ASSERT_INTERNAL_CONTEXT;
        impl::render_device_context* context = impl::global_context;

        if((s != wrap_mode::repeat && s != wrap_mode::mirrored_repeat && s != wrap_mode::clamp_to_edge)
           || (t != wrap_mode::repeat && t != wrap_mode::mirrored_repeat && t != wrap_mode::clamp_to_edge))
        {
            context->last_error = error::invalid_value;
            return;
        }

        context->RenderStates.tex_2d->sampler->set_wrap_s(s);
        context->RenderStates.tex_2d->sampler->set_wrap_t(t);
    }
}

void GetTextureWrapMode(uint32_t id, wrap_mode* s, wrap_mode* t)
{
    if(impl::bind_texture_pointer(id))
    {
        ASSERT_INTERNAL_CONTEXT;
        impl::render_device_context* context = impl::global_context;

        if(s)
        {
            *s = context->RenderStates.tex_2d->sampler->get_wrap_s();
        }
        if(t)
        {
            *t = context->RenderStates.tex_2d->sampler->get_wrap_t();
        }
    }
}

void SetTextureMinificationFilter(texture_filter Filter)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;
    if(Context->RenderStates.tex_2d)
    {
        Context->RenderStates.tex_2d->set_filter_min(Filter);
    }
    else
    {
        Context->last_error = error::invalid_operation;
    }
}

void SetTextureMagnificationFilter(texture_filter Filter)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;
    if(Context->RenderStates.tex_2d)
    {
        Context->RenderStates.tex_2d->set_filter_mag(Filter);
    }
    else
    {
        Context->last_error = error::invalid_operation;
    }
}

texture_filter GetTextureMinificationFilter()
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;
    if(Context->RenderStates.tex_2d)
    {
        texture_filter min, mag /* unused */;
        Context->RenderStates.tex_2d->sampler->get_texture_filters(mag, min);
        return min;
    }
    else
    {
        Context->last_error = error::invalid_operation;
    }
    return texture_filter::nearest;
}

texture_filter GetTextureMagnificationFilter()
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;
    if(Context->RenderStates.tex_2d)
    {
        texture_filter min /* unused */, mag;
        Context->RenderStates.tex_2d->sampler->get_texture_filters(mag, min);
        return mag;
    }
    else
    {
        Context->last_error = error::invalid_operation;
    }
    return texture_filter::nearest;
}

sampler_2d* GetSampler2d(uint32_t TextureId)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;

    if(TextureId == impl::default_tex_id)
    {
        return Context->DefaultTexture2d->sampler;
    }

    if(TextureId < Context->Texture2dHash.size())
    {
        impl::texture_2d* Tex = Context->Texture2dHash[TextureId];
        return (Tex ? Tex->sampler : nullptr);
    }

    return nullptr;
}

} /* namespace swr */
