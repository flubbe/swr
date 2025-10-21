/**
 * swr - a software rasterizer
 *
 * texture management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* morton codes */
#ifdef SWR_USE_MORTON_CODES
#    include "libmorton/morton.h"
#endif

/*
 * forward declaration.
 */
namespace rast
{
class sweep_rasterizer;
} /* namespace rast */

namespace swr
{

namespace impl
{

/** default texture id. */
constexpr int default_tex_id = 0;

/*
 * texture storage.
 */

/** Stores the texture data. */
template<typename T>
struct texture_storage
{
    using value_type = T;

    /** buffer holding the base texture and all mipmap levels. */
    std::vector<T> buffer;

    /** mipmap buffer entries. */
    std::vector<T*> data_ptrs;

    /** Allocate the texture data and set um the entries. */
    void allocate(size_t width, size_t height, bool mipmapping = true);

    /** Clear data. */
    void clear()
    {
        buffer.clear();
        data_ptrs.clear();
    }
};

template<typename T>
void texture_storage<T>::allocate(size_t width, size_t height, bool mipmapping)
{
    // check that width and height are a power of two (this is probably not strictly necessary, but the other texture code has that restriction).
    assert(utils::is_power_of_two(width));
    assert(utils::is_power_of_two(height));

#ifdef SWR_USE_MORTON_CODES
    assert(width == height);
#endif

    if(!mipmapping)
    {
        // just allocate the base texture. in this case, data_ptrs only holds a single element.
        data_ptrs.push_back(utils::align_vector(utils::alignment::sse, width * height, buffer));

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

    // base image.
    data_ptrs.push_back(utils::align_vector(utils::alignment::sse, width * height + ((width * height) >> 1), buffer));
    auto base_ptr = data_ptrs[0];

    // mipmaps.
#ifndef SWR_USE_MORTON_CODES
    auto pitch = width + (width >> 1);
    size_t h_offs = 0;
    for(size_t h = height >> 1; h > 0; h >>= 1)
    {
        data_ptrs.push_back(base_ptr + h_offs * pitch + width);
        h_offs += h;
    }
#else
    size_t dims = width;    // this is the same as the height.
    size_t offs = dims * dims;
    while(offs > 0)
    {
        data_ptrs.push_back(base_ptr + offs);
        offs >>= 2;
    }
#endif
}

/*
 * texture sampling.
 */

/** texture coordinate wrap function. assumes max to be a power of two. */
inline int wrap(wrap_mode m, int coord, int max)
{
    if(m == wrap_mode::repeat)
    {
        return coord & (max - 1);
    }
    else if(m == wrap_mode::mirrored_repeat)
    {
        auto t = coord & (max - 1);
        return (coord & max) ? (max - 1) - t : t;
    }
    else if(m == wrap_mode::clamp_to_edge)
    {
        coord = (coord >= max) ? max - 1 : coord;
        coord = (coord < 0) ? 0 : coord;
        return coord;
    }

    /* unknown wrap mode. return safe value. */
    return 0;
}

/** texture sampler implementation. */
class sampler_2d_impl : public sampler_2d
{
    friend class rast::sweep_rasterizer;

    /** associated texture. */
    texture_2d* associated_texture{nullptr};

    /** texture minification and magnification filters. */
    texture_filter filter_min{texture_filter::nearest}, filter_mag{texture_filter::nearest};

    /** edge value sampling. */
    wrap_mode wrap_s{wrap_mode::repeat}, wrap_t{wrap_mode::repeat};

    /**
     * given dFdx and dFdy, calculate the corresponding mipmap level,
     * see section 8.14 on p. 216 of https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     *
     * the function returns a float in the range [0, max_mipmap_level], where
     * max_mipmap_level is associated_texture->data.data_ptrs.size().
     *
     * note: the function assumes that the texture is square (see e.g. eq. (8.7) on p. 217 in the reference) and does not have borders.
     * also, we ignore any biases.
     */
    float calculate_mipmap_level(const ml::vec4& dFdx, const ml::vec4& dFdy) const;

    /*
     * sampling functions:
     *
     *   param setting              linear within mip level     has mipmapping
     *   ---------------------------------------------------------------------
     *         nearest                                  no                  no
     *          linear                                 yes                  no
     *
     *   reference: https://www.khronos.org/opengl/wiki/Sampler_Object
     *
     * sampling logic:
     *
     *   the sample functions access the texture data. the resulting data then is interpolated according to the method implemented by the function.
     *
     */

    /** get mipmap parameters for the specified mipmap level. if no mipmaps exists, returns parameters for the base image and sets level to zero. */
#ifdef SWR_USE_MORTON_CODES
    void get_mipmap_params(int& level, int& w, int& h) const;
#else
    void get_mipmap_params(int& level, int& w, int& h, int& pitch) const;
#endif

    /** nearest-neighbor sampling. */
    ml::vec4 sample_at_nearest(int mipmap_level, const swr::varying& uv) const;

    /** linear sampling. */
    ml::vec4 sample_at_linear(int mipmap_level, const swr::varying& uv) const;

public:
    /** constructor. */
    sampler_2d_impl(struct texture_2d* tex)
    : associated_texture(tex)
    {
    }

    /** set texture minification filter. */
    void set_filter_min(texture_filter min)
    {
        filter_min = min;
    }

    /** set texture magnification filter. */
    void set_filter_mag(texture_filter mag)
    {
        filter_mag = mag;
    }

    /** get texture minification filter. */
    texture_filter get_filter_min() const
    {
        return filter_min;
    }

    /** get texture magnification filter. */
    texture_filter get_filter_mag() const
    {
        return filter_mag;
    }

    /** set wrapping modes. */
    void set_wrap_s(wrap_mode s)
    {
        wrap_s = s;
    }

    /** set wrapping modes. */
    void set_wrap_t(wrap_mode t)
    {
        wrap_t = t;
    }

    /** get wrapping modes. */
    wrap_mode get_wrap_s() const
    {
        return wrap_s;
    }

    /** get wrapping modes. */
    wrap_mode get_wrap_t() const
    {
        return wrap_t;
    }

    /** sampling function. */
    ml::vec4 sample_at(const swr::varying& uv) const override
    {
        // calculate the mipmap level. this also tells if we need to apply the magnification or minification filter.
        // note that lambda>=0.
        float lambda = calculate_mipmap_level(uv.dFdx, uv.dFdy);
        int mipmap_level = static_cast<int>(std::floor(lambda));

        /* sample the texture according to the selected filter. */
        if(mipmap_level == 0)
        {
            // use the magnification filter.

            if(filter_mag == texture_filter::nearest)
            {
                return sample_at_nearest(0, uv);
            }
            else if(filter_mag == texture_filter::linear)
            {
                return sample_at_linear(0, uv);
            }
        }
        else
        {
            // here we know that mipmap_level>0, so we use the minification filter.

            if(filter_min == texture_filter::nearest)
            {
                // this filter does not use mipmaps.
                return sample_at_nearest(0, uv);
            }
            else if(filter_min == texture_filter::linear)
            {
                // this filter does not use mipmaps.
                return sample_at_linear(0, uv);
            }
        }

        // unknown filter.
        return ml::vec4::zero();
    }
};

/*
 * texture object.
 */

/** 2-dimensional texture with associated sampler. */
struct texture_2d
{
    /** texture id. */
    uint32_t id{0};

    /** texture width and height. */
    int width{0}, height{0};

    /** texture data. */
    texture_storage<ml::vec4> data;

    /** texture sampler. */
    std::unique_ptr<sampler_2d_impl> sampler;

    /** constructor. */
    texture_2d()
    {
        initialize_sampler();
    }

    /** constructor. */
    texture_2d(uint32_t in_id,
               int in_width = 0, int in_height = 0,
               wrap_mode wrap_s = wrap_mode::repeat, wrap_mode wrap_t = wrap_mode::repeat,
               texture_filter filter_mag = texture_filter::nearest, texture_filter filter_min = texture_filter::nearest);

    /** Set magnification texture filter. */
    void set_filter_mag(texture_filter filter_mag);

    /** Set minification texture filter. */
    void set_filter_min(texture_filter filter_min);

    /** Initialize the texture sampler. */
    void initialize_sampler();

    /** Set texture wrapping mode in s-direction. */
    swr::error set_wrap_s(wrap_mode s);

    /** Set texture wrapping mode in t-direction. */
    swr::error set_wrap_t(wrap_mode t);

    /** allocate texture data initialized to zero. */
    swr::error allocate(int level, int width, int height);

    /**
     * Set the texture data using the specified pixel format. the base texture level needs to be set up first through this call, since
     * it allocates the storage. the uploaded image needs to have a 4-component format, with 8 bits per component.
     */
    swr::error set_data(int level, int width, int height, pixel_format format, const std::vector<uint8_t>& data);

    /**
     * Set the sub-texture data using the specified pixel format. only valid to call after set_data has set the texture storage up.
     * the uploaded image needs to have a 4-component format, with 8 bits per component.
     */
    swr::error set_sub_data(int level, int x, int y, int width, int height, pixel_format format, const std::vector<uint8_t>& data);

    /** clear all texture data. */
    void clear();
};

/*
 * texture_2d constructor.
 */

inline texture_2d::texture_2d(
  uint32_t in_id,
  int in_width, int in_height,
  wrap_mode wrap_s, wrap_mode wrap_t,
  texture_filter filter_mag, texture_filter filter_min)
: id(in_id)
, width(in_width)
, height(in_height)
{
    initialize_sampler();

    sampler->set_filter_mag(filter_mag);
    sampler->set_filter_min(filter_min);

    sampler->set_wrap_s(wrap_s);
    sampler->set_wrap_t(wrap_t);
}

/*
 * sampler_2d_impl implementation.
 */

inline float sampler_2d_impl::calculate_mipmap_level(const ml::vec4& dFdx, const ml::vec4& dFdy) const
{
    /*
     * check if there are mipmaps available. if not, we don't need to calculate anything.
     */
    auto mipmap_levels = associated_texture->data.data_ptrs.size();
    if(mipmap_levels <= 1)
    {
        return 0;
    }
    float lod_max = static_cast<float>(mipmap_levels - 1);

    /*
     * first define the squares of the functions u and v from eq. (8.7) on p. 217.
     * note that we currently only support square textures without any borders.
     */
    float u_squared = associated_texture->width * associated_texture->width * dFdx.length_squared();
    float v_squared = associated_texture->height * associated_texture->height * dFdy.length_squared();

    /*
     * next, we combine eqs. (8.8) on p. 218 and (8.4) on p. 216. the factor 0.5f accounts for the square root.
     * we ignore any biases for now.
     */
    float lambda = 0.5f * std::log2(std::max(u_squared, v_squared));

    // clamp the level-of-detail parameter, roughly corresponding to eq. (8.6) on p. 216.
    return boost::algorithm::clamp(lambda, 0.f, lod_max);
}

#ifdef SWR_USE_MORTON_CODES
inline void sampler_2d_impl::get_mipmap_params(int& level, int& w, int& h) const
{
    if(associated_texture->data.data_ptrs.size() == 1)
    {
        // no mipmapping available.
        level = 0;
    }

    w = associated_texture->width >> level;
    h = associated_texture->height >> level;
}
#else
inline void sampler_2d_impl::get_mipmap_params(int& level, int& w, int& h, int& pitch) const
{
    if(associated_texture->data.data_ptrs.size() == 1)
    {
        // no mipmapping available.
        level = 0;
        pitch = associated_texture->width;
    }
    else
    {
        pitch = associated_texture->width + (associated_texture->width >> 1);
    }

    w = associated_texture->width >> level;
    h = associated_texture->height >> level;
}
#endif

inline ml::vec4 sampler_2d_impl::sample_at_nearest(int mipmap_level, const swr::varying& uv) const
{
#ifdef SWR_USE_MORTON_CODES
    int w{0}, h{0};

    get_mipmap_params(mipmap_level, w, h);
    ml::tvec2<int> texel_coords = {ml::truncate_unchecked(uv.value.x * w), ml::truncate_unchecked(uv.value.y * h)};
    texel_coords = {wrap(wrap_s, texel_coords.x, w), wrap(wrap_t, texel_coords.y, h)};

    return (associated_texture->data.data_ptrs[mipmap_level])[libmorton::morton2D_32_encode(texel_coords.x, texel_coords.y)];
#else
    int w{0}, h{0}, pitch{0};

    get_mipmap_params(mipmap_level, w, h, pitch);
    ml::tvec2<int> texel_coords = {ml::truncate_unchecked(uv.value.x * w), ml::truncate_unchecked(uv.value.y * h)};
    texel_coords = {wrap(wrap_s, texel_coords.x, w), wrap(wrap_t, texel_coords.y, h)};

    return (associated_texture->data.data_ptrs[mipmap_level])[texel_coords.y * pitch + texel_coords.x];
#endif
}

inline ml::vec4 sampler_2d_impl::sample_at_linear(int mipmap_level, const swr::varying& uv) const
{
#ifdef SWR_USE_MORTON_CODES
    int w{0}, h{0};

    get_mipmap_params(mipmap_level, w, h);

    // calculate nearest texel and interpolation parameters.
    ml::vec2 texel_coords = {uv.value.x * w - 0.5f, uv.value.y * h - 0.5f};
    ml::tvec2<int> texel_coords_dec = {static_cast<int>(std::floor(texel_coords.x)), static_cast<int>(std::floor(texel_coords.y))};
    ml::vec2 texel_coords_frac = {texel_coords.x - texel_coords_dec.x, texel_coords.y - texel_coords_dec.y};

    // calculate nearest four texel coordinates while respect the texture wrap mode.
    ml::tvec2<int> texel_coords_dec_wrap[4] = {
      {wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y, h)},
      {wrap(wrap_s, texel_coords_dec.x + 1, w), wrap(wrap_t, texel_coords_dec.y, h)},
      {wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y + 1, h)},
      {wrap(wrap_s, texel_coords_dec.x + 1, w),
       wrap(wrap_t, texel_coords_dec.y + 1, h)}};

