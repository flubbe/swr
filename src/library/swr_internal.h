/**
 * swr - a software rasterizer
 *
 * internal header. mostly includes commonly needed headers.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <atomic>
#include <type_traits>

#include <boost/container/static_vector.hpp>

/*
 * configurable options.
 */

/* use SIMD code by default. */
// #define SWR_USE_SIMD

/* use multiple threads by default. */
#define SWR_ENABLE_MULTI_THREADING

/* enable the use of morton codes (for texture access) by default. */
#define SWR_USE_MORTON_CODES

/*
 * headers.
 */

#include "swr/swr.h"
#include "swr/shaders.h"

#include "geometry/all.h"

#include "common/utils.h"

#include "states.h"
#include "pixelformat.h"
#include "output_merger.h"
#include "textures.h"
#include "renderbuffer.h"
#include "rasterizer/rasterizer.h"

#include "buffers.h"
#include "renderobject.h"
#include "context.h"

/*
 * rasterizer configuration.
 */

namespace swr
{

namespace impl
{

/** block size for triangle rasterization. */
#ifndef SWR_RASTERIZER_BLOCK_SHIFT
constexpr std::uint32_t rasterizer_block_shift{4};
#else
constexpr std::uint32_t rasterizer_block_shift{SWR_RASTERIZER_BLOCK_SHIFT};
#endif

/** Block size for triangle rasterization. The context buffer sizes have to be aligned on this value. */
constexpr std::uint32_t rasterizer_block_size{1 << rasterizer_block_shift};
static_assert(utils::is_power_of_two(rasterizer_block_size), "rasterizer_block_size has to be a power of 2");

/** round down to block size. */
template<typename T>
    requires(std::is_integral_v<T>)
inline T lower_align_on_block_size(const T& v)
{
    return v & ~(rasterizer_block_size - 1);
};

/** round up to block size. */
template<typename T>
    requires(std::is_integral_v<T>)
inline T upper_align_on_block_size(const T& v)
{
    return (v + (rasterizer_block_size - 1)) & ~(rasterizer_block_size - 1);
}

#ifdef DO_BENCHMARKING
extern std::atomic<std::uint64_t> profile_fragment_shader_cycles;
extern std::atomic<std::uint64_t> profile_depth_cycles;
extern std::atomic<std::uint64_t> profile_merge_cycles;
extern std::atomic<std::uint64_t> profile_raster_setup_cycles;
extern std::atomic<std::uint64_t> profile_interp_cycles;
extern std::atomic<std::uint64_t> profile_raster_add_triangle_cycles;
extern std::atomic<std::uint64_t> profile_raster_flush_cycles;
extern std::atomic<std::uint64_t> profile_raster_flush_scan_cycles;
extern std::atomic<std::uint64_t> profile_raster_flush_process_cycles;
extern std::atomic<std::uint64_t> profile_raster_flush_clear_cycles;
extern std::atomic<std::uint64_t> profile_raster_flush_nonempty_tiles;
extern std::atomic<std::uint64_t> profile_raster_flush_primitives;
extern std::atomic<std::uint64_t> profile_raster_flush_count;
extern std::atomic<std::uint64_t> profile_raster_flush_scanned_tiles;
extern std::atomic<std::uint64_t> profile_raster_block_total_cycles;
extern std::atomic<std::uint64_t> profile_raster_block_fragment_cycles;
extern std::atomic<std::uint64_t> profile_raster_block_merge_cycles;
extern std::atomic<std::uint64_t> profile_triangles_input;
extern std::atomic<std::uint64_t> profile_triangles_culled_degenerate;
extern std::atomic<std::uint64_t> profile_triangles_culled_face;
extern std::atomic<std::uint64_t> profile_triangles_submitted;
extern std::atomic<std::uint64_t> profile_triangle_tile_refs;
extern std::atomic<std::uint64_t> profile_triangle_block_tile_refs;
extern std::atomic<std::uint64_t> profile_triangle_checked_tile_refs;
extern std::atomic<std::uint64_t> profile_raster_direct_blocks;
extern std::atomic<std::uint64_t> profile_interp_varying_copies;
extern std::atomic<std::uint64_t> profile_fragment_shader_invocations;
extern std::atomic<std::uint64_t> profile_tile_shader_instance_probe_steps;
extern std::atomic<std::uint64_t> profile_clip_vertex_read_bytes;
extern std::atomic<std::uint64_t> profile_clip_vertex_write_bytes;
extern std::atomic<std::uint64_t> profile_raster_tile_payload_write_bytes;
extern std::atomic<std::uint64_t> profile_raster_tile_payload_checked_write_bytes;
extern std::atomic<std::uint64_t> profile_raster_tile_payload_block_write_bytes;
extern std::atomic<std::uint64_t> profile_raster_tile_info_write_bytes;
extern std::atomic<std::uint64_t> profile_raster_interp_write_bytes;
extern std::atomic<std::uint64_t> profile_raster_checked_lambda_write_bytes;
extern std::atomic<std::uint64_t> profile_raster_setup_triangle_cycles;
extern std::atomic<std::uint64_t> profile_raster_setup_bounds_cycles;
extern std::atomic<std::uint64_t> profile_raster_setup_iterate_cycles;
extern std::atomic<std::uint64_t> profile_raster_setup_direct_cycles;
extern std::atomic<std::uint64_t> profile_raster_setup_enqueue_cycles;
extern std::atomic<std::uint64_t> profile_raster_setup_iter_row_setup_cycles;
extern std::atomic<std::uint64_t> profile_raster_setup_iter_callback_cycles;
extern std::atomic<std::uint64_t> profile_raster_setup_cb_enqueue_cycles;
extern std::atomic<std::uint64_t> profile_raster_setup_cb_flush_inline_cycles;
extern std::atomic<std::uint64_t> profile_raster_setup_cb_direct_cycles;
extern std::atomic<std::uint64_t> profile_raster_flush_max_tile_prims;
extern std::atomic<std::uint64_t> profile_raster_flush_near_full_tiles;
extern std::atomic<std::uint64_t> profile_raster_flush_trigger_overflow_count;
#endif

} /* namespace impl */

} /* namespace swr */
