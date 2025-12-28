/**
 * swr - a software rasterizer
 *
 * render pipeline state management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <boost/container/static_vector.hpp>

namespace swr
{

namespace impl
{

/** States that are set on a per-primitive basis. */
struct render_states
{
    /* buffers. */
    ml::vec4 clear_color{ml::vec4::zero()};
    ml::fixed_32_t clear_depth{1};

    /* viewport transform. */
    int x{0}, y{0};
    unsigned int width{0}, height{0};
    float z_near{0}, z_far{1};

    /** scissor test. */
    bool scissor_test_enabled{false};
    utils::rect scissor_box;

    /* depth test. */
    bool depth_test_enabled{true};
    bool write_depth{true};
    comparison_func depth_func{comparison_func::less};

    /* culling. */
    bool culling_enabled{false};
    front_face_orientation front_face{front_face_orientation::ccw};
    cull_face_direction cull_mode{cull_face_direction::back};

    polygon_mode poly_mode{polygon_mode::fill};

    bool polygon_offset_fill_enabled{false};
    float polygon_offset_factor{0.0f};
    float polygon_offset_units{0.0f};

    /* blending */
    bool blending_enabled{false};
    blend_func blend_src{blend_func::one};
    blend_func blend_dst{blend_func::zero};

    /* texture units. */
    boost::container::static_vector<struct texture_2d*, geom::limits::max::texture_units> texture_2d_units; /* the context owns the textures. */
    std::uint32_t texture_2d_active_unit{0};
    boost::container::static_vector<struct sampler_2d*, geom::limits::max::texture_units> texture_2d_samplers; /* the textures own their samplers. */

    /* shaders */
    struct program_info* shader_info{nullptr}; /* the context owns the shader info */
    boost::container::static_vector<swr::uniform, geom::limits::max::uniform_locations> uniforms;

    /* framebuffer. this needs to be always valid for the drawing functions. */
    struct framebuffer_draw_target* draw_target{nullptr};

    /** default constructor. */
    render_states() = default;

    /** default copy constructor. */
    render_states(const render_states&) = default;

    /** default move constructor. */
    render_states(render_states&&) = default;

    /** default assignment operator. */
    render_states& operator=(const render_states&) = default;

    /** reset the states. */
    void reset(struct framebuffer_draw_target* default_draw_target)
    {
        clear_color = ml::vec4::zero();
        clear_depth = 1;

        x = 0;
        y = 0;
        width = 0;
        height = 0;

        z_near = 0;
        z_far = 1;

        scissor_test_enabled = false;
        scissor_box = utils::rect{0, 0, 0, 0};

        depth_test_enabled = false;
        write_depth = true;
        depth_func = comparison_func::less;

        culling_enabled = false;
        front_face = front_face_orientation::ccw;
        cull_mode = cull_face_direction::back;

        poly_mode = polygon_mode::fill;

        polygon_offset_fill_enabled = false;
        polygon_offset_factor = 0.0f;
        polygon_offset_units = 0.0f;

        blending_enabled = false;
        blend_src = blend_func::one;
        blend_dst = blend_func::zero;

        texture_2d_units.clear();
        texture_2d_units.shrink_to_fit();

        texture_2d_active_unit = 0;

        texture_2d_samplers.clear();
        texture_2d_samplers.shrink_to_fit();

        shader_info = nullptr;
        uniforms.clear();
        uniforms.shrink_to_fit();

        draw_target = default_draw_target;
    }

    /** set the clear color. */
    void set_clear_color(float r, float g, float b, float a)
    {
        clear_color = ml::clamp_to_unit_interval({r, g, b, a});
    }

    /** set the current clear depth. */
    void set_clear_depth(float z)
    {
        clear_depth = std::clamp(z, 0.f, 1.f);
    }

    /** set the viewport. */
    void set_viewport(int in_x, int in_y, unsigned int in_width, unsigned int in_height)
    {
        x = in_x;
        y = in_y;
        width = in_width;
        height = in_height;
    }

    /** update min and max depth values. */
    void set_depth_range(float in_z_near, float in_z_far)
    {
        z_near = std::clamp(in_z_near, 0.f, 1.f);
        z_far = std::clamp(in_z_far, 0.f, 1.f);
    }

    /** set scissor box. */
    void set_scissor_box(int x_min, int x_max, int y_min, int y_max)
    {
        scissor_box = utils::rect{x_min, x_max, y_min, y_max};
    }
};

} /* namespace impl */

} /* namespace swr */
