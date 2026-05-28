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

namespace
{

struct tile_depth_range_cache
{
    swr::impl::default_framebuffer* framebuffer{nullptr};
    int x{0};
    int y{0};
    bool valid{false};
    depth_range range{};
};

const depth_range& get_or_compute_depth_range(
  void* context)
{
    assert(context);

    auto& cache = *static_cast<tile_depth_range_cache*>(context);
    const auto& depth_buffer = cache.framebuffer->depth_buffer;
    const int x0 = cache.x;
    const int y0 = cache.y;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_raster_stored_depth_range_requests.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if(cache.valid)
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        swr::impl::profile_raster_stored_depth_range_hits.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        return cache.range;
    }

    const int width =
      std::max(
        0,
        std::min<int>(
          swr::impl::rasterizer_block_size,
          depth_buffer.info.width - x0));
    const int height =
      std::max(
        0,
        std::min<int>(
          swr::impl::rasterizer_block_size,
          depth_buffer.info.height - y0));

    if(width <= 0 || height <= 0)
    {
        cache.range = {0, 0};
        cache.valid = true;
        return cache.range;
    }

    constexpr int depth_value_size =
      static_cast<int>(sizeof(swr::impl::attachment_depth::value_type));
    const int row_stride = depth_buffer.info.pitch / depth_value_size;
    ml::fixed_32_t min_z = depth_buffer.info.data_ptr[y0 * row_stride + x0];
    ml::fixed_32_t max_z = min_z;

    for(int y = 0; y < height; ++y)
    {
        const int row = (y0 + y) * row_stride + x0;
        for(int x = 0; x < width; ++x)
        {
            const ml::fixed_32_t z = depth_buffer.info.data_ptr[row + x];
            min_z = std::min(min_z, z);
            max_z = std::max(max_z, z);
        }
    }

    cache.range = {min_z, max_z};
    cache.valid = true;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_raster_stored_depth_range_computes.fetch_add(1, std::memory_order_relaxed);
    swr::impl::profile_raster_stored_depth_range_hits.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    return cache.range;
}

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

    /*
     * per-tile depth cache.
     */

    tile_depth_range_cache depth_range_cache{
      .framebuffer = framebuffer,
      .x = static_cast<int>(tile_x),
      .y = static_cast<int>(tile_y)};
    const depth_range_provider tile_depth_range_provider =
      {get_or_compute_depth_range, &depth_range_cache};

    for(auto& it: in_tile.primitives)
    {
        bool block_was_processed = false;

        if(it.mode == tile_info::rasterization_mode::block)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_processed_block_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            block_was_processed = process_block(
              tile_x,
              tile_y,
              it,
              &tile_depth_range_provider);
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
              &tile_depth_range_provider);
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
              payload,
              std::span<const small_triangle_quad_payload>{
                in_tile.primitive_sparse_quad_payloads.data() + payload.quad_offset,
                payload.quad_count});

            block_was_processed = true;
        }

        if(block_was_processed
           && may_write_default_depth_buffer(it, framebuffer))
        {
            depth_range_cache.valid = false;
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
