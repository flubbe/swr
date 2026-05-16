/**
 * swr - a software rasterizer
 *
 * point rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "../swr_internal.h"

#include "interpolators.h"
#include "fragment.h"
#include "sweep.h"
#include "point.h"

namespace rast
{

void sweep_rasterizer::draw_point(const swr::impl::render_states& states, const geom::vertex& v)
{
    /*
     * a note on the pixel center adjustment:
     *
     * here we use it to find the correct position for the fragment as an integer (x,y)-coordinate.
     * by convention, the fragment's coordinate are then adjusted by the pixel center again in process_fragment.
     */

    const auto coords = v.coords.xy();
    ml::vec2_fixed<4> adjusted_coords(coords.x, coords.y);

    // set up attributes and varyings.
    boost::container::static_vector<swr::varying, swr::limits::max::varyings> temp(v.varyings.size());
    for(std::size_t i = 0; i < v.varyings.size(); ++i)
    {
        temp[i].value = v.varyings[i];

        /*
         * According to GLSL 4.6 spec, §8.14.1 [1]
         *
         * GLSL derivatives are computed using local differencing between neighboring
         * fragments, and implementations may use approximations. For point primitives,
         * neighboring fragments required for differencing may be unavailable or not
         * well-constrained, so exact derivative behavior is implementation-dependent.
         *
         * This rasterizer chooses to return zero derivatives for points.
         *
         * Reference
         * ---------
         * [1] https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html#fragment-processing-functions
         */

        temp[i].dFdx = ml::vec4::zero();
        temp[i].dFdy = ml::vec4::zero();
    }

    // create shader instance.
    swr::impl::fragment_shader_instance_container shader_instance{
      states.shader_info,
      states.uniforms,
      states.texture_2d_samplers};
    const swr::program_base* shader = shader_instance.get();

    rast::fragment_info info(v.coords.z, true, temp);
    swr::impl::fragment_output out;

    for_each_covered_point_pixel(
      adjusted_coords,
      states.draw_target->properties.width,
      states.draw_target->properties.height,
      [&](auto x, auto y)
      {
          process_fragment(x, y, states, shader, v.coords.w, info, out);
          states.draw_target->merge_color(
            0,
            x,
            y,
            out,
            states.blending_enabled,
            states.blend_src,
            states.blend_dst);
      });
}

} /* namespace rast */
