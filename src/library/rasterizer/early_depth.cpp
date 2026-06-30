/**
 * swr - a software rasterizer
 *
 * early depth testing decisions and tile-local depth data.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "../swr_internal.h"

#include <algorithm>
#include <cassert>

#include "geometry/interpolators.h"
#include "early_depth.h"

namespace rast
{

namespace
{

// The stored-depth range scan is full-tile work, so only spend it on candidates
// whose covered region can plausibly save a full tile of fragment processing.
constexpr unsigned int early_depth_reject_min_candidate_quads =
  early_depth_reject_tile_quad_count;

struct block_span
{
    int width{0};
    int height{0};
};

[[nodiscard]]
block_span compute_block_span(
  const swr::impl::attachment_depth& depth_buffer,
  unsigned int block_x,
  unsigned int block_y)
{
    const int x = static_cast<int>(block_x);
    const int y = static_cast<int>(block_y);
    if(x >= depth_buffer.info.width
       || y >= depth_buffer.info.height)
    {
        return {};
    }

    return {
      std::min<int>(swr::impl::rasterizer_block_size, depth_buffer.info.width - x),
      std::min<int>(swr::impl::rasterizer_block_size, depth_buffer.info.height - y)};
}

[[nodiscard]]
depth_range conservative_depth_range_for_block(
  const geom::linear_interpolator_2d<float>& depth,
  int block_width,
  int block_height)
{
    const float max_dx = static_cast<float>(block_width - 1);
    const float max_dy = static_cast<float>(block_height - 1);

    const float d00 = depth.value;
    const float d10 = d00 + depth.step.x * max_dx;
    const float d01 = d00 + depth.step.y * max_dy;
    const float d11 = d10 + depth.step.y * max_dy;

    const float min_depth = std::min(std::min(d00, d10), std::min(d01, d11));
    const float max_depth = std::max(std::max(d00, d10), std::max(d01, d11));
    return {ml::fixed_32_t{min_depth}, ml::fixed_32_t{max_depth}};
}

[[nodiscard]]
depth_range scan_depth_range(
  const swr::impl::attachment_depth& depth_buffer,
  unsigned int block_x,
  unsigned int block_y,
  block_span span)
{
    constexpr int depth_value_size =
      static_cast<int>(sizeof(swr::impl::attachment_depth::value_type));
    const int row_stride = depth_buffer.info.pitch / depth_value_size;
    const int start_x = static_cast<int>(block_x);
    const int start_y = static_cast<int>(block_y);

    depth_range range{
      depth_buffer.info.data_ptr[start_y * row_stride + start_x],
      depth_buffer.info.data_ptr[start_y * row_stride + start_x]};

    for(int y = 0; y < span.height; ++y)
    {
        const int row = (start_y + y) * row_stride + start_x;
        for(int x = 0; x < span.width; ++x)
        {
            const ml::fixed_32_t z = depth_buffer.info.data_ptr[row + x];
            range.min_depth = std::min(range.min_depth, z);
            range.max_depth = std::max(range.max_depth, z);
        }
    }

    return range;
}

[[nodiscard]]
bool depth_ranges_prove_rejected(
  swr::comparison_func depth_func,
  depth_range primitive_range,
  depth_range stored_range)
{
    if(depth_func == swr::comparison_func::less)
    {
        return primitive_range.min_depth >= stored_range.max_depth;
    }
    else if(depth_func == swr::comparison_func::less_equal)
    {
        return primitive_range.min_depth > stored_range.max_depth;
    }
    else if(depth_func == swr::comparison_func::greater)
    {
        return primitive_range.max_depth <= stored_range.min_depth;
    }
    else if(depth_func == swr::comparison_func::greater_equal)
    {
        return primitive_range.max_depth < stored_range.min_depth;
    }
    else if(depth_func == swr::comparison_func::equal)
    {
        return primitive_range.max_depth < stored_range.min_depth
               || primitive_range.min_depth > stored_range.max_depth;
    }
    else if(depth_func == swr::comparison_func::not_equal)
    {
        // Only reject when both ranges collapse to one identical value.
        return primitive_range.min_depth == primitive_range.max_depth
               && stored_range.min_depth == stored_range.max_depth
               && primitive_range.min_depth == stored_range.min_depth;
    }

    return false;
}

void record_block_reject_sample(
  early_depth_policy_state& policy,
  bool collect_policy_stats,
  bool rejected)
{
    if(collect_policy_stats)
    {
        policy.block_reject.record_test_result(rejected);
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    if(rejected)
    {
        swr::impl::profile_raster_early_depth_rejects.fetch_add(1, std::memory_order_relaxed);
    }
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

}    // namespace

tile_depth_cache::tile_depth_cache(
  swr::impl::default_framebuffer* framebuffer,
  unsigned int x,
  unsigned int y)
: framebuffer{framebuffer}
, x{static_cast<int>(x)}
, y{static_cast<int>(y)}
{
    assert(framebuffer);
}

const depth_range& tile_depth_cache::stored_depth_range()
{
    assert(framebuffer);

    const auto& depth_buffer = framebuffer->depth_buffer;
    const int x0 = x;
    const int y0 = y;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_raster_stored_depth_range_requests.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if(valid)
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        swr::impl::profile_raster_stored_depth_range_hits.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        return range;
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
        range = {0, 0};
        valid = true;
        return range;
    }

    range = scan_depth_range(
      depth_buffer,
      static_cast<unsigned int>(x0),
      static_cast<unsigned int>(y0),
      {width, height});
    valid = true;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_raster_stored_depth_range_computes.fetch_add(1, std::memory_order_relaxed);
    swr::impl::profile_raster_stored_depth_range_hits.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    return range;
}

std::optional<depth_range> tile_depth_cache::conservative_depth_range(
  const geom::linear_interpolator_2d<float>& depth) const
{
    assert(framebuffer);

    const block_span span =
      compute_block_span(
        framebuffer->depth_buffer,
        static_cast<unsigned int>(x),
        static_cast<unsigned int>(y));
    if(span.width <= 0
       || span.height <= 0)
    {
        return std::nullopt;
    }

    return conservative_depth_range_for_block(
      depth,
      span.width,
      span.height);
}

bool early_depth_controller::shader_allows_early_depth(
  const swr::program_metadata& metadata)
{
    return !metadata.fragment_shader_may_discard
           && !metadata.fragment_shader_may_write_depth;
}

bool early_depth_controller::render_state_allows_early_depth(
  const swr::impl::render_states& states)
{
    assert(states.shader_info != nullptr);

    return states.depth_test_enabled
           && states.depth_func != swr::comparison_func::pass
           && shader_allows_early_depth(states.shader_info->metadata);
}

fragment_depth_test_plan early_depth_controller::choose_depth_test_path(
  const swr::impl::render_states& states,
  early_depth_policy_state& policy)
{
    if(!render_state_allows_early_depth(states))
    {
        return {};
    }

    if(states.early_fragment_depth_test_mode == swr::rasterizer_feature_mode::off)
    {
        return {};
    }

    if(states.early_fragment_depth_test_mode == swr::rasterizer_feature_mode::on)
    {
        return {early_depth_test_path::early};
    }

    const early_fragment_depth_test_auto_action action =
      policy.fragment_test.choose_action();
    if(action == early_fragment_depth_test_auto_action::disabled)
    {
        return {};
    }

    return {
      action == early_fragment_depth_test_auto_action::enabled_collect
        ? early_depth_test_path::early_collect_stats
        : early_depth_test_path::early};
}

block_depth_reject_result early_depth_controller::try_reject_block(
  const block_depth_reject_request& request,
  early_depth_policy_state& policy)
{
    assert(request.default_framebuffer != nullptr);

    const auto& states = request.states;

    if(states.block_early_depth_reject_mode == swr::rasterizer_feature_mode::off)
    {
        return {};
    }

    if(request.candidate_quad_count < early_depth_reject_min_candidate_quads)
    {
        return {};
    }

    if(!states.depth_test_enabled)
    {
        return {};
    }

    if(states.depth_func == swr::comparison_func::pass)
    {
        return {};
    }

    // TODO For now this optimization is only implemented for the default framebuffer depth attachment.
    if(states.draw_target != request.default_framebuffer)
    {
        return {};
    }

    const auto& depth_buffer = request.default_framebuffer->depth_buffer;
    if(!depth_buffer.info.data_ptr)
    {
        return {};
    }

    const bool collect_policy_stats =
      states.block_early_depth_reject_mode == swr::rasterizer_feature_mode::automatic;

    if(states.depth_func == swr::comparison_func::fail)
    {
        record_block_reject_sample(policy, collect_policy_stats, true);
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        swr::impl::profile_raster_early_depth_reject_tests.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
        return {
          .tested = true,
          .rejected = true};
    }

    if(states.shader_info == nullptr
       || states.shader_info->metadata.fragment_shader_may_write_depth)
    {
        return {};
    }

    if(collect_policy_stats
       && !policy.block_reject.should_test())
    {
        return {};
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_raster_early_depth_reject_tests.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    const block_span span =
      compute_block_span(depth_buffer, request.block_x, request.block_y);
    if(span.width <= 0
       || span.height <= 0)
    {
        record_block_reject_sample(policy, collect_policy_stats, true);
        return {
          .tested = true,
          .rejected = true};
    }

    const depth_range primitive_range =
      conservative_depth_range_for_block(
        request.depth,
        span.width,
        span.height);

    depth_range stored_range{};
    if(request.tile_depth != nullptr)
    {
        stored_range = request.tile_depth->stored_depth_range();
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        swr::impl::profile_raster_early_depth_reject_tests_with_cached_range.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    }
    else
    {
        stored_range =
          scan_depth_range(
            depth_buffer,
            request.block_x,
            request.block_y,
            span);
    }

    const bool rejected =
      depth_ranges_prove_rejected(
        states.depth_func,
        primitive_range,
        stored_range);

    record_block_reject_sample(policy, collect_policy_stats, rejected);
    return {
      .tested = true,
      .rejected = rejected};
}

} /* namespace rast */
