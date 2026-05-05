/**
 * swr - a software rasterizer
 *
 * the graphics pipeline.
 *
 * most of the actual work (e.g. clipping, primitive assembly and rasterization) is delegated to subroutines implemented elsewhere.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#ifdef DO_BENCHMARKING
#    include <print>
#endif /* DO_BENCHMARKING */

/* user headers. */
#include "swr_internal.h"
#include "clipping.h"

namespace swr
{

/*
 * rendering pipeline.
 */

#ifdef DO_BENCHMARKING
namespace impl
{
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
} /* namespace impl */
#endif

#ifdef DO_BENCHMARKING
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
    const double tiles_per_tri = triangles_submitted > 0.0
                                   ? triangle_tile_refs / triangles_submitted
                                   : 0.0;
    const double block_tile_ref_ratio = triangle_tile_refs > 0.0
                                          ? static_cast<double>(g_pipeline_cycles.triangle_block_tile_refs) / triangle_tile_refs
                                          : 0.0;
    const double checked_tile_ref_ratio = triangle_tile_refs > 0.0
                                            ? static_cast<double>(g_pipeline_cycles.triangle_checked_tile_refs) / triangle_tile_refs
                                            : 0.0;
    const double flush_count = static_cast<double>(g_pipeline_cycles.raster_flush_count);
    const double scanned_tiles_per_flush = flush_count > 0.0
                                             ? static_cast<double>(g_pipeline_cycles.raster_flush_scanned_tiles) / flush_count
                                             : 0.0;
    const double max_tile_prims_per_flush = flush_count > 0.0
                                              ? static_cast<double>(g_pipeline_cycles.raster_flush_max_tile_prims) / flush_count
                                              : 0.0;
    const double near_full_tiles_per_flush = flush_count > 0.0
                                               ? static_cast<double>(g_pipeline_cycles.raster_flush_near_full_tiles) / flush_count
                                               : 0.0;
    const double shader_instance_probe_per_tile_ref = triangle_tile_refs > 0.0
                                                        ? static_cast<double>(g_pipeline_cycles.tile_shader_instance_probe_steps) / triangle_tile_refs
                                                        : 0.0;
    const double direct_block_ratio = triangle_tile_refs > 0.0
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
      "[swr][rdtsc] avg cycles/frame over {} frames: present={:.0f} vertex={:.0f} clip={:.0f} viewport={:.0f} assembly={:.0f} raster={:.0f} frag_shader={:.0f} depth={:.0f} merge={:.0f} raster_setup={:.0f} interp={:.0f} add_tri={:.0f} flush={:.0f} flush_scan={:.0f} flush_process={:.0f} flush_clear={:.0f} flush_count={:.1f} flush_tiles={:.1f} flush_prims={:.1f} scan_tiles={:.1f} scan_tiles_per_flush={:.1f} flush_max_tile_prims={:.1f} flush_near_full_tiles={:.1f} flush_overflow_triggers={:.1f} block_total={:.0f} block_frag={:.0f} block_merge={:.0f} tri_in={:.1f} tri_cull_deg={:.1f} tri_cull_face={:.1f} tri_submit={:.1f} tile_refs={:.1f} tiles_per_tri={:.2f} block_tile_refs={:.1f} checked_tile_refs={:.1f} block_tile_ref_ratio={:.2f} checked_tile_ref_ratio={:.2f} direct_blocks={:.1f} direct_block_ratio={:.2f} interp_var_copies={:.1f} frag_invocations={:.1f} shader_probe_steps={:.1f} probe_steps_per_tile_ref={:.2f} clip_read_bytes={:.1f} clip_write_bytes={:.1f} tile_payload_write_bytes={:.1f} tile_payload_checked_bytes={:.1f} tile_payload_block_bytes={:.1f} tile_info_bytes={:.1f} interp_bytes={:.1f} checked_lambda_bytes={:.1f} setup_tri={:.0f} setup_bounds={:.0f} setup_iter={:.0f} setup_iter_row={:.0f} setup_iter_cb={:.0f} setup_iter_other={:.0f} setup_cb_enqueue={:.0f} setup_cb_flush={:.0f} setup_cb_direct={:.0f} setup_cb_other={:.0f} setup_direct={:.0f} setup_enqueue={:.0f} clip_read_mib={:.2f} clip_write_mib={:.2f} tile_payload_write_mib={:.2f} tile_payload_checked_mib={:.2f} tile_payload_block_mib={:.2f} tile_info_mib={:.2f} interp_mib={:.2f} checked_lambda_mib={:.2f} tile_size={}",
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
    g_pipeline_cycles.raster_setup_direct = 0;
    g_pipeline_cycles.raster_setup_enqueue = 0;
}

} /* anonymous namespace */
#endif /* DO_BENCHMARKING */

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
        shader_instance.get()->vertex_shader(
          0 /* gl_VertexID */, 0 /* gl_InstanceID */,
          &obj.attribs[i * obj.attrib_count], obj.coords[i],
          gl_PointSize, nullptr /* gl_ClipDistance */,
          &obj.varyings[i * shader_instance.get_varying_count()]);

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
                for(std::size_t j = 0; j < varying_count; ++j)
                {
                    v.varyings[j] = obj.varyings[i * varying_count + j];
                }

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

