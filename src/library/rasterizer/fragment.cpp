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

/**
 * Process fragments and merge outputs.
 * 
 * We perform the following operations, in order:
 * 
 *  1) Scissor test.
 * 
 * If it succeeds, we calculate all interpolated values for the varyings.
 * 
 *  2) Set the dither reference values which may be used for sampling from textures. (!!todo)
 *  3) Call the fragment shader.
 *  4) Depth test (called here, since the fragment shader may modify the depth value).
 * 
 * To merge the outputs, we do:
 * 
 *  5) Calculate the color in the output buffer's pixel format.
 *  6) Apply color blending.
 */
void sweep_rasterizer::process_fragment(int x, int y, const swr::impl::render_states& states, float one_over_viewport_z, fragment_info& frag_info, swr::impl::fragment_output& out)
{
    SWR_STATS_INCREMENT(stats_frag.count);

    // initialize mask.
    out.write_color = false;

    /* stencil buffering is currently unimplemented and the stencil mask is default-initialized to false. */

    /*
     * Scissor test.
     */
    if(states.scissor_test_enabled)
    {
        if(x < states.scissor_box.x_min || x >= states.scissor_box.x_max
           || y < (raster_height - states.scissor_box.y_max) || y >= (raster_height - states.scissor_box.y_min))
        {
            SWR_STATS_INCREMENT(stats_frag.discard_scissor);
            return;
        }
    }

    /*
     * Compute z and interpolated values.
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
    out.color = {0, 0, 0, 1};
    float depth_value = frag_info.depth_value;

    /*
     * set up the fragment coordinate. this should match (15.1) on p. 415 in https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     * note that we need to reverse the y-axis.
     */
    const ml::vec4 frag_coord = {
      static_cast<float>(x) - pixel_center.x,
      raster_height - (static_cast<float>(y) - pixel_center.y),
      z};

    SWR_STATS_CLOCK(stats_frag.cycles);
    auto accept_fragment = states.shader_info->shader->fragment_shader(frag_coord, frag_info.front_facing, {0, 0}, frag_info.varyings, depth_value, out.color);
    SWR_STATS_UNCLOCK(stats_frag.cycles);

    if(accept_fragment == swr::discard)
    {
        SWR_STATS_INCREMENT(stats_frag.discard_shader);
        return;
    }

    /*
     * Depth test.
     */
    if(states.depth_test_enabled)
    {
        // discard fragment if depth testing is always failing.
        if(states.depth_func == swr::comparison_func::fail)
        {
            SWR_STATS_INCREMENT(stats_frag.discard_depth);
            return;
        }

        // read and compare depth buffer.
        ml::fixed_32_t* depth_buffer_ptr = framebuffer->depth_attachment.at(x, y);

        ml::fixed_32_t old_depth_value = *depth_buffer_ptr;
        ml::fixed_32_t new_depth_value{depth_value};

        // basic comparisons for depth test.
        bool depth_compare[] = {
          true,                               /* pass */
          false,                              /* fail */
          new_depth_value == old_depth_value, /* equal */
          false,                              /* not_equal */
          new_depth_value < old_depth_value,  /* less */
          false,                              /* less_equal */
          false,                              /* greater */
          false                               /* greater_equal */
        };

        // compound comparisons for depth test.
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::not_equal)] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less)] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater_equal)] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];

        if(depth_compare[static_cast<std::uint32_t>(states.depth_func)])
        {
            // accept and possibly write depth for the fragment.
            if(states.write_depth)
            {
                *depth_buffer_ptr = new_depth_value;
            }
        }
        else
        {
            // discard fragment.
            SWR_STATS_INCREMENT(stats_frag.discard_depth);
            return;
        }
    }

    out.write_color = true;
}

#define BOOL_TO_MASK(b) (~(static_cast<std::uint32_t>(b) - 1))

#define SET_UNIFORM_BOOL_MASK(bool_mask, value) \
    bool_mask[0] = value;                       \
    bool_mask[1] = value;                       \
    bool_mask[2] = value;                       \
    bool_mask[3] = value;

#define APPLY_BOOL_MASK(bool_mask, additional_bool_mask) \
    bool_mask[0] &= additional_bool_mask[0];             \
    bool_mask[1] &= additional_bool_mask[1];             \
    bool_mask[2] &= additional_bool_mask[2];             \
    bool_mask[3] &= additional_bool_mask[3];

