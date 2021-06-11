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

#include "../output_merger.h"

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
bool sweep_rasterizer::process_fragment(int x, int y, const swr::impl::render_states& states, float one_over_viewport_z, fragment_info& frag_info)
{
    SWR_STATS_INCREMENT(stats_frag.count);

    /*
     * Scissor test.
     */
    if(states.scissor_test_enabled)
    {
        if(x < states.scissor_box.x_min || x >= states.scissor_box.x_max
           || y < (raster_height - states.scissor_box.y_max) || y >= (raster_height - states.scissor_box.y_min))
        {
            SWR_STATS_INCREMENT(stats_frag.discard_scissor);
            return false;
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
    // !!fixme: currently, we only have the default framebuffer at position 0.
    boost::container::static_vector<ml::vec4, swr::max_color_attachments> color_attachments;
    color_attachments.push_back({0, 0, 0, 1});

    /*
     * set up the fragment coordinate. this should match (15.1) on p. 415 in https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     * note that we need to reverse the y-axis.
     */
    const ml::vec4 frag_coord = {
      static_cast<float>(x) - pixel_center.x,
      raster_height - (static_cast<float>(y) - pixel_center.y),
      frag_info.depth_value,
      z};

    SWR_STATS_CLOCK(stats_frag.cycles);
    auto accept_fragment = states.shader_info->shader->fragment_shader(frag_coord, frag_info.front_facing, {0, 0}, frag_info.varyings, frag_info.depth_value, color_attachments);
    SWR_STATS_UNCLOCK(stats_frag.cycles);

    if(accept_fragment == swr::discard)
    {
        SWR_STATS_INCREMENT(stats_frag.discard_shader);
        return false;
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
            return false;
        }

        // read and compare depth buffer.
        ml::fixed_32_t* depth_buffer_ptr = depth_buffer->at(x, y);

        ml::fixed_32_t old_depth_value = *depth_buffer_ptr;
        ml::fixed_32_t new_depth_value{frag_info.depth_value};

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
                *depth_buffer_ptr = frag_info.depth_value;
            }
        }
        else
        {
            // discard fragment.
            SWR_STATS_INCREMENT(stats_frag.discard_depth);
            return false;
        }
    }

    /*
     * Output merging.
     * 
     * get color values:
     */
    uint32_t out_color = color_buffer->pf_conv.to_pixel(ml::clamp_to_unit_interval(color_attachments[0]));

    /*
     * get color buffer pointer.
     */
    uint32_t* color_buffer_ptr = color_buffer->at(x, y);

    /*
     * Alpha blending.
     */
    if(states.blending_enabled)
    {
        SWR_STATS_INCREMENT(stats_frag.blending);
        out_color = swr::output_merger::blend(color_buffer->pf_conv, states, *color_buffer_ptr, out_color);
    }

    /*
     * write color buffer.
     */
    *color_buffer_ptr = out_color;

    return true;
}

