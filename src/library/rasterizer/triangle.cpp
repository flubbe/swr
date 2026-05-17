/**
 * swr - a software rasterizer
 *
 * triangle rasterization.
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
#include "triangle.h"
#include "block.h"

namespace rast
{

void sweep_rasterizer::process_block(unsigned int block_x, unsigned int block_y, tile_info& in_data)
{
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_block_total = 0;
    std::uint64_t stage_block_fragment = 0;
    std::uint64_t stage_block_merge = 0;
    utils::clock(stage_block_total);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    std::array<
      boost::container::static_vector<
        swr::varying,
        swr::limits::max::varyings>,
      4>
      temp_varyings;
    assert(in_data.attributes);
    auto& attributes = *in_data.attributes;
    attributes.setup_block_processing();
    const std::size_t varying_count = attributes.varyings.size();
    temp_varyings[0].resize(varying_count);
    temp_varyings[1].resize(varying_count);
    temp_varyings[2].resize(varying_count);
    temp_varyings[3].resize(varying_count);

    const bool front_facing = in_data.front_facing;

    ml::vec4 frag_depth;
    ml::vec4 one_over_viewport_z;
    swr::impl::fragment_output_block out;

    const swr::program_base* shader = tiles.entries[(block_y >> swr::impl::rasterizer_block_shift) * tiles.pitch + (block_x >> swr::impl::rasterizer_block_shift)]
                                        .shader_instances[in_data.shader_index]
                                        .shader;

    for_each_quad_in_triangle_block(
      block_x,
      block_y,
      attributes,
      [&](unsigned int x,
          unsigned int y,
          rast::triangle_interpolator& attributes_quad)
      {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
          std::uint64_t stage_interp = 0;
          utils::clock(stage_interp);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

          attributes_quad.get_data_block(
            temp_varyings,
            frag_depth,
            one_over_viewport_z);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
          utils::unclock(stage_interp);
          swr::impl::profile_interp_cycles.fetch_add(stage_interp, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

          std::array<rast::fragment_info, 4> frag_info =
            {{{frag_depth[0], front_facing, temp_varyings[0]},
              {frag_depth[1], front_facing, temp_varyings[1]},
              {frag_depth[2], front_facing, temp_varyings[2]},
              {frag_depth[3], front_facing, temp_varyings[3]}}};

#ifdef SWR_ENABLE_PIPELINE_PROFILING
          std::uint64_t stage_fragment_block = 0;
          utils::clock(stage_fragment_block);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

          process_fragment_block(
            x, y,
            *in_data.states,
            shader,
            one_over_viewport_z,
            frag_info,
            out);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
          utils::unclock(stage_fragment_block);
          stage_block_fragment += stage_fragment_block;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

#ifdef SWR_ENABLE_PIPELINE_PROFILING
          std::uint64_t stage_merge_block = 0;
          utils::clock(stage_merge_block);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

          if(out.write_color)
          {
              in_data.states->draw_target->merge_color_block(
                0,
                x, y,
                out,
                in_data.states->blending_enabled,
                in_data.states->blend_src,
                in_data.states->blend_dst);
          }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
          utils::unclock(stage_merge_block);
          stage_block_merge += stage_merge_block;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
      });

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_block_total);
    swr::impl::profile_raster_block_total_cycles.fetch_add(stage_block_total, std::memory_order_relaxed);
    swr::impl::profile_raster_block_fragment_cycles.fetch_add(stage_block_fragment, std::memory_order_relaxed);
    swr::impl::profile_raster_block_merge_cycles.fetch_add(stage_block_merge, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

void sweep_rasterizer::process_block_checked(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& in_data)
{
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_block_total = 0;
    std::uint64_t stage_block_fragment = 0;
    std::uint64_t stage_block_merge = 0;
    utils::clock(stage_block_total);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    std::array<
      boost::container::static_vector<
        swr::varying,
        swr::limits::max::varyings>,
      4>
      temp_varyings;

    assert(in_data.attributes);
    auto& attributes = *in_data.attributes;
    attributes.setup_block_processing();

    const std::size_t varying_count = attributes.varyings.size();
    temp_varyings[0].resize(varying_count);
    temp_varyings[1].resize(varying_count);
    temp_varyings[2].resize(varying_count);
    temp_varyings[3].resize(varying_count);

    const bool front_facing = in_data.front_facing;

    ml::vec4 frag_depth;
    ml::vec4 one_over_viewport_z;
    swr::impl::fragment_output_block out;

    const swr::program_base* shader = tiles.entries[(block_y >> swr::impl::rasterizer_block_shift) * tiles.pitch + (block_x >> swr::impl::rasterizer_block_shift)]
                                        .shader_instances[in_data.shader_index]
                                        .shader;
    assert(in_data.checked_lambdas);

    auto process_checked_quad =
      [&](int x,
          int y,
          int mask,
          rast::triangle_interpolator& attributes_quad)
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_interp = 0;
        utils::clock(stage_interp);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        attributes_quad.get_data_block(
          temp_varyings,
          frag_depth,
          one_over_viewport_z);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_interp);
        swr::impl::profile_interp_cycles.fetch_add(stage_interp, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        std::array<rast::fragment_info, 4> frag_info =
          {{{frag_depth[0], front_facing, temp_varyings[0]},
            {frag_depth[1], front_facing, temp_varyings[1]},
            {frag_depth[2], front_facing, temp_varyings[2]},
            {frag_depth[3], front_facing, temp_varyings[3]}}};

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_fragment_block = 0;
        utils::clock(stage_fragment_block);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if(mask == 0xF)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_checked_full_mask_quads.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            process_fragment_block(
              x,
              y,
              *in_data.states,
              shader,
              one_over_viewport_z,
              frag_info,
              out);
        }
        else
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_checked_partial_mask_quads.fetch_add(1, std::memory_order_relaxed);
            const unsigned int bit_count =
              ((mask & 8) ? 1u : 0u)
              + ((mask & 4) ? 1u : 0u)
              + ((mask & 2) ? 1u : 0u)
              + ((mask & 1) ? 1u : 0u);
            if(bit_count == 1u)
            {
                swr::impl::profile_checked_partial_pop1_quads.fetch_add(1, std::memory_order_relaxed);
            }
            else if(bit_count == 2u)
            {
                swr::impl::profile_checked_partial_pop2_quads.fetch_add(1, std::memory_order_relaxed);
            }
            else if(bit_count == 3u)
            {
                swr::impl::profile_checked_partial_pop3_quads.fetch_add(1, std::memory_order_relaxed);
            }
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            process_fragment_block(
              x,
              y,
              mask,
              *in_data.states,
              shader,
              one_over_viewport_z,
              frag_info,
              out);
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_fragment_block);
        stage_block_fragment += stage_fragment_block;

        std::uint64_t stage_merge_block = 0;
        utils::clock(stage_merge_block);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if(out.write_color)
        {
            in_data.states->draw_target->merge_color_block(
              0,
              x,
              y,
              out,
              in_data.states->blending_enabled,
              in_data.states->blend_src,
              in_data.states->blend_dst);
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_merge_block);
        stage_block_merge += stage_merge_block;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    }; /* process_checked_quad */

    for_each_covered_quad_in_checked_triangle_block(
      block_x,
      block_y,
      in_data.checked_quad_bounds,
      *in_data.checked_lambdas,
      attributes,
      process_checked_quad);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_block_total);
    swr::impl::profile_raster_block_total_cycles.fetch_add(stage_block_total, std::memory_order_relaxed);
    swr::impl::profile_raster_block_fragment_cycles.fetch_add(stage_block_fragment, std::memory_order_relaxed);
    swr::impl::profile_raster_block_merge_cycles.fetch_add(stage_block_merge, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

/**
 * Apply depth offset to triangle vertices.
 *
 * FIXME We do the setup for floating-point depth buffers here, but we probably want the fixed-point version.
 *
 * Ref: https://registry.khronos.org/OpenGL/specs/gl/glspec43.core.pdf, Section 14.6.5.
 */
static float setup_polygon_offset(
  const swr::impl::render_states& states,
  const geom::vertex& v1,
  const geom::vertex& v2,
  const geom::vertex& v3,
  float inv_area)
{
    ml::vec3 edges[2] = {
      (v2.coords - v1.coords).xyz(),
      (v3.coords - v1.coords).xyz()};    // edges in window coordinates
    ml::vec2 dz = ml::vec2{
                    edges[1].z * edges[0].y - edges[0].z * edges[1].y,
                    -edges[1].z * edges[0].x + edges[0].z * edges[1].x}
                  * inv_area;

#ifdef __GNUC__
    float m = std::max(fabsf(dz.x), fabsf(dz.y));    // Eq. (14.12)
#else
    float m = std::max(std::fabsf(dz.x), std::fabsf(dz.y));    // Eq. (14.12)
#endif

    /*
     * https://registry.khronos.org/OpenGL/specs/gl/glspec43.core.pdf, Section 14.6.5,
     * on floating-point depth buffers:
     *
     *     "In this case, the minimum resolvable difference for a given polygon is
     *      dependent on the maximum exponent, e, in the range of z values spanned
     *      by the primitive. If n is the number of bits in the floating-point mantissa,
     *      the minimum resolvable difference, r, for the given primitive is defined as
     *      r = 2^(e−n)."
     *
     * A 32-bit float has a 23-bit mantissa.
     */
    union float_integer
    {
        float f;
        std::int32_t i;
        std::uint32_t ui;

        float_integer(float in_f)
        : f{in_f}
        {
        }
    };
    // get the maximum exponent in the range of the z values spanned by the primitive
#ifdef __GNUC__
    float_integer r{
      std::max({fabsf(v1.coords.z), fabsf(v2.coords.z), fabsf(v3.coords.z)})};
#else
    float_integer r{
      std::max({std::fabsf(v1.coords.z), std::fabsf(v2.coords.z), std::fabsf(v3.coords.z)})};
#endif
    r.i &= 0xff << 23;

    // calculate r by subtracting the size of mantissa from exponent
    r.ui -= 23 << 23;

    // clamp to zero (this means no resolvable depth offset for very small numbers)
    r.i = std::max(r.i, 0);

    return m * states.polygon_offset_factor + r.f * states.polygon_offset_units;    // Eq. (14.13)
}

void sweep_rasterizer::draw_filled_triangle(
  const swr::impl::render_states& states,
  bool is_front_facing,
  const geom::vertex& v0,
  const geom::vertex& v1,
  const geom::vertex& v2)
{
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_raster_setup = 0;
    std::uint64_t stage_setup_triangle = 0;
    std::uint64_t stage_setup_bounds = 0;
    std::uint64_t stage_setup_iterate = 0;
    std::uint64_t stage_setup_direct = 0;
    std::uint64_t stage_setup_enqueue = 0;
    std::uint64_t stage_cb_flush_inline = 0;

    utils::clock(stage_raster_setup);
    utils::clock(stage_setup_triangle);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    triangle_info info = setup_triangle(v0, v1, v2);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_setup_triangle);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if(info.is_degenerate)
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_raster_setup);
        swr::impl::profile_raster_setup_cycles.fetch_add(stage_raster_setup, std::memory_order_relaxed);
        swr::impl::profile_raster_setup_triangle_cycles.fetch_add(stage_setup_triangle, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        return;
    }

    float polygon_offset = 0.f;
    if(states.polygon_offset_fill_enabled)
    {
        polygon_offset = setup_polygon_offset(states, v0, v1, v2, info.inv_area);
    }

    const bool y_needs_flip = states.draw_target == framebuffer;

