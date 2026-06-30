/**
 * swr - a software rasterizer
 *
 * texture management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <limits>

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

struct texture_color_2d;
struct texture_depth_2d;

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
    utils::sse_aligned_vector<T> buffer;

    /** mipmap buffer entries. */
    std::vector<T*> data_ptrs;

    /** row pitch per mip level. */
    std::vector<std::size_t> pitches;

    /** Allocate the texture data and set um the entries. */
    void allocate(
      std::size_t width,
      std::size_t height,
      bool mipmapping = true);

    /** Clear data. */
    void clear()
    {
        buffer.clear();
        data_ptrs.clear();
        pitches.clear();
    }
};

template<typename T>
void texture_storage<T>::allocate(
  std::size_t width,
  std::size_t height,
  bool mipmapping)
{
    // check that width and height are a power of two (this is probably not strictly necessary, but the other texture code has that restriction).
    assert(utils::is_power_of_two(width));
    assert(utils::is_power_of_two(height));

#ifdef SWR_USE_MORTON_CODES
    assert(width == height);
#endif
    data_ptrs.clear();
    pitches.clear();

    if(!mipmapping)
    {
        // just allocate the base texture. in this case, data_ptrs only holds a single element.
        buffer.resize(width * height);
        data_ptrs.emplace_back(buffer.data());
        pitches.emplace_back(width);

        return;
    }

    std::size_t total_texels = 0;
    for(std::size_t level_width = width, level_height = height;
        level_width > 0 && level_height > 0;
        level_width >>= 1, level_height >>= 1)
    {
        total_texels += level_width * level_height;
    }

    buffer.resize(total_texels);

    std::size_t offset = 0;
    for(std::size_t level_width = width, level_height = height;
        level_width > 0 && level_height > 0;
        level_width >>= 1, level_height >>= 1)
    {
        data_ptrs.emplace_back(buffer.data() + offset);
        pitches.emplace_back(level_width);
        offset += level_width * level_height;
    }
}

/*
 * texture sampling.
 */

/** texture coordinate wrap function. assumes max to be a power of two. */
inline int wrap(
  wrap_mode m,
  int coord,
  int max)
{
    if(m == wrap_mode::repeat)
    {
        return coord & (max - 1);
    }

    if(m == wrap_mode::mirrored_repeat)
    {
        auto t = coord & (max - 1);
        return (coord & max) ? (max - 1) - t : t;
    }

    if(m == wrap_mode::clamp_to_edge)
    {
        coord = (coord >= max) ? max - 1 : coord;
        coord = (coord < 0) ? 0 : coord;
        return coord;
    }

    /* unknown wrap mode. return safe value. */
    return 0;
}

