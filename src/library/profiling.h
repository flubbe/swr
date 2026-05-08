/**
 * swr - a software rasterizer
 *
 * pipeline profiling counters.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <atomic>
#include <cstdint>

namespace swr::impl
{

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

}    // namespace swr::impl