constexpr std::size_t min_tasks_per_thread = 4;

static void vertex_shader_task(
  impl::render_object* obj,
  std::size_t offset,
  std::size_t end,
  impl::vertex_shader_instance_container* shader_instance)
{
    for(std::size_t i = offset; i < end; ++i)
    {
        float gl_PointSize{0}; /* currently unused */
        shader_instance->get()->vertex_shader(
          0 /* gl_VertexID */, 0 /* gl_InstanceID */,
          &obj->attribs[i * obj->attrib_count], obj->coords[i],
          gl_PointSize, nullptr /* gl_ClipDistance */,
          &obj->varyings[i * shader_instance->get_varying_count()]);

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

static void transform_to_viewport_coords(swr::impl::sdl_render_context::thread_pool_type& thread_pool, impl::vertex_buffer& vb, float x, float y, float width, float height, float z_near, float z_far)
{
    auto thread_count = thread_pool.get_thread_count();
    std::size_t thread_vertex_count = std::max(min_tasks_per_thread, vb.size() / thread_count);

    std::size_t offset = 0;
    for(; offset + thread_vertex_count < vb.size(); offset += thread_vertex_count)
    {
        thread_pool.push_immediate_task(transform_to_viewport_coords_task, &vb, offset, offset + thread_vertex_count, x, y, width, height, z_near, z_far);
    }

    // push remaining vertices.
    if(offset < vb.size())
    {
        thread_pool.push_immediate_task(transform_to_viewport_coords_task, &vb, offset, vb.size(), x, y, width, height, z_near, z_far);
    }
}

static void clip_vertex_buffer(swr::impl::render_object* obj)
{
    obj->clipped_vertices.clear();

    // check we have valid drawing and polygon modes.
    assert(obj->mode == vertex_buffer_mode::points || obj->mode == vertex_buffer_mode::lines || obj->mode == vertex_buffer_mode::triangles);
    assert(obj->states.poly_mode == polygon_mode::point || obj->states.poly_mode == polygon_mode::line || obj->states.poly_mode == polygon_mode::fill);

    /*
     * clip the vertex buffer.
     *
     * if we only want to draw a list of points, we already have enough clipping
     * information from the previous call to invoke_vertex_shader_and_clip_preprocess.
     *
     * Clipping pre-assembles the primitives, i.e. it creates triangles.
     */
    if(obj->mode == vertex_buffer_mode::points || obj->states.poly_mode == polygon_mode::point)
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
                for(std::size_t j = 0; j < varying_count; ++j)
                {
                    v.varyings[j] = obj->varyings[i * varying_count + j];
                }

                obj->clipped_vertices.emplace_back(v);
            }
        }
    }
    else if(obj->mode == vertex_buffer_mode::lines)
    {
        clip_line_buffer(*obj, impl::line_list);
    }
    else if(obj->mode == vertex_buffer_mode::triangles && obj->states.poly_mode == polygon_mode::line)
    {
        clip_triangle_buffer(*obj, impl::line_list);
    }
    else if(obj->states.poly_mode == polygon_mode::fill)
    {
        /* here we necessarily have list_it.Mode == triangles */
        clip_triangle_buffer(*obj, impl::triangle_list);
    }
}

