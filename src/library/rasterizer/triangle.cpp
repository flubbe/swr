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
#include "early_depth.h"

namespace rast
{

namespace
{

[[nodiscard]]
unsigned int estimate_quad_count(quad_bounds bounds)
{
    if(bounds.empty())
    {
        return 0;
    }

    const unsigned int width = bounds.end_x - bounds.start_x;
    const unsigned int height = bounds.end_y - bounds.start_y;
    return ((width + 1) / 2) * ((height + 1) / 2);
}

constexpr std::uint8_t full_fragment_mask = 0b1111;
const ml::vec2 fragment_pixel_center{0.5f, 0.5f};

static void clamp_fragment_depth_values(
  std::array<float, 4>& depth_value)
{
#ifdef SWR_USE_SIMD
    _mm_store_ps(
      depth_value.data(),
      _mm_min_ps(
        _mm_max_ps(
          _mm_load_ps(depth_value.data()),
          _mm_set1_ps(0.0f)),
        _mm_set1_ps(1.0f)));
#else  /* SWR_USE_SIMD */
    depth_value[0] = std::clamp(depth_value[0], 0.f, 1.f);
    depth_value[1] = std::clamp(depth_value[1], 0.f, 1.f);
    depth_value[2] = std::clamp(depth_value[2], 0.f, 1.f);
    depth_value[3] = std::clamp(depth_value[3], 0.f, 1.f);
#endif /* SWR_USE_SIMD */
}

[[nodiscard]]
static std::uint64_t count_fragment_masked(
  std::uint8_t mask)
{
    return ((mask & 8) ? 1u : 0u)
           + ((mask & 4) ? 1u : 0u)
           + ((mask & 2) ? 1u : 0u)
           + ((mask & 1) ? 1u : 0u);
}

template<bool collect_early_depth_stats>
void process_precomputed_fragment_block_early_z(
  swr::impl::default_framebuffer* framebuffer,
  int x,
  int y,
  std::uint8_t mask,
  unsigned int offset_x,
  unsigned int offset_y,
  const swr::impl::render_states& states,
  const swr::program_base* in_shader,
  const small_triangle_interpolator& attributes,
  const ml::vec4& in_depth,
  const ml::vec4& one_over_viewport_z,
  bool front_facing,
  std::array<
    boost::container::static_vector<
      swr::varying,
      swr::limits::max::varyings>,
    4>& temp_varyings,
  swr::impl::fragment_output_block& out,
  early_depth_sample* early_depth = nullptr)
{
    const bool is_default_framebuffer = (states.draw_target == framebuffer);
    const int framebuffer_height = states.draw_target->properties.height;
    if constexpr(collect_early_depth_stats)
    {
        assert(early_depth != nullptr);
        early_depth->reset();
    }

    std::uint8_t active_mask = mask;
    std::uint8_t depth_mask = active_mask;
    std::uint8_t write_color = active_mask;
    std::uint8_t write_stencil = 0b0;

    const int x0 = x;
    const int x1 = x + 1;
    const int y0 = y;
    const int y1 = y + 1;

    if(states.scissor_test_enabled)
    {
        const int x_min = states.scissor_box.x_min;
        const int x_max = states.scissor_box.x_max;
        int y_min{states.scissor_box.y_min};
        int y_max{states.scissor_box.y_max};

        if(is_default_framebuffer)
        {
            const int y_temp = y_min;
            y_min = framebuffer_height - y_max;
            y_max = framebuffer_height - y_temp;
        }

        const std::uint8_t scissor_mask =
          (((x0 >= x_min && x0 < x_max && y0 >= y_min && y0 < y_max) ? 1 : 0) << 3)
          | (((x1 >= x_min && x1 < x_max && y0 >= y_min && y0 < y_max) ? 1 : 0) << 2)
          | (((x0 >= x_min && x0 < x_max && y1 >= y_min && y1 < y_max) ? 1 : 0) << 1)
          | ((x1 >= x_min && x1 < x_max && y1 >= y_min && y1 < y_max) ? 1 : 0);

        if(scissor_mask == 0)
        {
            out.write_color = 0;
            out.write_stencil = 0;
            return;
        }

        active_mask &= scissor_mask;
        if(active_mask == 0)
        {
            out.write_color = 0;
            out.write_stencil = 0;
            return;
        }

        depth_mask &= active_mask;
        write_color &= active_mask;
        write_stencil &= active_mask;
    }

    alignas(utils::alignment::sse) std::array<float, 4> depth_value = {
      in_depth[0],
      in_depth[1],
      in_depth[2],
      in_depth[3]};
    alignas(utils::alignment::sse) std::array<float, 4> depth_test_value = depth_value;
    clamp_fragment_depth_values(depth_test_value);
    const std::uint8_t early_depth_input_mask = depth_mask;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_depth = 0;
    utils::clock(stage_depth);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    states.draw_target->depth_compare_write_block(
      x,
      y,
      depth_test_value,
      states.depth_func,
      states.write_depth,
      depth_mask);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_depth);
    swr::impl::profile_depth_cycles.fetch_add(stage_depth, std::memory_order_relaxed);
    const std::uint8_t prof_early_depth_rejected_mask =
      early_depth_input_mask & static_cast<std::uint8_t>(~depth_mask);
    const std::uint64_t prof_early_depth_tested =
      count_fragment_masked(early_depth_input_mask);
    const std::uint64_t prof_early_depth_rejected =
      count_fragment_masked(prof_early_depth_rejected_mask);
    swr::impl::profile_fragment_early_depth_blocks.fetch_add(1, std::memory_order_relaxed);
    swr::impl::profile_fragment_early_depth_fragments_tested.fetch_add(
      prof_early_depth_tested,
      std::memory_order_relaxed);
    swr::impl::profile_fragment_early_depth_fragments_rejected.fetch_add(
      prof_early_depth_rejected,
      std::memory_order_relaxed);
    if(early_depth_input_mask == full_fragment_mask)
    {
        swr::impl::profile_fragment_early_depth_full_mask_blocks.fetch_add(1, std::memory_order_relaxed);
        swr::impl::profile_fragment_early_depth_full_mask_fragments_tested.fetch_add(
          prof_early_depth_tested,
          std::memory_order_relaxed);
        swr::impl::profile_fragment_early_depth_full_mask_fragments_rejected.fetch_add(
          prof_early_depth_rejected,
          std::memory_order_relaxed);
    }
    else
    {
        swr::impl::profile_fragment_early_depth_partial_mask_blocks.fetch_add(1, std::memory_order_relaxed);
        swr::impl::profile_fragment_early_depth_partial_mask_fragments_tested.fetch_add(
          prof_early_depth_tested,
          std::memory_order_relaxed);
        swr::impl::profile_fragment_early_depth_partial_mask_fragments_rejected.fetch_add(
          prof_early_depth_rejected,
          std::memory_order_relaxed);
    }
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if constexpr(collect_early_depth_stats)
    {
        const std::uint8_t early_depth_rejected_mask =
          early_depth_input_mask & static_cast<std::uint8_t>(~depth_mask);
        early_depth->set(
          count_fragment_masked(early_depth_input_mask),
          count_fragment_masked(early_depth_rejected_mask));
    }

