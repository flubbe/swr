/**
 * swr - a software rasterizer
 *
 * software rasterizer interface implementation.
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

namespace rast
{

/*
 * sweep_rasterizer implementation.
 */

void sweep_rasterizer::add_point(
  const swr::impl::render_states* states,
  geom::vertex* vertex)
{
    draw_list.emplace_back(
      states,
      vertex);
}

void sweep_rasterizer::add_line(
  const swr::impl::render_states* states,
  geom::vertex* v1,
  geom::vertex* v2)
{
    draw_list.emplace_back(
      states,
      v1,
      v2);
}

void sweep_rasterizer::add_triangle(
  const swr::impl::render_states* states,
  bool is_front_facing,
  geom::vertex* v1,
  geom::vertex* v2,
  geom::vertex* v3)
{
    draw_list.emplace_back(
      states,
      is_front_facing,
      v1,
      v2,
      v3);
}

void sweep_rasterizer::draw_primitives()
{
    tiles.clear_shader_instances();

#ifdef SWR_ENABLE_MULTI_THREADING
    if(thread_pool->get_thread_count() > 1)
    {
        draw_primitives_parallel();
    }
    else
    {
        draw_primitives_sequential();
    }
#else
    draw_primitives_sequential();
#endif
}

void sweep_rasterizer::draw_primitives_sequential()
{
    for(auto& it: draw_list)
    {
        // draw the primitive.
        if(it.type == primitive::primitive_type::point)
        {
            draw_point(
              *it.states,
              *it.v[0]);
        }
        else if(it.type == primitive::primitive_type::line)
        {
            draw_line(
              *it.states,
              true,
              *it.v[0],
              *it.v[1]);
        }
        else if(it.type == primitive::primitive_type::triangle)
        {
            draw_filled_triangle(
              *it.states,
              it.is_front_facing,
              *it.v[0],
              *it.v[1],
              *it.v[2]);
        }

        // process tile cache.
        process_tile_cache();
    }
    draw_list.clear();
}

#ifdef SWR_ENABLE_MULTI_THREADING
void sweep_rasterizer::draw_primitives_parallel()
{
    if(!draw_list.size())
    {
        return;
    }

    std::size_t triangles_in_tile_cache = 0;
    for(auto& it: draw_list)
    {
        // if needed, process triangles to keep draw order.
        if(it.type != primitive::primitive_type::triangle
           && triangles_in_tile_cache > 0)
        {
            process_tile_cache();
            triangles_in_tile_cache = 0;
        }

        // draw the primitive.
        if(it.type == primitive::primitive_type::point)
        {
            draw_point(
              *it.states,
              *it.v[0]);
        }
        else if(it.type == primitive::primitive_type::line)
        {
            draw_line(
              *it.states,
              true,
              *it.v[0],
              *it.v[1]);
        }
        else if(it.type == primitive::primitive_type::triangle)
        {
            draw_filled_triangle(
              *it.states,
              it.is_front_facing,
              *it.v[0],
              *it.v[1],
              *it.v[2]);
            ++triangles_in_tile_cache;
        }
    }

    if(triangles_in_tile_cache > 0)
    {
        process_tile_cache();
    }

    draw_list.clear();
}

#endif /* SWR_ENABLE_MULTI_THREADING */

/*
 * tile processing.
 */

void sweep_rasterizer::process_tile(tile& in_tile)
{
    for(auto& it: in_tile.primitives)
    {
        if(it.mode == tile_info::rasterization_mode::block)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_processed_block_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            process_block(
              in_tile.x,
              in_tile.y,
              it);
        }
        else if(uses_checked_lambdas(it.mode))
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_processed_checked_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            process_block_checked(
              in_tile.x,
              in_tile.y,
              it);
        }
        else if(it.mode == tile_info::rasterization_mode::small_checked)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_processed_checked_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            assert(it.precomputed_payload_index < in_tile.primitive_small_payloads.size());
            process_block_small_checked(
              in_tile.x,
              in_tile.y,
              it,
              in_tile.primitive_small_payloads[it.precomputed_payload_index]);
        }
        else if(it.mode == tile_info::rasterization_mode::sparse_checked)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_processed_checked_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            assert(it.precomputed_payload_index < in_tile.primitive_sparse_payloads.size());
            const auto& payload = in_tile.primitive_sparse_payloads[it.precomputed_payload_index];
            assert(payload.quad_offset + payload.quad_count <= in_tile.primitive_sparse_quad_payloads.size());
            process_block_sparse_checked(
              in_tile.x,
              in_tile.y,
              it,
              payload,
              std::span<const small_triangle_quad_payload>{
                in_tile.primitive_sparse_quad_payloads.data() + payload.quad_offset,
                payload.quad_count});
        }
    }
}

#ifdef SWR_ENABLE_MULTI_THREADING

void sweep_rasterizer::process_tile_static(
  sweep_rasterizer* rasterizer,
  tile* in_tile)
{
    rasterizer->process_tile(*in_tile);
}

#endif /* SWR_ENABLE_MULTI_THREADING */

} /* namespace rast */