/** texture sampler implementation. */
class sampler_2d_impl final
: public sampler_2d
, public sampler_depth_2d
, public sampler_shadow_2d
{
    /** associated texture. */
    texture_2d* associated_texture{nullptr};

    /** texture minification filter. */
    texture_filter filter_min{texture_filter::nearest};

    /** texture magnification filter. */
    texture_filter filter_mag{texture_filter::nearest};

    /** edge value sampling. */
    wrap_mode wrap_s{wrap_mode::repeat};

    /** edge value sampling. */
    wrap_mode wrap_t{wrap_mode::repeat};

    /**
     * given dFdx and dFdy, calculate the corresponding mipmap level,
     * see section 8.14 on p. 216 of https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     *
     * the function returns a float in the range [0, max_mipmap_level], where
     * max_mipmap_level is associated_texture->mip_level_count().
     *
     * note: the function assumes that the texture is square (see e.g. eq. (8.7) on p. 217 in the reference) and does not have borders.
     * also, we ignore any biases.
     */
    float calculate_mipmap_level(
      const ml::vec4& dFdx,
      const ml::vec4& dFdy) const;

    /*
     * sampling functions:
     *
     *              param setting    linear within mip level     has mipmapping    linear between mip levels
     *   ---------------------------------------------------------------------------------------------------
     *                    nearest                         no                 no                            -
     *                     linear                        yes                 no                            -
     *     nearest_mipmap_nearest                         no                yes                           no
     *      linear_mipmap_nearest                        yes                yes                           no
     *
     *   reference: https://www.khronos.org/opengl/wiki/Sampler_Object
     *
     * sampling logic:
     *
     *   the sample functions access the texture data. the resulting data then is interpolated according to the method implemented by the function.
     *
     */

    /** get mipmap parameters for the specified mipmap level. if no mipmaps exists, returns parameters for the base image and sets level to zero. */
    void get_mipmap_params(int& level, int& w, int& h) const;
#ifndef SWR_USE_MORTON_CODES
    void get_mipmap_params(int& level, int& w, int& h, int& pitch) const;
#endif

    /** nearest-neighbor sampling. */
    ml::vec4 sample_at_nearest(int mipmap_level, const swr::varying& uv) const;

    /** linear sampling. */
    ml::vec4 sample_at_linear(int mipmap_level, const swr::varying& uv) const;

    /** nearest-neighbor depth sampling. */
    float sample_depth_at_nearest(int mipmap_level, const swr::varying& uv) const;

    /** linear depth sampling. */
    float sample_depth_at_linear(int mipmap_level, const swr::varying& uv) const;

    /** nearest-neighbor comparison sampling. */
    float sample_compare_at_nearest(int mipmap_level, const swr::varying& uv) const;

    /** linear comparison sampling. */
    float sample_compare_at_linear(int mipmap_level, const swr::varying& uv) const;

    /** apply the configured depth comparison operation. */
    float compare_depth_values(float reference, float sampled_depth) const;

public:
    /** constructor. */
    explicit sampler_2d_impl(struct texture_2d* tex) noexcept
    : associated_texture{tex}
    {
    }

    /** retarget the sampler to a different texture object. */
    void set_associated_texture(struct texture_2d* tex) noexcept
    {
        associated_texture = tex;
    }

    /** set texture minification filter. */
    void set_filter_min(texture_filter min) noexcept
    {
        filter_min = min;
    }

    /** set texture magnification filter. */
    void set_filter_mag(texture_filter mag) noexcept
    {
        filter_mag = mag;
    }

    /** get texture minification filter. */
    texture_filter get_filter_min() const noexcept
    {
        return filter_min;
    }

    /** get texture magnification filter. */
    texture_filter get_filter_mag() const noexcept
    {
        return filter_mag;
    }

    /** set wrapping modes. */
    void set_wrap_s(wrap_mode s) noexcept
    {
        wrap_s = s;
    }

    /** set wrapping modes. */
    void set_wrap_t(wrap_mode t) noexcept
    {
        wrap_t = t;
    }

    /** get wrapping modes. */
    wrap_mode get_wrap_s() const noexcept
    {
        return wrap_s;
    }

    /** get wrapping modes. */
    wrap_mode get_wrap_t() const noexcept
    {
        return wrap_t;
    }

    ml::vec4 sample_at(const swr::varying& uv) const override;
    float sample_depth_at(const swr::varying& uv) const override;
    float sample_compare_at(const swr::varying& uv) const override;

    texture_compare_mode compare_mode() const override;
    comparison_func compare_func() const override;

    ml::tvec2<int> size(std::uint32_t mipmap_level) const override;
};

/*
 * texture object.
 */

/** 2-dimensional texture base type with associated samplers. */
struct texture_2d
{
    /** texture id. */
    std::uint32_t id{0};

    /** texture width and height. */
    std::uint32_t width{0}, height{0};

    /** source format of the texture. */
    pixel_format format{pixel_format::unsupported};

    /** texture sampler. */
    std::unique_ptr<sampler_2d_impl> sampler;

    /** constructor. */
    texture_2d()
    {
        initialize_sampler();
    }

    /** virtual destructor. */
    virtual ~texture_2d() = default;

    /** constructor. */
    texture_2d(
      std::uint32_t in_id,
      std::uint32_t in_width = 0,
      std::uint32_t in_height = 0,
      wrap_mode wrap_s = wrap_mode::repeat,
      wrap_mode wrap_t = wrap_mode::repeat,
      texture_filter filter_mag = texture_filter::nearest,
      texture_filter filter_min = texture_filter::nearest);

    /** Set magnification texture filter. */
    void set_filter_mag(texture_filter filter_mag);

    /** Set minification texture filter. */
    void set_filter_min(texture_filter filter_min);

    /** Return magnification texture filter. */
    texture_filter get_filter_mag() const
    {
        return sampler->get_filter_mag();
    }

    /** Return minification texture filter. */
    texture_filter get_filter_min() const
    {
        return sampler->get_filter_min();
    }

    /** Initialize the texture sampler. */
    void initialize_sampler();

    /** Set texture wrapping mode in s-direction. */
    swr::error set_wrap_s(wrap_mode s);

    /** Set texture wrapping mode in t-direction. */
    swr::error set_wrap_t(wrap_mode t);

    /** Return texture wrapping mode in s-direction. */
    wrap_mode get_wrap_s() const
    {
        return sampler->get_wrap_s();
    }

    /** Return texture wrapping mode in t-direction. */
    wrap_mode get_wrap_t() const
    {
        return sampler->get_wrap_t();
    }

    /** allocate texture data initialized to zero. */
    virtual swr::error allocate(
      std::uint32_t level,
      std::uint32_t width,
      std::uint32_t height,
      pixel_format format) = 0;

    /**
     * Set the texture data using the specified pixel format. the base texture level needs to be set up first through this call, since
     * it allocates the storage. the uploaded image needs to have a 4-component format, with 8 bits per component.
     */
    virtual swr::error set_data(
      std::uint32_t level,
      std::uint32_t width,
      std::uint32_t height,
      pixel_format format,
      const std::vector<std::uint8_t>& data) = 0;