/** the same as above, but operates on 2x2 tiles. does not return any value. */
void sweep_rasterizer::process_fragment_2x2(int x, int y, const swr::impl::render_states& states, float one_over_viewport_z[4], fragment_info frag_info[4])
{
    SWR_STATS_INCREMENT2(stats_frag.count, 4);

    int xx = x + 1, yy = y + 1;

    uint32_t write_mask[4] = {~static_cast<uint32_t>(0), ~static_cast<uint32_t>(0), ~static_cast<uint32_t>(0), ~static_cast<uint32_t>(0)};

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
            return;
        }

        uint32_t scissor_write_mask[4] = {
          ~(static_cast<uint32_t>(scissor_mask[0]) - 1),
          ~(static_cast<uint32_t>(scissor_mask[1]) - 1),
          ~(static_cast<uint32_t>(scissor_mask[2]) - 1),
          ~(static_cast<uint32_t>(scissor_mask[3]) - 1)};

        write_mask[0] &= scissor_write_mask[0];
        write_mask[1] &= scissor_write_mask[1];
        write_mask[2] &= scissor_write_mask[2];
        write_mask[3] &= scissor_write_mask[3];
    }

    /*
     * Compute z and interpolated values.
     */
    __m128 sse_z = _mm_div_ps(_mm_set_ps1(1.0f), _mm_set_ps(one_over_viewport_z[0], one_over_viewport_z[1], one_over_viewport_z[2], one_over_viewport_z[3]));
    float z[4] __attribute__((aligned(16)));    //!!fixme: GNU
    _mm_store_ps(z, sse_z);

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
    // !!fixme: currently, we only have the default framebuffer at position 0.
    boost::container::static_vector<ml::vec4, swr::max_color_attachments> color_attachments[4];
    color_attachments[0].push_back({0, 0, 0, 1});
    color_attachments[1].push_back({0, 0, 0, 1});
    color_attachments[2].push_back({0, 0, 0, 1});
    color_attachments[3].push_back({0, 0, 0, 1});

    /*
     * set up the fragment coordinate. this should match (15.1) on p. 415 in https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     * note that we need to reverse the y-axis.
     */
    const ml::vec4 frag_coord[4] = {
      {static_cast<float>(x) - pixel_center.x, raster_height - (static_cast<float>(y) - pixel_center.y), frag_info[0].depth_value, z[0]},
      {static_cast<float>(xx) - pixel_center.x, raster_height - (static_cast<float>(y) - pixel_center.y), frag_info[1].depth_value, z[1]},
      {static_cast<float>(x) - pixel_center.x, raster_height - (static_cast<float>(yy) - pixel_center.y), frag_info[2].depth_value, z[2]},
      {static_cast<float>(xx) - pixel_center.x, raster_height - (static_cast<float>(yy) - pixel_center.y), frag_info[3].depth_value, z[3]},
    };

    SWR_STATS_CLOCK(stats_frag.cycles);
    swr::fragment_shader_result accept_mask[4] = {
      states.shader_info->shader->fragment_shader(frag_coord[0], frag_info[0].front_facing, {0, 0}, frag_info[0].varyings, frag_info[0].depth_value, color_attachments[0]),
      states.shader_info->shader->fragment_shader(frag_coord[1], frag_info[1].front_facing, {0, 0}, frag_info[1].varyings, frag_info[1].depth_value, color_attachments[1]),
      states.shader_info->shader->fragment_shader(frag_coord[2], frag_info[2].front_facing, {0, 0}, frag_info[2].varyings, frag_info[2].depth_value, color_attachments[2]),
      states.shader_info->shader->fragment_shader(frag_coord[3], frag_info[3].front_facing, {0, 0}, frag_info[3].varyings, frag_info[3].depth_value, color_attachments[3])};
    SWR_STATS_UNCLOCK(stats_frag.cycles);

    if(accept_mask[0] == swr::discard && accept_mask[1] == swr::discard && accept_mask[2] == swr::discard && accept_mask[3] == swr::discard)
    {
        SWR_STATS_INCREMENT2(stats_frag.discard_shader, 4);
        return;
    }

    uint32_t accept_write_mask[4] = {
      ~(static_cast<uint32_t>(accept_mask[0]) - 1),
      ~(static_cast<uint32_t>(accept_mask[1]) - 1),
      ~(static_cast<uint32_t>(accept_mask[2]) - 1),
      ~(static_cast<uint32_t>(accept_mask[3]) - 1)};

    write_mask[0] &= accept_write_mask[0];
    write_mask[1] &= accept_write_mask[1];
    write_mask[2] &= accept_write_mask[2];
    write_mask[3] &= accept_write_mask[3];

    /*
     * Depth test.
     */
    if(states.depth_test_enabled)
    {
        // discard fragment if depth testing is always failing.
        if(states.depth_func == swr::comparison_func::fail)
        {
            SWR_STATS_INCREMENT2(stats_frag.discard_depth, 4);
            return;
        }

        // read and compare depth buffer.
        ml::fixed_32_t* depth_buffer_ptr[4] = {depth_buffer->at(x, y), depth_buffer->at(xx, y), depth_buffer->at(x, yy), depth_buffer->at(xx, yy)};

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

        uint32_t depth_write_mask[4] = {
          ~(static_cast<uint32_t>(depth_mask[0]) - 1),
          ~(static_cast<uint32_t>(depth_mask[1]) - 1),
          ~(static_cast<uint32_t>(depth_mask[2]) - 1),
          ~(static_cast<uint32_t>(depth_mask[3]) - 1)};

        write_mask[0] &= depth_write_mask[0];
        write_mask[1] &= depth_write_mask[1];
        write_mask[2] &= depth_write_mask[2];
        write_mask[3] &= depth_write_mask[3];

        if(states.write_depth)
        {
            *(depth_buffer_ptr[0]) = ml::wrap((ml::unwrap(ml::fixed_32_t{frag_info[0].depth_value}) & depth_write_mask[0]) | (ml::unwrap(*(depth_buffer_ptr[0])) & ~depth_write_mask[0]));
            *(depth_buffer_ptr[1]) = ml::wrap((ml::unwrap(ml::fixed_32_t{frag_info[1].depth_value}) & depth_write_mask[1]) | (ml::unwrap(*(depth_buffer_ptr[1])) & ~depth_write_mask[1]));
            *(depth_buffer_ptr[2]) = ml::wrap((ml::unwrap(ml::fixed_32_t{frag_info[2].depth_value}) & depth_write_mask[2]) | (ml::unwrap(*(depth_buffer_ptr[2])) & ~depth_write_mask[2]));
            *(depth_buffer_ptr[3]) = ml::wrap((ml::unwrap(ml::fixed_32_t{frag_info[3].depth_value}) & depth_write_mask[3]) | (ml::unwrap(*(depth_buffer_ptr[3])) & ~depth_write_mask[3]));
        }

        auto depth_pass_count = static_cast<int>(depth_mask[0]) + static_cast<int>(depth_mask[1]) + static_cast<int>(depth_mask[2]) + static_cast<int>(depth_mask[3]);
        SWR_STATS_INCREMENT2(stats_frag.discard_depth, 4 - depth_pass_count);

        if(!depth_pass_count)
        {
            return;
        }
    }

    /*
     * Output merging.
     * 
     * get color values:
     */
    uint32_t out_color[4] = {
      color_buffer->pf_conv.to_pixel(ml::clamp_to_unit_interval(color_attachments[0][0])),
      color_buffer->pf_conv.to_pixel(ml::clamp_to_unit_interval(color_attachments[1][0])),
      color_buffer->pf_conv.to_pixel(ml::clamp_to_unit_interval(color_attachments[2][0])),
      color_buffer->pf_conv.to_pixel(ml::clamp_to_unit_interval(color_attachments[3][0]))};

    /*
     * get color buffer pointer.
     */
    uint32_t* color_buffer_ptr[4] = {color_buffer->at(x, y), color_buffer->at(xx, y), color_buffer->at(x, yy), color_buffer->at(xx, yy)};

    /*
     * Alpha blending.
     */
    if(states.blending_enabled)
    {
        SWR_STATS_INCREMENT(stats_frag.blending);
        out_color[0] = swr::output_merger::blend(color_buffer->pf_conv, states, *color_buffer_ptr[0], out_color[0]);
        out_color[1] = swr::output_merger::blend(color_buffer->pf_conv, states, *color_buffer_ptr[1], out_color[1]);
        out_color[2] = swr::output_merger::blend(color_buffer->pf_conv, states, *color_buffer_ptr[2], out_color[2]);
        out_color[3] = swr::output_merger::blend(color_buffer->pf_conv, states, *color_buffer_ptr[3], out_color[3]);
    }

    /*
     * write color buffer.
     */
    *(color_buffer_ptr[0]) = (out_color[0] & write_mask[0]) | (*(color_buffer_ptr[0]) & ~write_mask[0]);
    *(color_buffer_ptr[1]) = (out_color[1] & write_mask[1]) | (*(color_buffer_ptr[1]) & ~write_mask[1]);
    *(color_buffer_ptr[2]) = (out_color[2] & write_mask[2]) | (*(color_buffer_ptr[2]) & ~write_mask[2]);
    *(color_buffer_ptr[3]) = (out_color[3] & write_mask[3]) | (*(color_buffer_ptr[3]) & ~write_mask[3]);
}

} /* namespace rast */