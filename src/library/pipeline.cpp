/**
 * swr - a software rasterizer
 *
 * the graphics pipeline.
 *
 * most of the actual work (e.g. clipping, primitive assembly and rasterization)
 * is delegated to subroutines implemented elsewhere.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#ifdef SWR_ENABLE_PIPELINE_PROFILING
#    include <print>
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

/* user headers. */
#include "swr_internal.h"
#include "clipping.h"

namespace swr
{

/*
 * rendering pipeline.
 */

#ifdef SWR_ENABLE_PIPELINE_PROFILING
namespace
{

struct pipeline_cycle_profile
{
    std::uint64_t vertex{0};
    std::uint64_t clipping{0};
    std::uint64_t viewport{0};
    std::uint64_t assembly{0};
    std::uint64_t rasterizer{0};
    std::uint64_t present_total{0};
    std::uint64_t fragment_shader{0};
    std::uint64_t depth{0};
    std::uint64_t merge{0};
    std::uint64_t raster_setup{0};
    std::uint64_t interp{0};
    std::uint64_t raster_add_triangle{0};
    std::uint64_t raster_flush{0};
    std::uint64_t raster_flush_scan{0};
    std::uint64_t raster_flush_process{0};
    std::uint64_t raster_flush_clear{0};
    std::uint64_t raster_flush_nonempty_tiles{0};
    std::uint64_t raster_flush_primitives{0};
    std::uint64_t raster_flush_count{0};
    std::uint64_t raster_flush_scanned_tiles{0};
    std::uint64_t raster_block_total{0};
    std::uint64_t raster_block_fragment{0};
    std::uint64_t raster_block_merge{0};
    std::uint64_t triangles_input{0};
    std::uint64_t triangles_culled_degenerate{0};
    std::uint64_t triangles_culled_face{0};
    std::uint64_t triangles_submitted{0};
    std::uint64_t triangle_tile_refs{0};
    std::uint64_t triangle_block_tile_refs{0};
    std::uint64_t triangle_checked_tile_refs{0};
    std::uint64_t raster_direct_blocks{0};
    std::uint64_t interp_varying_copies{0};
    std::uint64_t fragment_shader_invocations{0};
    std::uint64_t tile_shader_instance_probe_steps{0};
    std::uint64_t clip_vertex_read_bytes{0};
    std::uint64_t clip_vertex_write_bytes{0};
    std::uint64_t clip_parallel_across_objects_frames{0};
    std::uint64_t clip_parallel_internal_object_frames{0};
    std::uint64_t clip_serial_frames{0};
    std::uint64_t clip_parallel_internal_object_tasks{0};
    std::uint64_t clip_parallel_internal_object_primitives{0};
    std::uint64_t clip_parallel_reject_small_primitive_count{0};
    std::uint64_t clip_parallel_reject_no_discard{0};
    std::uint64_t clip_parallel_reject_low_discard_ratio{0};
    std::uint64_t clip_input_triangles{0};
    std::uint64_t clip_output_triangles{0};
    std::uint64_t raster_tile_payload_write_bytes{0};
    std::uint64_t raster_tile_payload_checked_write_bytes{0};
    std::uint64_t raster_tile_payload_block_write_bytes{0};
    std::uint64_t raster_tile_info_write_bytes{0};
    std::uint64_t raster_interp_write_bytes{0};
    std::uint64_t raster_checked_lambda_write_bytes{0};
    std::uint64_t raster_setup_triangle{0};
    std::uint64_t raster_setup_bounds{0};
    std::uint64_t raster_setup_iterate{0};
    std::uint64_t raster_setup_direct{0};
    std::uint64_t raster_setup_enqueue{0};
    std::uint64_t raster_setup_iter_row_setup{0};
    std::uint64_t raster_setup_iter_callback{0};
    std::uint64_t raster_setup_cb_enqueue{0};
    std::uint64_t raster_setup_cb_flush_inline{0};
    std::uint64_t raster_setup_cb_direct{0};
    std::uint64_t raster_flush_max_tile_prims{0};
    std::uint64_t raster_flush_near_full_tiles{0};
    std::uint64_t raster_flush_trigger_overflow_count{0};
    std::uint64_t raster_processed_block_primitives{0};
    std::uint64_t raster_processed_checked_primitives{0};
    std::uint64_t checked_full_mask_quads{0};
    std::uint64_t checked_partial_mask_quads{0};
    std::uint64_t checked_quad_tests{0};
    std::uint64_t checked_empty_quads{0};
    std::uint64_t checked_partial_pop1_quads{0};
    std::uint64_t checked_partial_pop2_quads{0};
    std::uint64_t checked_partial_pop3_quads{0};
    std::uint64_t checked_sparse_thin_x_primitives{0};
    std::uint64_t checked_sparse_thin_y_primitives{0};
    std::uint64_t frame_count{0};
};

pipeline_cycle_profile g_pipeline_cycles;
constexpr std::uint64_t profile_log_interval_frames = 120;

inline void log_pipeline_profile_if_needed()
{
    if(g_pipeline_cycles.frame_count == 0
       || (g_pipeline_cycles.frame_count % profile_log_interval_frames) != 0)
    {
        return;
    }

    const auto f = static_cast<double>(profile_log_interval_frames);
    const double triangles_submitted = static_cast<double>(g_pipeline_cycles.triangles_submitted);
    const double triangle_tile_refs = static_cast<double>(g_pipeline_cycles.triangle_tile_refs);
    const double tiles_per_tri =
      triangles_submitted > 0.0
        ? triangle_tile_refs / triangles_submitted
        : 0.0;
    const double block_tile_ref_ratio =
      triangle_tile_refs > 0.0
        ? static_cast<double>(g_pipeline_cycles.triangle_block_tile_refs) / triangle_tile_refs
        : 0.0;
    const double checked_tile_ref_ratio =
      triangle_tile_refs > 0.0
        ? static_cast<double>(g_pipeline_cycles.triangle_checked_tile_refs) / triangle_tile_refs
        : 0.0;
    const double flush_count = static_cast<double>(g_pipeline_cycles.raster_flush_count);
    const double scanned_tiles_per_flush =
      flush_count > 0.0
        ? static_cast<double>(g_pipeline_cycles.raster_flush_scanned_tiles) / flush_count
        : 0.0;
    const double max_tile_prims_per_flush =
      flush_count > 0.0
        ? static_cast<double>(g_pipeline_cycles.raster_flush_max_tile_prims) / flush_count
        : 0.0;
    const double near_full_tiles_per_flush =
      flush_count > 0.0
        ? static_cast<double>(g_pipeline_cycles.raster_flush_near_full_tiles) / flush_count
        : 0.0;
    const double shader_instance_probe_per_tile_ref =
      triangle_tile_refs > 0.0
        ? static_cast<double>(g_pipeline_cycles.tile_shader_instance_probe_steps) / triangle_tile_refs
        : 0.0;
    const double direct_block_ratio =
      triangle_tile_refs > 0.0
        ? static_cast<double>(g_pipeline_cycles.raster_direct_blocks) / triangle_tile_refs
        : 0.0;
    const double clip_read_mib = static_cast<double>(g_pipeline_cycles.clip_vertex_read_bytes) / (1024.0 * 1024.0);
    const double clip_write_mib = static_cast<double>(g_pipeline_cycles.clip_vertex_write_bytes) / (1024.0 * 1024.0);
    const double tile_payload_write_mib = static_cast<double>(g_pipeline_cycles.raster_tile_payload_write_bytes) / (1024.0 * 1024.0);
    const double tile_payload_checked_write_mib = static_cast<double>(g_pipeline_cycles.raster_tile_payload_checked_write_bytes) / (1024.0 * 1024.0);
    const double tile_payload_block_write_mib = static_cast<double>(g_pipeline_cycles.raster_tile_payload_block_write_bytes) / (1024.0 * 1024.0);
    const double tile_info_write_mib = static_cast<double>(g_pipeline_cycles.raster_tile_info_write_bytes) / (1024.0 * 1024.0);
    const double interp_write_mib = static_cast<double>(g_pipeline_cycles.raster_interp_write_bytes) / (1024.0 * 1024.0);
    const double checked_lambda_write_mib = static_cast<double>(g_pipeline_cycles.raster_checked_lambda_write_bytes) / (1024.0 * 1024.0);
    const double processed_primitives_total =
      static_cast<double>(g_pipeline_cycles.raster_processed_block_primitives + g_pipeline_cycles.raster_processed_checked_primitives);
    const double processed_checked_ratio =
      processed_primitives_total > 0.0
        ? static_cast<double>(g_pipeline_cycles.raster_processed_checked_primitives) / processed_primitives_total
        : 0.0;
    const double checked_quad_total =
      static_cast<double>(g_pipeline_cycles.checked_full_mask_quads + g_pipeline_cycles.checked_partial_mask_quads);
    const double checked_full_mask_ratio =
      checked_quad_total > 0.0
        ? static_cast<double>(g_pipeline_cycles.checked_full_mask_quads) / checked_quad_total
        : 0.0;
    const double checked_empty_quad_ratio =
      g_pipeline_cycles.checked_quad_tests > 0
        ? static_cast<double>(g_pipeline_cycles.checked_empty_quads) / static_cast<double>(g_pipeline_cycles.checked_quad_tests)
        : 0.0;
    const double checked_partial_quad_total =
      static_cast<double>(g_pipeline_cycles.checked_partial_mask_quads);
    const double checked_partial_pop1_ratio =
      checked_partial_quad_total > 0.0
        ? static_cast<double>(g_pipeline_cycles.checked_partial_pop1_quads) / checked_partial_quad_total
        : 0.0;
    const double checked_partial_pop2_ratio =
      checked_partial_quad_total > 0.0
        ? static_cast<double>(g_pipeline_cycles.checked_partial_pop2_quads) / checked_partial_quad_total
        : 0.0;
    const double checked_partial_pop3_ratio =
      checked_partial_quad_total > 0.0
        ? static_cast<double>(g_pipeline_cycles.checked_partial_pop3_quads) / checked_partial_quad_total
        : 0.0;
    const double checked_sparse_total =
      static_cast<double>(g_pipeline_cycles.checked_sparse_thin_x_primitives + g_pipeline_cycles.checked_sparse_thin_y_primitives);
    const double checked_sparse_thin_x_ratio =
      checked_sparse_total > 0.0
        ? static_cast<double>(g_pipeline_cycles.checked_sparse_thin_x_primitives) / checked_sparse_total
        : 0.0;
    const double clip_triangle_expand_ratio =
      g_pipeline_cycles.clip_input_triangles > 0
        ? static_cast<double>(g_pipeline_cycles.clip_output_triangles) / static_cast<double>(g_pipeline_cycles.clip_input_triangles)
        : 0.0;
    const double setup_iter_other =
      static_cast<double>(g_pipeline_cycles.raster_setup_iterate)
      - static_cast<double>(g_pipeline_cycles.raster_setup_iter_row_setup)
      - static_cast<double>(g_pipeline_cycles.raster_setup_iter_callback);
    const double setup_cb_other =
      static_cast<double>(g_pipeline_cycles.raster_setup_iter_callback)
      - static_cast<double>(g_pipeline_cycles.raster_setup_cb_enqueue)
      - static_cast<double>(g_pipeline_cycles.raster_setup_cb_flush_inline)
      - static_cast<double>(g_pipeline_cycles.raster_setup_cb_direct);

    std::println(
      "[swr][rdtsc] avg cycles/frame over {} frames: present={:.0f} "
      "vertex={:.0f} clip={:.0f} viewport={:.0f} assembly={:.0f} raster={:.0f} "
      "frag_shader={:.0f} depth={:.0f} merge={:.0f} raster_setup={:.0f} "
      "interp={:.0f} add_tri={:.0f} flush={:.0f} flush_scan={:.0f} flush_process={:.0f} "
      "flush_clear={:.0f} flush_count={:.1f} flush_tiles={:.1f} flush_prims={:.1f} "
      "scan_tiles={:.1f} scan_tiles_per_flush={:.1f} flush_max_tile_prims={:.1f} "
      "flush_near_full_tiles={:.1f} flush_overflow_triggers={:.1f} block_total={:.0f} "
      "block_frag={:.0f} block_merge={:.0f} tri_in={:.1f} tri_cull_deg={:.1f} "
      "tri_cull_face={:.1f} tri_submit={:.1f} tile_refs={:.1f} tiles_per_tri={:.2f} "
      "block_tile_refs={:.1f} checked_tile_refs={:.1f} block_tile_ref_ratio={:.2f} "
      "checked_tile_ref_ratio={:.2f} direct_blocks={:.1f} direct_block_ratio={:.2f} "
      "interp_var_copies={:.1f} frag_invocations={:.1f} shader_probe_steps={:.1f} "
      "probe_steps_per_tile_ref={:.2f} clip_read_bytes={:.1f} clip_write_bytes={:.1f} "
      "tile_payload_write_bytes={:.1f} tile_payload_checked_bytes={:.1f} "
      "tile_payload_block_bytes={:.1f} tile_info_bytes={:.1f} interp_bytes={:.1f} "
      "checked_lambda_bytes={:.1f} setup_tri={:.0f} setup_bounds={:.0f} setup_iter={:.0f} "
      "setup_iter_row={:.0f} setup_iter_cb={:.0f} setup_iter_other={:.0f} setup_cb_enqueue={:.0f} "
      "setup_cb_flush={:.0f} setup_cb_direct={:.0f} setup_cb_other={:.0f} setup_direct={:.0f} "
      "setup_enqueue={:.0f} clip_read_mib={:.2f} clip_write_mib={:.2f} "
      "tile_payload_write_mib={:.2f} tile_payload_checked_mib={:.2f} "
      "tile_payload_block_mib={:.2f} tile_info_mib={:.2f} interp_mib={:.2f} "
      "checked_lambda_mib={:.2f} clip_mt_across_obj={:.1f} clip_mt_internal={:.1f}"
      "clip_serial={:.1f} clip_mt_tasks={:.1f} clip_mt_prims={:.1f} "
      "clip_reject_small={:.1f} clip_reject_no_discard={:.1f} "
      "clip_reject_low_discard={:.1f} clip_tri_in={:.1f} clip_tri_out={:.1f} "
      "clip_tri_expand={:.2f} proc_block_prims={:.1f} proc_checked_prims={:.1f} "
      "proc_checked_ratio={:.2f} checked_full_quads={:.1f} checked_partial_quads={:.1f} "
      "checked_full_ratio={:.2f} checked_quad_tests={:.1f} checked_empty_quads={:.1f} "
      "checked_empty_ratio={:.2f} checked_partial_pop1_quads={:.1f} "
      "checked_partial_pop2_quads={:.1f} checked_partial_pop3_quads={:.1f} "
      "checked_partial_pop1_ratio={:.2f} checked_partial_pop2_ratio={:.2f} "
      "checked_partial_pop3_ratio={:.2f} checked_sparse_thin_x_prims={:.1f} "
      "checked_sparse_thin_y_prims={:.1f} checked_sparse_thin_x_ratio={:.2f} tile_size={}",
      profile_log_interval_frames,
      static_cast<double>(g_pipeline_cycles.present_total) / f,
      static_cast<double>(g_pipeline_cycles.vertex) / f,
      static_cast<double>(g_pipeline_cycles.clipping) / f,
      static_cast<double>(g_pipeline_cycles.viewport) / f,
      static_cast<double>(g_pipeline_cycles.assembly) / f,
      static_cast<double>(g_pipeline_cycles.rasterizer) / f,
      static_cast<double>(g_pipeline_cycles.fragment_shader) / f,
      static_cast<double>(g_pipeline_cycles.depth) / f,
      static_cast<double>(g_pipeline_cycles.merge) / f,
      static_cast<double>(g_pipeline_cycles.raster_setup) / f,
      static_cast<double>(g_pipeline_cycles.interp) / f,
      static_cast<double>(g_pipeline_cycles.raster_add_triangle) / f,
      static_cast<double>(g_pipeline_cycles.raster_flush) / f,
      static_cast<double>(g_pipeline_cycles.raster_flush_scan) / f,
      static_cast<double>(g_pipeline_cycles.raster_flush_process) / f,
      static_cast<double>(g_pipeline_cycles.raster_flush_clear) / f,
      static_cast<double>(g_pipeline_cycles.raster_flush_count) / f,
      static_cast<double>(g_pipeline_cycles.raster_flush_nonempty_tiles) / f,
      static_cast<double>(g_pipeline_cycles.raster_flush_primitives) / f,
      static_cast<double>(g_pipeline_cycles.raster_flush_scanned_tiles) / f,
      scanned_tiles_per_flush,
      max_tile_prims_per_flush,
      near_full_tiles_per_flush,
      static_cast<double>(g_pipeline_cycles.raster_flush_trigger_overflow_count) / f,
      static_cast<double>(g_pipeline_cycles.raster_block_total) / f,
      static_cast<double>(g_pipeline_cycles.raster_block_fragment) / f,
      static_cast<double>(g_pipeline_cycles.raster_block_merge) / f,
      static_cast<double>(g_pipeline_cycles.triangles_input) / f,
      static_cast<double>(g_pipeline_cycles.triangles_culled_degenerate) / f,
      static_cast<double>(g_pipeline_cycles.triangles_culled_face) / f,
      triangles_submitted / f,
      triangle_tile_refs / f,
      tiles_per_tri,
      static_cast<double>(g_pipeline_cycles.triangle_block_tile_refs) / f,
      static_cast<double>(g_pipeline_cycles.triangle_checked_tile_refs) / f,
      block_tile_ref_ratio,
      checked_tile_ref_ratio,
      static_cast<double>(g_pipeline_cycles.raster_direct_blocks) / f,
      direct_block_ratio,
      static_cast<double>(g_pipeline_cycles.interp_varying_copies) / f,
      static_cast<double>(g_pipeline_cycles.fragment_shader_invocations) / f,
      static_cast<double>(g_pipeline_cycles.tile_shader_instance_probe_steps) / f,
      shader_instance_probe_per_tile_ref,
      static_cast<double>(g_pipeline_cycles.clip_vertex_read_bytes) / f,
      static_cast<double>(g_pipeline_cycles.clip_vertex_write_bytes) / f,
      static_cast<double>(g_pipeline_cycles.raster_tile_payload_write_bytes) / f,
      static_cast<double>(g_pipeline_cycles.raster_tile_payload_checked_write_bytes) / f,
      static_cast<double>(g_pipeline_cycles.raster_tile_payload_block_write_bytes) / f,
      static_cast<double>(g_pipeline_cycles.raster_tile_info_write_bytes) / f,
      static_cast<double>(g_pipeline_cycles.raster_interp_write_bytes) / f,
      static_cast<double>(g_pipeline_cycles.raster_checked_lambda_write_bytes) / f,
      static_cast<double>(g_pipeline_cycles.raster_setup_triangle) / f,
      static_cast<double>(g_pipeline_cycles.raster_setup_bounds) / f,
      static_cast<double>(g_pipeline_cycles.raster_setup_iterate) / f,
      static_cast<double>(g_pipeline_cycles.raster_setup_iter_row_setup) / f,
      static_cast<double>(g_pipeline_cycles.raster_setup_iter_callback) / f,
      setup_iter_other / f,
      static_cast<double>(g_pipeline_cycles.raster_setup_cb_enqueue) / f,
      static_cast<double>(g_pipeline_cycles.raster_setup_cb_flush_inline) / f,
      static_cast<double>(g_pipeline_cycles.raster_setup_cb_direct) / f,
      setup_cb_other / f,
      static_cast<double>(g_pipeline_cycles.raster_setup_direct) / f,
      static_cast<double>(g_pipeline_cycles.raster_setup_enqueue) / f,
      clip_read_mib / f,
      clip_write_mib / f,
      tile_payload_write_mib / f,
      tile_payload_checked_write_mib / f,
      tile_payload_block_write_mib / f,
      tile_info_write_mib / f,
      interp_write_mib / f,
      checked_lambda_write_mib / f,
      static_cast<double>(g_pipeline_cycles.clip_parallel_across_objects_frames) / f,
      static_cast<double>(g_pipeline_cycles.clip_parallel_internal_object_frames) / f,
      static_cast<double>(g_pipeline_cycles.clip_serial_frames) / f,
      static_cast<double>(g_pipeline_cycles.clip_parallel_internal_object_tasks) / f,
      static_cast<double>(g_pipeline_cycles.clip_parallel_internal_object_primitives) / f,
      static_cast<double>(g_pipeline_cycles.clip_parallel_reject_small_primitive_count) / f,
      static_cast<double>(g_pipeline_cycles.clip_parallel_reject_no_discard) / f,
      static_cast<double>(g_pipeline_cycles.clip_parallel_reject_low_discard_ratio) / f,
      static_cast<double>(g_pipeline_cycles.clip_input_triangles) / f,
      static_cast<double>(g_pipeline_cycles.clip_output_triangles) / f,
      clip_triangle_expand_ratio,
      static_cast<double>(g_pipeline_cycles.raster_processed_block_primitives) / f,
      static_cast<double>(g_pipeline_cycles.raster_processed_checked_primitives) / f,
      processed_checked_ratio,
      static_cast<double>(g_pipeline_cycles.checked_full_mask_quads) / f,
      static_cast<double>(g_pipeline_cycles.checked_partial_mask_quads) / f,
      checked_full_mask_ratio,
      static_cast<double>(g_pipeline_cycles.checked_quad_tests) / f,
      static_cast<double>(g_pipeline_cycles.checked_empty_quads) / f,
      checked_empty_quad_ratio,
      static_cast<double>(g_pipeline_cycles.checked_partial_pop1_quads) / f,
      static_cast<double>(g_pipeline_cycles.checked_partial_pop2_quads) / f,
      static_cast<double>(g_pipeline_cycles.checked_partial_pop3_quads) / f,
      checked_partial_pop1_ratio,
      checked_partial_pop2_ratio,
      checked_partial_pop3_ratio,
      static_cast<double>(g_pipeline_cycles.checked_sparse_thin_x_primitives) / f,
      static_cast<double>(g_pipeline_cycles.checked_sparse_thin_y_primitives) / f,
      checked_sparse_thin_x_ratio,
      impl::rasterizer_block_size);

    g_pipeline_cycles.vertex = 0;
    g_pipeline_cycles.clipping = 0;
    g_pipeline_cycles.viewport = 0;
    g_pipeline_cycles.assembly = 0;
    g_pipeline_cycles.rasterizer = 0;
    g_pipeline_cycles.present_total = 0;
    g_pipeline_cycles.fragment_shader = 0;
    g_pipeline_cycles.depth = 0;
    g_pipeline_cycles.merge = 0;
    g_pipeline_cycles.raster_setup = 0;
    g_pipeline_cycles.interp = 0;
    g_pipeline_cycles.raster_add_triangle = 0;
    g_pipeline_cycles.raster_flush = 0;
    g_pipeline_cycles.raster_flush_scan = 0;
    g_pipeline_cycles.raster_flush_process = 0;
    g_pipeline_cycles.raster_flush_clear = 0;
    g_pipeline_cycles.raster_flush_nonempty_tiles = 0;
    g_pipeline_cycles.raster_flush_primitives = 0;
    g_pipeline_cycles.raster_flush_count = 0;
    g_pipeline_cycles.raster_flush_scanned_tiles = 0;
    g_pipeline_cycles.raster_block_total = 0;
    g_pipeline_cycles.raster_block_fragment = 0;
    g_pipeline_cycles.raster_block_merge = 0;
    g_pipeline_cycles.triangles_input = 0;
    g_pipeline_cycles.triangles_culled_degenerate = 0;
    g_pipeline_cycles.triangles_culled_face = 0;
    g_pipeline_cycles.triangles_submitted = 0;
    g_pipeline_cycles.triangle_tile_refs = 0;
    g_pipeline_cycles.triangle_block_tile_refs = 0;
    g_pipeline_cycles.triangle_checked_tile_refs = 0;
    g_pipeline_cycles.raster_direct_blocks = 0;
    g_pipeline_cycles.interp_varying_copies = 0;
    g_pipeline_cycles.fragment_shader_invocations = 0;
    g_pipeline_cycles.tile_shader_instance_probe_steps = 0;
    g_pipeline_cycles.clip_vertex_read_bytes = 0;
    g_pipeline_cycles.clip_vertex_write_bytes = 0;
    g_pipeline_cycles.clip_parallel_across_objects_frames = 0;
    g_pipeline_cycles.clip_parallel_internal_object_frames = 0;
    g_pipeline_cycles.clip_serial_frames = 0;
    g_pipeline_cycles.clip_parallel_internal_object_tasks = 0;
    g_pipeline_cycles.clip_parallel_internal_object_primitives = 0;
    g_pipeline_cycles.clip_parallel_reject_small_primitive_count = 0;
    g_pipeline_cycles.clip_parallel_reject_no_discard = 0;
    g_pipeline_cycles.clip_parallel_reject_low_discard_ratio = 0;
    g_pipeline_cycles.clip_input_triangles = 0;
    g_pipeline_cycles.clip_output_triangles = 0;
    g_pipeline_cycles.raster_tile_payload_write_bytes = 0;
    g_pipeline_cycles.raster_tile_payload_checked_write_bytes = 0;
    g_pipeline_cycles.raster_tile_payload_block_write_bytes = 0;
    g_pipeline_cycles.raster_tile_info_write_bytes = 0;
    g_pipeline_cycles.raster_interp_write_bytes = 0;
    g_pipeline_cycles.raster_checked_lambda_write_bytes = 0;
    g_pipeline_cycles.raster_setup_triangle = 0;
    g_pipeline_cycles.raster_setup_bounds = 0;
    g_pipeline_cycles.raster_setup_iterate = 0;
    g_pipeline_cycles.raster_setup_iter_row_setup = 0;
    g_pipeline_cycles.raster_setup_iter_callback = 0;
    g_pipeline_cycles.raster_setup_cb_enqueue = 0;
    g_pipeline_cycles.raster_setup_cb_flush_inline = 0;
    g_pipeline_cycles.raster_setup_cb_direct = 0;
    g_pipeline_cycles.raster_flush_max_tile_prims = 0;
    g_pipeline_cycles.raster_flush_near_full_tiles = 0;
    g_pipeline_cycles.raster_flush_trigger_overflow_count = 0;
    g_pipeline_cycles.raster_processed_block_primitives = 0;
    g_pipeline_cycles.raster_processed_checked_primitives = 0;
    g_pipeline_cycles.checked_full_mask_quads = 0;
    g_pipeline_cycles.checked_partial_mask_quads = 0;
    g_pipeline_cycles.checked_quad_tests = 0;
    g_pipeline_cycles.checked_empty_quads = 0;
    g_pipeline_cycles.checked_partial_pop1_quads = 0;
    g_pipeline_cycles.checked_partial_pop2_quads = 0;
    g_pipeline_cycles.checked_partial_pop3_quads = 0;
    g_pipeline_cycles.checked_sparse_thin_x_primitives = 0;
    g_pipeline_cycles.checked_sparse_thin_y_primitives = 0;
    g_pipeline_cycles.raster_setup_direct = 0;
    g_pipeline_cycles.raster_setup_enqueue = 0;
}

inline std::uint64_t exchange_profile_counter(std::atomic<std::uint64_t>& counter)
{
    return counter.exchange(0, std::memory_order_relaxed);
}

inline void collect_pipeline_profile_frame()
{
    g_pipeline_cycles.fragment_shader += exchange_profile_counter(impl::profile_fragment_shader_cycles);
    g_pipeline_cycles.depth += exchange_profile_counter(impl::profile_depth_cycles);
    g_pipeline_cycles.merge += exchange_profile_counter(impl::profile_merge_cycles);
    g_pipeline_cycles.raster_setup += exchange_profile_counter(impl::profile_raster_setup_cycles);
    g_pipeline_cycles.interp += exchange_profile_counter(impl::profile_interp_cycles);
    g_pipeline_cycles.raster_add_triangle += exchange_profile_counter(impl::profile_raster_add_triangle_cycles);
    g_pipeline_cycles.raster_flush += exchange_profile_counter(impl::profile_raster_flush_cycles);
    g_pipeline_cycles.raster_flush_scan += exchange_profile_counter(impl::profile_raster_flush_scan_cycles);
    g_pipeline_cycles.raster_flush_process += exchange_profile_counter(impl::profile_raster_flush_process_cycles);
    g_pipeline_cycles.raster_flush_clear += exchange_profile_counter(impl::profile_raster_flush_clear_cycles);
    g_pipeline_cycles.raster_flush_nonempty_tiles += exchange_profile_counter(impl::profile_raster_flush_nonempty_tiles);
    g_pipeline_cycles.raster_flush_primitives += exchange_profile_counter(impl::profile_raster_flush_primitives);
    g_pipeline_cycles.raster_flush_count += exchange_profile_counter(impl::profile_raster_flush_count);
    g_pipeline_cycles.raster_flush_scanned_tiles += exchange_profile_counter(impl::profile_raster_flush_scanned_tiles);
    g_pipeline_cycles.raster_block_total += exchange_profile_counter(impl::profile_raster_block_total_cycles);
    g_pipeline_cycles.raster_block_fragment += exchange_profile_counter(impl::profile_raster_block_fragment_cycles);
    g_pipeline_cycles.raster_block_merge += exchange_profile_counter(impl::profile_raster_block_merge_cycles);
    g_pipeline_cycles.triangles_input += exchange_profile_counter(impl::profile_triangles_input);
    g_pipeline_cycles.triangles_culled_degenerate += exchange_profile_counter(impl::profile_triangles_culled_degenerate);
    g_pipeline_cycles.triangles_culled_face += exchange_profile_counter(impl::profile_triangles_culled_face);
    g_pipeline_cycles.triangles_submitted += exchange_profile_counter(impl::profile_triangles_submitted);
    g_pipeline_cycles.triangle_tile_refs += exchange_profile_counter(impl::profile_triangle_tile_refs);
    g_pipeline_cycles.triangle_block_tile_refs += exchange_profile_counter(impl::profile_triangle_block_tile_refs);
    g_pipeline_cycles.triangle_checked_tile_refs += exchange_profile_counter(impl::profile_triangle_checked_tile_refs);
    g_pipeline_cycles.raster_direct_blocks += exchange_profile_counter(impl::profile_raster_direct_blocks);
    g_pipeline_cycles.interp_varying_copies += exchange_profile_counter(impl::profile_interp_varying_copies);
    g_pipeline_cycles.fragment_shader_invocations += exchange_profile_counter(impl::profile_fragment_shader_invocations);
    g_pipeline_cycles.tile_shader_instance_probe_steps += exchange_profile_counter(impl::profile_tile_shader_instance_probe_steps);
    g_pipeline_cycles.clip_vertex_read_bytes += exchange_profile_counter(impl::profile_clip_vertex_read_bytes);
    g_pipeline_cycles.clip_vertex_write_bytes += exchange_profile_counter(impl::profile_clip_vertex_write_bytes);
    g_pipeline_cycles.clip_parallel_across_objects_frames += exchange_profile_counter(impl::profile_clip_parallel_across_objects_frames);
    g_pipeline_cycles.clip_parallel_internal_object_frames += exchange_profile_counter(impl::profile_clip_parallel_internal_object_frames);
    g_pipeline_cycles.clip_serial_frames += exchange_profile_counter(impl::profile_clip_serial_frames);
    g_pipeline_cycles.clip_parallel_internal_object_tasks += exchange_profile_counter(impl::profile_clip_parallel_internal_object_tasks);
    g_pipeline_cycles.clip_parallel_internal_object_primitives += exchange_profile_counter(impl::profile_clip_parallel_internal_object_primitives);
    g_pipeline_cycles.clip_parallel_reject_small_primitive_count += exchange_profile_counter(impl::profile_clip_parallel_reject_small_primitive_count);
    g_pipeline_cycles.clip_parallel_reject_no_discard += exchange_profile_counter(impl::profile_clip_parallel_reject_no_discard);
    g_pipeline_cycles.clip_parallel_reject_low_discard_ratio += exchange_profile_counter(impl::profile_clip_parallel_reject_low_discard_ratio);
    g_pipeline_cycles.clip_input_triangles += exchange_profile_counter(impl::profile_clip_input_triangles);
    g_pipeline_cycles.clip_output_triangles += exchange_profile_counter(impl::profile_clip_output_triangles);
    g_pipeline_cycles.raster_tile_payload_write_bytes += exchange_profile_counter(impl::profile_raster_tile_payload_write_bytes);
    g_pipeline_cycles.raster_tile_payload_checked_write_bytes += exchange_profile_counter(impl::profile_raster_tile_payload_checked_write_bytes);
    g_pipeline_cycles.raster_tile_payload_block_write_bytes += exchange_profile_counter(impl::profile_raster_tile_payload_block_write_bytes);
    g_pipeline_cycles.raster_tile_info_write_bytes += exchange_profile_counter(impl::profile_raster_tile_info_write_bytes);
    g_pipeline_cycles.raster_interp_write_bytes += exchange_profile_counter(impl::profile_raster_interp_write_bytes);
    g_pipeline_cycles.raster_checked_lambda_write_bytes += exchange_profile_counter(impl::profile_raster_checked_lambda_write_bytes);
    g_pipeline_cycles.raster_setup_triangle += exchange_profile_counter(impl::profile_raster_setup_triangle_cycles);
    g_pipeline_cycles.raster_setup_bounds += exchange_profile_counter(impl::profile_raster_setup_bounds_cycles);
    g_pipeline_cycles.raster_setup_iterate += exchange_profile_counter(impl::profile_raster_setup_iterate_cycles);
    g_pipeline_cycles.raster_setup_iter_row_setup += exchange_profile_counter(impl::profile_raster_setup_iter_row_setup_cycles);
    g_pipeline_cycles.raster_setup_iter_callback += exchange_profile_counter(impl::profile_raster_setup_iter_callback_cycles);
    g_pipeline_cycles.raster_setup_cb_enqueue += exchange_profile_counter(impl::profile_raster_setup_cb_enqueue_cycles);
    g_pipeline_cycles.raster_setup_cb_flush_inline += exchange_profile_counter(impl::profile_raster_setup_cb_flush_inline_cycles);
    g_pipeline_cycles.raster_setup_cb_direct += exchange_profile_counter(impl::profile_raster_setup_cb_direct_cycles);
    g_pipeline_cycles.raster_flush_max_tile_prims += exchange_profile_counter(impl::profile_raster_flush_max_tile_prims);
    g_pipeline_cycles.raster_flush_near_full_tiles += exchange_profile_counter(impl::profile_raster_flush_near_full_tiles);
    g_pipeline_cycles.raster_flush_trigger_overflow_count += exchange_profile_counter(impl::profile_raster_flush_trigger_overflow_count);
    g_pipeline_cycles.raster_processed_block_primitives += exchange_profile_counter(impl::profile_raster_processed_block_primitives);
    g_pipeline_cycles.raster_processed_checked_primitives += exchange_profile_counter(impl::profile_raster_processed_checked_primitives);
    g_pipeline_cycles.checked_full_mask_quads += exchange_profile_counter(impl::profile_checked_full_mask_quads);
    g_pipeline_cycles.checked_partial_mask_quads += exchange_profile_counter(impl::profile_checked_partial_mask_quads);
    g_pipeline_cycles.checked_quad_tests += exchange_profile_counter(impl::profile_checked_quad_tests);
    g_pipeline_cycles.checked_empty_quads += exchange_profile_counter(impl::profile_checked_empty_quads);
    g_pipeline_cycles.checked_partial_pop1_quads += exchange_profile_counter(impl::profile_checked_partial_pop1_quads);
    g_pipeline_cycles.checked_partial_pop2_quads += exchange_profile_counter(impl::profile_checked_partial_pop2_quads);
    g_pipeline_cycles.checked_partial_pop3_quads += exchange_profile_counter(impl::profile_checked_partial_pop3_quads);
    g_pipeline_cycles.checked_sparse_thin_x_primitives += exchange_profile_counter(impl::profile_checked_sparse_thin_x_primitives);
    g_pipeline_cycles.checked_sparse_thin_y_primitives += exchange_profile_counter(impl::profile_checked_sparse_thin_y_primitives);
    g_pipeline_cycles.raster_setup_direct += exchange_profile_counter(impl::profile_raster_setup_direct_cycles);
    g_pipeline_cycles.raster_setup_enqueue += exchange_profile_counter(impl::profile_raster_setup_enqueue_cycles);
}

inline void finalize_pipeline_profile_frame(std::uint64_t stage_present_total)
{
    g_pipeline_cycles.present_total += stage_present_total;
    ++g_pipeline_cycles.frame_count;
    log_pipeline_profile_if_needed();
}

} /* anonymous namespace */
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

#ifndef SWR_ENABLE_MULTI_THREADING

/*
 * single-threaded vertex processing functions.
 */
namespace st
{

/** Call vertex shaders and set clipping markers. */
static bool invoke_vertex_shader_and_clip_preprocess(
  impl::vertex_shader_instance_container& shader_instance,
  impl::render_object& obj)
{
    // check if the whole buffer should be discarded.
    bool clip_discard{true};

