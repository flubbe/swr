/**
 * swr - a software rasterizer
 * 
 * fragment processing.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "../swr_internal.h"

#include "interpolators.h"
#include "fragment.h"
#include "sweep.h"

namespace rast
{

/*
 * statically verify assumptions.
 */
static_assert(swr::fragment_shader_result::discard == 0, "swr::fragment_shader_result::discard needs to evaluate to the numerical value 0");
static_assert(swr::fragment_shader_result::accept == 1, "swr::fragment_shader_result::accept needs to evaluate to the numerical value 1");

/**
 * Generate fragment color values, generate color write mask, perform depth testing and depth writing.
 * Everything is performed with respect to the currently active draw target.
 * 
 * More precisely, we do the following operations, in order:
 * 
 *  1) Scissor test.
 * 
 * If it succeeds, we calculate all interpolated values for the varyings.
 * 
 *  2) Call the fragment shader.
 *  3) Depth test (note that this cannot be done earlier, since the fragment shader may modify the depth value).
 */
void sweep_rasterizer::process_fragment(int x, int y, const swr::impl::render_states& states, float one_over_viewport_z, fragment_info& frag_info, swr::impl::fragment_output& out)
{
    /*
     * Scissor test.
     */
    if(states.scissor_test_enabled)
    {
        int y_min{states.scissor_box.y_min};
        int y_max{states.scissor_box.y_max};

        // the default framebuffer needs a flip.
        if(states.draw_target == framebuffer)
        {
            int y_temp = y_min;
            y_min = states.draw_target->properties.height - y_max;
            y_max = states.draw_target->properties.height - y_temp;
        }

        if(x < states.scissor_box.x_min || x >= states.scissor_box.x_max
           || y < y_min || y >= y_max)
        {
            out.write_flags = 0;
            return;
        }
    }

    // initialize write flags.
    uint32_t write_flags = swr::impl::fragment_output::fof_write_color;

    /* stencil buffering is currently unimplemented and the stencil mask is default-initialized to false. */

    /*
     * Compute z and interpolated values.
     * 
     * Recall that one_over_viewport_z comes from the clip coordinates' w component. That is, it is
     * called w_c in eq. (15.1) on p.415 of https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf,
     * and with respect to the notation found there, we compute w_f here.
     */
    float z = 1.0f / one_over_viewport_z;
    for(auto& it: frag_info.varyings)
    {
        if(it.iq == swr::interpolation_qualifier::smooth)
        {
            it.value *= z;
            it.dFdx *= z;
            it.dFdy *= z;
        }
    }

    /*
     * Execute the fragment shader.
	 */
    //!!fixme: From docs: gl_PointCoord: contains the coordinate of a fragment within a point. currently undefined.

    /*
     * set up the output color attachments for the fragment shader. the default color is explicitly unspecified in OpenGL, and we
     * choose {0,0,0,1} for initialization. see e.g. https://stackoverflow.com/questions/29119097/glsl-default-value-for-output-color 
     */
    ml::vec4 color{0, 0, 0, 1};
    float depth_value = frag_info.depth_value;

    /*
     * set up the fragment coordinate. this should match (15.1) on p. 415 in https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     * note that we need to reverse the y-axis for the default framebuffer.
     */
    ml::vec4 frag_coord;
    if(states.draw_target == framebuffer)
    {
        frag_coord = {
          static_cast<float>(x) - pixel_center.x,
          framebuffer->properties.height - (static_cast<float>(y) - pixel_center.y),
          depth_value,
          z};
    }
    else
    {
        frag_coord = {
          static_cast<float>(x) - pixel_center.x,
          static_cast<float>(y) - pixel_center.y,
          depth_value,
          z};
    }

    auto accept_fragment = states.shader_info->shader->fragment_shader(frag_coord, frag_info.front_facing, {0, 0}, frag_info.varyings, depth_value, color);
    if(accept_fragment == swr::discard)
    {
        out.write_flags = 0;
        return;
    }

    /*
     * Depth test.
     */
    bool depth_write_mask = true;
    if(states.depth_test_enabled)
    {
        depth_value = boost::algorithm::clamp(depth_value, 0.f, 1.f);
        states.draw_target->depth_compare_write(x, y, depth_value, states.depth_func, states.write_depth, depth_write_mask);
    }

    auto to_mask = [](bool b) -> uint32_t
    { return ~(static_cast<std::uint32_t>(b) - 1); };

    out.color = color;
    out.write_flags = write_flags & to_mask(depth_write_mask);
}

