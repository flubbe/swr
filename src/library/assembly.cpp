/**
 * swr - a software rasterizer
 *
 * primitive assembly.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{

namespace impl
{

/** polygon orientation used for culling. */
enum class polygon_orientation
{
    not_convex, /** the polygon was not convex. */
    degenerate, /** the polygon is degenerate. */
    cw,         /** clockwise orientation. */
    ccw         /** counter-clockwise orientation. */
};

namespace
{

/**
 * Extract the polygon information out of a line loop, which in turn consists of vertices.
 * Some vertices have markers to indicate where a polygon ends (and thus, where the next starts).
 *
 * @param vb The vertex buffer holding the vertex list.
 * @param start_index The starting vertex of the polygon.
 * @param end_index If and ending marker is detected, this holds the index of the first vertex
 *           greater or equal to start_index having an ending marker.
 * @return If an end marker is found, the function returns true.
 */
bool next_polygon(
  const vertex_buffer& vb,
  std::size_t start_index,
  std::size_t& end_index)
{
    for(std::size_t i = start_index; i < vb.size(); ++i)
    {
        // check for end vertex.
        if(vb[i].flags & geom::vf_line_strip_end)
        {
            end_index = i;
            return true;
        }
    }

    // we did not find an ending marker.
    return false;
}

/*
 * face culling helpers.
 */

/** calculate the signed area of the triangle `(v1, v2, v3)`. */
int triangle_area_sign(
  const ml::vec2 v1,
  const ml::vec2 v2,
  const ml::vec2 v3)
{
    // edge1 = v2-v1, edge2 = v3-v1.
    return (v2 - v1).area_sign(v3 - v1);
}

/**
 * Calculate the orientation of a convex 2d polygon given by the raster coordinates of the vertices.
 *
 * @param vb The vertex buffer holding the vertex list.
 * @param start_vertex The index of the first vertex of the polygon
 * @param end_vertex The index of the last vertex of the polygon.
 * @return Returns if the polygon is oriented clockwise, counter-clockwise, or if it is degenerate.
 *         Additionally, if the function detects non-convexity, it returns polygon_orientation::not_convex.
 */
polygon_orientation get_polygon_orientation(
  const vertex_buffer& vb,
  const std::size_t start_vertex,
  const std::size_t end_vertex)
{
    assert(start_vertex <= end_vertex);
    assert(end_vertex < vb.size());

    // a non-negenerate convex polygon needs to have at least 3 vertices.
    if(end_vertex - start_vertex < 2)
    {
        return polygon_orientation::degenerate;
    }

    // count the local orientation at each corner.
    int positive_corners = 0;
    int negative_corners = 0;

    // loop through the vertex list and calculate the orientation at each corner.
    ml::vec2 v1 = vb[start_vertex].coords.xy();
    ml::vec2 v2 = vb[start_vertex + 1].coords.xy();
    ml::vec2 v3;

    for(std::size_t i = start_vertex + 2; i <= end_vertex; ++i)
    {
        v3 = vb[i].coords.xy();
        int sign = triangle_area_sign(v1, v2, v3);

        v1 = v2;
        v2 = v3;

        positive_corners += (sign > 0);
        negative_corners += (sign < 0);
    }

    // the above loop misses two corners, which we check here separately.
    int sign1 = triangle_area_sign(
      v2,
      v3,
      vb[start_vertex].coords.xy());
    int sign2 = triangle_area_sign(
      v3,
      vb[start_vertex].coords.xy(),
      vb[start_vertex + 1].coords.xy());

    positive_corners += (sign1 > 0) + (sign2 > 0);
    negative_corners += (sign1 < 0) + (sign2 < 0);

    if(positive_corners > 0 && negative_corners == 0)
    {
        return polygon_orientation::cw;
    }
    else if(positive_corners == 0 && negative_corners > 0)
    {
        return polygon_orientation::ccw;
    }
    else if(positive_corners > 0 && negative_corners > 0)
    {
        return polygon_orientation::not_convex;
    }

    return polygon_orientation::degenerate;
}

/**
 * Decide if we should face-cull a polygon with a known orientation.
 *
 * @param cull_mode current cull mode.
 * @param front_face  current front-face mode.
 * @param orientation the polygon's orientation inside the viewport.
 * @return returns true if the polygon should be culled based on the
 *         render states and the polygon's orientation.
 */
bool face_cull_polygon(
  swr::cull_face_direction cull_mode,
  swr::front_face_orientation front_face,
  polygon_orientation orientation)
{
    if(cull_mode == cull_face_direction::front_and_back)
    {
        // reject all polygons.
        return true;
    }

    if(cull_mode == cull_face_direction::front)
    {
        // reject front-facing polygons.
        return (front_face == front_face_orientation::cw
                && orientation == polygon_orientation::cw)
               || (front_face == front_face_orientation::ccw
                   && orientation == polygon_orientation::ccw);
    }
    else if(cull_mode == cull_face_direction::back)
    {
        // reject back-facing polygons.
        return (front_face == front_face_orientation::cw
                && orientation == polygon_orientation::ccw)
               || (front_face == front_face_orientation::ccw
                   && orientation == polygon_orientation::cw);
    }

    // accept.
    return false;
}

/** check if a given face orientation should be rejected based on the cull mode. */
bool cull_reject(
  cull_face_direction mode,
  cull_face_direction test_direction)
{
    return (mode == cull_face_direction::front_and_back)
           || (mode == test_direction);
}

/*
 * primitive assembly profiling helpers.
 */

/** Null profiling sink. Disables profiling. */
struct no_primitive_assembly_profile
{
    /** Count an input point. No-op for the null profiling sink. */
    void point_input() noexcept {}

