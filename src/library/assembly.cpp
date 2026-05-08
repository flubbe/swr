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

/**
 * Extract the polygon information out of a line loop, which in turn consists of vertices.
 * Some vertices have markers to indicate where a polygon ends (and thus, where the next starts).
 *
 * @param vb The vertex buffer holding the vertex list.
 * @param start_index The starting vertex of the polygon.
 * @param end_index If and ending marker is detected, this holds the index of the first vertex greater or equal to start_index having an ending marker.
 * @return If an end marker is found, the function returns true.
 */
static bool next_polygon(
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

/** calculate the signed area of the triangle (v1,v2,v3). */
static int triangle_area_sign(
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
static polygon_orientation get_polygon_orientation(
  const vertex_buffer& vb,
  const std::size_t start_vertex,
  const std::size_t end_vertex)
{
    assert(end_vertex < vb.size());

    // a non-negenerate convex polygon needs to have at least 3 vertices.
    if(start_vertex + 2 > end_vertex)
    {
        return polygon_orientation::degenerate;
    }

    // count the local orientation at each corner.
    int positive_corners = 0;
    int negative_corners = 0;

    // loop through the vertex list and calculate the orientation at each corner.
    auto v1 = vb[start_vertex].coords.xy();
    auto v2 = vb[start_vertex + 1].coords.xy();
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
 * @return returns true if the polygon should be culled based on the render states and the polygon's orientation.
 */
static bool face_cull_polygon(
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
static bool cull_reject(
  cull_face_direction mode,
  cull_face_direction test_direction)
{
    return (mode == cull_face_direction::front_and_back)
           || (mode == test_direction);
}

#ifdef SWR_ENABLE_MULTI_THREADING

namespace
{

constexpr std::size_t min_parallel_assembly_triangles = 2048;

struct assembled_triangle_ref
{
    std::size_t index{0};
    bool is_front_facing{false};
};

struct assembled_triangle_chunk
{
    std::vector<assembled_triangle_ref> triangles;

#    ifdef DO_BENCHMARKING
    std::uint64_t tri_input_count{0};
    std::uint64_t tri_cull_degenerate_count{0};
    std::uint64_t tri_cull_face_count{0};
    std::uint64_t tri_submit_count{0};
#    endif
};

static void assemble_fill_triangles_chunk(
  const render_states* states,
  const vertex_buffer* vb,
  std::size_t begin_triangle,
  std::size_t end_triangle,
  assembled_triangle_chunk* out)
{
    out->triangles.clear();
    out->triangles.reserve(end_triangle - begin_triangle);

    for(std::size_t tri = begin_triangle; tri < end_triangle; ++tri)
    {
        const std::size_t i = tri * 3;
        auto& v1 = (*vb)[i];
        auto& v2 = (*vb)[i + 1];
        auto& v3 = (*vb)[i + 2];

#    ifdef DO_BENCHMARKING
        ++out->tri_input_count;
#    endif

        const int area_sign = triangle_area_sign(
          v1.coords.xy(),
          v2.coords.xy(),
          v3.coords.xy());

        if(area_sign == 0)
        {
#    ifdef DO_BENCHMARKING
            ++out->tri_cull_degenerate_count;
#    endif

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
#    ifdef DO_BENCHMARKING
            ++out->tri_cull_face_count;
#    endif

            continue;
        }

        out->triangles.emplace_back(assembled_triangle_ref{i, is_front_facing});

#    ifdef DO_BENCHMARKING
        ++out->tri_submit_count;
#    endif
    }
}

} /* namespace */

#endif /* SWR_ENABLE_MULTI_THREADING */

void render_context::assemble_primitives(
  const render_states* states,
  vertex_buffer_mode mode,
  vertex_buffer& vb)
{
#ifdef DO_BENCHMARKING
    std::uint64_t tri_input_count = 0;
    std::uint64_t tri_cull_degenerate_count = 0;
    std::uint64_t tri_cull_face_count = 0;
    std::uint64_t tri_submit_count = 0;
#endif

    // choose drawing mode.
    if(mode == vertex_buffer_mode::points
       || states->poly_mode == polygon_mode::point)
    {
        /* draw a list of points */
        for(auto& vertex_it: vb)
        {
            rasterizer->add_point(states, &vertex_it);
        }
    }
    else if(mode == vertex_buffer_mode::lines)
    {
        /* draw a list of lines */
        std::size_t size = vb.size() & ~1;
        for(std::size_t i = 0; i < size; i += 2)
        {
            rasterizer->add_line(states, &vb[i], &vb[i + 1]);
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
                // note that LastIndex gets updated here.
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

                    prev_vertex = cur_vertex;
                }
                // close the strip.
                rasterizer->add_line(states, prev_vertex, first_vertex);
            }
        }
        else if(states->poly_mode == polygon_mode::fill)
        {
#ifdef SWR_ENABLE_MULTI_THREADING
            const std::size_t triangle_count = vb.size() / 3;
            const std::size_t thread_count = thread_pool.get_thread_count();
            const bool do_parallel_assembly =
              thread_count > 1
              && triangle_count >= min_parallel_assembly_triangles;

            if(do_parallel_assembly)
            {
                const std::size_t task_count = std::min(thread_count, triangle_count);
                std::vector<assembled_triangle_chunk> chunks(task_count);

                for(std::size_t t = 0; t < task_count; ++t)
                {
                    const std::size_t begin_triangle = (t * triangle_count) / task_count;
                    const std::size_t end_triangle = ((t + 1) * triangle_count) / task_count;

                    thread_pool.push_immediate_task(
                      assemble_fill_triangles_chunk,
                      states,
                      &vb,
                      begin_triangle,
                      end_triangle,
                      &chunks[t]);
                }

                thread_pool.run_tasks_and_wait();

                for(const auto& chunk: chunks)
                {
#    ifdef DO_BENCHMARKING
                    tri_input_count += chunk.tri_input_count;
                    tri_cull_degenerate_count += chunk.tri_cull_degenerate_count;
                    tri_cull_face_count += chunk.tri_cull_face_count;
                    tri_submit_count += chunk.tri_submit_count;
#    endif

                    for(const auto& tri: chunk.triangles)
                    {
                        auto& v1 = vb[tri.index];
                        auto& v2 = vb[tri.index + 1];
                        auto& v3 = vb[tri.index + 2];
                        rasterizer->add_triangle(states, tri.is_front_facing, &v1, &v2, &v3);
                    }
                }
            }
            else
            {
#endif /* SWR_ENABLE_MULTI_THREADING */
                /* draw a list of triangles */
                for(std::size_t i = 0; i < vb.size(); i += 3)
                {
                    auto& v1 = vb[i];
                    auto& v2 = vb[i + 1];
                    auto& v3 = vb[i + 2];

#ifdef DO_BENCHMARKING
                    ++tri_input_count;
#endif

                    const int area_sign = triangle_area_sign(v1.coords.xy(), v2.coords.xy(), v3.coords.xy());
                    if(area_sign == 0)
                    {
#ifdef DO_BENCHMARKING
                        ++tri_cull_degenerate_count;
#endif
                        continue;
                    }

                    const bool is_front_facing =
                      (states->front_face == front_face_orientation::cw && area_sign >= 0)
                      || (states->front_face == front_face_orientation::ccw && area_sign <= 0);

                    const cull_face_direction orient =
                      is_front_facing
                        ? cull_face_direction::front
                        : cull_face_direction::back;

                    if(states->culling_enabled
                       && cull_reject(states->cull_mode, orient))
                    {
                        // reject
#ifdef DO_BENCHMARKING
                        ++tri_cull_face_count;
#endif
                        continue;
                    }

                    rasterizer->add_triangle(states, is_front_facing, &v1, &v2, &v3);
#ifdef DO_BENCHMARKING
                    ++tri_submit_count;
#endif
                }
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

#ifdef DO_BENCHMARKING
    if(mode == vertex_buffer_mode::triangles
       && states->poly_mode == polygon_mode::fill)
    {
        profile_triangles_input.fetch_add(tri_input_count, std::memory_order_relaxed);
        profile_triangles_culled_degenerate.fetch_add(tri_cull_degenerate_count, std::memory_order_relaxed);
        profile_triangles_culled_face.fetch_add(tri_cull_face_count, std::memory_order_relaxed);
        profile_triangles_submitted.fetch_add(tri_submit_count, std::memory_order_relaxed);
    }
#endif
}

} /* namespace impl */

} /* namespace swr */
