/**
 * swr - a software rasterizer
 * 
 * texture management.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace swr
{

namespace impl
{

/*
 * texture storage.
 */

/** Stores the texture data as 32-bit floating-points per channel. */
struct texture_storage
{
    /** buffer holding the base texture and all mipmap levels. */
    std::vector<ml::vec4> buffer;

    /** mipmap buffer entries. */
    std::vector<ml::vec4*> data_ptrs;

    /** Allocate the texture data and set um the entries. */
    void allocate(size_t width, size_t height, bool mipmapping = true);

    /** Clear data. */
    void clear()
    {
        buffer.clear();
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
    texture_storage data;

    /** texture sampler. */
    std::unique_ptr<class sampler_2d_impl> sampler;

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
    void set_wrap_s(wrap_mode s);

    /** Set texture wrapping mode in t-direction. */
    void set_wrap_t(wrap_mode t);

    /** 
     * Set the texture data using the specified pixel format. the base texture level needs to be set up first through this call, since
     * it allocates the storage. the uploaded image needs to have a 4-component format, with 8 bits per component.
     */
    void set_data(int level, int width, int height, pixel_format format, wrap_mode wrap_s, wrap_mode wrap_t, const std::vector<uint8_t>& data);

    /** 
     * Set the sub-texture data using the specified pixel format. only valid to call after set_data has set the texture storage up. 
     * the uploaded image needs to have a 4-component format, with 8 bits per component.
     */
    void set_sub_data(int level, int x, int y, int width, int height, pixel_format format, const std::vector<uint8_t>& data);

    /** clear all texture data. */
    void clear();
};

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
    friend struct texture_2d;

    /** associated texture. */
    struct texture_2d* associated_texture{nullptr};

    /** texture minification and magnification filters. */
    texture_filter filter_min{texture_filter::nearest}, filter_mag{texture_filter::nearest};

    /** edge value sampling. */
    wrap_mode wrap_s{wrap_mode::repeat}, wrap_t{wrap_mode::repeat};

    /*
     * mipmap parameters. these have to be calculated in the rasterization stage,
     * since the sampler has no knowledge of the texel-to-pixel mapping.
     */

    /** calculated mipmap level. */
    float mipmap_level_parameter{0.0f};

    /** actual mipmap level. */
    int mipmap_level{0};

    /** mipmap parameter. */
    float mipmap_delta_max_squared{0.0f};

    /*
     * dither info.
     */
    ml::vec2 dither_offset;

    /*
     * sampling functions:
     * 
     *   param setting              linear within mip level     has mipmapping 
     *   ---------------------------------------------------------------------
     *         nearest                                  no                  no
     *        dithered                                (yes)                 no
     * 
     *   reference: https://www.khronos.org/opengl/wiki/Sampler_Object
     * 
     * sampling logic:
     * 
     *   the sample functions access the texture data. the resulting data then is interpolated according to the method implemented by the function.
     * 
     */

    /** get mipmap parameters for the specified mipmap level. if no mipmaps exists, returns parameters for the base image and sets level to zero. */
    void get_mipmap_params(int& level, int& w, int& h, int& pitch) const
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

        w = associated_texture->width >> mipmap_level;
        h = associated_texture->height >> mipmap_level;
    }

    /** nearest-neighbor sampling. */
    ml::vec4 sample_at_nearest(const ml::vec2 uv) const
    {
        int mipmap_level{0};    // only consider mipmap level 0.
        int w{0}, h{0}, pitch{0};

        get_mipmap_params(mipmap_level, w, h, pitch);
        ml::tvec2<int> texel_coords = {ml::truncate_unchecked(uv.u * w), ml::truncate_unchecked(uv.v * h)};
        texel_coords = {wrap(wrap_s, texel_coords.x, w), wrap(wrap_t, texel_coords.y, h)};

        return (associated_texture->data.data_ptrs[mipmap_level])[texel_coords.y * pitch + texel_coords.x];
    }

    /** dithered sampling. */
    ml::vec4 sample_at_dithered(const ml::vec2 uv) const
    {
        int mipmap_level{0};    // only consider mipmap level 0.
        int w{0}, h{0}, pitch{0};

        get_mipmap_params(mipmap_level, w, h, pitch);
        ml::vec2 dithered_texel_coords = {static_cast<float>(ml::truncate_unchecked(uv.u * w)), static_cast<float>(ml::truncate_unchecked(uv.v * h))};
        dithered_texel_coords += dither_offset;
        ml::tvec2<int> texel_coords = {ml::truncate_unchecked(dithered_texel_coords.x), ml::truncate_unchecked(dithered_texel_coords.y)};
        texel_coords = {wrap(wrap_s, texel_coords.x, w), wrap(wrap_t, texel_coords.y, h)};

        return (associated_texture->data.data_ptrs[mipmap_level])[texel_coords.y * pitch + texel_coords.x];
    }

public:
    /** constructor. */
    sampler_2d_impl(texture_2d* tex)
    : associated_texture(tex)
    {
    }

    /** update mipmap information. */
    void update_mipmap_info(float level_param, int level, float delta_max_sqr)
    {
        mipmap_level_parameter = level_param;
        mipmap_level = level;
        mipmap_delta_max_squared = delta_max_sqr;
    }

    /** update dither reference value. */
    void update_dither(int x, int y)
    {
        int index_x = x & 1;
        int index_y = y & 1;

        // dither kernel.
        const float kernel[8] = {
          0.00f, -0.25f, 0.25f, 0.50f,
          0.50f, 0.25f, -0.25f, 0.00f};

        int i = (index_x << 2) | (index_y << 1);
        dither_offset = {kernel[i], kernel[i + 1]};
    }

    /** set texture magnification and minification filters. */
    void set_texture_filters(texture_filter mag, texture_filter min)
    {
        filter_mag = mag;
        filter_min = min;
    }

    /** get texture magnification and minification filters. */
    void get_texture_filters(texture_filter& mag, texture_filter& min) const
    {
        mag = filter_mag;
        min = filter_min;
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
    ml::vec4 sample_at(const ml::vec2 uv) const override
    {
        // check which filter we are using.
        bool force_minification_filter{false};

        /*
         * When applying dithering as a magnification filter, it does not look very good
         * at the point where the texel-to-pixel-ratio is approximately one. We handle
         * this case separately by changing the filter to a nearest-neighbor one.
         */
        if(filter_mag == texture_filter::dithered && mipmap_level == 0)
        {
            //!!fixme: the 0.5f is somewhat arbitrary and may need adjustment.
            force_minification_filter = (mipmap_level_parameter > 0.5f);
        }

        if(mipmap_level > 0 || force_minification_filter)
        {
            if(filter_min == texture_filter::nearest)
            {
                return sample_at_nearest(uv);
            }
            else if(filter_min == texture_filter::dithered)
            {
                return sample_at_dithered(uv);
            }
        }
        else
        {
            if(filter_mag == texture_filter::nearest)
            {
                return sample_at_nearest(uv);
            }
            else if(filter_mag == texture_filter::dithered)
            {
                return sample_at_dithered(uv);
            }
        }

        // unknown filter.
        return ml::vec4::zero();
    }
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

    sampler->set_texture_filters(filter_mag, filter_min);
    sampler->set_wrap_s(wrap_s);
    sampler->set_wrap_t(wrap_t);
}

} /* namespace impl */

} /* namespace swr */