    /** Count an emitted point. No-op for the null profiling sink. */
    void point_emitted() noexcept {}

    /** Count an input line. No-op for the null profiling sink. */
    void line_input() noexcept {}

    /** Count an emitted line. No-op for the null profiling sink. */
    void line_emitted() noexcept {}

    /** Count an input triangle. No-op for the null profiling sink. */
    void triangle_input() noexcept {}

    /** Count a degenerate (rejected) triangle. No-op for the null profiling sink. */
    void triangle_culled_degenerate() noexcept {}

    /** Count a culled face/triangle. No-op for the null profiling sink. */
    void triangle_culled_face() noexcept {}

    /** Count an emitted triangle. No-op for the null profiling sink. */
    void triangle_emitted() noexcept {}

    /**
     * Accumulates counters from another profile instance.
     *
     * Intended for combining per-thread or per-chunk profiling results into a
     * single aggregate profile.
     *
     * No-op for the null profiling sink.
     */
    void merge(const no_primitive_assembly_profile&) noexcept {}
};

/**
 * Collects statistics for the primitive assembly stage.
 *
 * Instances are typically accumulated per worker/thread and merged into a
 * final aggregate using `merge()`.
 */
struct fill_primitive_assembly_profile
{
    /** Number of points presented to the assembly stage. */
    std::uint64_t point_input_count{0};

    /** Number of points emitted for rasterization. */
    std::uint64_t point_submit_count{0};

    /** Number of lines presented to the assembly stage. */
    std::uint64_t line_input_count{0};

    /** Number of lines emitted for rasterization. */
    std::uint64_t line_submit_count{0};

    /** Number of triangles presented to the assembly stage. */
    std::uint64_t tri_input_count{0};

    /** Number of triangles rejected due to zero area. */
    std::uint64_t tri_cull_degenerate_count{0};

    /** Number of triangles rejected by face culling. */
    std::uint64_t tri_cull_face_count{0};

    /** Number of triangles emitted for rasterization. */
    std::uint64_t tri_submit_count{0};

    /** Count an input point. */
    void point_input() noexcept
    {
        ++point_input_count;
    }

    /** Count an emitted point. */
    void point_emitted() noexcept
    {
        ++point_submit_count;
    }

    /** Count an input line. */
    void line_input() noexcept
    {
        ++line_input_count;
    }

    /** Count an emitted line. */
    void line_emitted() noexcept
    {
        ++line_submit_count;
    }

    /** Count an input triangle. */
    void triangle_input() noexcept
    {
        ++tri_input_count;
    }

    /** Count a degenerate (rejected) triangle. */
    void triangle_culled_degenerate() noexcept
    {
        ++tri_cull_degenerate_count;
    }

    /** Count a culled triangle. */
    void triangle_culled_face() noexcept
    {
        ++tri_cull_face_count;
    }

    /** Count an emitted triangle. */
    void triangle_emitted() noexcept
    {
        ++tri_submit_count;
    }

