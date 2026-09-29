/**
 * swr - a software rasterizer
 *
 * Render pipeline state management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <boost/container/static_vector.hpp>

namespace swr
{

namespace impl
{

/** 2D texture bindings type. */
using texture_2d_bindings = boost::container::static_vector<
  struct texture_2d*,
  swr::limits::max::texture_units>;

/** The default framebuffer has id 0. */
constexpr std::uint32_t default_framebuffer_id = 0;

/** States that are set on a per-primitive basis. */
struct render_states
{
    /* Buffers. */
    ml::vec4 clear_color{ml::vec4::zero()};
    ml::fixed_32_t clear_depth{1};

    /* Viewport transform. */
    int x{0}, y{0};
    unsigned int width{0}, height{0};
    float z_near{0}, z_far{1};

    /** Scissor test. */
    bool scissor_test_enabled{false};
    utils::rect scissor_box;

    /* Depth test. */
    bool depth_test_enabled{true};
    bool write_depth{true};
    comparison_func depth_func{comparison_func::less};

    /* Rasterizer implementation features. */
    rasterizer_feature_mode block_early_depth_reject_mode{rasterizer_feature_mode::automatic};
    rasterizer_feature_mode early_fragment_depth_test_mode{rasterizer_feature_mode::automatic};

    /* Culling. */
    bool culling_enabled{false};
    front_face_orientation front_face{front_face_orientation::ccw};
    cull_face_direction cull_mode{cull_face_direction::back};

    polygon_mode poly_mode{polygon_mode::fill};

    bool polygon_offset_fill_enabled{false};
    float polygon_offset_factor{0.0f};
    float polygon_offset_units{0.0f};

    /* Blending */
    bool blending_enabled{false};
    blend_func blend_src{blend_func::one};
    blend_func blend_dst{blend_func::zero};

    /* Texture units. */
    texture_2d_bindings texture_2d_units; /* the context owns the textures. */
    std::uint32_t texture_2d_active_unit{0};
    sampler_bindings texture_2d_samplers; /* the textures own their samplers. */

    /* Shaders */
    struct program_info* shader_info{nullptr}; /* the context owns the shader info */
    uniform_bindings uniforms;

    /** Draw target. */
    std::uint32_t draw_target{default_framebuffer_id};

    /** Default constructors. */
    render_states() = default;
    render_states(const render_states&) = default;
    render_states(render_states&&) = default;

    /** Default assignment operators. */
    render_states& operator=(const render_states&) = default;
    render_states& operator=(render_states&&) = default;

    /** Reset the state. */
    void reset()
    {
        *this = {};
    }

    /**
     * Set the clear color.
     *
     * @param r Red color component.
     * @param g Green color component.
     * @param b Blue color component.
     * @param a Alpha component.
     * @note The inputs are clamped to the interval `[0,1]`.
     */
    void set_clear_color(float r, float g, float b, float a)
    {
        clear_color = ml::clamp_to_unit_interval({r, g, b, a});
    }

    /**
     * Set the current clear depth.
     *
     * @param z The clear depth.
     * @note The input is clamped to the interval `[0,1]`.
     */
    void set_clear_depth(float z)
    {
        clear_depth = std::clamp(z, 0.f, 1.f);
    }

    /**
     * Set the viewport.
     *
     * @param x Viewport `x`.
     * @param y Viewport `y`.
     * @param width Viewport width.
     * @param height Viewport height.
     */
    void set_viewport(int x, int y, unsigned int width, unsigned int height)
    {
        this->x = x;
        this->y = y;
        this->width = width;
        this->height = height;
    }

    /**
     * Update min and max depth values.
     *
     * @param z_near Near `z` value.
     * @param z_far Far `z` value.
     * @note The inputs are clamped to the interval `[0,1]`.
     */
    void set_depth_range(float z_near, float z_far)
    {
        this->z_near = std::clamp(z_near, 0.f, 1.f);
        this->z_far = std::clamp(z_far, 0.f, 1.f);
    }

    /**
     * Set scissor box.
     *
     * @param x_min Left border coordinate.
     * @param x_max Right border coordinate.
     * @param y_min Top border coordinate.
     * @param y_max Bottom border coordinate.
     */
    void set_scissor_box(int x_min, int x_max, int y_min, int y_max)
    {
        scissor_box = utils::rect{x_min, x_max, y_min, y_max};
    }
};

} /* namespace impl */

} /* namespace swr */
