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
#include "sweep_st.h"

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
    for(auto& it: *frag_info.varyings)
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
    auto accept_fragment = states.shader_info->shader->fragment_shader(frag_coord, frag_info.front_facing, {0, 0}, *frag_info.varyings, frag_info.depth_value, color_attachments);
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

        if(states.depth_func == swr::comparison_func::pass
           || (states.depth_func == swr::comparison_func::equal && new_depth_value == old_depth_value)
           || (states.depth_func == swr::comparison_func::not_equal && new_depth_value != old_depth_value)
           || (states.depth_func == swr::comparison_func::less && new_depth_value < old_depth_value)
           || (states.depth_func == swr::comparison_func::less_equal && new_depth_value <= old_depth_value)
           || (states.depth_func == swr::comparison_func::greater && new_depth_value > old_depth_value)
           || (states.depth_func == swr::comparison_func::greater_equal && new_depth_value >= old_depth_value))
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

} /* namespace rast */