    /**
     * Accumulates counters from another profile instance.
     *
     * Intended for combining per-thread or per-chunk profiling results into a
     * single aggregate profile.
     */
    void merge(const fill_primitive_assembly_profile& profile) noexcept
    {
        point_input_count += profile.point_input_count;
        point_submit_count += profile.point_submit_count;
        line_input_count += profile.line_input_count;
        line_submit_count += profile.line_submit_count;
        tri_input_count += profile.tri_input_count;
        tri_cull_degenerate_count += profile.tri_cull_degenerate_count;
        tri_cull_face_count += profile.tri_cull_face_count;
        tri_submit_count += profile.tri_submit_count;
    }
};

#ifdef SWR_ENABLE_PIPELINE_PROFILING

using primitive_assembly_profile = fill_primitive_assembly_profile;

/** Submit a profile to the global profiling counters. */
void submit_primitive_assembly_profile(
  const fill_primitive_assembly_profile& profile) noexcept
{
    profile_points_input.fetch_add(
      profile.point_input_count,
      std::memory_order_relaxed);
    profile_points_submitted.fetch_add(
      profile.point_submit_count,
      std::memory_order_relaxed);
    profile_lines_input.fetch_add(
      profile.line_input_count,
      std::memory_order_relaxed);
    profile_lines_submitted.fetch_add(
      profile.line_submit_count,
      std::memory_order_relaxed);
    profile_triangles_input.fetch_add(
      profile.tri_input_count,
      std::memory_order_relaxed);
    profile_triangles_culled_degenerate.fetch_add(
      profile.tri_cull_degenerate_count,
      std::memory_order_relaxed);
    profile_triangles_culled_face.fetch_add(
      profile.tri_cull_face_count,
      std::memory_order_relaxed);
    profile_triangles_submitted.fetch_add(
      profile.tri_submit_count,
      std::memory_order_relaxed);
}
#else /* SWR_ENABLE_PIPELINE_PROFILING */

using primitive_assembly_profile = no_primitive_assembly_profile;

/** Submit a profile to the global profiling counters. No-op for the null profiling sink. */
void submit_primitive_assembly_profile(
  const no_primitive_assembly_profile&) noexcept
{
}

#endif /* SWR_ENABLE_PIPELINE_PROFILING */

/** Provides indexed triangle indices for assembling indexed draw calls. */
struct indexed_triangle_source
{
    /** Triangle indices. */
    std::span<const std::uint32_t> indices;