    /**
     * Set the sub-texture data using the specified pixel format. only valid to call after set_data has set the texture storage up.
     * the uploaded image needs to have a 4-component format, with 8 bits per component.
     */
    virtual swr::error set_sub_data(
      std::uint32_t level,
      std::uint32_t in_x,
      std::uint32_t in_y,
      std::uint32_t in_width,
      std::uint32_t in_height,
      pixel_format format,
      const std::vector<std::uint8_t>& data) = 0;

    /** clear all texture data. */
    virtual void clear();

    /** Return this texture as a color texture, if supported. */
    virtual texture_color_2d* as_texture_color_2d()
    {
        return nullptr;
    }

    /** Return this texture as a color texture, if supported. */
    virtual const texture_color_2d* as_texture_color_2d() const
    {
        return nullptr;
    }

    /** Return this texture as a depth texture, if supported. */
    virtual texture_depth_2d* as_texture_depth_2d()
    {
        return nullptr;
    }

    /** Return this texture as a depth texture, if supported. */
    virtual const texture_depth_2d* as_texture_depth_2d() const
    {
        return nullptr;
    }

    /** Set the depth comparison mode, if supported. */
    virtual swr::error set_compare_mode(texture_compare_mode)
    {
        return error::invalid_operation;
    }

    /** Return the depth comparison mode. */
    virtual texture_compare_mode get_compare_mode() const
    {
        return texture_compare_mode::none;
    }

    /** Set the depth comparison function, if supported. */
    virtual swr::error set_compare_func(comparison_func)
    {
        return error::invalid_operation;
    }

    /** Return the depth comparison function. */
    virtual comparison_func get_compare_func() const
    {
        return comparison_func::less_equal;
    }

    /** return the currently active mip level count. */
    virtual std::size_t mip_level_count() const = 0;

    /** return the row pitch of the selected mip level. */
    virtual std::size_t mip_pitch(std::uint32_t level) const = 0;
};

/** 2-dimensional color texture with color storage only. */
struct texture_color_2d final : public texture_2d
{
    /** color texture data. */
    texture_storage<ml::vec4> data;

    texture_color_2d() = default;
    texture_color_2d(
      std::uint32_t in_id,
      std::uint32_t in_width = 0,
      std::uint32_t in_height = 0,
      wrap_mode wrap_s = wrap_mode::repeat,
      wrap_mode wrap_t = wrap_mode::repeat,
      texture_filter filter_mag = texture_filter::nearest,
      texture_filter filter_min = texture_filter::nearest);

    swr::error allocate(
      std::uint32_t level,
      std::uint32_t width,
      std::uint32_t height,
      pixel_format format) override;
    swr::error set_data(
      std::uint32_t level,
      std::uint32_t width,
      std::uint32_t height,
      pixel_format format,
      const std::vector<std::uint8_t>& data) override;
    swr::error set_sub_data(
      std::uint32_t level,
      std::uint32_t in_x,
      std::uint32_t in_y,
      std::uint32_t in_width,
      std::uint32_t in_height,
      pixel_format format,
      const std::vector<std::uint8_t>& data) override;
    void clear() override;

    texture_color_2d* as_texture_color_2d() override
    {
        return this;
    }
    const texture_color_2d* as_texture_color_2d() const override
    {
        return this;
    }

    std::size_t mip_level_count() const override
    {
        return data.data_ptrs.size();
    }

    std::size_t mip_pitch(std::uint32_t level) const override
    {
        return data.pitches[level];
    }
};

/** 2-dimensional depth texture with depth storage only. */
struct texture_depth_2d final : public texture_2d
{
    /** depth texture data. */
    texture_storage<ml::fixed_32_t> data;

    /** comparison function used for shadow lookups. */
    comparison_func compare_func{comparison_func::less_equal};

    /** comparison mode used for depth textures. */
    texture_compare_mode compare_mode{texture_compare_mode::none};

    texture_depth_2d() = default;
    texture_depth_2d(
      std::uint32_t in_id,
      std::uint32_t in_width = 0,
      std::uint32_t in_height = 0,
      wrap_mode wrap_s = wrap_mode::repeat,
      wrap_mode wrap_t = wrap_mode::repeat,
      texture_filter filter_mag = texture_filter::nearest,
      texture_filter filter_min = texture_filter::nearest);

    swr::error allocate(
      std::uint32_t level,
      std::uint32_t width,
      std::uint32_t height,
      pixel_format format) override;
    swr::error set_data(
      std::uint32_t level,
      std::uint32_t width,
      std::uint32_t height,
      pixel_format format,
      const std::vector<std::uint8_t>& data) override;
    swr::error set_sub_data(
      std::uint32_t level,
      std::uint32_t in_x,
      std::uint32_t in_y,
      std::uint32_t in_width,
      std::uint32_t in_height,
      pixel_format format,
      const std::vector<std::uint8_t>& data) override;
    void clear() override;