    active_mask &= depth_mask;
    if(active_mask == 0)
    {
        out.write_color = 0;
        out.write_stencil = 0;
        return;
    }

    write_color &= active_mask;
    write_stencil &= active_mask;

    attributes.get_varyings_block_at(
      offset_x,
      offset_y,
      temp_varyings);

    std::array<rast::fragment_info, 4> frag_info =
      {{{depth_value[0], front_facing, temp_varyings[0]},
        {depth_value[1], front_facing, temp_varyings[1]},
        {depth_value[2], front_facing, temp_varyings[2]},
        {depth_value[3], front_facing, temp_varyings[3]}}};

#ifdef SWR_USE_SIMD
    alignas(utils::alignment::sse) std::array<float, 4> z;
    _mm_store_ps(
      z.data(),
      _mm_div_ps(
        _mm_set1_ps(1.0f),
        _mm_set_ps(
          one_over_viewport_z[3],
          one_over_viewport_z[2],
          one_over_viewport_z[1],
          one_over_viewport_z[0])));
#else  /* SWR_USE_SIMD */
    const ml::vec4 z = ml::vec4::one() / ml::vec4(one_over_viewport_z);
#endif /* SWR_USE_SIMD */

    for(std::size_t i = 0; i < states.shader_info->iqs.size(); ++i)
    {
        if(states.shader_info->iqs[i] == swr::interpolation_qualifier::smooth)
        {
            frag_info[0].varyings[i].value *= z[0];
            frag_info[1].varyings[i].value *= z[1];
            frag_info[2].varyings[i].value *= z[2];
            frag_info[3].varyings[i].value *= z[3];

            frag_info[0].varyings[i].dFdx = frag_info[1].varyings[i].value - frag_info[0].varyings[i].value;
            frag_info[1].varyings[i].dFdx = frag_info[0].varyings[i].dFdx;
            frag_info[2].varyings[i].dFdx = frag_info[3].varyings[i].value - frag_info[2].varyings[i].value;
            frag_info[3].varyings[i].dFdx = frag_info[2].varyings[i].dFdx;

            frag_info[0].varyings[i].dFdy = frag_info[2].varyings[i].value - frag_info[0].varyings[i].value;
            frag_info[1].varyings[i].dFdy = frag_info[3].varyings[i].value - frag_info[1].varyings[i].value;
            frag_info[2].varyings[i].dFdy = frag_info[0].varyings[i].dFdy;
            frag_info[3].varyings[i].dFdy = frag_info[1].varyings[i].dFdy;
        }
    }

    std::array<ml::vec4, 4> color =
      {{{0, 0, 0, 1},
        {0, 0, 0, 1},
        {0, 0, 0, 1},
        {0, 0, 0, 1}}};

    const float fx0 = static_cast<float>(x0) - fragment_pixel_center.x;
    const float fx1 = static_cast<float>(x1) - fragment_pixel_center.x;
    const float fy0 = static_cast<float>(y0) - fragment_pixel_center.y;
    const float fy1 = static_cast<float>(y1) - fragment_pixel_center.y;
    std::uint8_t accept_mask = 0;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_fragment_shader = 0;
    utils::clock(stage_fragment_shader);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    auto make_frag_coord = [&](int fragment_index) -> ml::vec4
    {
        float fx = fx0;
        float fy = fy0;
        switch(fragment_index)
        {
        case 1:
            fx = fx1;
            break;
        case 2:
            fy = fy1;
            break;
        case 3:
            fx = fx1;
            fy = fy1;
            break;
        default:
            break;
        }

        if(is_default_framebuffer)
        {
            fy = framebuffer_height - fy;
        }

        return {fx, fy, depth_value[fragment_index], z[fragment_index]};
    };