    // get color values.
    ml::vec4 texels[4] = {
      (associated_texture->data.data_ptrs[mipmap_level])[libmorton::morton2D_32_encode(texel_coords_dec_wrap[0].x, texel_coords_dec_wrap[0].y)],
      (associated_texture->data.data_ptrs[mipmap_level])[libmorton::morton2D_32_encode(texel_coords_dec_wrap[1].x, texel_coords_dec_wrap[1].y)],
      (associated_texture->data.data_ptrs[mipmap_level])[libmorton::morton2D_32_encode(texel_coords_dec_wrap[2].x, texel_coords_dec_wrap[2].y)],
      (associated_texture->data.data_ptrs[mipmap_level])[libmorton::morton2D_32_encode(texel_coords_dec_wrap[3].x, texel_coords_dec_wrap[3].y)],
    };

    // return linearly interpolated color.
    return ml::lerp(texel_coords_frac.y, ml::lerp(texel_coords_frac.x, texels[0], texels[1]), ml::lerp(texel_coords_frac.x, texels[2], texels[3]));
#else
    int w{0}, h{0}, pitch{0};

    get_mipmap_params(mipmap_level, w, h, pitch);

    // calculate nearest texel and interpolation parameters.
    ml::vec2 texel_coords = {uv.value.x * w - 0.5f, uv.value.y * h - 0.5f};
    ml::tvec2<int> texel_coords_dec = {static_cast<int>(std::floor(texel_coords.x)), static_cast<int>(std::floor(texel_coords.y))};
    ml::vec2 texel_coords_frac = {texel_coords.x - texel_coords_dec.x, texel_coords.y - texel_coords_dec.y};

    // calculate nearest four texel coordinates while respect the texture wrap mode.
    ml::tvec2<int> texel_coords_dec_wrap[4] = {
      {wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y, h)},
      {wrap(wrap_s, texel_coords_dec.x + 1, w), wrap(wrap_t, texel_coords_dec.y, h)},
      {wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y + 1, h)},
      {wrap(wrap_s, texel_coords_dec.x + 1, w),
       wrap(wrap_t, texel_coords_dec.y + 1, h)}};

    // get color values.
    ml::vec4 texels[4] = {
      (associated_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[0].y * pitch + texel_coords_dec_wrap[0].x],
      (associated_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[1].y * pitch + texel_coords_dec_wrap[1].x],
      (associated_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[2].y * pitch + texel_coords_dec_wrap[2].x],
      (associated_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[3].y * pitch + texel_coords_dec_wrap[3].x],
    };

    // return linearly interpolated color.
    return ml::lerp(texel_coords_frac.y, ml::lerp(texel_coords_frac.x, texels[0], texels[1]), ml::lerp(texel_coords_frac.x, texels[2], texels[3]));
#endif
}

} /* namespace impl */

} /* namespace swr */
