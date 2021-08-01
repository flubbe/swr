/**
 * swr - a software rasterizer
 * 
 * software rasterizer interface implementation.
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
 * sweep_rasterizer implementation.
 */

void sweep_rasterizer::add_point(const swr::impl::render_states* states, const geom::vertex* vertex)
{
    draw_list.push_back({states, vertex});
}

void sweep_rasterizer::add_line(const swr::impl::render_states* states, const geom::vertex* v1, const geom::vertex* v2)
{
    draw_list.push_back({states, v1, v2});
}

void sweep_rasterizer::add_triangle(const swr::impl::render_states* states, bool is_front_facing, const geom::vertex* v1, const geom::vertex* v2, const geom::vertex* v3)
{
    draw_list.push_back({states, is_front_facing, v1, v2, v3});
}

void sweep_rasterizer::draw_primitives()
{
#ifdef SWR_ENABLE_STATS
    stats_frag.reset_counters();
    stats_rast.reset_counters();
#endif

#ifdef SWR_ENABLE_MULTI_THREADING
    if(rasterizer_threads.get_thread_count() > 1)
    {
        draw_primitives_parallel();
    }
    else
    {
        draw_primitives_sequentially();
    }
#else
    draw_primitives_sequentially();
#endif
}

void sweep_rasterizer::draw_primitives_sequentially()
{
    for(auto& it: draw_list)
    {
        // let the (fragment-)shader know the active render states.
        it.states->shader_info->shader->update_uniforms(&it.states->uniforms);
        it.states->shader_info->shader->update_samplers(&it.states->texture_2d_samplers);

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

        // process tile cache.
        process_tile_cache();
    }
    draw_list.clear();
}

#ifdef SWR_ENABLE_MULTI_THREADING
void sweep_rasterizer::draw_primitives_parallel()
{
    swr::comparison_func last_depth_func = draw_list.size() ? draw_list[0].states->depth_func : swr::comparison_func::less;

    for(auto& it: draw_list)
    {
        // let the (fragment-)shader know the active render states.
        it.states->shader_info->shader->update_uniforms(&it.states->uniforms);
        it.states->shader_info->shader->update_samplers(&it.states->texture_2d_samplers);

        /*
         * check if we need to draw the triangles in the queue. this is the case if:
         * 
         *  *) the depth test is disabled or has changed, or
         *  *) blending is enabled.
         * 
         * since currently only triangles are processed in parallel, we also need
         * to execute the draw calls before drawing any other primitive.
         */
        if(it.type != primitive::triangle)
        {
            process_tile_cache();
        }
        else if(!it.states->depth_test_enabled || it.states->blending_enabled)
        {
            process_tile_cache();
        }
        else if(it.states->depth_test_enabled && last_depth_func != it.states->depth_func)
        {
            // if the depth buffer mode changed, ensure that all depth operations have finished.
            process_tile_cache();
            last_depth_func = it.states->depth_func;
        }

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

    // run possibly waiting tasks.
    process_tile_cache();
    clear_tile_cache();

    draw_list.clear();
}

void sweep_rasterizer::process_tile_static(sweep_rasterizer* rasterizer, tile* in_tile)
{
    rasterizer->process_tile(*in_tile);
}

#endif /* SWR_ENABLE_MULTI_THREADING */

} /* namespace rast */