    texture_depth_2d* as_texture_depth_2d() override
    {
        return this;
    }
    const texture_depth_2d* as_texture_depth_2d() const override
    {
        return this;
    }

    swr::error set_compare_mode(texture_compare_mode mode) override;
    texture_compare_mode get_compare_mode() const override
    {
        return compare_mode;
    }

    swr::error set_compare_func(comparison_func func) override;
    comparison_func get_compare_func() const override
    {
        return compare_func;
    }

    std::size_t mip_level_count() const override
    {
        return data.data_ptrs.size();
    }

    std::size_t mip_pitch(std::uint32_t level) const override
    {
        return data.pitches[level];
    }
};

/*
 * texture_2d/texture_color_2d/texture_depth_2d constructors.
 */

inline texture_2d::texture_2d(
  std::uint32_t in_id,
  std::uint32_t in_width,
  std::uint32_t in_height,
  wrap_mode wrap_s,
  wrap_mode wrap_t,
  texture_filter filter_mag,
  texture_filter filter_min)
: id{in_id}
, width{in_width}
, height{in_height}
{
    initialize_sampler();

    sampler->set_filter_mag(filter_mag);
    sampler->set_filter_min(filter_min);

    sampler->set_wrap_s(wrap_s);
    sampler->set_wrap_t(wrap_t);
}

inline texture_color_2d::texture_color_2d(
  std::uint32_t in_id,
  std::uint32_t in_width,
  std::uint32_t in_height,
  wrap_mode wrap_s,
  wrap_mode wrap_t,
  texture_filter filter_mag,
  texture_filter filter_min)
: texture_2d{
    in_id,
    in_width,
    in_height,
    wrap_s,
    wrap_t,
    filter_mag,
    filter_min}
{
}

inline texture_depth_2d::texture_depth_2d(
  std::uint32_t in_id,
  std::uint32_t in_width,
  std::uint32_t in_height,
  wrap_mode wrap_s,
  wrap_mode wrap_t,
  texture_filter filter_mag,
  texture_filter filter_min)
: texture_2d{
    in_id,
    in_width,
    in_height,
    wrap_s,
    wrap_t,
    filter_mag,
    filter_min}
{
}

/*
 * sampler_2d_impl implementation.
 */

inline float sampler_2d_impl::calculate_mipmap_level(
  const ml::vec4& dFdx,
  const ml::vec4& dFdy) const
{
    /*
     * check if there are mipmaps available. if not, we don't need to calculate anything.
     */
    auto mipmap_levels = associated_texture->mip_level_count();
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
    return std::clamp(lambda, 0.f, lod_max);
}

inline void sampler_2d_impl::get_mipmap_params(
  int& level,
  int& w,
  int& h) const
{
    if(associated_texture->mip_level_count() == 1)
    {
        // no mipmapping available.
        level = 0;
    }

    w = associated_texture->width >> level;
    h = associated_texture->height >> level;
}
#ifndef SWR_USE_MORTON_CODES
inline void sampler_2d_impl::get_mipmap_params(
  int& level,
  int& w,
  int& h,
  int& pitch) const
{
    if(associated_texture->mip_level_count() == 1)
    {
        // no mipmapping available.
        level = 0;
    }

    w = associated_texture->width >> level;
    h = associated_texture->height >> level;
    pitch = static_cast<int>(associated_texture->mip_pitch(level));
}
#endif

inline ml::vec4 sampler_2d_impl::sample_at(
  const swr::varying& uv) const
{
    if(associated_texture->as_texture_depth_2d() != nullptr)
    {
        const float depth = sample_depth_at(uv);
        return {depth, depth, depth, 1.0f};
    }

    float lambda = calculate_mipmap_level(uv.dFdx, uv.dFdy);
    int mipmap_level = static_cast<int>(std::floor(lambda));

    if(mipmap_level == 0)
    {
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
        if(filter_min == texture_filter::nearest)
        {
            return sample_at_nearest(0, uv);
        }
        else if(filter_min == texture_filter::linear)
        {
            return sample_at_linear(0, uv);
        }
        else if(filter_min == texture_filter::nearest_mipmap_nearest)
        {
            return sample_at_nearest(mipmap_level, uv);
        }
        else if(filter_min == texture_filter::linear_mipmap_nearest)
        {
            return sample_at_linear(mipmap_level, uv);
        }
    }

    return ml::vec4::zero();
}

