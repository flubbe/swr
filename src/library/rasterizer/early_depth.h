/**
 * swr - a software rasterizer
 *
 * early depth testing decisions and tile-local depth data.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <optional>

#include "../swr_internal.h"

#include "early_depth_policy.h"

namespace rast
{

/** Inclusive min/max depth range. */
struct depth_range
{
    /** Minimum depth value. */
    ml::fixed_32_t min_depth{};

    /** Maximum depth value. */
    ml::fixed_32_t max_depth{};
};

/** Full-tile quad count used by block-level early depth rejection. */
inline constexpr std::uint32_t early_depth_reject_tile_quad_count =
  (swr::impl::rasterizer_block_size / 2)
  * (swr::impl::rasterizer_block_size / 2);

/** Tile-local cached view of the stored depth range. */
class tile_depth_cache
{
    /** Default framebuffer that owns the depth attachment. */
    swr::impl::default_framebuffer* framebuffer{nullptr};

    /** Left raster coordinate of the tile. */
    int x{0};

    /** Top raster coordinate of the tile. */
    int y{0};

    /** Whether range currently contains the framebuffer data for this tile. */
    bool valid{false};

    /** Cached inclusive stored-depth range for the tile. */
    depth_range range{};

public:
    /** Construct a tile depth context for one rasterizer block. */
    tile_depth_cache(
      swr::impl::default_framebuffer* framebuffer,
      unsigned int x,
      unsigned int y);

    /** Return the stored-depth range, computing it lazily on first use. */
    [[nodiscard]]
    const depth_range& stored_depth_range();

    /** Return this tile's conservative depth range for a primitive depth plane. */
    [[nodiscard]]
    std::optional<depth_range> conservative_depth_range(
      const geom::linear_interpolator_2d<float>& depth) const;

    /** Invalidate the cached range after a primitive may have written depth. */
    void invalidate()
    {
        valid = false;
    }
};

/** Fragment-level early depth execution path for one rasterizer block. */
enum class early_depth_test_path
{
    /** Run the ordinary late-depth path. */
    late,

    /** Run early fragment depth testing without policy telemetry. */
    early,

    /** Run early fragment depth testing and collect policy telemetry. */
    early_collect_stats
};

/** Fragment-level early depth decision for one rasterizer block. */
struct fragment_depth_test_plan
{
    /** Chosen execution path. */
    early_depth_test_path path{early_depth_test_path::late};

    /** Whether the selected path performs depth testing before fragment shading. */
    [[nodiscard]]
    bool uses_early_depth() const
    {
        return path != early_depth_test_path::late;
    }

    /** Whether the selected path should report early-depth telemetry to the auto policy. */
    [[nodiscard]]
    bool collects_auto_stats() const
    {
        return path == early_depth_test_path::early_collect_stats;
    }
};

/** Inputs needed for a block-level conservative early depth reject decision. */
struct block_depth_reject_request
{
    /** Default framebuffer whose depth attachment may be used for the optimization. */
    const swr::impl::default_framebuffer* default_framebuffer{nullptr};

    /** Active render states for this primitive. */
    const swr::impl::render_states& states;

    /** Left raster coordinate of the block. */
    unsigned int block_x{0};

    /** Top raster coordinate of the block. */
    unsigned int block_y{0};

    /** Interpolated primitive depth plane at the block origin. */
    const geom::linear_interpolator_2d<float>& depth;

    /** Optional tile-local stored-depth cache. */
    tile_depth_cache* tile_depth{nullptr};

    /** Estimated number of candidate quads that could be skipped. */
    unsigned int candidate_quad_count{0};
};

/** Outcome of a block-level conservative early depth reject decision. */
struct block_depth_reject_result
{
    /** Whether the block-level depth-range test actually ran. */
    bool tested{false};

    /** Whether the whole block was proven rejected. */
    bool rejected{false};
};

/** Stateless early-depth decision surface used by triangle/block rasterization. */
struct early_depth_controller
{
    /** Whether shader metadata permits moving depth testing before fragment shading. */
    [[nodiscard]]
    static bool shader_allows_early_depth(
      const swr::program_metadata& metadata);

    /** Whether the current render state permits moving depth testing before fragment shading. */
    [[nodiscard]]
    static bool render_state_allows_early_depth(
      const swr::impl::render_states& states);

    /** Choose the fragment-level early depth path for one rasterizer block. */
    [[nodiscard]]
    static fragment_depth_test_plan choose_depth_test_path(
      const swr::impl::render_states& states,
      early_depth_policy_state& policy);

    /** Try to reject a whole rasterizer block with conservative depth ranges. */
    [[nodiscard]]
    static block_depth_reject_result try_reject_block(
      const block_depth_reject_request& request,
      early_depth_policy_state& policy);
};

} /* namespace rast */