    /**
     * Return the vertex index of the specified triangle corner.
     *
     * @param tri The triangle ordinal in the indexed triangle list.
     * @param corner The corner within the triangle (0, 1 or 2).
     * @return The vertex buffer index for the requested corner.
     */
    std::size_t operator()(
      std::size_t tri,
      std::size_t corner) const
    {
        assert(tri * 3 + corner < indices.size());
        return indices[tri * 3 + corner];
    }
};

/**
 * Assemble indexed fill triangles over a triangle range.
 *
 * @tparam TriangleSource The callable type used to resolve triangle indices.
 * @tparam EmitTriangle The callback invoked for each emitted triangle.
 * @tparam Profile The profiling sink type used to count assembly statistics.
 * @param states Render state controls for face orientation and culling.
 * @param vb The vertex buffer containing triangle vertices.
 * @param begin_triangle The first triangle index to process.
 * @param end_triangle One past the last triangle index to process.
 * @param profile The profile instance updated during assembly.
 * @param src The triangle source callable that provides vertex indices.
 * @param emit_triangle Callback invoked with resolved triangle indices and facing.
 */
template<
  typename TriangleSource,
  typename EmitTriangle,
  typename Profile = no_primitive_assembly_profile>
void assemble_fill_indexed_triangles_range(
  const render_states* states,
  const vertex_buffer* vb,
  std::size_t begin_triangle,
  std::size_t end_triangle,
  Profile& profile,
  TriangleSource src,
  EmitTriangle emit_triangle)
{
    for(std::size_t tri = begin_triangle; tri < end_triangle; ++tri)
    {
        const std::size_t i1 = src(tri, 0);
        const std::size_t i2 = src(tri, 1);
        const std::size_t i3 = src(tri, 2);
        assert(i1 < vb->size());
        assert(i2 < vb->size());
        assert(i3 < vb->size());

        const auto& v1 = (*vb)[i1];
        const auto& v2 = (*vb)[i2];
        const auto& v3 = (*vb)[i3];

        profile.triangle_input();

        const int area_sign = triangle_area_sign(
          v1.coords.xy(),
          v2.coords.xy(),
          v3.coords.xy());
        if(area_sign == 0)
        {
            profile.triangle_culled_degenerate();
            continue;
        }

        const bool is_front_facing =
          (states->front_face == front_face_orientation::cw
           && area_sign >= 0)
          || (states->front_face == front_face_orientation::ccw
              && area_sign <= 0);

        const cull_face_direction orient =
          is_front_facing
            ? cull_face_direction::front
            : cull_face_direction::back;

        if(states->culling_enabled
           && cull_reject(states->cull_mode, orient))
        {
            profile.triangle_culled_face();
            continue;
        }

        emit_triangle(i1, i2, i3, is_front_facing);
        profile.triangle_emitted();
    }
}

/**
 * Assemble nonindexed fill triangles over a triangle range.
 *
 * @tparam EmitTriangle The callback invoked for each emitted triangle.
 * @tparam Profile The profiling sink type used to count assembly statistics.
 * @param states Render state controls for face orientation and culling.
 * @param vb The vertex buffer containing triangle vertices.
 * @param begin_triangle The first triangle index to process.
 * @param end_triangle One past the last triangle index to process.
 * @param profile The profile instance updated during assembly.
 * @param emit_triangle Callback invoked with the first vertex index and facing.
 */
template<
  typename EmitTriangle,
  typename Profile = no_primitive_assembly_profile>
void assemble_fill_nonindexed_triangles_range(
  const render_states* states,
  const vertex_buffer* vb,
  std::size_t begin_triangle,
  std::size_t end_triangle,
  Profile& profile,
  EmitTriangle emit_triangle)
{
    const std::size_t begin_index = begin_triangle * 3;
    const std::size_t end_index = end_triangle * 3;
    for(std::size_t i = begin_index; i < end_index; i += 3)
    {
        assert(i + 2 < vb->size());

        const auto& v1 = (*vb)[i];
        const auto& v2 = (*vb)[i + 1];
        const auto& v3 = (*vb)[i + 2];

        profile.triangle_input();

        const int area_sign = triangle_area_sign(
          v1.coords.xy(),
          v2.coords.xy(),
          v3.coords.xy());
        if(area_sign == 0)
        {
            profile.triangle_culled_degenerate();
            continue;
        }

        const bool is_front_facing =
          (states->front_face == front_face_orientation::cw
           && area_sign >= 0)
          || (states->front_face == front_face_orientation::ccw
              && area_sign <= 0);

        const cull_face_direction orient =
          is_front_facing
            ? cull_face_direction::front
            : cull_face_direction::back;

        if(states->culling_enabled
           && cull_reject(states->cull_mode, orient))
        {
            profile.triangle_culled_face();
            continue;
        }

        emit_triangle(i, is_front_facing);
        profile.triangle_emitted();
    }
}

#ifdef SWR_ENABLE_MULTI_THREADING

/**
 * Minimum number of triangles required before fill-triangle assembly is
 * distributed across worker threads.
 *
 * This threshold is intended to amortize thread-pool scheduling overhead and
 * may require tuning for different workloads or hardware.
 */
constexpr std::size_t min_parallel_assembly_triangles = 2048;

/**
 * Reference to a nonindexed triangle assembled for later submission.
 *
 * Stores the index of the first vertex of the triangle and its facing.
 */
struct nonindexed_assembled_triangle_ref
{
    /** Index of the first vertex. */
    std::size_t index{0};

    /** Whether the triangle is front-facing. */
    bool is_front_facing{false};
};

/** Reference to an indexed triangle assembled for later submission. */
struct indexed_assembled_triangle_ref
{
    /** Index of the first triangle vertex. */
    std::size_t i1{0};

    /** Index of the second triangle vertex. */
    std::size_t i2{0};

    /** Index of the third triangle vertex. */
    std::size_t i3{0};

    /** Whether the triangle is front-facing. */
    bool is_front_facing{false};
};

/**
 * Chunk of assembled triangles produced by parallel assembly.
 *
 * Holds the collected triangle references and the associated profile.
 */
template<typename TriangleRef>
struct assembled_triangle_chunk
{
    std::vector<TriangleRef> triangles;