static void process_vertices(impl::render_context* context)
{
    // create shaders.
    std::size_t total_shader_size = 0;
    for(const auto& obj: context->render_object_list)
    {
        total_shader_size += obj.states.shader_info->shader->size();
        total_shader_size = utils::align(utils::alignment::sse, total_shader_size);
    }

    std::byte* storage = utils::align_vector(utils::alignment::sse, total_shader_size, context->program_storage);
    context->program_instances.reserve(context->render_object_list.size());

    for(auto& obj: context->render_object_list)
    {
        context->program_instances.emplace_back(
          std::make_pair(
            &obj,
            impl::vertex_shader_instance_container{storage, obj.states.shader_info, obj.states.uniforms}));

        storage += obj.states.shader_info->shader->size();
        storage = utils::align(utils::alignment::sse, storage);
    }

    // invoke vertex shaders.
#    ifdef DO_BENCHMARKING
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
#    ifdef DO_BENCHMARKING
    utils::unclock(stage_vertex);
    g_pipeline_cycles.vertex += stage_vertex;
#    endif

    // clipping.
#    ifdef DO_BENCHMARKING
    std::uint64_t stage_clipping = 0;
    utils::clock(stage_clipping);
#    endif
    for(auto& [obj, shader]: context->program_instances)
    {
        context->thread_pool.push_task(clip_vertex_buffer, obj);
    }
    context->thread_pool.run_tasks_and_wait();
#    ifdef DO_BENCHMARKING
    utils::unclock(stage_clipping);
    g_pipeline_cycles.clipping += stage_clipping;
#    endif

    // viewport transform.
#    ifdef DO_BENCHMARKING
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
#    ifdef DO_BENCHMARKING
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

#ifdef DO_BENCHMARKING
    std::uint64_t stage_present_total = 0;
    utils::clock(stage_present_total);
#endif

#ifdef SWR_ENABLE_MULTI_THREADING
    mt::process_vertices(context);

    // primitive assembly.
#    ifdef DO_BENCHMARKING
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
#    ifdef DO_BENCHMARKING
    utils::unclock(stage_assembly);
    g_pipeline_cycles.assembly += stage_assembly;
#    endif
#else
    // process render commands.
#    ifdef DO_BENCHMARKING
    std::uint64_t stage_assembly = 0;
    std::uint64_t stage_vertex = 0;
    std::uint64_t stage_clipping = 0;
    std::uint64_t stage_viewport = 0;
#    endif
    for(auto& it: context->render_object_list)
    {
#    ifdef DO_BENCHMARKING
        utils::clock(stage_vertex);
#    endif
        st::process_vertices(it);
#    ifdef DO_BENCHMARKING
        utils::unclock(stage_vertex);
        g_pipeline_cycles.vertex += stage_vertex;
        stage_vertex = 0;
#    endif

        if(!it.clipped_vertices.empty())
        {
#    ifdef DO_BENCHMARKING
            utils::clock(stage_assembly);
#    endif
            // Assemble primitives from drawing lists. The primitives are passed on to the triangle rasterizer.
            context->assemble_primitives(
              &it.states,
              it.mode,
              it.clipped_vertices);
#    ifdef DO_BENCHMARKING
            utils::unclock(stage_assembly);
            g_pipeline_cycles.assembly += stage_assembly;
            stage_assembly = 0;
#    endif
        }
    }
#endif

    // invoke triangle rasterizer.
#ifdef DO_BENCHMARKING
    std::uint64_t stage_rasterizer = 0;
    utils::clock(stage_rasterizer);
#endif
    context->rasterizer->draw_primitives();
#ifdef DO_BENCHMARKING
    utils::unclock(stage_rasterizer);
    g_pipeline_cycles.rasterizer += stage_rasterizer;
#endif

    // flush all lists.
    context->render_object_list.clear();

#ifdef DO_BENCHMARKING
    utils::unclock(stage_present_total);
    g_pipeline_cycles.present_total += stage_present_total;
    g_pipeline_cycles.fragment_shader += impl::profile_fragment_shader_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.depth += impl::profile_depth_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.merge += impl::profile_merge_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup += impl::profile_raster_setup_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.interp += impl::profile_interp_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_add_triangle += impl::profile_raster_add_triangle_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush += impl::profile_raster_flush_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush_scan += impl::profile_raster_flush_scan_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush_process += impl::profile_raster_flush_process_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush_clear += impl::profile_raster_flush_clear_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush_nonempty_tiles += impl::profile_raster_flush_nonempty_tiles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush_primitives += impl::profile_raster_flush_primitives.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush_count += impl::profile_raster_flush_count.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush_scanned_tiles += impl::profile_raster_flush_scanned_tiles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_block_total += impl::profile_raster_block_total_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_block_fragment += impl::profile_raster_block_fragment_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_block_merge += impl::profile_raster_block_merge_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.triangles_input += impl::profile_triangles_input.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.triangles_culled_degenerate += impl::profile_triangles_culled_degenerate.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.triangles_culled_face += impl::profile_triangles_culled_face.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.triangles_submitted += impl::profile_triangles_submitted.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.triangle_tile_refs += impl::profile_triangle_tile_refs.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.triangle_block_tile_refs += impl::profile_triangle_block_tile_refs.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.triangle_checked_tile_refs += impl::profile_triangle_checked_tile_refs.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_direct_blocks += impl::profile_raster_direct_blocks.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.interp_varying_copies += impl::profile_interp_varying_copies.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.fragment_shader_invocations += impl::profile_fragment_shader_invocations.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.tile_shader_instance_probe_steps += impl::profile_tile_shader_instance_probe_steps.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.clip_vertex_read_bytes += impl::profile_clip_vertex_read_bytes.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.clip_vertex_write_bytes += impl::profile_clip_vertex_write_bytes.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_tile_payload_write_bytes += impl::profile_raster_tile_payload_write_bytes.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_tile_payload_checked_write_bytes += impl::profile_raster_tile_payload_checked_write_bytes.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_tile_payload_block_write_bytes += impl::profile_raster_tile_payload_block_write_bytes.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_tile_info_write_bytes += impl::profile_raster_tile_info_write_bytes.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_interp_write_bytes += impl::profile_raster_interp_write_bytes.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_checked_lambda_write_bytes += impl::profile_raster_checked_lambda_write_bytes.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup_triangle += impl::profile_raster_setup_triangle_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup_bounds += impl::profile_raster_setup_bounds_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup_iterate += impl::profile_raster_setup_iterate_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup_iter_row_setup += impl::profile_raster_setup_iter_row_setup_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup_iter_callback += impl::profile_raster_setup_iter_callback_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup_cb_enqueue += impl::profile_raster_setup_cb_enqueue_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup_cb_flush_inline += impl::profile_raster_setup_cb_flush_inline_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup_cb_direct += impl::profile_raster_setup_cb_direct_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush_max_tile_prims += impl::profile_raster_flush_max_tile_prims.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush_near_full_tiles += impl::profile_raster_flush_near_full_tiles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_flush_trigger_overflow_count += impl::profile_raster_flush_trigger_overflow_count.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup_direct += impl::profile_raster_setup_direct_cycles.exchange(0, std::memory_order_relaxed);
    g_pipeline_cycles.raster_setup_enqueue += impl::profile_raster_setup_enqueue_cycles.exchange(0, std::memory_order_relaxed);
    ++g_pipeline_cycles.frame_count;
    log_pipeline_profile_if_needed();
#endif
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
