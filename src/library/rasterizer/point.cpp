/**
 * swr - a software rasterizer
 *
 * point rasterization.
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
 * !!todo: Look up which values to put in dFdx, dFdy.
 */

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

    /*
     * A point is rastered as two triangles in a Z pattern, and triangle fill rules are applied.
     * It is sufficient to get the nearest pixel center and check if it is
     *
     *  (i) completely inside the triangles
     * (ii) if not, check if it is on the top or left edge of the upper triangle.
     */

    int x = ml::integral_part(adjusted_coords.x - cnl::wrap<ml::vec2_fixed<4>::type>(FILL_RULE_EDGE_BIAS));
    int y = ml::integral_part(adjusted_coords.y - cnl::wrap<ml::vec2_fixed<4>::type>(FILL_RULE_EDGE_BIAS));

    if(x >= 0 && x < states.draw_target->properties.width
       && y >= 0 && y < states.draw_target->properties.height)
    {
        // set up attributes and varyings.
        boost::container::static_vector<swr::varying, geom::limits::max::varyings> temp(v.varyings.size());
        for(size_t i = 0; i < v.varyings.size(); ++i)
        {
            temp[i].value = v.varyings[i];
            temp[i].dFdx = ml::vec4::zero();    // !!fixme: see comment above.
            temp[i].dFdy = ml::vec4::zero();    // !!fixme: see comment above.
        }

        // create shader instance.
        std::vector<std::byte> shader_storage{states.shader_info->shader->size()};
        swr::program_base* shader = states.shader_info->shader->create_fragment_shader_instance(shader_storage.data(), states.uniforms, states.texture_2d_samplers);

        // draw the point.
        rast::fragment_info info(v.coords.z, true, temp);
        swr::impl::fragment_output out;

        process_fragment(x, y, states, shader, v.coords.w, info, out);
        states.draw_target->merge_color(0, x, y, out, states.blending_enabled, states.blend_src, states.blend_dst);
    }
}

} /* namespace rast */