    /** Profile. Type might be empty. */
    [[no_unique_address]] primitive_assembly_profile profile;
};

using nonindexed_assembled_triangle_chunk =
  assembled_triangle_chunk<nonindexed_assembled_triangle_ref>;
using indexed_assembled_triangle_chunk =
  assembled_triangle_chunk<indexed_assembled_triangle_ref>;

/**
 * Assemble nonindexed triangles into an output chunk.
 *
 * The chunk is cleared and refilled with references to assembled triangles,
 * and the provided profile is updated with assembly statistics.
 */
void assemble_fill_nonindexed_triangles_chunk(
  const render_states* states,
  const vertex_buffer* vb,
  std::size_t begin_triangle,
  std::size_t end_triangle,
  nonindexed_assembled_triangle_chunk* out)
{
    out->triangles.clear();
    out->triangles.reserve(end_triangle - begin_triangle);
    out->profile = {};

    assemble_fill_nonindexed_triangles_range(
      states,
      vb,
      begin_triangle,
      end_triangle,
      out->profile,
      [out](
        std::size_t index,
        bool is_front_facing)
      {
          out->triangles.emplace_back(
            nonindexed_assembled_triangle_ref{index, is_front_facing});
      });
}

/**
 * Assemble indexed triangles into an output chunk.
 *
 * The chunk is cleared and refilled with references to assembled triangles,
 * and the provided profile is updated with assembly statistics.
 */
template<typename TriangleSource>
void assemble_fill_indexed_triangles_chunk(
  const render_states* states,
  const vertex_buffer* vb,
  std::size_t begin_triangle,
  std::size_t end_triangle,
  indexed_assembled_triangle_chunk* out,
  TriangleSource src)
{
    out->triangles.clear();
    out->triangles.reserve(end_triangle - begin_triangle);
    out->profile = {};

    assemble_fill_indexed_triangles_range(
      states,
      vb,
      begin_triangle,
      end_triangle,
      out->profile,
      src,
      [out](
        std::size_t i1,
        std::size_t i2,
        std::size_t i3,
        bool is_front_facing)
      {
          out->triangles.emplace_back(
            indexed_assembled_triangle_ref{i1, i2, i3, is_front_facing});
      });
}

/** Submit assembled nonindexed fill triangles from a worker chunk to the rasterizer. */
void submit_assembled_fill_nonindexed_triangles_chunk(
  rast::rasterizer* rasterizer,
  const render_states* states,
  vertex_buffer& vb,
  const nonindexed_assembled_triangle_chunk& chunk)
{
    for(const auto& tri: chunk.triangles)
    {
        auto& v1 = vb[tri.index];
        auto& v2 = vb[tri.index + 1];
        auto& v3 = vb[tri.index + 2];
        rasterizer->add_triangle(
          states,
          tri.is_front_facing,
          &v1,
          &v2,
          &v3);
    }
}

/** Submit assembled indexed fill triangles from a worker chunk to the rasterizer. */
void submit_assembled_fill_indexed_triangles_chunk(
  rast::rasterizer* rasterizer,
  const render_states* states,
  vertex_buffer& vb,
  const indexed_assembled_triangle_chunk& chunk)
{
    for(const auto& tri: chunk.triangles)
    {
        auto& v1 = vb[tri.i1];
        auto& v2 = vb[tri.i2];
        auto& v3 = vb[tri.i3];
        rasterizer->add_triangle(
          states,
          tri.is_front_facing,
          &v1,
          &v2,
          &v3);
    }
}

#endif /* SWR_ENABLE_MULTI_THREADING */

} /* namespace */