/** the same as above, but operates on 2x2 tiles. does not return any value. */
void sweep_rasterizer::process_fragment_block(int x, int y, const swr::impl::render_states& states, float one_over_viewport_z[4], fragment_info frag_info[4], swr::impl::fragment_output_block& out)
{
    SWR_STATS_INCREMENT2(stats_frag.count, 4);

    // initialize masks.
    bool depth_mask[4] = {states.write_depth, states.write_depth, states.write_depth, states.write_depth};
    SET_UNIFORM_BOOL_MASK(out.write_color, true);

    /* stencil buffering is currently unimplemented and the stencil mask is default-initialized to false. */

    int xx = x + 1, yy = y + 1;

    /*
     * Scissor test.
     */
    if(states.scissor_test_enabled)
    {
        auto scissor_check = [&states, this](int _x, int _y) -> bool
        { return _x >= states.scissor_box.x_min && _x < states.scissor_box.x_max && _y >= (raster_height - states.scissor_box.y_max) && _y < (raster_height - states.scissor_box.y_min); };
        bool scissor_mask[4] = {
          scissor_check(x, y), scissor_check(xx, y), scissor_check(x, yy), scissor_check(xx, yy)};

        SWR_STATS_INCREMENT2(stats_frag.discard_scissor, static_cast<uint64_t>(scissor_mask[0]) + static_cast<uint64_t>(scissor_mask[1]) + static_cast<uint64_t>(scissor_mask[2]) + static_cast<uint64_t>(scissor_mask[3]));

        if(!(scissor_mask[0] || scissor_mask[1] || scissor_mask[2] || scissor_mask[3]))
        {
            // the mask only contains 'false'.
            SET_UNIFORM_BOOL_MASK(out.write_color, false);
            SET_UNIFORM_BOOL_MASK(out.write_stencil, false);
            return;
        }

        APPLY_BOOL_MASK(depth_mask, scissor_mask);
        APPLY_BOOL_MASK(out.write_color, scissor_mask);
        APPLY_BOOL_MASK(out.write_stencil, scissor_mask);
    }

    /*
     * Compute z and interpolated values.
     */
#if defined(SWR_USE_SIMD)
    DECLARE_ALIGNED_FLOAT4(z);
    _mm_store_ps(z, _mm_div_ps(_mm_set_ps1(1.0f), _mm_set_ps(one_over_viewport_z[0], one_over_viewport_z[1], one_over_viewport_z[2], one_over_viewport_z[3])));
#else
    const ml::vec4 z = ml::vec4::one() / ml::vec4(one_over_viewport_z);
#endif

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
    std::fill_n(out.color, 4, ml::vec4{0, 0, 0, 1});
    float depth_value[4] = {
      frag_info[0].depth_value,
      frag_info[1].depth_value,
      frag_info[2].depth_value,
      frag_info[3].depth_value};

    /*
     * set up the fragment coordinate. this should match (15.1) on p. 415 in https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     * note that we need to reverse the y-axis.
     */
    const ml::vec4 frag_coord[4] = {
      {static_cast<float>(x) - pixel_center.x, raster_height - (static_cast<float>(y) - pixel_center.y), depth_value[0], z[0]},
      {static_cast<float>(xx) - pixel_center.x, raster_height - (static_cast<float>(y) - pixel_center.y), depth_value[1], z[1]},
      {static_cast<float>(x) - pixel_center.x, raster_height - (static_cast<float>(yy) - pixel_center.y), depth_value[2], z[2]},
      {static_cast<float>(xx) - pixel_center.x, raster_height - (static_cast<float>(yy) - pixel_center.y), depth_value[3], z[3]},
    };

    SWR_STATS_CLOCK(stats_frag.cycles);
    swr::fragment_shader_result accept_mask[4] = {
      states.shader_info->shader->fragment_shader(frag_coord[0], frag_info[0].front_facing, {0, 0}, frag_info[0].varyings, depth_value[0], out.color[0]),
      states.shader_info->shader->fragment_shader(frag_coord[1], frag_info[1].front_facing, {0, 0}, frag_info[1].varyings, depth_value[1], out.color[1]),
      states.shader_info->shader->fragment_shader(frag_coord[2], frag_info[2].front_facing, {0, 0}, frag_info[2].varyings, depth_value[2], out.color[2]),
      states.shader_info->shader->fragment_shader(frag_coord[3], frag_info[3].front_facing, {0, 0}, frag_info[3].varyings, depth_value[3], out.color[3])};
    SWR_STATS_UNCLOCK(stats_frag.cycles);

    static_assert(swr::fragment_shader_result::discard == 0, "swr::fragment_shader_result::discard needs to evaluate to the numerical value 0");
    if(!(accept_mask[0] || accept_mask[1] || accept_mask[2] || accept_mask[3]))
    {
        SWR_STATS_INCREMENT2(stats_frag.discard_shader, 4);
        SET_UNIFORM_BOOL_MASK(out.write_color, false);
        SET_UNIFORM_BOOL_MASK(out.write_stencil, false);
        return;
    }

    static_assert(swr::fragment_shader_result::accept == 1, "swr::fragment_shader_result::accept needs to evaluate to the numerical value 1");
    APPLY_BOOL_MASK(depth_mask, accept_mask);
    APPLY_BOOL_MASK(out.write_color, accept_mask);
    APPLY_BOOL_MASK(out.write_stencil, accept_mask);

    /*
     * Depth test.
     */
    if(states.depth_test_enabled)
    {
        // discard fragment if depth testing is always failing.
        if(states.depth_func == swr::comparison_func::fail)
        {
            SWR_STATS_INCREMENT2(stats_frag.discard_depth, 4);
            SET_UNIFORM_BOOL_MASK(out.write_color, false);
            SET_UNIFORM_BOOL_MASK(out.write_stencil, false);
            return;
        }

        // read and compare depth buffer.
        ml::fixed_32_t* depth_buffer_ptr[4];
        framebuffer->depth_attachment.at(x, y, depth_buffer_ptr);

        ml::fixed_32_t old_depth_value[4] = {*depth_buffer_ptr[0], *depth_buffer_ptr[1], *depth_buffer_ptr[2], *depth_buffer_ptr[3]};
        ml::fixed_32_t new_depth_value[4] = {frag_info[0].depth_value, frag_info[1].depth_value, frag_info[2].depth_value, frag_info[3].depth_value};

        // basic comparisons for depth test.
        bool depth_compare[][4] = {
          {true, true, true, true},                                                                                                                                                 /* pass */
          {false, false, false, false},                                                                                                                                             /* fail */
          {new_depth_value[0] == old_depth_value[0], new_depth_value[1] == old_depth_value[1], new_depth_value[2] == old_depth_value[2], new_depth_value[3] == old_depth_value[3]}, /* equal */
          {false, false, false, false},                                                                                                                                             /* not_equal */
          {new_depth_value[0] < old_depth_value[0], new_depth_value[1] < old_depth_value[1], new_depth_value[2] < old_depth_value[2], new_depth_value[3] < old_depth_value[3]},     /* less */
          {false, false, false, false},                                                                                                                                             /* less -<equal */
          {false, false, false, false},                                                                                                                                             /* greater */
          {false, false, false, false}                                                                                                                                              /* greater_equal */
        };

        // compound comparisons for depth test.
        for(int k = 0; k < 4; ++k)
        {
            depth_compare[static_cast<std::uint32_t>(swr::comparison_func::not_equal)][k] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
            depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)][k] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less)][k] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
            depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)][k] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)][k];
            depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater_equal)][k] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
        }

        bool depth_mask[4] = {
          depth_compare[static_cast<std::uint32_t>(states.depth_func)][0], depth_compare[static_cast<std::uint32_t>(states.depth_func)][1], depth_compare[static_cast<std::uint32_t>(states.depth_func)][2], depth_compare[static_cast<std::uint32_t>(states.depth_func)][3]};

        APPLY_BOOL_MASK(depth_mask, depth_mask);
        APPLY_BOOL_MASK(out.write_color, depth_mask);
        //!!fixme: set stencil mask?

        // write depth.
        if(depth_mask[0] || depth_mask[1] || depth_mask[2] || depth_mask[3])
        {
            uint32_t depth_write_mask[4] = {BOOL_TO_MASK(depth_mask[0]), BOOL_TO_MASK(depth_mask[1]), BOOL_TO_MASK(depth_mask[2]), BOOL_TO_MASK(depth_mask[3])};

            // depth conversion.
            ml::fixed_32_t write_depth[4] = {
              depth_value[0], depth_value[1], depth_value[2], depth_value[3]};

            *(depth_buffer_ptr[0]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[0])) & ~depth_write_mask[0]) | (ml::unwrap(ml::fixed_32_t{write_depth[0]}) & depth_write_mask[0]));
            *(depth_buffer_ptr[1]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[1])) & ~depth_write_mask[1]) | (ml::unwrap(ml::fixed_32_t{write_depth[1]}) & depth_write_mask[1]));
            *(depth_buffer_ptr[2]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[2])) & ~depth_write_mask[2]) | (ml::unwrap(ml::fixed_32_t{write_depth[2]}) & depth_write_mask[2]));
            *(depth_buffer_ptr[3]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[3])) & ~depth_write_mask[3]) | (ml::unwrap(ml::fixed_32_t{write_depth[3]}) & depth_write_mask[3]));
        }
    }
}

} /* namespace rast */