    // allocate varyings.
    obj.allocate_varyings(shader_instance.get_varying_count());

    for(std::size_t i = 0; i < obj.coord_count; ++i)
    {
        float gl_PointSize{0}; /* currently unused */
        const auto vertex_attribs = obj.attribs_for_vertex(i);
        shader_instance.get()->vertex_shader(
          0 /* gl_VertexID */, 0 /* gl_InstanceID */,
          vertex_attribs.data(), obj.coords[i],
          gl_PointSize, nullptr /* gl_ClipDistance */,
          obj.varyings_for_vertex(i).data());

        /*
         * Set clipping markers for this vertex. A visible vertex has to satisfy the relations
         *
         *    -w <= x <= w
         *    -w <= y <= w
         *    -w <= z <= w
         *      0 < w.
         */
        if(obj.coords[i].x < -obj.coords[i].w || obj.coords[i].x > obj.coords[i].w
           || obj.coords[i].y < -obj.coords[i].w || obj.coords[i].y > obj.coords[i].w
           || obj.coords[i].z < -obj.coords[i].w || obj.coords[i].z > obj.coords[i].w
           || obj.coords[i].w <= 0)
        {
            obj.vertex_flags[i] |= geom::vf_clip_discard;
        }
        else
        {
            clip_discard = false;
        }
    }