void render_context::assemble_primitives(
  const render_states* states,
  vertex_buffer_mode mode,
  vertex_buffer& vb)
{
    primitive_assembly_profile profile;

    // choose drawing mode.
    if(mode == vertex_buffer_mode::points
       || states->poly_mode == polygon_mode::point)
    {
        /* draw a list of points */
        for(auto& vertex_it: vb)
        {
            profile.point_input();

            rasterizer->add_point(states, &vertex_it);
            profile.point_emitted();
        }
    }
    else if(mode == vertex_buffer_mode::lines)
    {
        /* draw a list of lines */
        std::size_t size = vb.size() & ~1;
        for(std::size_t i = 0; i < size; i += 2)
        {
            profile.line_input();

            rasterizer->add_line(states, &vb[i], &vb[i + 1]);
            profile.line_emitted();
        }
    }
    else if(mode == vertex_buffer_mode::triangles)
    {
        if(vb.size() < 3)
        {
            return;
        }

        // depending on the polygon mode, the vertex buffer either holds a list of triangles or a list of points.
        if(states->poly_mode == polygon_mode::line)
        {
            std::size_t last_index = 0;
            for(std::size_t first_index = 0; first_index < vb.size(); first_index = last_index + 1)
            {
                // last_index gets updated here.
                if(!next_polygon(vb, first_index, last_index))
                {
                    // no polygon found.
                    break;
                }

                // culling.
                if(states->culling_enabled)
                {
                    auto orientation = get_polygon_orientation(
                      vb,
                      first_index,
                      last_index);
                    if(orientation == polygon_orientation::not_convex
                       || orientation == polygon_orientation::degenerate)
                    {
                        // do not consider degenerate polygons or non-convex ones.
                        continue;
                    }

                    if(face_cull_polygon(
                         states->cull_mode,
                         states->front_face,
                         orientation))
                    {
                        // don't draw.
                        continue;
                    }
                }

                // add the lines to the rasterizer.
                auto* first_vertex = &vb[first_index];
                auto* prev_vertex = first_vertex;

                for(std::size_t i = first_index + 1; i <= last_index; ++i)
                {
                    auto* cur_vertex = &vb[i];

                    // Add the current line to the rasterizer.
                    rasterizer->add_line(states, prev_vertex, cur_vertex);
                    profile.line_emitted();

                    prev_vertex = cur_vertex;
                }
                // close the strip.
                rasterizer->add_line(states, prev_vertex, first_vertex);
                profile.line_emitted();
            }
        }
        else if(states->poly_mode == polygon_mode::fill)
        {
            const std::size_t triangle_count = vb.size() / 3;
#ifdef SWR_ENABLE_MULTI_THREADING
            const std::size_t thread_count = thread_pool.get_thread_count();
            const bool do_parallel_assembly =
              thread_count > 1
              && triangle_count >= min_parallel_assembly_triangles;

            if(do_parallel_assembly)
            {
                const std::size_t task_count = std::min(thread_count, triangle_count);
                std::vector<nonindexed_assembled_triangle_chunk> chunks(task_count);

                for(std::size_t t = 0; t < task_count; ++t)
                {
                    const std::size_t begin_triangle = (t * triangle_count) / task_count;
                    const std::size_t end_triangle = ((t + 1) * triangle_count) / task_count;

                    thread_pool.push_immediate_task(
                      assemble_fill_nonindexed_triangles_chunk,
                      states,
                      &vb,
                      begin_triangle,
                      end_triangle,
                      &chunks[t]);
                }

                thread_pool.run_tasks_and_wait();

                for(const auto& chunk: chunks)
                {
                    profile.merge(chunk.profile);

                    submit_assembled_fill_nonindexed_triangles_chunk(
                      rasterizer.get(),
                      states,
                      vb,
                      chunk);
                }
            }
            else
            {
#endif /* SWR_ENABLE_MULTI_THREADING */
                auto* rasterizer_ptr = rasterizer.get();
                assemble_fill_nonindexed_triangles_range(
                  states,
                  &vb,
                  0,
                  triangle_count,
                  profile,
                  [rasterizer_ptr, states, &vb](
                    std::size_t i,
                    bool is_front_facing)
                  {
                      auto& v1 = vb[i];
                      auto& v2 = vb[i + 1];
                      auto& v3 = vb[i + 2];
                      rasterizer_ptr->add_triangle(
                        states,
                        is_front_facing,
                        &v1,
                        &v2,
                        &v3);
                  });
#ifdef SWR_ENABLE_MULTI_THREADING
            }
#endif /* SWR_ENABLE_MULTI_THREADING */
        }
        else
        {
            // this intentionally breaks the debugger.
            assert(states->poly_mode == polygon_mode::line || states->poly_mode == polygon_mode::fill);
        }
    }

    submit_primitive_assembly_profile(profile);
}