/** the same as above, but operates on 2x2 tiles. does not return any value. */
void sweep_rasterizer::process_fragment_block(int x, int y, const swr::impl::render_states& states, float one_over_viewport_z[4], fragment_info frag_info[4], swr::impl::fragment_output_block& out)
{
    /*
     * helper lambdas.
     */
    auto set_uniform_mask = [](bool mask[4], bool v)
    {
        mask[0] = mask[1] = mask[2] = mask[3] = v;
    };
    auto apply_mask = [](bool mask[4], const auto additional_mask[4])
    {
        mask[0] &= static_cast<bool>(additional_mask[0]);
        mask[1] &= static_cast<bool>(additional_mask[1]);
        mask[2] &= static_cast<bool>(additional_mask[2]);
        mask[3] &= static_cast<bool>(additional_mask[3]);
    };
    auto copy_array4 = [](auto to[4], const auto from[4])
    {
        to[0] = from[0];
        to[1] = from[1];
        to[2] = from[2];
        to[3] = from[3];
    };

    // initialize masks.
    bool depth_mask[4] = {out.write_color[0], out.write_color[1], out.write_color[2], out.write_color[3]};
    depth_mask[0] &= states.write_depth;
    depth_mask[1] &= states.write_depth;
    depth_mask[2] &= states.write_depth;
    depth_mask[3] &= states.write_depth;

    bool write_color[4] = {true, true, true, true};
    bool write_stencil[4] = {false, false, false, false}; /* unimplemented */

    /* stencil buffering is currently unimplemented and the stencil mask is default-initialized to false. */

    // block coordinates
    const ml::tvec2<int> coords[4] = {{x, y}, {x + 1, y}, {x, y + 1}, {x + 1, y + 1}};

    /*
     * Scissor test.
     */
    if(states.scissor_test_enabled)
    {
        int y_min{states.scissor_box.y_min};
        int y_max{states.scissor_box.y_max};

        // the default framebuffer needs a flip.
        if(states.draw_target == framebuffer)
        {
            int y_temp = y_min;
            y_min = states.draw_target->properties.height - y_max;
            y_max = states.draw_target->properties.height - y_temp;
        }

        auto scissor_check = [y_min, y_max, &states, this](int _x, int _y) -> bool
        { return _x >= states.scissor_box.x_min && _x < states.scissor_box.x_max && _y >= y_min && _y < y_max; };
        bool scissor_mask[4] = {
          scissor_check(coords[0].x, coords[0].y), scissor_check(coords[1].x, coords[1].y), scissor_check(coords[2].x, coords[2].y), scissor_check(coords[3].x, coords[3].y)};

        if(!(scissor_mask[0] || scissor_mask[1] || scissor_mask[2] || scissor_mask[3]))
        {
            // the mask only contains 'false'.
            set_uniform_mask(out.write_color, false);
            set_uniform_mask(out.write_stencil, false);

            return;
        }

        apply_mask(depth_mask, scissor_mask);
        apply_mask(write_color, scissor_mask);
        apply_mask(write_stencil, scissor_mask);
    }

    /*
     * Compute z and interpolated values.
     */
#ifdef SWR_USE_SIMD
    DECLARE_ALIGNED_FLOAT4(z);
    _mm_store_ps(z, _mm_div_ps(_mm_set_ps1(1.0f), _mm_set_ps(one_over_viewport_z[3], one_over_viewport_z[2], one_over_viewport_z[1], one_over_viewport_z[0])));
#else  /* SWR_USE_SIMD */
    const ml::vec4 z = ml::vec4::one() / ml::vec4(one_over_viewport_z);
#endif /* SWR_USE_SIMD */

    for(int k = 0; k < 4; ++k)
    {
        for(auto& it: frag_info[k].varyings)
        {
            if(it.iq == swr::interpolation_qualifier::smooth)
            {
                it.value *= z[k];
                it.dFdx *= z[k];
                it.dFdy *= z[k];
            }
        }
    }

    /*
     * Execute the fragment shader.
	 */
    //!!fixme: From docs: gl_PointCoord: contains the coordinate of a fragment within a point. currently undefined.

    /*
     * set up the output color attachments for the fragment shader. the default color is explicitly unspecified in OpenGL, and we
     * choose {0,0,0,1} for initialization. see e.g. https://stackoverflow.com/questions/29119097/glsl-default-value-for-output-color 
     */

    ml::vec4 color[4] = {ml::vec4{0, 0, 0, 1}, ml::vec4{0, 0, 0, 1}, ml::vec4{0, 0, 0, 1}, ml::vec4{0, 0, 0, 1}};
    DECLARE_ALIGNED_FLOAT4(depth_value) = {
      frag_info[0].depth_value,
      frag_info[1].depth_value,
      frag_info[2].depth_value,
      frag_info[3].depth_value};

    /*
     * set up the fragment coordinate. this should match (15.1) on p. 415 in https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     * note that we need to reverse the y-axis.
     */
    ml::vec4 frag_coord[4] = {
      {static_cast<float>(coords[0].x) - pixel_center.x, static_cast<float>(coords[0].y) - pixel_center.y, depth_value[0], z[0]},
      {static_cast<float>(coords[1].x) - pixel_center.x, static_cast<float>(coords[1].y) - pixel_center.y, depth_value[1], z[1]},
      {static_cast<float>(coords[2].x) - pixel_center.x, static_cast<float>(coords[2].y) - pixel_center.y, depth_value[2], z[2]},
      {static_cast<float>(coords[3].x) - pixel_center.x, static_cast<float>(coords[3].y) - pixel_center.y, depth_value[3], z[3]},
    };

    if(states.draw_target == framebuffer)
    {
        frag_coord[0].y = framebuffer->properties.height - frag_coord[0].y;
        frag_coord[1].y = framebuffer->properties.height - frag_coord[1].y;
        frag_coord[2].y = framebuffer->properties.height - frag_coord[2].y;
        frag_coord[3].y = framebuffer->properties.height - frag_coord[3].y;
    }

    swr::fragment_shader_result accept_mask[4] = {
      states.shader_info->shader->fragment_shader(frag_coord[0], frag_info[0].front_facing, {0, 0}, frag_info[0].varyings, depth_value[0], color[0]),
      states.shader_info->shader->fragment_shader(frag_coord[1], frag_info[1].front_facing, {0, 0}, frag_info[1].varyings, depth_value[1], color[1]),
      states.shader_info->shader->fragment_shader(frag_coord[2], frag_info[2].front_facing, {0, 0}, frag_info[2].varyings, depth_value[2], color[2]),
      states.shader_info->shader->fragment_shader(frag_coord[3], frag_info[3].front_facing, {0, 0}, frag_info[3].varyings, depth_value[3], color[3])};

    if(!(accept_mask[0] || accept_mask[1] || accept_mask[2] || accept_mask[3]))
    {
        set_uniform_mask(out.write_color, false);
        set_uniform_mask(out.write_stencil, false);
        return;
    }

    apply_mask(depth_mask, accept_mask);
    apply_mask(write_color, accept_mask);
    apply_mask(write_stencil, accept_mask);

    /*
     * Depth test.
     */
    if(states.depth_test_enabled)
    {
#ifdef SWR_USE_SIMD
        _mm_store_ps(depth_value, _mm_min_ps(_mm_max_ps(_mm_load_ps(depth_value), _mm_set_ps1(0.0f)), _mm_set_ps1(1.0f)));
#else  /* SWR_USE_SIMD */
        depth_value[0] = boost::algorithm::clamp(depth_value[0], 0, 1);
        depth_value[1] = boost::algorithm::clamp(depth_value[1], 0, 1);
        depth_value[2] = boost::algorithm::clamp(depth_value[2], 0, 1);
        depth_value[3] = boost::algorithm::clamp(depth_value[3], 0, 1);
#endif /* SWR_USE_SIMD */
        states.draw_target->depth_compare_write_block(x, y, depth_value, states.depth_func, states.write_depth, depth_mask);
    }
    apply_mask(write_color, depth_mask);
    apply_mask(write_stencil, depth_mask);

    // copy color and masks into output
    copy_array4(out.color, color);
    copy_array4(out.write_color, write_color);
    copy_array4(out.write_stencil, write_stencil);
}

} /* namespace rast */