#ifdef SWR_ENABLE_MULTI_THREADING
    const bool has_pending_tile_work = !tiles.active_tile_indices.empty();
    const bool allow_direct_block_path =
      !thread_pool
      || thread_pool->get_thread_count() <= 1
      || !has_pending_tile_work;
#else
    const bool allow_direct_block_path = true;
#endif

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::clock(stage_setup_bounds);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    const bounding_box bounds = compute_triangle_bounds(
      states,
      info,
      y_needs_flip);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_setup_bounds);
    utils::clock(stage_setup_iterate);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    const bool is_single_block_triangle =
      allow_direct_block_path
      && (bounds.end_x - bounds.start_x) == static_cast<int>(swr::impl::rasterizer_block_size)
      && (bounds.end_y - bounds.start_y) == static_cast<int>(swr::impl::rasterizer_block_size);

    std::span<const ml::vec4> base_varyings{
      v0.varyings.data(),
      v0.varyings.size()};
    if(states.shader_info->uses_flat_varyings()
       && v0.flat_varying_ref != nullptr)
    {
        base_varyings = {
          v0.flat_varying_ref,
          states.shader_info->varying_count};
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t triangle_tile_ref_count = 0;
    std::uint64_t triangle_block_tile_ref_count = 0;
    std::uint64_t triangle_checked_tile_ref_count = 0;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    for_each_covered_triangle_block_with_bounds(
      states,
      bounds,
      info,
      base_varyings,
      polygon_offset,
      [&](int x,
          int y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes_row,
          tile_info::rasterization_mode mode)
      {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
          std::uint64_t stage_add_triangle = 0;
          utils::clock(stage_add_triangle);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

          const quad_bounds checked_quad_bounds = [&]() -> quad_bounds
          {
              if(mode == tile_info::rasterization_mode::checked)
              {
                  auto computed_bounds = compute_checked_quad_bounds(
                    bounds,
                    x,
                    y);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
                  if(mode == tile_info::rasterization_mode::checked)
                  {
                      const unsigned int checked_width = computed_bounds.end_x - computed_bounds.start_x;
                      const unsigned int checked_height = computed_bounds.end_y - computed_bounds.start_y;
                      if(checked_width <= 2 && checked_height > 2)
                      {
                          swr::impl::profile_checked_sparse_thin_x_primitives.fetch_add(1, std::memory_order_relaxed);
                      }
                      else if(checked_height <= 2 && checked_width > 2)
                      {
                          swr::impl::profile_checked_sparse_thin_y_primitives.fetch_add(1, std::memory_order_relaxed);
                      }
                  }
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

                  return computed_bounds;
              }

              return full_block_quad_bounds(
                static_cast<unsigned int>(x),
                static_cast<unsigned int>(y));
          }();

          bool needs_flush = false;
          const bool use_direct_path_for_block =
            (is_single_block_triangle && allow_direct_block_path);

          if(use_direct_path_for_block)
          {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
              std::uint64_t stage_direct = 0;
              utils::clock(stage_direct);
              swr::impl::profile_raster_direct_blocks.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

              const std::size_t tile_index =
                (static_cast<unsigned int>(y) >> swr::impl::rasterizer_block_shift) * tiles.pitch
                + (static_cast<unsigned int>(x) >> swr::impl::rasterizer_block_shift);
              assert(tile_index < tiles.entries.size());

              auto& tile = tiles.entries[tile_index];
              const std::size_t shader_index = tile.get_fragment_shader_index(&states);
              auto direct_attributes = attributes_row;    // attributes need to be mutable for tile_info.
              const geom::barycentric_coordinate_block* direct_checked_lambdas =
                (mode == tile_info::rasterization_mode::checked)
                  ? &lambdas_box
                  : nullptr;
              tile_info direct_info{
                &states,
                shader_index,
                direct_checked_lambdas,
                checked_quad_bounds,
                &direct_attributes,
                is_front_facing,
                mode};
              direct_info.attributes->setup_block_processing();

              if(mode == tile_info::rasterization_mode::block)
              {
                  process_block(
                    x,
                    y,
                    direct_info);
              }
              else
              {
                  process_block_checked(
                    x,
                    y,
                    direct_info);
              }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
              utils::unclock(stage_direct);
              stage_setup_direct += stage_direct;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
          }
          else
          {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
              std::uint64_t stage_enqueue = 0;
              utils::clock(stage_enqueue);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

              if(mode == tile_info::rasterization_mode::checked)
              {
                  needs_flush = tiles.add_triangle_checked(
                    x,
                    y,
                    &states,
                    lambdas_box,
                    checked_quad_bounds,
                    attributes_row,
                    is_front_facing);
              }
              else
              {
                  needs_flush = tiles.add_triangle(
                    x,
                    y,
                    &states,
                    lambdas_box,
                    attributes_row,
                    is_front_facing,
                    mode);
              }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
              utils::unclock(stage_enqueue);
              stage_setup_enqueue += stage_enqueue;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
          }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
          ++triangle_tile_ref_count;
          if(mode == tile_info::rasterization_mode::block)
          {
              ++triangle_block_tile_ref_count;
          }
          else
          {
              ++triangle_checked_tile_ref_count;
          }

          utils::unclock(stage_add_triangle);
          swr::impl::profile_raster_add_triangle_cycles.fetch_add(stage_add_triangle, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

          if(needs_flush)
          {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
              swr::impl::profile_raster_flush_trigger_overflow_count.fetch_add(1, std::memory_order_relaxed);

              std::uint64_t stage_flush = 0;
              utils::clock(stage_flush);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

              process_tile_cache();

#ifdef SWR_ENABLE_PIPELINE_PROFILING
              utils::unclock(stage_flush);
              swr::impl::profile_raster_flush_cycles.fetch_add(stage_flush, std::memory_order_relaxed);
              stage_cb_flush_inline += stage_flush;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
          }
      });

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_setup_iterate);
    swr::impl::profile_triangle_tile_refs.fetch_add(triangle_tile_ref_count, std::memory_order_relaxed);
    swr::impl::profile_triangle_block_tile_refs.fetch_add(triangle_block_tile_ref_count, std::memory_order_relaxed);
    swr::impl::profile_triangle_checked_tile_refs.fetch_add(triangle_checked_tile_ref_count, std::memory_order_relaxed);
    swr::impl::profile_raster_setup_triangle_cycles.fetch_add(stage_setup_triangle, std::memory_order_relaxed);
    swr::impl::profile_raster_setup_bounds_cycles.fetch_add(stage_setup_bounds, std::memory_order_relaxed);
    swr::impl::profile_raster_setup_iterate_cycles.fetch_add(stage_setup_iterate, std::memory_order_relaxed);
    swr::impl::profile_raster_setup_direct_cycles.fetch_add(stage_setup_direct, std::memory_order_relaxed);
    swr::impl::profile_raster_setup_enqueue_cycles.fetch_add(stage_setup_enqueue, std::memory_order_relaxed);
    swr::impl::profile_raster_setup_cb_enqueue_cycles.fetch_add(stage_setup_enqueue, std::memory_order_relaxed);
    swr::impl::profile_raster_setup_cb_flush_inline_cycles.fetch_add(stage_cb_flush_inline, std::memory_order_relaxed);
    swr::impl::profile_raster_setup_cb_direct_cycles.fetch_add(stage_setup_direct, std::memory_order_relaxed);
    utils::unclock(stage_raster_setup);
    swr::impl::profile_raster_setup_cycles.fetch_add(stage_raster_setup, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

} /* namespace rast */