void render_context::assemble_indexed_primitives(
  const render_states* states,
  vertex_buffer_mode mode,
  vertex_buffer& vb,
  std::span<const std::uint32_t> indices)
{
    primitive_assembly_profile profile;

    if(indices.empty())
    {
        return;
    }

    if(mode == vertex_buffer_mode::points
       || states->poly_mode == polygon_mode::point)
    {
        for(const std::uint32_t index: indices)
        {
            assert(index < vb.size());

            profile.point_input();

            rasterizer->add_point(states, &vb[index]);
            profile.point_emitted();
        }
    }
    else if(mode == vertex_buffer_mode::lines)
    {
        const std::size_t size = indices.size() & ~std::size_t{1};
        for(std::size_t i = 0; i < size; i += 2)
        {
            assert(indices[i] < vb.size());
            assert(indices[i + 1] < vb.size());

            profile.line_input();

            rasterizer->add_line(states, &vb[indices[i]], &vb[indices[i + 1]]);
            profile.line_emitted();
        }
    }
    else if(mode == vertex_buffer_mode::triangles
            && states->poly_mode == polygon_mode::line)
    {
        const std::size_t size = (indices.size() / 3) * 3;
        for(std::size_t i = 0; i < size; i += 3)
        {
            assert(indices[i] < vb.size());
            assert(indices[i + 1] < vb.size());
            assert(indices[i + 2] < vb.size());

            auto& v1 = vb[indices[i]];
            auto& v2 = vb[indices[i + 1]];
            auto& v3 = vb[indices[i + 2]];

            profile.triangle_input();

            if(states->culling_enabled)
            {
                const int area_sign =
                  triangle_area_sign(v1.coords.xy(), v2.coords.xy(), v3.coords.xy());
                if(area_sign == 0)
                {
                    profile.triangle_culled_degenerate();
                    continue;
                }

                const bool is_front_facing =
                  (states->front_face == front_face_orientation::cw && area_sign >= 0)
                  || (states->front_face == front_face_orientation::ccw && area_sign <= 0);
                const cull_face_direction orient =
                  is_front_facing
                    ? cull_face_direction::front
                    : cull_face_direction::back;
                if(cull_reject(states->cull_mode, orient))
                {
                    profile.triangle_culled_face();
                    continue;
                }
            }

            rasterizer->add_line(states, &v1, &v2);
            profile.line_emitted();

            rasterizer->add_line(states, &v2, &v3);
            profile.line_emitted();

            rasterizer->add_line(states, &v3, &v1);
            profile.line_emitted();
        }
    }
    else if(mode == vertex_buffer_mode::triangles
            && states->poly_mode == polygon_mode::fill)
    {
        const std::size_t triangle_count = indices.size() / 3;
        if(triangle_count == 0)
        {
            return;
        }

#ifdef SWR_ENABLE_MULTI_THREADING
        const std::size_t thread_count = thread_pool.get_thread_count();
        const bool do_parallel_assembly =
          thread_count > 1
          && triangle_count >= min_parallel_assembly_triangles;

        if(do_parallel_assembly)
        {
            const std::size_t task_count = std::min(thread_count, triangle_count);
            std::vector<indexed_assembled_triangle_chunk> chunks(task_count);

            for(std::size_t t = 0; t < task_count; ++t)
            {
                const std::size_t begin_triangle = (t * triangle_count) / task_count;
                const std::size_t end_triangle = ((t + 1) * triangle_count) / task_count;

                thread_pool.push_immediate_task(
                  assemble_fill_indexed_triangles_chunk<indexed_triangle_source>,
                  states,
                  &vb,
                  begin_triangle,
                  end_triangle,
                  &chunks[t],
                  indexed_triangle_source{indices});
            }

            thread_pool.run_tasks_and_wait();

            for(const auto& chunk: chunks)
            {
                profile.merge(chunk.profile);

                submit_assembled_fill_indexed_triangles_chunk(
                  rasterizer.get(),
                  states,
                  vb,
                  chunk);
            }
        }
        else
        {
#endif /* SWR_ENABLE_MULTI_THREADING */
            auto* rasterizer_ptr = rasterizer.get();
            assemble_fill_indexed_triangles_range(
              states,
              &vb,
              0,
              triangle_count,
              profile,
              indexed_triangle_source{indices},
              [rasterizer_ptr, states, &vb](
                std::size_t i1,
                std::size_t i2,
                std::size_t i3,
                bool is_front_facing)
              {
                  auto& v1 = vb[i1];
                  auto& v2 = vb[i2];
                  auto& v3 = vb[i3];
                  rasterizer_ptr->add_triangle(
                    states,
                    is_front_facing,
                    &v1,
                    &v2,
                    &v3);
              });
#ifdef SWR_ENABLE_MULTI_THREADING
        }
#endif /* SWR_ENABLE_MULTI_THREADING */
    }

    submit_primitive_assembly_profile(profile);
}

} /* namespace impl */

} /* namespace swr */