    return clip_discard;
}

/**
 * Transform from homogeneous clip space to viewport coordinates.
 */
static void transform_to_viewport_coords(
  impl::vertex_buffer& vb,
  float x,
  float y,
  float width,
  float height,
  float z_near,
  float z_far)
{
    for(auto& vertex_it: vb)
    {
        // calculate the normalized device coordinates.
        // w is set to 1/w (see https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf, section 15.2.2).
        vertex_it.coords.divide_by_w();

        // normalized device coordinates are in the range [-1,1], which we need to convert to viewport coordinates.

        // Note that the y direction needs to be flipped, since viewport y coordinates go from top down, while NDC
        // coordinates go bottom up. The flipping of the Y coordinate also flips the orientation of the primitives.
        float viewport_x = (1 + vertex_it.coords.x) * 0.5f * width + x;
        float viewport_y = (1 - vertex_it.coords.y) * 0.5f * height + y;

        // the viewport z coordinates is defined by linearly mapping z from the range [0,1] to [z_near, z_far].
        float viewport_z = ml::lerp(0.5f * (1.0f + vertex_it.coords.z), z_near, z_far);

        // Then, store the viewport coordinates.
        vertex_it.coords = {viewport_x, viewport_y, viewport_z, vertex_it.coords.w};
    }
}

static void process_vertices(swr::impl::render_object& obj)
{
    obj.clipped_vertices.clear();

    if(obj.coord_count == 0 || obj.indices.empty())
    {
        return;
    }

    // create shader instance.
    impl::vertex_shader_instance_container shader_instance{
      obj.states.shader_info->storage.data(),
      obj.states.shader_info,
      obj.states.uniforms};

    /*
     * Invoke the vertex shaders and preprocess vertices with respect to clipping.
     * The shaders take the view coordinates as inputs and output the homogeneous clip coordinates.
     * The clip preprecessing sets a marker for each vertex outside the view frustum.
     */
    bool discard_buffer = invoke_vertex_shader_and_clip_preprocess(shader_instance, obj);
    if(discard_buffer)
    {
        return;
    }

    // check we have valid drawing and polygon modes.
    assert(obj.mode == vertex_buffer_mode::points
           || obj.mode == vertex_buffer_mode::lines
           || obj.mode == vertex_buffer_mode::triangles);
    assert(obj.states.poly_mode == polygon_mode::point
           || obj.states.poly_mode == polygon_mode::line
           || obj.states.poly_mode == polygon_mode::fill);

    /*
     * clip the vertex buffer.
     *
     * if we only want to draw a list of points, we already have enough clipping
     * information from the previous call to invoke_vertex_shader_and_clip_preprocess.
     *
     * Clipping pre-assembles the primitives, i.e. it creates triangles.
     */
    if(obj.mode == vertex_buffer_mode::points
       || obj.states.poly_mode == polygon_mode::point)
    {
        const auto varying_count = obj.states.shader_info->varying_count;
        obj.clipped_vertices.reserve(obj.indices.size());

        geom::vertex v;
        v.varyings.resize(varying_count);

        // copy the correct points.
        for(const auto& i: obj.indices)
        {
            if(!(obj.vertex_flags[i] & geom::vf_clip_discard))
            {
                v.coords = obj.coords[i];
                v.flags = obj.vertex_flags[i];
                const auto vertex_varyings = obj.varyings_for_vertex(i);
                std::copy(
                  std::begin(vertex_varyings),
                  std::end(vertex_varyings),
                  v.varyings.begin());

                obj.clipped_vertices.emplace_back(v);
            }
        }
    }
    else if(obj.mode == vertex_buffer_mode::lines)
    {
        clip_line_buffer(obj, impl::line_list);
    }
    else if(obj.mode == vertex_buffer_mode::triangles
            && obj.states.poly_mode == polygon_mode::line)
    {
        clip_triangle_buffer(obj, impl::line_list);
    }
    else if(obj.states.poly_mode == polygon_mode::fill)
    {
        /* here we necessarily have list_it.Mode == triangles */
        clip_triangle_buffer(obj, impl::triangle_list);
    }

    // skip the rest of the pipeline if no clipped vertices were produced.
    if(!obj.clipped_vertices.empty())
    {
        // perspective divide and viewport transformation.
        transform_to_viewport_coords(
          obj.clipped_vertices,
          obj.states.x, obj.states.y,
          obj.states.width, obj.states.height,
          obj.states.z_near, obj.states.z_far);
    }
}

} /* namespace st */

#else /* !SWR_ENABLE_MULTI_THREADING */

/*
 * multi-threaded vertex processing functions
 */

namespace mt
{

/** Minimum work units assigned when slicing vertex work per thread. */
constexpr std::size_t min_tasks_per_thread = 4;

/** Target primitive chunk size for one clipping task. */
constexpr std::size_t min_clip_primitives_per_task = 128;

/** Minimum primitive count before per-object clipping chunking is enabled. */
constexpr std::size_t min_parallel_clip_primitives = 256;

/** Minimum fraction of discarded indices required to parallelize per-object clipping. */
constexpr float min_parallel_clip_discard_ratio = 0.02f;

/*
 * Cross-object clipping parallelization targets two regimes:
 * 1) high aggregate primitive work, and
 * 2) many small draw objects that are individually too small to chunk efficiently.
 */

/** Minimum render-object count required before considering cross-object clipping parallelization. */
constexpr std::size_t min_parallel_clip_render_objects = 2;

/** Aggregate primitive budget per worker thread for cross-object clipping fan-out. */
constexpr std::size_t min_parallel_clip_primitives_per_thread = 192;

/** Per-object primitive floor used to allow parallelization of many tiny render objects. */
constexpr std::size_t min_parallel_clip_primitives_per_object_floor = 64;

static std::size_t clip_primitive_count(
  const swr::impl::render_object* obj)
{
    if(obj->mode == vertex_buffer_mode::points
       || obj->states.poly_mode == polygon_mode::point)
    {
        return obj->indices.size();
    }

    if(obj->mode == vertex_buffer_mode::lines)
    {
        return obj->indices.size() / 2;
    }

    // triangles rendered either as line strips (poly_mode::line) or filled triangles.
    return obj->indices.size() / 3;
}

static bool should_parallelize_clipping_across_objects(
  impl::sdl_render_context::thread_pool_type& thread_pool,
  const std::vector<std::pair<
    swr::impl::render_object*,
    impl::vertex_shader_instance_container>>& program_instances)
{
    const std::size_t thread_count = thread_pool.get_thread_count();
    const std::size_t object_count = program_instances.size();