    if(active_mask == full_fragment_mask)
    {
        ml::vec4 frag_coord0 = make_frag_coord(0);
        ml::vec4 frag_coord1 = make_frag_coord(1);
        ml::vec4 frag_coord2 = make_frag_coord(2);
        ml::vec4 frag_coord3 = make_frag_coord(3);

        accept_mask |= in_shader->fragment_shader(frag_coord0, frag_info[0].front_facing, {0, 0}, frag_info[0].varyings, depth_value[0], color[0]) << 3;
        accept_mask |= in_shader->fragment_shader(frag_coord1, frag_info[1].front_facing, {0, 0}, frag_info[1].varyings, depth_value[1], color[1]) << 2;
        accept_mask |= in_shader->fragment_shader(frag_coord2, frag_info[2].front_facing, {0, 0}, frag_info[2].varyings, depth_value[2], color[2]) << 1;
        accept_mask |= in_shader->fragment_shader(frag_coord3, frag_info[3].front_facing, {0, 0}, frag_info[3].varyings, depth_value[3], color[3]);
    }
    else
    {
        if(active_mask & 8)
        {
            ml::vec4 frag_coord0 = make_frag_coord(0);
            accept_mask |= in_shader->fragment_shader(frag_coord0, frag_info[0].front_facing, {0, 0}, frag_info[0].varyings, depth_value[0], color[0]) << 3;
        }
        if(active_mask & 4)
        {
            ml::vec4 frag_coord1 = make_frag_coord(1);
            accept_mask |= in_shader->fragment_shader(frag_coord1, frag_info[1].front_facing, {0, 0}, frag_info[1].varyings, depth_value[1], color[1]) << 2;
        }
        if(active_mask & 2)
        {
            ml::vec4 frag_coord2 = make_frag_coord(2);
            accept_mask |= in_shader->fragment_shader(frag_coord2, frag_info[2].front_facing, {0, 0}, frag_info[2].varyings, depth_value[2], color[2]) << 1;
        }
        if(active_mask & 1)
        {
            ml::vec4 frag_coord3 = make_frag_coord(3);
            accept_mask |= in_shader->fragment_shader(frag_coord3, frag_info[3].front_facing, {0, 0}, frag_info[3].varyings, depth_value[3], color[3]);
        }
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_fragment_shader);
    swr::impl::profile_fragment_shader_cycles.fetch_add(stage_fragment_shader, std::memory_order_relaxed);
    swr::impl::profile_fragment_shader_invocations.fetch_add(
      count_fragment_masked(active_mask),
      std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if(accept_mask == 0)
    {
        out.write_color = 0;
        out.write_stencil = 0;
        return;
    }

    depth_mask &= accept_mask;
    write_color &= accept_mask;
    write_stencil &= accept_mask;

    out.color[0] = color[0];
    out.color[1] = color[1];
    out.color[2] = color[2];
    out.color[3] = color[3];
    out.write_color = write_color;
    out.write_stencil = write_stencil;
}

}    // namespace

#ifdef SWR_ENABLE_PIPELINE_PROFILING

inline void profile_checked_quad_mask(std::uint8_t mask)
{
    // a mask of 0b1111 means the 2x2 block is fully covered.
    if(mask == 0b1111)
    {
        swr::impl::profile_checked_full_mask_quads.fetch_add(1, std::memory_order_relaxed);
        return;
    }

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
}

#endif /* SWR_ENABLE_PIPELINE_PROFILING */

template<
  early_depth_test_path fragment_depth_path>
bool sweep_rasterizer::process_block_impl(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& data,
  block_raster_context& context)
{
    constexpr bool collect_early_fragment_depth_test_auto_stats =
      fragment_depth_path == early_depth_test_path::early_collect_stats;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_block_total = 0;
    std::uint64_t stage_block_fragment = 0;
    std::uint64_t stage_block_merge = 0;
    utils::clock(stage_block_total);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    assert(data.attributes);
    auto& attributes = *data.attributes;
    const block_depth_reject_result block_reject =
      early_depth_controller::try_reject_block(
        {.default_framebuffer = context.framebuffer,
         .states = *data.states,
         .block_x = block_x,
         .block_y = block_y,
         .depth = attributes.depth_value,
         .tile_depth = context.depth_context,
         .candidate_quad_count = early_depth_reject_tile_quad_count},
        context.early_depth_policy);
    if(block_reject.rejected)
    {
        return false;
    }

    attributes.setup_block_processing();

    const std::size_t varying_count = attributes.varyings.size();

    std::array<
      boost::container::static_vector<
        swr::varying,
        swr::limits::max::varyings>,
      4>
      temp_varyings;
    temp_varyings[0].resize(varying_count);
    temp_varyings[1].resize(varying_count);
    temp_varyings[2].resize(varying_count);
    temp_varyings[3].resize(varying_count);

    const bool front_facing = data.front_facing;

    ml::vec4 frag_depth;
    ml::vec4 one_over_viewport_z;
    swr::impl::fragment_output_block out;
    early_depth_sample early_fragment_depth_test_auto_sample;

    const swr::program_base* shader =
      context.fragment_shader(block_x, block_y, data.shader_index);

    auto block_callback =
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

        if constexpr(collect_early_fragment_depth_test_auto_stats)
        {
            early_depth_sample early_depth;
            process_fragment_block_early_z_collect_stats(
              x,
              y,
              *data.states,
              shader,
              one_over_viewport_z,
              frag_info,
              out,
              early_depth);
            early_fragment_depth_test_auto_sample.add(early_depth);
        }
        else
        {
            if constexpr(fragment_depth_path == early_depth_test_path::early)
            {
                process_fragment_block_early_z(
                  x,
                  y,
                  *data.states,
                  shader,
                  one_over_viewport_z,
                  frag_info,
                  out);
            }
            else
            {
                process_fragment_block(
                  x,
                  y,
                  *data.states,
                  shader,
                  one_over_viewport_z,
                  frag_info,
                  out);
            }
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_fragment_block);
        stage_block_fragment += stage_fragment_block;

        std::uint64_t stage_merge_block = 0;
        utils::clock(stage_merge_block);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if(out.write_color)
        {
            data.states->draw_target->merge_color_block(
              0,
              x,
              y,
              out,
              data.states->blending_enabled,
              data.states->blend_src,
              data.states->blend_dst);
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_merge_block);
        stage_block_merge += stage_merge_block;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    }; /* block_callback */