inline float sampler_2d_impl::sample_depth_at(
  const swr::varying& uv) const
{
    float lambda = calculate_mipmap_level(uv.dFdx, uv.dFdy);
    int mipmap_level = static_cast<int>(std::floor(lambda));

    if(mipmap_level == 0)
    {
        if(filter_mag == texture_filter::nearest)
        {
            return sample_depth_at_nearest(0, uv);
        }
        else if(filter_mag == texture_filter::linear)
        {
            return sample_depth_at_linear(0, uv);
        }
    }
    else
    {
        if(filter_min == texture_filter::nearest)
        {
            return sample_depth_at_nearest(0, uv);
        }
        else if(filter_min == texture_filter::linear)
        {
            return sample_depth_at_linear(0, uv);
        }
        else if(filter_min == texture_filter::nearest_mipmap_nearest)
        {
            return sample_depth_at_nearest(mipmap_level, uv);
        }
        else if(filter_min == texture_filter::linear_mipmap_nearest)
        {
            return sample_depth_at_linear(mipmap_level, uv);
        }
    }

    return 0.0f;
}

inline float sampler_2d_impl::sample_compare_at(
  const swr::varying& uv) const
{
    const auto* depth_texture = associated_texture->as_texture_depth_2d();
    if(depth_texture == nullptr
       || depth_texture->get_compare_mode() != texture_compare_mode::ref_to_texture)
    {
        return sample_depth_at(uv);
    }

    const ml::vec4 uv_dFdx{uv.dFdx.x, uv.dFdx.y, 0.0f, 0.0f};
    const ml::vec4 uv_dFdy{uv.dFdy.x, uv.dFdy.y, 0.0f, 0.0f};
    float lambda = calculate_mipmap_level(uv_dFdx, uv_dFdy);
    int mipmap_level = static_cast<int>(std::floor(lambda));

    if(mipmap_level == 0)
    {
        if(filter_mag == texture_filter::nearest)
        {
            return sample_compare_at_nearest(0, uv);
        }
        else if(filter_mag == texture_filter::linear)
        {
            return sample_compare_at_linear(0, uv);
        }
    }
    else
    {
        if(filter_min == texture_filter::nearest)
        {
            return sample_compare_at_nearest(0, uv);
        }
        else if(filter_min == texture_filter::linear)
        {
            return sample_compare_at_linear(0, uv);
        }
        else if(filter_min == texture_filter::nearest_mipmap_nearest)
        {
            return sample_compare_at_nearest(mipmap_level, uv);
        }
        else if(filter_min == texture_filter::linear_mipmap_nearest)
        {
            return sample_compare_at_linear(mipmap_level, uv);
        }
    }

    return 0.0f;
}

inline texture_compare_mode sampler_2d_impl::compare_mode() const
{
    const auto* depth_texture = associated_texture->as_texture_depth_2d();
    return depth_texture != nullptr
             ? depth_texture->get_compare_mode()
             : texture_compare_mode::none;
}

inline comparison_func sampler_2d_impl::compare_func() const
{
    const auto* depth_texture = associated_texture->as_texture_depth_2d();
    return depth_texture != nullptr
             ? depth_texture->get_compare_func()
             : comparison_func::less_equal;
}

inline ml::tvec2<int> sampler_2d_impl::size(
  std::uint32_t mipmap_level) const
{
    int w{0}, h{0};
    int level = static_cast<int>(mipmap_level);
    get_mipmap_params(level, w, h);
    return {w, h};
}

inline ml::vec4 sampler_2d_impl::sample_at_nearest(
  int mipmap_level,
  const swr::varying& uv) const
{
    const auto* color_texture = associated_texture->as_texture_color_2d();
    if(color_texture == nullptr)
    {
        return ml::vec4::zero();
    }

#ifdef SWR_USE_MORTON_CODES
    int w{0}, h{0};

    get_mipmap_params(mipmap_level, w, h);
    ml::tvec2<int> texel_coords = {
      static_cast<int>(std::floor(uv.value.x * w)),
      static_cast<int>(std::floor(uv.value.y * h))};
    texel_coords = {
      wrap(wrap_s, texel_coords.x, w),
      wrap(wrap_t, texel_coords.y, h)};
    texel_coords.y = h - 1 - texel_coords.y;

    return (color_texture->data.data_ptrs[mipmap_level])[libmorton::morton2D_32_encode(texel_coords.x, texel_coords.y)];
#else
    int w{0}, h{0}, pitch{0};

    get_mipmap_params(mipmap_level, w, h, pitch);
    ml::tvec2<int> texel_coords = {
      static_cast<int>(std::floor(uv.value.x * w)),
      static_cast<int>(std::floor(uv.value.y * h))};
    texel_coords = {
      wrap(wrap_s, texel_coords.x, w),
      wrap(wrap_t, texel_coords.y, h)};
    texel_coords.y = h - 1 - texel_coords.y;

    return (color_texture->data.data_ptrs[mipmap_level])[texel_coords.y * pitch + texel_coords.x];
#endif
}

