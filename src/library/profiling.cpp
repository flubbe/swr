/**
 * swr - a software rasterizer
 *
 * pipeline profiling counters.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "swr_internal.h"

namespace swr::impl
{

#ifdef SWR_ENABLE_PIPELINE_PROFILING
std::atomic<std::uint64_t> profile_fragment_shader_cycles{0};
std::atomic<std::uint64_t> profile_depth_cycles{0};
std::atomic<std::uint64_t> profile_merge_cycles{0};
std::atomic<std::uint64_t> profile_raster_setup_cycles{0};
std::atomic<std::uint64_t> profile_interp_cycles{0};
std::atomic<std::uint64_t> profile_raster_add_triangle_cycles{0};
std::atomic<std::uint64_t> profile_raster_flush_cycles{0};
std::atomic<std::uint64_t> profile_raster_flush_scan_cycles{0};
std::atomic<std::uint64_t> profile_raster_flush_process_cycles{0};
std::atomic<std::uint64_t> profile_raster_flush_clear_cycles{0};
std::atomic<std::uint64_t> profile_raster_flush_nonempty_tiles{0};
std::atomic<std::uint64_t> profile_raster_flush_primitives{0};
std::atomic<std::uint64_t> profile_raster_flush_count{0};
std::atomic<std::uint64_t> profile_raster_flush_scanned_tiles{0};
std::atomic<std::uint64_t> profile_raster_block_total_cycles{0};
std::atomic<std::uint64_t> profile_raster_block_fragment_cycles{0};
std::atomic<std::uint64_t> profile_raster_block_merge_cycles{0};
std::atomic<std::uint64_t> profile_triangles_input{0};
std::atomic<std::uint64_t> profile_triangles_culled_degenerate{0};
std::atomic<std::uint64_t> profile_triangles_culled_face{0};
std::atomic<std::uint64_t> profile_triangles_submitted{0};
std::atomic<std::uint64_t> profile_triangle_tile_refs{0};
std::atomic<std::uint64_t> profile_triangle_block_tile_refs{0};
std::atomic<std::uint64_t> profile_triangle_checked_tile_refs{0};
std::atomic<std::uint64_t> profile_raster_direct_blocks{0};
std::atomic<std::uint64_t> profile_interp_varying_copies{0};
std::atomic<std::uint64_t> profile_fragment_shader_invocations{0};
std::atomic<std::uint64_t> profile_tile_shader_instance_probe_steps{0};
std::atomic<std::uint64_t> profile_clip_vertex_read_bytes{0};
std::atomic<std::uint64_t> profile_clip_vertex_write_bytes{0};
std::atomic<std::uint64_t> profile_raster_tile_payload_write_bytes{0};
std::atomic<std::uint64_t> profile_raster_tile_payload_checked_write_bytes{0};
std::atomic<std::uint64_t> profile_raster_tile_payload_block_write_bytes{0};
std::atomic<std::uint64_t> profile_raster_tile_info_write_bytes{0};
std::atomic<std::uint64_t> profile_raster_interp_write_bytes{0};
std::atomic<std::uint64_t> profile_raster_checked_lambda_write_bytes{0};
std::atomic<std::uint64_t> profile_raster_setup_triangle_cycles{0};
std::atomic<std::uint64_t> profile_raster_setup_bounds_cycles{0};
std::atomic<std::uint64_t> profile_raster_setup_iterate_cycles{0};
std::atomic<std::uint64_t> profile_raster_setup_direct_cycles{0};
std::atomic<std::uint64_t> profile_raster_setup_enqueue_cycles{0};
std::atomic<std::uint64_t> profile_raster_setup_iter_row_setup_cycles{0};
std::atomic<std::uint64_t> profile_raster_setup_iter_callback_cycles{0};
std::atomic<std::uint64_t> profile_raster_setup_cb_enqueue_cycles{0};
std::atomic<std::uint64_t> profile_raster_setup_cb_flush_inline_cycles{0};
std::atomic<std::uint64_t> profile_raster_setup_cb_direct_cycles{0};
std::atomic<std::uint64_t> profile_raster_flush_max_tile_prims{0};
std::atomic<std::uint64_t> profile_raster_flush_near_full_tiles{0};
std::atomic<std::uint64_t> profile_raster_flush_trigger_overflow_count{0};
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

} /* namespace swr::impl */