    for_each_quad_in_triangle_block(
      block_x,
      block_y,
      attributes,
      block_callback);

    if constexpr(collect_early_fragment_depth_test_auto_stats)
    {
        context.early_depth_policy.fragment_test.record_test_result(
          early_fragment_depth_test_auto_sample);
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_block_total);
    swr::impl::profile_raster_block_total_cycles.fetch_add(stage_block_total, std::memory_order_relaxed);
    swr::impl::profile_raster_block_fragment_cycles.fetch_add(stage_block_fragment, std::memory_order_relaxed);
    swr::impl::profile_raster_block_merge_cycles.fetch_add(stage_block_merge, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    return true;
}

bool sweep_rasterizer::process_block(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& data,
  block_raster_context& context)
{
    const fragment_depth_test_plan fragment_depth_plan =
      early_depth_controller::choose_depth_test_path(
        *data.states,
        context.early_depth_policy);

    if(fragment_depth_plan.path == early_depth_test_path::early_collect_stats)
    {
        return process_block_impl<early_depth_test_path::early_collect_stats>(
          block_x,
          block_y,
          data,
          context);
    }

    if(fragment_depth_plan.path == early_depth_test_path::early)
    {
        return process_block_impl<early_depth_test_path::early>(
          block_x,
          block_y,
          data,
          context);
    }

    return process_block_impl<early_depth_test_path::late>(
      block_x,
      block_y,
      data,
      context);
}

template<
  early_depth_test_path fragment_depth_path>
bool sweep_rasterizer::process_block_checked_impl(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& data,
  block_raster_context& context)
{
    constexpr bool collect_early_fragment_depth_test_auto_stats =
      fragment_depth_path == early_depth_test_path::early_collect_stats;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_block_total = 0;
    std::uint64_t stage_block_fragment = 0;
    std::uint64_t stage_block_merge = 0;
    utils::clock(stage_block_total);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    assert(data.attributes);
    auto& attributes = *data.attributes;
    const unsigned int candidate_quad_count =
      estimate_quad_count(data.checked_quad_bounds);
    const block_depth_reject_result block_reject =
      early_depth_controller::try_reject_block(
        {.default_framebuffer = context.framebuffer,
         .states = *data.states,
         .block_x = block_x,
         .block_y = block_y,
         .depth = attributes.depth_value,
         .tile_depth = context.depth_context,
         .candidate_quad_count = candidate_quad_count},
        context.early_depth_policy);
    if(block_reject.rejected)
    {
        return false;
    }

    attributes.setup_block_processing();

    const std::size_t varying_count = attributes.varyings.size();

    std::array<
      boost::container::static_vector<
        swr::varying,
        swr::limits::max::varyings>,
      4>
      temp_varyings;

    temp_varyings[0].resize(varying_count);
    temp_varyings[1].resize(varying_count);
    temp_varyings[2].resize(varying_count);
    temp_varyings[3].resize(varying_count);

    const bool front_facing = data.front_facing;

    ml::vec4 frag_depth;
    ml::vec4 one_over_viewport_z;
    swr::impl::fragment_output_block out;
    early_depth_sample early_fragment_depth_test_auto_sample;

    const swr::program_base* shader =
      context.fragment_shader(block_x, block_y, data.shader_index);
    assert(data.checked_lambdas);

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

        profile_checked_quad_mask(static_cast<std::uint8_t>(mask));
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if constexpr(collect_early_fragment_depth_test_auto_stats)
        {
            early_depth_sample early_depth;

            // a mask of 0b1111 means the 2x2 block is fully covered.
            if(mask == 0b1111)
            {
                process_fragment_block_early_z_collect_stats(
                  x,
                  y,
                  *data.states,
                  shader,
                  one_over_viewport_z,
                  frag_info,
                  out,
                  early_depth);
            }
            else
            {
                process_fragment_block_early_z_collect_stats(
                  x,
                  y,
                  static_cast<std::uint8_t>(mask),
                  *data.states,
                  shader,
                  one_over_viewport_z,
                  frag_info,
                  out,
                  early_depth);
            }

            early_fragment_depth_test_auto_sample.add(early_depth);
        }
        else
        {
            // a mask of 0b1111 means the 2x2 block is fully covered.
            if(mask == 0b1111)
            {
                if constexpr(fragment_depth_path == early_depth_test_path::early)
                {
                    process_fragment_block_early_z(
                      x,
                      y,
                      *data.states,
                      shader,
                      one_over_viewport_z,
                      frag_info,
                      out);
                }
                else
                {
                    process_fragment_block(
                      x,
                      y,
                      *data.states,
                      shader,
                      one_over_viewport_z,
                      frag_info,
                      out);
                }
            }
            else
            {
                if constexpr(fragment_depth_path == early_depth_test_path::early)
                {
                    process_fragment_block_early_z(
                      x,
                      y,
                      static_cast<std::uint8_t>(mask),
                      *data.states,
                      shader,
                      one_over_viewport_z,
                      frag_info,
                      out);
                }
                else
                {
                    process_fragment_block(
                      x,
                      y,
                      static_cast<std::uint8_t>(mask),
                      *data.states,
                      shader,
                      one_over_viewport_z,
                      frag_info,
                      out);
                }
            }
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_fragment_block);
        stage_block_fragment += stage_fragment_block;

        std::uint64_t stage_merge_block = 0;
        utils::clock(stage_merge_block);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if(out.write_color)
        {
            data.states->draw_target->merge_color_block(
              0,
              x,
              y,
              out,
              data.states->blending_enabled,
              data.states->blend_src,
              data.states->blend_dst);
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_merge_block);
        stage_block_merge += stage_merge_block;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    }; /* process_checked_quad */