inline float sampler_2d_impl::sample_depth_at_nearest(
  int mipmap_level,
  const swr::varying& uv) const
{
    const auto* depth_texture = associated_texture->as_texture_depth_2d();
    if(depth_texture == nullptr)
    {
        // FIXME check behavior.
        return sample_at_nearest(mipmap_level, uv).x;
    }

#ifdef SWR_USE_MORTON_CODES
    int w{0}, h{0};

    get_mipmap_params(mipmap_level, w, h);
    ml::tvec2<int> texel_coords = {
      static_cast<int>(std::floor(uv.value.x * w)),
      static_cast<int>(std::floor(uv.value.y * h))};
    texel_coords = {
      wrap(wrap_s, texel_coords.x, w),
      wrap(wrap_t, texel_coords.y, h)};
    texel_coords.y = h - 1 - texel_coords.y;

    return ml::to_float(
      (depth_texture->data.data_ptrs[mipmap_level])[libmorton::morton2D_32_encode(texel_coords.x, texel_coords.y)]);
#else
    int w{0}, h{0}, pitch{0};

    get_mipmap_params(mipmap_level, w, h, pitch);
    ml::tvec2<int> texel_coords = {
      static_cast<int>(std::floor(uv.value.x * w)),
      static_cast<int>(std::floor(uv.value.y * h))};
    texel_coords = {
      wrap(wrap_s, texel_coords.x, w),
      wrap(wrap_t, texel_coords.y, h)};
    texel_coords.y = h - 1 - texel_coords.y;

    return ml::to_float(
      (depth_texture->data.data_ptrs[mipmap_level])[texel_coords.y * pitch + texel_coords.x]);
#endif
}

inline float sampler_2d_impl::compare_depth_values(
  float reference,
  float sampled_depth) const
{
    const auto* depth_texture = associated_texture->as_texture_depth_2d();
    if(depth_texture == nullptr)
    {
        return sampled_depth;
    }

    switch(depth_texture->get_compare_func())
    {
    case comparison_func::pass:
        return 1.0f;
    case comparison_func::fail:
        return 0.0f;
    case comparison_func::equal:
        return reference == sampled_depth ? 1.0f : 0.0f;
    case comparison_func::not_equal:
        return reference != sampled_depth ? 1.0f : 0.0f;
    case comparison_func::less:
        return reference < sampled_depth ? 1.0f : 0.0f;
    case comparison_func::less_equal:
        return reference <= sampled_depth ? 1.0f : 0.0f;
    case comparison_func::greater:
        return reference > sampled_depth ? 1.0f : 0.0f;
    case comparison_func::greater_equal:
        return reference >= sampled_depth ? 1.0f : 0.0f;
    }

    return 0.0f;
}

inline float sampler_2d_impl::sample_compare_at_nearest(
  int mipmap_level,
  const swr::varying& uv) const
{
    const float sampled_depth = sample_depth_at_nearest(mipmap_level, uv);
    return compare_depth_values(uv.value.z, sampled_depth);
}

inline ml::vec4 sampler_2d_impl::sample_at_linear(
  int mipmap_level,
  const swr::varying& uv) const
{
    const auto* color_texture = associated_texture->as_texture_color_2d();
    if(color_texture == nullptr)
    {
        return ml::vec4::zero();
    }

#ifdef SWR_USE_MORTON_CODES
    int w{0}, h{0};

    get_mipmap_params(mipmap_level, w, h);

    // calculate nearest texel and interpolation parameters.
    const ml::vec2 texel_coords = {uv.value.x * w - 0.5f, uv.value.y * h - 0.5f};
    const ml::tvec2<int> texel_coords_dec = {
      static_cast<int>(std::floor(texel_coords.x)),
      static_cast<int>(std::floor(texel_coords.y))};
    const ml::vec2 texel_coords_frac = {
      texel_coords.x - texel_coords_dec.x,
      texel_coords.y - texel_coords_dec.y};

    // calculate nearest four texel coordinates while respecting the texture wrap mode.
    std::array<ml::tvec2<int>, 4> texel_coords_dec_wrap = {
      {{wrap(wrap_s, texel_coords_dec.x, w),
        wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w),
        wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x, w),
        wrap(wrap_t, texel_coords_dec.y + 1, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w),
        wrap(wrap_t, texel_coords_dec.y + 1, h)}}};
    for(auto& texel_coord: texel_coords_dec_wrap)
    {
        texel_coord.y = h - 1 - texel_coord.y;
    }

    // get color values.
    const std::array<std::uint_fast32_t, 4> idxs = {
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[0].x, texel_coords_dec_wrap[0].y),
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[1].x, texel_coords_dec_wrap[1].y),
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[2].x, texel_coords_dec_wrap[2].y),
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[3].x, texel_coords_dec_wrap[3].y),
    };

    const std::array<ml::vec4, 4> texels = {
      (color_texture->data.data_ptrs[mipmap_level])[idxs[0]],
      (color_texture->data.data_ptrs[mipmap_level])[idxs[1]],
      (color_texture->data.data_ptrs[mipmap_level])[idxs[2]],
      (color_texture->data.data_ptrs[mipmap_level])[idxs[3]],
    };

    // return linearly interpolated color.
    return ml::lerp(
      texel_coords_frac.y,
      ml::lerp(texel_coords_frac.x, texels[0], texels[1]),
      ml::lerp(texel_coords_frac.x, texels[2], texels[3]));