    if(thread_count <= 1
       || object_count < min_parallel_clip_render_objects)
    {
        return false;
    }

    std::size_t total_clip_primitives = 0;
    for(const auto& [obj, shader]: program_instances)
    {
        total_clip_primitives += clip_primitive_count(obj);
    }

    // Path 1: enough aggregate work to keep all threads busy.
    const std::size_t total_work_threshold =
      thread_count * min_parallel_clip_primitives_per_thread;
    if(total_clip_primitives >= total_work_threshold)
    {
        return true;
    }

    // Path 2: lots of tiny objects; allow parallel fan-out with a lower per-object floor.
    if(object_count >= thread_count
       && total_clip_primitives >= object_count * min_parallel_clip_primitives_per_object_floor)
    {
        return true;
    }

    return false;
}

static void clip_lines_chunk_task(
  const swr::impl::render_object* obj,
  std::size_t index_begin,
  std::size_t index_end,
  impl::vertex_buffer* out_vertices)
{
    clip_line_buffer_range(
      *obj,
      impl::line_list,
      index_begin,
      index_end,
      *out_vertices);
}

static void clip_triangles_chunk_task(
  const swr::impl::render_object* obj,
  impl::clip_output output_type,
  std::size_t index_begin,
  std::size_t index_end,
  impl::vertex_buffer* out_vertices)
{
    clip_triangle_buffer_range(
      *obj,
      output_type,
      index_begin,
      index_end,
      *out_vertices);
}

static void clip_indexed_primitives_parallel(
  impl::sdl_render_context::thread_pool_type& thread_pool,
  swr::impl::render_object* obj,
  std::size_t indices_per_primitive,
  impl::clip_output output_type)
{
    const std::size_t primitive_count = obj->indices.size() / indices_per_primitive;
    const std::size_t thread_count = thread_pool.get_thread_count();
    const std::size_t max_task_count =
      std::max<std::size_t>(
        1,
        (primitive_count + min_clip_primitives_per_task - 1) / min_clip_primitives_per_task);
    const std::size_t task_count = std::min(thread_count, max_task_count);

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
    impl::profile_clip_parallel_internal_object_tasks.fetch_add(task_count, std::memory_order_relaxed);
    impl::profile_clip_parallel_internal_object_primitives.fetch_add(primitive_count, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if(task_count <= 1 || primitive_count == 0)
    {
        if(indices_per_primitive == 2)
        {
            clip_line_buffer(*obj, output_type);
        }
        else
        {
            clip_triangle_buffer(*obj, output_type);
        }
        return;
    }

    std::vector<impl::vertex_buffer> chunk_outputs(task_count);

    for(std::size_t task_index = 0; task_index < task_count; ++task_index)
    {
        const std::size_t primitive_begin = (task_index * primitive_count) / task_count;
        const std::size_t primitive_end = ((task_index + 1) * primitive_count) / task_count;
        const std::size_t index_begin = primitive_begin * indices_per_primitive;
        const std::size_t index_end = primitive_end * indices_per_primitive;

        if(indices_per_primitive == 2)
        {
            thread_pool.push_immediate_task(
              clip_lines_chunk_task,
              obj,
              index_begin,
              index_end,
              &chunk_outputs[task_index]);
        }
        else
        {
            thread_pool.push_immediate_task(
              clip_triangles_chunk_task,
              obj,
              output_type,
              index_begin,
              index_end,
              &chunk_outputs[task_index]);
        }
    }

    thread_pool.run_tasks_and_wait();

    obj->clipped_vertices.clear();
    std::size_t output_size = 0;
    for(const auto& chunk: chunk_outputs)
    {
        output_size += chunk.size();
    }
    obj->clipped_vertices.reserve(output_size);

    for(auto& chunk: chunk_outputs)
    {
        obj->clipped_vertices.insert(
          std::end(obj->clipped_vertices),
          std::make_move_iterator(std::begin(chunk)),
          std::make_move_iterator(std::end(chunk)));
    }
}

static bool should_parallelize_clipping(
  impl::sdl_render_context::thread_pool_type& thread_pool,
  const swr::impl::render_object* obj,
  std::size_t indices_per_primitive)
{
    if(thread_pool.get_thread_count() <= 1)
    {
        return false;
    }

    const std::size_t primitive_count = obj->indices.size() / indices_per_primitive;
    if(primitive_count < min_parallel_clip_primitives)
    {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        impl::profile_clip_parallel_reject_small_primitive_count.fetch_add(1, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
        return false;
    }

    std::size_t discarded_index_count = 0;
    for(const std::uint32_t i: obj->indices)
    {
        if(obj->vertex_flags[i] & geom::vf_clip_discard)
        {
            ++discarded_index_count;
        }
    }

    if(discarded_index_count == 0)
    {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        impl::profile_clip_parallel_reject_no_discard.fetch_add(1, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
        return false;
    }

    const float discard_ratio =
      static_cast<float>(discarded_index_count)
      / static_cast<float>(obj->indices.size());
    if(discard_ratio < min_parallel_clip_discard_ratio)
    {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        impl::profile_clip_parallel_reject_low_discard_ratio.fetch_add(1, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
        return false;
    }
    return true;
}

static void clip_vertex_buffer_serial(
  swr::impl::render_object* obj)
{
    obj->clipped_vertices.clear();

    // check we have valid drawing and polygon modes.
    assert(obj->mode == vertex_buffer_mode::points
           || obj->mode == vertex_buffer_mode::lines
           || obj->mode == vertex_buffer_mode::triangles);
    assert(obj->states.poly_mode == polygon_mode::point
           || obj->states.poly_mode == polygon_mode::line
           || obj->states.poly_mode == polygon_mode::fill);

    /*
     * clip the vertex buffer.
     *
     * if we only want to draw a list of points, we already have enough clipping
     * information from the previous call to invoke_vertex_shader_and_clip_preprocess.
     *
     * Clipping pre-assembles the primitives, i.e. it creates triangles.
     */
    if(obj->mode == vertex_buffer_mode::points
       || obj->states.poly_mode == polygon_mode::point)
    {
        const auto varying_count = obj->states.shader_info->varying_count;
        obj->clipped_vertices.reserve(obj->indices.size());

        geom::vertex v;
        v.varyings.resize(varying_count);

        // copy the correct points.
        for(const auto& i: obj->indices)
        {
            if(!(obj->vertex_flags[i] & geom::vf_clip_discard))
            {
                v.coords = obj->coords[i];
                v.flags = obj->vertex_flags[i];
                const auto vertex_varyings = obj->varyings_for_vertex(i);
                std::copy(
                  std::begin(vertex_varyings),
                  std::end(vertex_varyings),
                  v.varyings.begin());

                obj->clipped_vertices.emplace_back(v);
            }
        }
    }
    else if(obj->mode == vertex_buffer_mode::lines)
    {
        clip_line_buffer(*obj, impl::line_list);
    }
    else if(obj->mode == vertex_buffer_mode::triangles
            && obj->states.poly_mode == polygon_mode::line)
    {
        clip_triangle_buffer(
          *obj,
          impl::line_list);
    }
    else if(obj->states.poly_mode == polygon_mode::fill)
    {
        /* here we necessarily have list_it.Mode == triangles */
        clip_triangle_buffer(
          *obj,
          impl::triangle_list);
    }
}

static void clip_vertex_buffer_serial_task(
  swr::impl::render_object* obj)
{
    clip_vertex_buffer_serial(obj);
}

static void vertex_shader_task(
  impl::render_object* obj,
  std::size_t offset,
  std::size_t end,
  impl::vertex_shader_instance_container* shader_instance)
{
    for(std::size_t i = offset; i < end; ++i)
    {
        float gl_PointSize{0}; /* currently unused */
        const auto vertex_attribs = obj->attribs_for_vertex(i);
        shader_instance->get()->vertex_shader(
          0 /* gl_VertexID */, 0 /* gl_InstanceID */,
          vertex_attribs.data(), obj->coords[i],
          gl_PointSize, nullptr /* gl_ClipDistance */,
          obj->varyings_for_vertex(i).data());

        /*
         * Set clipping markers for this vertex. A visible vertex has to satisfy the relations
         *
         *    -w <= x <= w
         *    -w <= y <= w
         *    -w <= z <= w
         *      0 < w.
         */
        if(obj->coords[i].x < -obj->coords[i].w || obj->coords[i].x > obj->coords[i].w
           || obj->coords[i].y < -obj->coords[i].w || obj->coords[i].y > obj->coords[i].w
           || obj->coords[i].z < -obj->coords[i].w || obj->coords[i].z > obj->coords[i].w
           || obj->coords[i].w <= 0)
        {
            obj->vertex_flags[i] |= geom::vf_clip_discard;
        }
    }
}

static void invoke_vertex_shader_and_clip_preprocess(
  impl::sdl_render_context::thread_pool_type& thread_pool,
  impl::vertex_shader_instance_container& shader_instance,
  impl::render_object& obj)
{
    const auto thread_count = thread_pool.get_thread_count();
    const std::size_t thread_vertex_count = std::max(
      min_tasks_per_thread,
      obj.coord_count / thread_count);

    // allocate varyings.
    obj.allocate_varyings(shader_instance.get_varying_count());

    // push shader tasks to thread pool.
    std::size_t offset = 0;
    for(; offset + thread_vertex_count < obj.coord_count; offset += thread_vertex_count)
    {
        thread_pool.push_immediate_task(
          vertex_shader_task,
          &obj,
          offset,
          offset + thread_vertex_count,
          &shader_instance);
    }

    if(offset < obj.coord_count)
    {
        thread_pool.push_immediate_task(
          vertex_shader_task,
          &obj,
          offset,
          obj.coord_count,
          &shader_instance);
    }
}

/**
 * @brief Apply the viewport transformation to a part of a vertex buffer. Meant to be supplied to a thread pool.
 *
 * @param vb Pointer to the vertex buffer.
 * @param offset Starting offset into the vertex buffer.
 * @param end End offset. Has to be less than vb->size().
 * @param x Viewport x coordinate.
 * @param y Viewport y coordinate.
 * @param width Viewport width.
 * @param height Viewport height.
 * @param z_near Near clipping plane coordinate.
 * @param z_far Far clipping plane coordinate.
 */
static void transform_to_viewport_coords_task(
  impl::vertex_buffer* vb,
  std::size_t offset,
  std::size_t end,
  float x,
  float y,
  float width,
  float height,
  float z_near,
  float z_far)
{
    for(std::size_t i = offset; i < end; ++i)
    {
        geom::vertex& v = (*vb)[i];

        // calculate the normalized device coordinates.
        // w is set to 1/w (see https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf, section 15.2.2).
        v.coords.divide_by_w();

        // normalized device coordinates are in the range [-1,1], which we need to convert to viewport coordinates.

        // Note that the y direction needs to be flipped, since viewport y coordinates go from top down, while NDC
        // coordinates go bottom up. The flipping of the Y coordinate also flips the orientation of the primitives.
        float viewport_x = (1 + v.coords.x) * 0.5f * width + x;
        float viewport_y = (1 - v.coords.y) * 0.5f * height + y;

        // the viewport z coordinates is defined by linearly mapping z from the range [0,1] to [z_near, z_far].
        float viewport_z = ml::lerp(0.5f * (1.0f + v.coords.z), z_near, z_far);

        // Then, store the viewport coordinates.
        v.coords = {viewport_x, viewport_y, viewport_z, v.coords.w};
    }
}

static void transform_to_viewport_coords(
  swr::impl::sdl_render_context::thread_pool_type& thread_pool,
  impl::vertex_buffer& vb,
  float x,
  float y,
  float width,
  float height,
  float z_near,
  float z_far)
{
    auto thread_count = thread_pool.get_thread_count();
    std::size_t thread_vertex_count = std::max(min_tasks_per_thread, vb.size() / thread_count);

    std::size_t offset = 0;
    for(; offset + thread_vertex_count < vb.size(); offset += thread_vertex_count)
    {
        thread_pool.push_immediate_task(
          transform_to_viewport_coords_task,
          &vb,
          offset,
          offset + thread_vertex_count,
          x,
          y,
          width,
          height,
          z_near,
          z_far);
    }

    // push remaining vertices.
    if(offset < vb.size())
    {
        thread_pool.push_immediate_task(
          transform_to_viewport_coords_task,
          &vb,
          offset,
          vb.size(),
          x,
          y,
          width,
          height,
          z_near,
          z_far);
    }
}

static void clip_vertex_buffer(
  impl::sdl_render_context::thread_pool_type& thread_pool,
  swr::impl::render_object* obj)
{
    if(obj->mode == vertex_buffer_mode::lines)
    {
        if(should_parallelize_clipping(
             thread_pool,
             obj,
             2))
        {
            clip_indexed_primitives_parallel(
              thread_pool,
              obj,
              2,
              impl::line_list);
        }
        else
        {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
            impl::profile_clip_serial_frames.fetch_add(1, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
            clip_line_buffer(*obj, impl::line_list);
        }
    }
    else if(obj->mode == vertex_buffer_mode::triangles
            && obj->states.poly_mode == polygon_mode::line)
    {
        if(should_parallelize_clipping(
             thread_pool,
             obj,
             3))
        {
            clip_indexed_primitives_parallel(
              thread_pool,
              obj,
              3,
              impl::line_list);
        }
        else
        {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
            impl::profile_clip_serial_frames.fetch_add(1, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
            clip_triangle_buffer(
              *obj,
              impl::line_list);
        }
    }
    else if(obj->states.poly_mode == polygon_mode::fill)
    {
        /* here we necessarily have list_it.Mode == triangles */
        if(should_parallelize_clipping(
             thread_pool,
             obj,
             3))
        {
            clip_indexed_primitives_parallel(
              thread_pool,
              obj,
              3,
              impl::triangle_list);
        }
        else
        {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
            impl::profile_clip_serial_frames.fetch_add(1, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
            clip_triangle_buffer(
              *obj,
              impl::triangle_list);
        }
    }
    else
    {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        impl::profile_clip_serial_frames.fetch_add(1, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
        clip_vertex_buffer_serial(obj);
    }
}

static void process_vertices(
  impl::render_context* context)
{
    /*
     * create shaders.
     */

    std::size_t total_shader_size = 0;
    for(const auto& obj: context->render_object_list)
    {
        total_shader_size += obj.states.shader_info->shader->size();
        total_shader_size = utils::align(
          utils::alignment::sse,
          total_shader_size);
    }

    std::byte* storage = utils::align_vector(
      utils::alignment::sse,
      total_shader_size,
      context->program_storage);
    context->program_instances.reserve(context->render_object_list.size());

    for(auto& obj: context->render_object_list)
    {
        context->program_instances.emplace_back(
          std::make_pair(
            &obj,
            impl::vertex_shader_instance_container{
              storage,
              obj.states.shader_info,
              obj.states.uniforms}));

        storage += obj.states.shader_info->shader->size();
        storage = utils::align(utils::alignment::sse, storage);
    }

    /*
     * invoke vertex shaders.
     */

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_vertex = 0;
    utils::clock(stage_vertex);
#    endif

    for(auto& [obj, shader]: context->program_instances)
    {
        if(obj->attrib_count != 0
           && !obj->indices.empty())
        {
            invoke_vertex_shader_and_clip_preprocess(
              context->thread_pool,
              shader,
              *obj);
        }
    }
    context->thread_pool.run_tasks_and_wait();

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_vertex);
    g_pipeline_cycles.vertex += stage_vertex;
#    endif

    /*
     * clipping.
     */

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_clipping = 0;
    utils::clock(stage_clipping);
#    endif

    /*
     * Avoid nested thread-pool waits in worker tasks: object-level parallel
     * clipping uses serial per-object clipping, while single-object clipping
     * can still use internal chunk parallelism.
     */
    const bool parallelize_across_objects =
      should_parallelize_clipping_across_objects(
        context->thread_pool,
        context->program_instances);

    if(parallelize_across_objects)
    {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        impl::profile_clip_parallel_across_objects_frames.fetch_add(1, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
        for(auto& [obj, shader]: context->program_instances)
        {
            context->thread_pool.push_immediate_task(
              clip_vertex_buffer_serial_task,
              obj);
        }
        context->thread_pool.run_tasks_and_wait();
    }
    else
    {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        impl::profile_clip_parallel_internal_object_frames.fetch_add(1, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
        // Single/few-object path can parallelize internally by primitive chunk.
        for(auto& [obj, shader]: context->program_instances)
        {
            clip_vertex_buffer(context->thread_pool, obj);
        }
    }

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_clipping);
    g_pipeline_cycles.clipping += stage_clipping;
#    endif

    /*
     * viewport transform.
     */

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_viewport = 0;
    utils::clock(stage_viewport);
#    endif
    for(auto& [obj, shader]: context->program_instances)
    {
        // skip the rest of the pipeline if no clipped vertices were produced.
        if(!obj->clipped_vertices.empty())
        {
            // perspective divide and viewport transformation.
            transform_to_viewport_coords(
              context->thread_pool,
              obj->clipped_vertices,
              obj->states.x, obj->states.y,
              obj->states.width, obj->states.height,
              obj->states.z_near, obj->states.z_far);
        }
    }
    context->thread_pool.run_tasks_and_wait();

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_viewport);
    g_pipeline_cycles.viewport += stage_viewport;
#    endif

    // clear shaders to force destructors being called, so that we do not have to care about the storage anymore.
    context->program_instances.clear();
    context->program_storage.clear();
}

} /* namespace mt */

#endif /* SWR_ENABLE_MULTI_THREADING */

/*
 * Execute the graphics pipeline and output an image into the frame buffer. The function operates on
 * the draw list produced by the drawing functions. For each draw list entry, execute:
 *
 *  1) the vertex shader
 *  2) clipping
 *  3) the viewport transformation (including perspective divide)
 *  4) primitive assembly
 *
 * The assembled primitives are then drawn by the rasterizer into the frame buffer and the draw list is emptied.
 * To display the image, the buffer needs to be copied to e.g. to a window.
 */
void Present()
{
    ASSERT_INTERNAL_CONTEXT;
    auto context = impl::global_context;

    // immediately return if there is nothing to do.
    if(context->render_object_list.empty())
    {
        return;
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_present_total = 0;
    utils::clock(stage_present_total);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

#ifdef SWR_ENABLE_MULTI_THREADING
    mt::process_vertices(context);

    // primitive assembly.
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_assembly = 0;
    utils::clock(stage_assembly);
#    endif
    for(auto& it: context->render_object_list)
    {
        if(it.clipped_vertices.empty())
        {
            continue;
        }

        // Assemble primitives from drawing lists. The primitives are passed on to the triangle rasterizer.
        context->assemble_primitives(
          &it.states,
          it.mode,
          it.clipped_vertices);
    }
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_assembly);
    g_pipeline_cycles.assembly += stage_assembly;
#    endif
#else
    // process render commands.
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_assembly = 0;
    std::uint64_t stage_vertex = 0;
    std::uint64_t stage_clipping = 0;
    std::uint64_t stage_viewport = 0;
#    endif
    for(auto& it: context->render_object_list)
    {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::clock(stage_vertex);
#    endif
        st::process_vertices(it);
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_vertex);
        g_pipeline_cycles.vertex += stage_vertex;
        stage_vertex = 0;
#    endif

        if(!it.clipped_vertices.empty())
        {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
            utils::clock(stage_assembly);
#    endif
            // Assemble primitives from drawing lists. The primitives are passed on to the triangle rasterizer.
            context->assemble_primitives(
              &it.states,
              it.mode,
              it.clipped_vertices);
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
            utils::unclock(stage_assembly);
            g_pipeline_cycles.assembly += stage_assembly;
            stage_assembly = 0;
#    endif
        }
    }
#endif

    // invoke triangle rasterizer.
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_rasterizer = 0;
    utils::clock(stage_rasterizer);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    context->rasterizer->draw_primitives();
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_rasterizer);
    g_pipeline_cycles.rasterizer += stage_rasterizer;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    // flush all lists.
    context->render_object_list.clear();

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_present_total);
    collect_pipeline_profile_frame();
    finalize_pipeline_profile_frame(stage_present_total);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

/*
 * depth buffer.
 */

void ClearDepthBuffer()
{
    ASSERT_INTERNAL_CONTEXT;

    impl::global_context->clear_depth_buffer();
}

void SetClearDepth(float z)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->states.set_clear_depth(z);
}

/*
 * color buffer.
 */

void ClearColorBuffer()
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->clear_color_buffer();
}

void SetClearColor(float r, float g, float b, float a)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->states.set_clear_color(r, g, b, a);
}

/*
 * scissor test.
 */

void SetScissorBox(int x, int y, int width, int height)
{
    ASSERT_INTERNAL_CONTEXT;
    auto context = impl::global_context;

    if(width < 0 || height < 0)
    {
        context->last_error = error::invalid_value;
        return;
    }

    context->states.set_scissor_box(x, x + width, y, y + height);
}

/*
 * viewport transform.
 */

void SetViewport(int x, int y, unsigned int width, unsigned int height)
{
    ASSERT_INTERNAL_CONTEXT;
    auto context = impl::global_context;

    context->states.set_viewport(x, y, width, height);
}

void DepthRange(float zNear, float zFar)
{
    ASSERT_INTERNAL_CONTEXT;
    auto context = impl::global_context;

    context->states.set_depth_range(zNear, zFar);
}

} /* namespace swr */
