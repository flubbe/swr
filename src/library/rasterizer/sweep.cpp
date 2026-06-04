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

#include <utility>

#include "interpolators.h"
#include "fragment.h"
#include "sweep.h"

namespace rast
{

namespace
{

[[nodiscard]]
bool may_write_default_depth_buffer(
  const tile_info& primitive,
  const swr::impl::default_framebuffer* framebuffer)
{
    return primitive.states != nullptr
           && primitive.states->draw_target == framebuffer
           && framebuffer->depth_buffer.info.data_ptr
           && primitive.states->depth_test_enabled
           && primitive.states->write_depth
           && primitive.states->depth_func != swr::comparison_func::fail;
}

} /* namespace */

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

#ifdef SWR_ENABLE_MULTI_THREADING
std::size_t early_depth_policy_store::allocate_instance_id()
{
    static std::atomic<std::size_t> next_id{1};
    return next_id.fetch_add(1, std::memory_order_relaxed);
}

early_depth_policy_store::early_depth_policy_store(
  swr::impl::render_context::thread_pool_type* in_thread_pool)
: instance_id{allocate_instance_id()}
, thread_pool{in_thread_pool}
{
    assert(thread_pool);
    worker_states.resize(
      std::max<std::size_t>(
        1,
        thread_pool->get_thread_count() + 1));
}

early_depth_policy_state& early_depth_policy_store::current_state()
{
    assert(instance_id != 0);
    assert(!worker_states.empty());

    if(thread_pool->get_thread_count() <= 1)
    {
        return worker_states.front();
    }

    thread_local std::vector<
      std::pair<std::size_t, std::size_t>>
      worker_state_assignments;

    for(const auto& assignment: worker_state_assignments)
    {
        if(assignment.first == instance_id)
        {
            return worker_states[assignment.second];
        }
    }

    const std::size_t raw_state_index =
      next_worker_state.fetch_add(
        1,
        std::memory_order_relaxed);
    const std::size_t state_index =
      raw_state_index % worker_states.size();

    worker_state_assignments.emplace_back(
      instance_id,
      state_index);
    return worker_states[state_index];
}
#else
early_depth_policy_state& early_depth_policy_store::current_state()
{
    return state;
}
#endif

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
    const unsigned int tile_x = in_tile.x;
    const unsigned int tile_y = in_tile.y;
    early_depth_policy_state& early_depth_policy =
      early_depth_policies.current_state();

    /*
     * per-tile depth cache.
     */

    tile_depth_cache depth_context{
      framebuffer,
      tile_x,
      tile_y};
    block_raster_context block_context{
      .framebuffer = framebuffer,
      .tiles = tiles,
      .early_depth_policy = early_depth_policy,
      .depth_context = &depth_context};

    for(auto& it: in_tile.primitives)
    {
        bool block_was_processed = false;
        const bool may_write_depth =
          may_write_default_depth_buffer(it, framebuffer);

        if(it.mode == tile_info::rasterization_mode::block)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_processed_block_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            block_was_processed = process_block(
              tile_x,
              tile_y,
              it,
              block_context);
        }
        else if(uses_checked_lambdas(it.mode))
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_processed_checked_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            block_was_processed = process_block_checked(
              tile_x,
              tile_y,
              it,
              block_context);
        }
        else if(it.mode == tile_info::rasterization_mode::small_checked)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_processed_checked_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            assert(it.precomputed_payload_index < in_tile.primitive_small_payloads.size());

            process_block_small_checked(
              tile_x,
              tile_y,
              it,
              block_context,
              in_tile.primitive_small_payloads[it.precomputed_payload_index]);

            block_was_processed = true;
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
              tile_x,
              tile_y,
              it,
              block_context,
              payload,
              std::span<const small_triangle_quad_payload>{
                in_tile.primitive_sparse_quad_payloads.data() + payload.quad_offset,
                payload.quad_count});

            block_was_processed = true;
        }

        if(block_was_processed
           && may_write_depth)
        {
            depth_context.invalidate();
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