#else
    int w{0}, h{0}, pitch{0};

    get_mipmap_params(mipmap_level, w, h, pitch);

    // calculate nearest texel and interpolation parameters.
    ml::vec2 texel_coords = {uv.value.x * w - 0.5f, uv.value.y * h - 0.5f};
    ml::tvec2<int> texel_coords_dec = {
      static_cast<int>(std::floor(texel_coords.x)),
      static_cast<int>(std::floor(texel_coords.y))};
    ml::vec2 texel_coords_frac = {texel_coords.x - texel_coords_dec.x, texel_coords.y - texel_coords_dec.y};

    // calculate nearest four texel coordinates while respecting the texture wrap mode.
    std::array<ml::tvec2<int>, 4> texel_coords_dec_wrap = {
      {{wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w), wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y + 1, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w),
        wrap(wrap_t, texel_coords_dec.y + 1, h)}}};
    for(auto& texel_coord: texel_coords_dec_wrap)
    {
        texel_coord.y = h - 1 - texel_coord.y;
    }

    // get color values.
    std::array<ml::vec4, 4> texels = {
      (color_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[0].y * pitch + texel_coords_dec_wrap[0].x],
      (color_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[1].y * pitch + texel_coords_dec_wrap[1].x],
      (color_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[2].y * pitch + texel_coords_dec_wrap[2].x],
      (color_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[3].y * pitch + texel_coords_dec_wrap[3].x],
    };

    // return linearly interpolated color.
    return ml::lerp(
      texel_coords_frac.y,
      ml::lerp(texel_coords_frac.x, texels[0], texels[1]),
      ml::lerp(texel_coords_frac.x, texels[2], texels[3]));
#endif
}

inline float sampler_2d_impl::sample_depth_at_linear(
  int mipmap_level,
  const swr::varying& uv) const
{
    const auto* depth_texture = associated_texture->as_texture_depth_2d();
    if(depth_texture == nullptr)
    {
        return sample_at_linear(mipmap_level, uv).x;
    }

#ifdef SWR_USE_MORTON_CODES
    int w{0}, h{0};

    get_mipmap_params(mipmap_level, w, h);

    const ml::vec2 texel_coords = {uv.value.x * w - 0.5f, uv.value.y * h - 0.5f};
    const ml::tvec2<int> texel_coords_dec{
      static_cast<int>(std::floor(texel_coords.x)),
      static_cast<int>(std::floor(texel_coords.y))};
    const ml::vec2 texel_coords_frac = {
      texel_coords.x - texel_coords_dec.x,
      texel_coords.y - texel_coords_dec.y};

    std::array<ml::tvec2<int>, 4> texel_coords_dec_wrap{
      {{wrap(wrap_s, texel_coords_dec.x, w),
        wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w),
        wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x, w),
        wrap(wrap_t, texel_coords_dec.y + 1, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w),
        wrap(wrap_t, texel_coords_dec.y + 1, h)}}};
    for(auto& c: texel_coords_dec_wrap)
    {
        c.y = h - 1 - c.y;
    }

    const std::array<std::uint_fast32_t, 4> idxs = {
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[0].x, texel_coords_dec_wrap[0].y),
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[1].x, texel_coords_dec_wrap[1].y),
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[2].x, texel_coords_dec_wrap[2].y),
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[3].x, texel_coords_dec_wrap[3].y),
    };

    const std::array<float, 4> values = {
      ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[idxs[0]]),
      ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[idxs[1]]),
      ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[idxs[2]]),
      ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[idxs[3]])};
#else
    int w{0}, h{0}, pitch{0};

    get_mipmap_params(mipmap_level, w, h, pitch);

    const ml::vec2 texel_coords = {uv.value.x * w - 0.5f, uv.value.y * h - 0.5f};
    const ml::tvec2<int> texel_coords_dec{
      static_cast<int>(std::floor(texel_coords.x)),
      static_cast<int>(std::floor(texel_coords.y))};
    const ml::vec2 texel_coords_frac = {
      texel_coords.x - texel_coords_dec.x,
      texel_coords.y - texel_coords_dec.y};

    std::array<ml::tvec2<int>, 4> texel_coords_dec_wrap{
      {{wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w), wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y + 1, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w),
        wrap(wrap_t, texel_coords_dec.y + 1, h)}}};
    for(auto& c: texel_coords_dec_wrap)
    {
        c.y = h - 1 - c.y;
    }

    std::array<float, 4> values = {
      ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[0].y * pitch + texel_coords_dec_wrap[0].x]),
      ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[1].y * pitch + texel_coords_dec_wrap[1].x]),
      ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[2].y * pitch + texel_coords_dec_wrap[2].x]),
      ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[3].y * pitch + texel_coords_dec_wrap[3].x])};
