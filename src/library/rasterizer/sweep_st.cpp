/**
 * swr - a software rasterizer
 * 
 * software rasterizer interface implementation.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers */
#include <vector>
#include <list>
#include <unordered_map>
#include <cstring>

#include <boost/algorithm/clamp.hpp>

/* user headers. */
#include "../swr_internal.h"

#include "interpolators.h"
#include "fragment.h"
#include "sweep_st.h"

namespace rast
{

/*
 * sweep_rasterizer_single_threaded implementation.
 */

void sweep_rasterizer_single_threaded::set_dimensions(int in_width, int in_height)
{
    raster_width = in_width;
    raster_height = in_height;
}

void sweep_rasterizer_single_threaded::add_point(const swr::impl::render_states* states, const geom::vertex* vertex)
{
    draw_list.push_back(primitive(states, vertex));
}

void sweep_rasterizer_single_threaded::add_line(const swr::impl::render_states* states, const geom::vertex* v1, const geom::vertex* v2)
{
    draw_list.push_back(primitive(states, v1, v2));
}

void sweep_rasterizer_single_threaded::add_triangle(const swr::impl::render_states* states, bool is_front_facing, const geom::vertex* v1, const geom::vertex* v2, const geom::vertex* v3)
{
    draw_list.push_back(primitive(states, is_front_facing, v1, v2, v3));
}

void sweep_rasterizer_single_threaded::draw_primitives()
{
    stats_frag.reset_counters();

    for(auto& it: draw_list)
    {
        // let the (fragment-)shader know the active render states.
        it.states->shader_info->shader->update_uniforms(&it.states->uniforms);

        // draw the primitive.
        if(it.type == primitive::point)
        {
            draw_point(*it.states, *it.v[0]);
        }
        else if(it.type == primitive::line)
        {
            draw_line(*it.states, true, *it.v[0], *it.v[1]);
        }
        else if(it.type == primitive::triangle)
        {
            draw_filled_triangle(*it.states, it.is_front_facing, *it.v[0], *it.v[1], *it.v[2]);
        }
    }

    draw_list.resize(0);
}

} /* namespace rast */