    for_each_covered_quad_in_checked_triangle_block(
      block_x,
      block_y,
      data.checked_quad_bounds,
      *data.checked_lambdas,
      attributes,
      process_checked_quad);

    if constexpr(collect_early_fragment_depth_test_auto_stats)
    {
        context.early_depth_policy.fragment_test.record_test_result(
          early_fragment_depth_test_auto_sample);
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_block_total);
    swr::impl::profile_raster_block_total_cycles.fetch_add(stage_block_total, std::memory_order_relaxed);
    swr::impl::profile_raster_block_fragment_cycles.fetch_add(stage_block_fragment, std::memory_order_relaxed);
    swr::impl::profile_raster_block_merge_cycles.fetch_add(stage_block_merge, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    return true;
}

bool sweep_rasterizer::process_block_checked(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& data,
  block_raster_context& context)
{
    const fragment_depth_test_plan fragment_depth_plan =
      early_depth_controller::choose_depth_test_path(
        *data.states,
        context.early_depth_policy);

    if(fragment_depth_plan.path == early_depth_test_path::early_collect_stats)
    {
        return process_block_checked_impl<early_depth_test_path::early_collect_stats>(
          block_x,
          block_y,
          data,
          context);
    }

    if(fragment_depth_plan.path == early_depth_test_path::early)
    {
        return process_block_checked_impl<early_depth_test_path::early>(
          block_x,
          block_y,
          data,
          context);
    }

    return process_block_checked_impl<early_depth_test_path::late>(
      block_x,
      block_y,
      data,
      context);
}

template<
  early_depth_test_path fragment_depth_path>
void sweep_rasterizer::process_block_precomputed_checked_impl(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& data,
  block_raster_context& context,
  const small_triangle_interpolator& attributes,
  std::span<const small_triangle_quad_payload> quads)
{
    constexpr bool collect_early_fragment_depth_test_auto_stats =
      fragment_depth_path == early_depth_test_path::early_collect_stats;

    // Small/sparse payloads already narrowed the work to explicit covered quads,
    // so the full-block depth range reject is intentionally skipped here.
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

    const bool front_facing = data.front_facing;

    ml::vec4 frag_depth;
    ml::vec4 one_over_viewport_z;
    swr::impl::fragment_output_block out;
    early_depth_sample early_fragment_depth_test_auto_sample;

    const swr::program_base* shader =
      context.fragment_shader(block_x, block_y, data.shader_index);

    for(const auto& quad: quads)
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_interp = 0;
        utils::clock(stage_interp);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        const unsigned int offset_x = quad.x - block_x;
        const unsigned int offset_y = quad.y - block_y;
        if constexpr(fragment_depth_path == early_depth_test_path::late)
        {
            attributes.get_data_block_at(
              offset_x,
              offset_y,
              temp_varyings,
              frag_depth,
              one_over_viewport_z);
        }
        else
        {
            attributes.get_depth_block_at(
              offset_x,
              offset_y,
              frag_depth,
              one_over_viewport_z);
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_interp);
        swr::impl::profile_interp_cycles.fetch_add(stage_interp, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_fragment_block = 0;
        utils::clock(stage_fragment_block);
        profile_checked_quad_mask(quad.mask);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if constexpr(collect_early_fragment_depth_test_auto_stats)
        {
            early_depth_sample early_depth;
            process_precomputed_fragment_block_early_z<true>(
              framebuffer,
              static_cast<int>(quad.x),
              static_cast<int>(quad.y),
              quad.mask,
              offset_x,
              offset_y,
              *data.states,
              shader,
              attributes,
              frag_depth,
              one_over_viewport_z,
              front_facing,
              temp_varyings,
              out,
              &early_depth);

            early_fragment_depth_test_auto_sample.add(early_depth);
        }
        else
        {
            if constexpr(fragment_depth_path == early_depth_test_path::early)
            {
                process_precomputed_fragment_block_early_z<false>(
                  framebuffer,
                  static_cast<int>(quad.x),
                  static_cast<int>(quad.y),
                  quad.mask,
                  offset_x,
                  offset_y,
                  *data.states,
                  shader,
                  attributes,
                  frag_depth,
                  one_over_viewport_z,
                  front_facing,
                  temp_varyings,
                  out);
            }
            else
            {
                std::array<rast::fragment_info, 4> frag_info =
                  {{{frag_depth[0], front_facing, temp_varyings[0]},
                    {frag_depth[1], front_facing, temp_varyings[1]},
                    {frag_depth[2], front_facing, temp_varyings[2]},
                    {frag_depth[3], front_facing, temp_varyings[3]}}};

                if(quad.mask == full_fragment_mask)
                {
                    process_fragment_block(
                      quad.x,
                      quad.y,
                      *data.states,
                      shader,
                      one_over_viewport_z,
                      frag_info,
                      out);
                }
                else
                {
                    process_fragment_block(
                      quad.x,
                      quad.y,
                      quad.mask,
                      *data.states,
                      shader,
                      one_over_viewport_z,
                      frag_info,
                      out);
                }
            }
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_fragment_block);
        stage_block_fragment += stage_fragment_block;

        std::uint64_t stage_merge_block = 0;
        utils::clock(stage_merge_block);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if(out.write_color)
        {
            data.states->draw_target->merge_color_block(
              0,
              quad.x,
              quad.y,
              out,
              data.states->blending_enabled,
              data.states->blend_src,
              data.states->blend_dst);
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_merge_block);
        stage_block_merge += stage_merge_block;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    }

    if constexpr(collect_early_fragment_depth_test_auto_stats)
    {
        context.early_depth_policy.fragment_test.record_test_result(
          early_fragment_depth_test_auto_sample);
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_block_total);
    swr::impl::profile_raster_block_total_cycles.fetch_add(stage_block_total, std::memory_order_relaxed);
    swr::impl::profile_raster_block_fragment_cycles.fetch_add(stage_block_fragment, std::memory_order_relaxed);
    swr::impl::profile_raster_block_merge_cycles.fetch_add(stage_block_merge, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

void sweep_rasterizer::process_block_precomputed_checked(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& data,
  block_raster_context& context,
  const small_triangle_interpolator& attributes,
  std::span<const small_triangle_quad_payload> quads)
{
    const fragment_depth_test_plan fragment_depth_plan =
      early_depth_controller::choose_depth_test_path(
        *data.states,
        context.early_depth_policy);

    if(fragment_depth_plan.path == early_depth_test_path::early_collect_stats)
    {
        process_block_precomputed_checked_impl<early_depth_test_path::early_collect_stats>(
          block_x,
          block_y,
          data,
          context,
          attributes,
          quads);
        return;
    }

    if(fragment_depth_plan.path == early_depth_test_path::early)
    {
        process_block_precomputed_checked_impl<early_depth_test_path::early>(
          block_x,
          block_y,
          data,
          context,
          attributes,
          quads);
        return;
    }

    process_block_precomputed_checked_impl<early_depth_test_path::late>(
      block_x,
      block_y,
      data,
      context,
      attributes,
      quads);
}

void sweep_rasterizer::process_block_small_checked(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& data,
  block_raster_context& context,
  const small_triangle_payload& payload)
{
    process_block_precomputed_checked(
      block_x,
      block_y,
      data,
      context,
      payload.attributes,
      std::span{
        payload.quads.data(),
        payload.quad_count});
}

void sweep_rasterizer::process_block_sparse_checked(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& data,
  block_raster_context& context,
  const sparse_triangle_payload& payload)
{
    process_block_precomputed_checked(
      block_x,
      block_y,
      data,
      context,
      payload.attributes,
      payload.quads);
}

void sweep_rasterizer::process_block_sparse_checked(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& data,
  block_raster_context& context,
  const sparse_triangle_tile_payload& payload,
  std::span<const small_triangle_quad_payload> quads)
{
    process_block_precomputed_checked(
      block_x,
      block_y,
      data,
      context,
      payload.attributes,
      quads);
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
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);

    ml::vec3 edges[2] = {
      (v2.coords - v1.coords).xyz(),
      (v3.coords - v1.coords).xyz()};    // edges in window coordinates
    ml::vec2 dz = ml::vec2{
                    edges[1].z * edges[0].y - edges[0].z * edges[1].y,
                    -edges[1].z * edges[0].x + edges[0].z * edges[1].x}
                  * inv_area;

#ifdef __GNUC__
    float m = std::max(fabs(dz.x), fabs(dz.y));    // Eq. (14.12)
#else
    float m = std::max(std::fabs(dz.x), std::fabs(dz.y));    // Eq. (14.12)
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

    // get the maximum exponent in the range of the z values spanned by the primitive
#ifdef __GNUC__
    float max_z = std::max({fabs(v1.coords.z),
                            fabs(v2.coords.z),
                            fabs(v3.coords.z)});
#else
    float max_z = std::max({std::fabs(v1.coords.z),
                            std::fabs(v2.coords.z),
                            std::fabs(v3.coords.z)});
#endif

    // clamp to zero (this means no resolvable depth offset for very small numbers)
    float r = 0.0f;
    if(std::isfinite(max_z))
    {
        std::uint32_t bits = std::bit_cast<std::uint32_t>(max_z);

        // Keep only exponent bits.
        bits &= 0xffu << 23;

        constexpr std::uint32_t mantissa_adjust = 23u << 23;

        // Convert 2^e to 2^(e - 23), matching r = 2^(e - n) for float.
        // Clamp to zero for very small exponents.
        if(bits > mantissa_adjust)
        {
            bits -= mantissa_adjust;
            r = std::bit_cast<float>(bits);
        }
    }

    return m * states.polygon_offset_factor
           + r * states.polygon_offset_units;    // Eq. (14.13)
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
    const bool single_thread_direct_block_path =
      thread_pool->get_thread_count() <= 1;
#else  /* SWR_ENABLE_MULTI_THREADING */
    const bool single_thread_direct_block_path = true;
#endif /* SWR_ENABLE_MULTI_THREADING */

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::clock(stage_setup_bounds);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    const bounding_box bounds = compute_triangle_bounds(
      states,
      info,
      y_needs_flip);
    const triangle_rasterization_classification rasterization =
      classify_triangle_rasterization(bounds, info);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_setup_bounds);
    utils::clock(stage_setup_iterate);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    const bool is_single_block_triangle =
      (bounds.end_x - bounds.start_x) == static_cast<int>(swr::impl::rasterizer_block_size)
      && (bounds.end_y - bounds.start_y) == static_cast<int>(swr::impl::rasterizer_block_size);

    std::span<const ml::vec4> provoking_vertex_varyings{
      v0.varyings.data(),
      v0.varyings.size()};
    if(states.shader_info->uses_flat_varyings()
       && v0.provoking_vertex_varyings != nullptr)
    {
        provoking_vertex_varyings = {
          v0.provoking_vertex_varyings,
          states.shader_info->varying_count};
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t triangle_tile_ref_count = 0;
    std::uint64_t triangle_block_tile_ref_count = 0;
    std::uint64_t triangle_checked_tile_ref_count = 0;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    auto emit_triangle_block_impl =
      [&](int x,
          int y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes_row,
          tile_info::rasterization_mode mode,
          const quad_bounds* override_checked_quad_bounds,
          const small_triangle_payload* precomputed_small_payload,
          const sparse_triangle_payload* precomputed_sparse_payload,
          bool prefer_small_payload)
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_add_triangle = 0;
        utils::clock(stage_add_triangle);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        const quad_bounds checked_quad_bounds = [&]() -> quad_bounds
        {
            if(uses_checked_lambdas(mode))
            {
                const auto computed_bounds =
                  override_checked_quad_bounds
                    ? *override_checked_quad_bounds
                    : compute_checked_quad_bounds(
                        bounds,
                        x,
                        y);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
                if(mode == tile_info::rasterization_mode::thin_y_major)
                {
                    swr::impl::profile_checked_sparse_thin_x_primitives.fetch_add(1, std::memory_order_relaxed);
                }
                else if(mode == tile_info::rasterization_mode::thin_x_major)
                {
                    swr::impl::profile_checked_sparse_thin_y_primitives.fetch_add(1, std::memory_order_relaxed);
                }
                else if(mode == tile_info::rasterization_mode::checked)
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
        bool emitted_tile_ref = true;

        if(is_single_block_triangle
           && single_thread_direct_block_path)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            std::uint64_t stage_direct = 0;
            utils::clock(stage_direct);
            swr::impl::profile_raster_direct_blocks.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            std::size_t tile_index = 0;
            tile* tile_ptr = nullptr;
            if(!tiles.try_get_tile(
                 static_cast<unsigned int>(x),
                 static_cast<unsigned int>(y),
                 tile_index,
                 tile_ptr))
            {
                return;
            }

            auto& tile = *tile_ptr;
            const std::size_t shader_index = tile.get_fragment_shader_index(&states);
            const bool use_precomputed_payload =
              (prefer_small_payload
               && precomputed_small_payload)
              || (!prefer_small_payload
                  && is_thin_rasterization_mode(mode)
                  && precomputed_sparse_payload);
            const tile_info::rasterization_mode direct_mode =
              use_precomputed_payload
                ? (prefer_small_payload
                     ? tile_info::rasterization_mode::small_checked
                     : tile_info::rasterization_mode::sparse_checked)
                : mode;
            const geom::barycentric_coordinate_block* direct_checked_lambdas =
              uses_checked_lambdas(direct_mode)
                ? &lambdas_box
                : nullptr;

            auto direct_attributes = attributes_row;    // attributes need to be mutable for tile_info.
            tile_info direct_info{
              &states,
              shader_index,
              direct_checked_lambdas,
              checked_quad_bounds,
              use_precomputed_payload ? nullptr : &direct_attributes,
              is_front_facing,
              direct_mode};
            early_depth_policy_state& early_depth_policy =
              early_depth_policies.current_state();
            block_raster_context block_context{
              .framebuffer = framebuffer,
              .tiles = tiles,
              .early_depth_policy = early_depth_policy,
              .depth_context = nullptr};

            if(mode == tile_info::rasterization_mode::block)
            {
                process_block(
                  x,
                  y,
                  direct_info,
                  block_context);
            }
            else if(use_precomputed_payload)
            {
                if(prefer_small_payload)
                {
                    process_block_small_checked(
                      x,
                      y,
                      direct_info,
                      block_context,
                      *precomputed_small_payload);
                }
                else
                {
                    process_block_sparse_checked(
                      x,
                      y,
                      direct_info,
                      block_context,
                      *precomputed_sparse_payload);
                }
            }
            else
            {
                process_block_checked(
                  x,
                  y,
                  direct_info,
                  block_context);
            }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
            utils::unclock(stage_direct);
            stage_setup_direct += stage_direct;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
        }
        else /* is_single_block_triangle && single_thread_direct_block_path */
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            std::uint64_t stage_enqueue = 0;
            utils::clock(stage_enqueue);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            if(uses_checked_lambdas(mode))
            {
                if(prefer_small_payload
                   && mode == tile_info::rasterization_mode::checked)
                {
                    if(precomputed_small_payload)
                    {
                        needs_flush = tiles.add_small_triangle_checked_payload(
                          x,
                          y,
                          &states,
                          checked_quad_bounds,
                          *precomputed_small_payload,
                          is_front_facing,
                          emitted_tile_ref);
                    }
                    else
                    {
                        needs_flush = tiles.add_small_triangle_checked(
                          x,
                          y,
                          &states,
                          lambdas_box,
                          checked_quad_bounds,
                          attributes_row,
                          is_front_facing,
                          emitted_tile_ref);
                    }
                }
                else if(is_thin_rasterization_mode(mode)
                        && precomputed_sparse_payload)
                {
                    needs_flush = tiles.add_sparse_triangle_checked_payload(
                      x,
                      y,
                      &states,
                      checked_quad_bounds,
                      *precomputed_sparse_payload,
                      is_front_facing,
                      emitted_tile_ref);
                }
                else
                {
                    needs_flush = tiles.add_triangle_checked(
                      x,
                      y,
                      &states,
                      lambdas_box,
                      checked_quad_bounds,
                      attributes_row,
                      is_front_facing,
                      mode);
                }
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
        if(emitted_tile_ref)
        {
            ++triangle_tile_ref_count;
            if(mode == tile_info::rasterization_mode::block)
            {
                ++triangle_block_tile_ref_count;
            }
            else
            {
                ++triangle_checked_tile_ref_count;
            }
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
    }; /* emit_triangle_block_impl */

    auto emit_triangle_block =
      [&](int x,
          int y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes_row,
          tile_info::rasterization_mode mode,
          const quad_bounds* override_checked_quad_bounds = nullptr)
    {
        emit_triangle_block_impl(
          x,
          y,
          lambdas_box,
          attributes_row,
          mode,
          override_checked_quad_bounds,
          nullptr,
          nullptr,
          false);
    };

    auto emit_small_triangle_block =
      [&](int x,
          int y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes_row,
          tile_info::rasterization_mode mode,
          const quad_bounds* override_checked_quad_bounds = nullptr,
          const small_triangle_payload* precomputed_small_payload = nullptr)
    {
        if(!single_thread_direct_block_path)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            std::uint64_t stage_add_triangle = 0;
            utils::clock(stage_add_triangle);

            std::uint64_t stage_enqueue = 0;
            utils::clock(stage_enqueue);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            assert(mode == tile_info::rasterization_mode::checked);
            assert(override_checked_quad_bounds != nullptr);

            const quad_bounds checked_quad_bounds = *override_checked_quad_bounds;

            bool needs_flush = false;
            bool emitted_tile_ref = true;

            if(precomputed_small_payload)
            {
                needs_flush = tiles.add_small_triangle_checked_payload(
                  x,
                  y,
                  &states,
                  checked_quad_bounds,
                  *precomputed_small_payload,
                  is_front_facing,
                  emitted_tile_ref);
            }
            else
            {
                needs_flush = tiles.add_small_triangle_checked(
                  x,
                  y,
                  &states,
                  lambdas_box,
                  checked_quad_bounds,
                  attributes_row,
                  is_front_facing,
                  emitted_tile_ref);
            }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
            utils::unclock(stage_enqueue);
            stage_setup_enqueue += stage_enqueue;

            if(emitted_tile_ref)
            {
                ++triangle_tile_ref_count;
                ++triangle_checked_tile_ref_count;
            }

            utils::unclock(stage_add_triangle);
            swr::impl::profile_raster_add_triangle_cycles.fetch_add(
              stage_add_triangle,
              std::memory_order_relaxed);
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

            return;
        }

        emit_triangle_block_impl(
          x,
          y,
          lambdas_box,
          attributes_row,
          mode,
          override_checked_quad_bounds,
          precomputed_small_payload,
          nullptr,
          true);
    };

    auto emit_small_triangle_payload_block =
      [&](int x,
          int y,
          const geom::barycentric_coordinate_block&,
          tile_info::rasterization_mode mode,
          const quad_bounds* override_checked_quad_bounds,
          const small_triangle_payload* precomputed_small_payload)
    {
        assert(!single_thread_direct_block_path);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_add_triangle = 0;
        utils::clock(stage_add_triangle);

        std::uint64_t stage_enqueue = 0;
        utils::clock(stage_enqueue);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        assert(mode == tile_info::rasterization_mode::checked);
        assert(override_checked_quad_bounds != nullptr);
        assert(precomputed_small_payload != nullptr);

        const quad_bounds checked_quad_bounds = *override_checked_quad_bounds;
        bool emitted_tile_ref = true;

        bool needs_flush = tiles.add_small_triangle_checked_payload(
          x,
          y,
          &states,
          checked_quad_bounds,
          *precomputed_small_payload,
          is_front_facing,
          emitted_tile_ref);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_enqueue);
        stage_setup_enqueue += stage_enqueue;

        if(emitted_tile_ref)
        {
            ++triangle_tile_ref_count;
            ++triangle_checked_tile_ref_count;
        }

        utils::unclock(stage_add_triangle);
        swr::impl::profile_raster_add_triangle_cycles.fetch_add(
          stage_add_triangle,
          std::memory_order_relaxed);
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
    };

    auto emit_thin_triangle_block =
      [&](int x,
          int y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes_row,
          tile_info::rasterization_mode mode,
          quad_bounds thin_quad_bounds,
          const sparse_triangle_payload* precomputed_sparse_payload = nullptr)
    {
        emit_triangle_block_impl(
          x,
          y,
          lambdas_box,
          attributes_row,
          mode,
          &thin_quad_bounds,
          nullptr,
          precomputed_sparse_payload,
          false);
    };

    if(rasterization.is_small_quad)
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        swr::impl::profile_raster_small_quad_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if(!single_thread_direct_block_path
           && small_triangle_interpolator::can_store_without_allocation(states.shader_info->iqs.size()))
        {
            for_each_small_quad_triangle_payload(
              states,
              bounds,
              info,
              provoking_vertex_varyings,
              polygon_offset,
              emit_small_triangle_payload_block);
        }
        else
        {
            for_each_small_quad_triangle(
              states,
              bounds,
              info,
              provoking_vertex_varyings,
              polygon_offset,
              emit_small_triangle_block);
        }
    }
    else if(is_thin_rasterization_mode(rasterization.mode))
    {
        for_each_thin_triangle_block_with_bounds(
          states,
          bounds,
          info,
          provoking_vertex_varyings,
          polygon_offset,
          rasterization.mode,
          emit_thin_triangle_block);
    }
    else
    {
        for_each_covered_triangle_block_general_with_bounds(
          states,
          bounds,
          info,
          provoking_vertex_varyings,
          polygon_offset,
          emit_triangle_block);
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_setup_iterate);
    utils::unclock(stage_raster_setup);

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

    swr::impl::profile_raster_setup_cycles.fetch_add(stage_raster_setup, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

} /* namespace rast */