#endif

    return ml::lerp(
      texel_coords_frac.y,
      ml::lerp(texel_coords_frac.x, values[0], values[1]),
      ml::lerp(texel_coords_frac.x, values[2], values[3]));
}

inline float sampler_2d_impl::sample_compare_at_linear(int mipmap_level, const swr::varying& uv) const
{
    const auto* depth_texture = associated_texture->as_texture_depth_2d();
    if(depth_texture == nullptr)
    {
        return compare_depth_values(uv.value.z, sample_depth_at_linear(mipmap_level, uv));
    }

#ifdef SWR_USE_MORTON_CODES
    int w{0}, h{0};

    get_mipmap_params(mipmap_level, w, h);

    const ml::vec2 texel_coords = {uv.value.x * w - 0.5f, uv.value.y * h - 0.5f};
    const ml::tvec2<int> texel_coords_dec{
      static_cast<int>(std::floor(texel_coords.x)),
      static_cast<int>(std::floor(texel_coords.y))};
    const ml::vec2 texel_coords_frac = {
      texel_coords.x - texel_coords_dec.x,
      texel_coords.y - texel_coords_dec.y};

    std::array<ml::tvec2<int>, 4> texel_coords_dec_wrap{
      {{wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w), wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y + 1, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w),
        wrap(wrap_t, texel_coords_dec.y + 1, h)}}};
    for(auto& c: texel_coords_dec_wrap)
    {
        c.y = h - 1 - c.y;
    }

    const std::array<std::uint_fast32_t, 4> idxs = {
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[0].x, texel_coords_dec_wrap[0].y),
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[1].x, texel_coords_dec_wrap[1].y),
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[2].x, texel_coords_dec_wrap[2].y),
      libmorton::morton2D_32_encode(texel_coords_dec_wrap[3].x, texel_coords_dec_wrap[3].y),
    };

    const std::array<float, 4> depth_values = {
      ml::to_float(depth_texture->data.data_ptrs[mipmap_level][idxs[0]]),
      ml::to_float(depth_texture->data.data_ptrs[mipmap_level][idxs[1]]),
      ml::to_float(depth_texture->data.data_ptrs[mipmap_level][idxs[2]]),
      ml::to_float(depth_texture->data.data_ptrs[mipmap_level][idxs[3]])};

    const std::array<float, 4> values = {
      compare_depth_values(uv.value.z, depth_values[0]),
      compare_depth_values(uv.value.z, depth_values[1]),
      compare_depth_values(uv.value.z, depth_values[2]),
      compare_depth_values(uv.value.z, depth_values[3]),
    };
#else
    int w{0}, h{0}, pitch{0};

    get_mipmap_params(mipmap_level, w, h, pitch);

    const ml::vec2 texel_coords = {uv.value.x * w - 0.5f, uv.value.y * h - 0.5f};
    const ml::tvec2<int> texel_coords_dec{
      static_cast<int>(std::floor(texel_coords.x)),
      static_cast<int>(std::floor(texel_coords.y))};
    const ml::vec2 texel_coords_frac = {
      texel_coords.x - texel_coords_dec.x,
      texel_coords.y - texel_coords_dec.y};

    std::array<ml::tvec2<int>, 4> texel_coords_dec_wrap{
      {{wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w), wrap(wrap_t, texel_coords_dec.y, h)},
       {wrap(wrap_s, texel_coords_dec.x, w), wrap(wrap_t, texel_coords_dec.y + 1, h)},
       {wrap(wrap_s, texel_coords_dec.x + 1, w),
        wrap(wrap_t, texel_coords_dec.y + 1, h)}}};
    for(auto& c: texel_coords_dec_wrap)
    {
        c.y = h - 1 - c.y;
    }

    std::array<float, 4> values = {
      compare_depth_values(uv.value.z, ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[0].y * pitch + texel_coords_dec_wrap[0].x])),
      compare_depth_values(uv.value.z, ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[1].y * pitch + texel_coords_dec_wrap[1].x])),
      compare_depth_values(uv.value.z, ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[2].y * pitch + texel_coords_dec_wrap[2].x])),
      compare_depth_values(uv.value.z, ml::to_float((depth_texture->data.data_ptrs[mipmap_level])[texel_coords_dec_wrap[3].y * pitch + texel_coords_dec_wrap[3].x]))};
#endif

    return ml::lerp(
      texel_coords_frac.y,
      ml::lerp(texel_coords_frac.x, values[0], values[1]),
      ml::lerp(texel_coords_frac.x, values[2], values[3]));
}

} /* namespace impl */

} /* namespace swr */
