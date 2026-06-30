/**
 * swr - a software rasterizer
 *
 * triangle clipping in homogeneous coordinates.
 *
 * references:
 *  [1] http://fabiensanglard.net/polygon_codec/
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <span>
#include <vector>
#include <algorithm>
#include <limits>

#include <boost/container/static_vector.hpp>

/* user headers. */
#include "swr_internal.h"
#include "clipping.h"

namespace swr
{

namespace impl
{

/*
 * From https://fabiensanglard.net/polygon_codec/:
 *
 *    The clipping actually produces vertices with a W=0 component. That would cause a
 *    divide by zero. A way to solve this is to clip against the W=0.00001 plane.
 */
constexpr float W_CLIPPING_PLANE = 1e-5f;

/**
 * We scale the calculated intersection parameter slightly to account for floating-point inaccuracies.
 * The scaling is always towards the vertex outside the clipping region.
 */
constexpr float SCALE_INTERSECTION_PARAMETER = 1.0001f;

constexpr std::size_t MAX_CLIPPED_TRIANGLE_VERTICES = 16;

using clipped_vertex_buffer =
  boost::container::static_vector<
    geom::vertex,
    MAX_CLIPPED_TRIANGLE_VERTICES>;

/**
 * clip with respect to these axes. more precisely, clip against the
 * planes with plane equations (x=w,x=-w), (y=w,y=-w), (z=w,z=-w).
 */
enum clip_axis
{
    x_axis = 0,
    y_axis = 1,
    z_axis = 2
};

namespace clip_detail
{

enum class plane_kind
{
    axis_positive,    // axis <= w
    axis_negative,    // -axis <= w
    w_min             // w >= W_CLIPPING_PLANE
};

struct plane
{
    plane_kind kind;
    clip_axis axis;
};

static float plane_eval(
  const geom::vertex& v,
  const plane p)
{
    switch(p.kind)
    {
    case plane_kind::axis_positive:
        return v.coords.w - v.coords[p.axis];
    case plane_kind::axis_negative:
        return v.coords.w + v.coords[p.axis];
    case plane_kind::w_min:
        return v.coords.w - W_CLIPPING_PLANE;
    }

    assert(false);
    return 0.0f;
}

static bool is_inside(
  const geom::vertex& v,
  const plane p)
{
    return plane_eval(v, p) >= 0.0f;
}

static float intersection_parameter_raw(
  const geom::vertex& inside_vert,
  const geom::vertex& outside_vert,
  const plane p)
{
    const float inside_eval = plane_eval(inside_vert, p);
    const float outside_eval = plane_eval(outside_vert, p);

    assert(inside_eval >= 0.0f);
    assert(outside_eval < 0.0f);

    const float denom = inside_eval - outside_eval;
    assert(denom > 0.0f);

    const float t = inside_eval / denom;
    assert(t >= 0.0f && t <= 1.0f);

    return t;
}

static geom::vertex intersect(
  const geom::vertex& inside_vert,
  const geom::vertex& outside_vert,
  const plane p)
{
    float t = intersection_parameter_raw(inside_vert, outside_vert, p);

    // axis-plane clipping gets the small outward bias.
    if(p.kind != plane_kind::w_min)
    {
        t *= SCALE_INTERSECTION_PARAMETER;
    }

    return lerp(t, inside_vert, outside_vert);
}

/**
 * Pure clipping of a single open line segment (2 vertices) against one plane.
 *
 * Returns:
 *   - 0 vertices if fully outside
 *   - 2 vertices if partially or fully visible
 *
 * This is intentionally not treated as a closed polygon.
 */
static clipped_vertex_buffer clip_line_segment_against_plane(
  std::span<const geom::vertex> in_line,
  const plane p)
{
    clipped_vertex_buffer out;

    if(in_line.size() != 2)
    {
        return out;
    }

    const geom::vertex& a = in_line[0];
    const geom::vertex& b = in_line[1];

    const bool a_inside = is_inside(a, p);
    const bool b_inside = is_inside(b, p);

    out.reserve(2);

    if(a_inside)
    {
        out.emplace_back(a);
    }

    if(a_inside != b_inside)
    {
        const geom::vertex& inside_vert = a_inside ? a : b;
        const geom::vertex& outside_vert = a_inside ? b : a;
        out.emplace_back(intersect(inside_vert, outside_vert, p));
    }

    if(b_inside)
    {
        out.emplace_back(b);
    }

    return out;
}

/**
 * Pure Sutherland-Hodgman clipping of a closed polygon against one plane.
 *
 * Handles triangles and general polygons.
 */
static void clip_closed_polygon_against_plane(
  std::span<const geom::vertex> in_poly,
  plane p,
  clipped_vertex_buffer& out)
{
    out.clear();

    if(in_poly.empty())
        return;

    out.reserve(in_poly.size() + 1);

    const geom::vertex* prev_vert = &in_poly.back();
    bool prev_inside = is_inside(*prev_vert, p);

    for(const geom::vertex& vert: in_poly)
    {
        const bool cur_inside = is_inside(vert, p);

        if(prev_inside && cur_inside)
        {
            out.emplace_back(vert);
        }
        else if(prev_inside && !cur_inside)
        {
            out.emplace_back(intersect(*prev_vert, vert, p));
        }
        else if(!prev_inside && cur_inside)
        {
            out.emplace_back(intersect(vert, *prev_vert, p));
            out.emplace_back(vert);
        }

        prev_vert = &vert;
        prev_inside = cur_inside;
    }
}

static std::span<const geom::vertex> clip_polygon_against_enabled_frustum_planes(
  std::span<const std::uint32_t> vertex_flags,
  std::span<const geom::vertex> input,
  clipped_vertex_buffer& a,
  clipped_vertex_buffer& b)
{
    assert(vertex_flags.size() == input.size());

    std::uint32_t flags = 0;
    for(std::uint32_t f: vertex_flags)
    {
        flags |= f;
    }

    flags &= geom::vf_clip_discard;

    auto cur = input;
    clipped_vertex_buffer* out = &a;

    auto clip_if_needed = [&](std::uint32_t flag, plane p)
    {
        if((flags & flag) == 0 || cur.empty())
        {
            return;
        }

        clip_closed_polygon_against_plane(cur, p, *out);
        cur = std::span<const geom::vertex>{out->data(), out->size()};
        out = out == &a ? &b : &a;
    };

    clip_if_needed(geom::vf_clip_w_min, plane{plane_kind::w_min, z_axis});

    clip_if_needed(geom::vf_clip_x_max, plane{plane_kind::axis_positive, x_axis});
    clip_if_needed(geom::vf_clip_x_min, plane{plane_kind::axis_negative, x_axis});

    clip_if_needed(geom::vf_clip_y_max, plane{plane_kind::axis_positive, y_axis});
    clip_if_needed(geom::vf_clip_y_min, plane{plane_kind::axis_negative, y_axis});

    clip_if_needed(geom::vf_clip_z_max, plane{plane_kind::axis_positive, z_axis});
    clip_if_needed(geom::vf_clip_z_min, plane{plane_kind::axis_negative, z_axis});

    return cur;
}

static clipped_vertex_buffer clip_line_against_enabled_frustum_planes(const clipped_vertex_buffer& line)
{
    clipped_vertex_buffer result = clip_line_segment_against_plane(
      {line.data(), line.size()},
      plane{plane_kind::w_min, z_axis});

    result = clip_line_segment_against_plane({result.data(), result.size()}, plane{plane_kind::axis_positive, x_axis});
    result = clip_line_segment_against_plane({result.data(), result.size()}, plane{plane_kind::axis_negative, x_axis});
    result = clip_line_segment_against_plane({result.data(), result.size()}, plane{plane_kind::axis_positive, y_axis});
    result = clip_line_segment_against_plane({result.data(), result.size()}, plane{plane_kind::axis_negative, y_axis});
    result = clip_line_segment_against_plane({result.data(), result.size()}, plane{plane_kind::axis_positive, z_axis});
    result = clip_line_segment_against_plane({result.data(), result.size()}, plane{plane_kind::axis_negative, z_axis});

    return result;
}

static geom::vertex load_vertex(
  const render_object& obj,
  const std::uint32_t index)
{
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    const std::uint32_t varying_count = obj.states.shader_info->varying_count;
    constexpr std::uint64_t coord_bytes = sizeof(ml::vec4);
    constexpr std::uint64_t flag_bytes = sizeof(std::uint32_t);
    const std::uint64_t varying_bytes = static_cast<std::uint64_t>(varying_count) * sizeof(ml::vec4);
    swr::impl::profile_clip_vertex_read_bytes.fetch_add(coord_bytes + flag_bytes + varying_bytes, std::memory_order_relaxed);
    swr::impl::profile_clip_vertex_write_bytes.fetch_add(coord_bytes + flag_bytes + varying_bytes, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    geom::vertex v;
    v.coords = obj.coords[index];
    const auto vertex_varyings = obj.varyings_for_vertex(index);
    v.varyings.assign(
      std::begin(vertex_varyings),
      std::end(vertex_varyings));

    v.flags = obj.vertex_flags[index];
    return v;
}

static clipped_vertex_buffer load_line_vertices(
  const render_object& obj,
  const std::array<std::uint32_t, 2>& indices)
{
    clipped_vertex_buffer line;
    line.reserve(2);
    line.emplace_back(load_vertex(obj, indices[0]));
    line.emplace_back(load_vertex(obj, indices[1]));
    return line;
}

static clipped_vertex_buffer load_triangle_vertices(
  const render_object& obj,
  const std::array<std::uint32_t, 3> indices,
  clipped_vertex_buffer& tri)
{
    tri.clear();
    tri.reserve(3);

    const std::uint32_t varying_count = obj.states.shader_info->varying_count;
    const ml::vec4* provoking_vertex_varyings = nullptr;
    if(varying_count > 0)
    {
        provoking_vertex_varyings = obj.varyings_for_vertex(indices[0]).data();
    }

    auto append_vertex = [&](std::uint32_t index)
    {
        geom::vertex v = load_vertex(obj, index);
        v.provoking_vertex_varyings = provoking_vertex_varyings;
        tri.emplace_back(v);
    };

    append_vertex(indices[0]);
    append_vertex(indices[1]);
    append_vertex(indices[2]);

    return tri;
}

static void append_vertices(
  vertex_buffer& dst,
  std::span<const geom::vertex> src)
{
    dst.insert(dst.end(), src.begin(), src.end());
}

static float ndc_bbox_area(
  const ml::vec2& a,
  const ml::vec2& b,
  const ml::vec2& c)
{
    const float x_min = std::min({a.x, b.x, c.x});
    const float x_max = std::max({a.x, b.x, c.x});
    const float y_min = std::min({a.y, b.y, c.y});
    const float y_max = std::max({a.y, b.y, c.y});

    return (x_max - x_min) * (y_max - y_min);
}

static void append_convex_polygon_as_fan_min_projected_bbox(
  vertex_buffer& out_vertices,
  std::span<const geom::vertex> poly)
{
    const std::size_t n = poly.size();
    assert(n >= 3);

    if(n == 3)
    {
        append_vertices(out_vertices, poly);
        return;
    }

    assert(n <= MAX_CLIPPED_TRIANGLE_VERTICES);
    boost::container::static_vector<ml::vec2, MAX_CLIPPED_TRIANGLE_VERTICES> ndc_coords;

    for(const geom::vertex& v: poly)
    {
        const float inv_w = 1.0f / v.coords.w;    // FIXME division
        ndc_coords.emplace_back(ml::vec2{
          v.coords.x * inv_w,
          v.coords.y * inv_w});
    }

    std::size_t best_center = 0;
    float best_cost = std::numeric_limits<float>::infinity();

    for(std::size_t center = 0; center < n; ++center)
    {
        float cost = 0.0f;
        for(std::size_t step = 1; step + 1 < n; ++step)
        {
            const ml::vec2& a = ndc_coords[center];
            const ml::vec2& b = ndc_coords[(center + step) % n];
            const ml::vec2& c = ndc_coords[(center + step + 1) % n];
            cost += ndc_bbox_area(a, b, c);
        }

        if(cost < best_cost)
        {
            best_cost = cost;
            best_center = center;
        }
    }

    const geom::vertex& center = poly[best_center];
    for(std::size_t step = 1; step + 1 < n; ++step)
    {
        const geom::vertex& b = poly[(best_center + step) % n];
        const geom::vertex& c = poly[(best_center + step + 1) % n];
        out_vertices.emplace_back(center);
        out_vertices.emplace_back(b);
        out_vertices.emplace_back(c);
    }
}

}    // namespace clip_detail

/**
 * Clip a vertex buffer/index buffer pair against the view frustum. the index buffer/vertex buffer pair is assumed
 * to contain a line list, i.e., if i is divisible by 2, then in_ib[i] and in_ib[i+1] need to be indices into in_vb
 * forming a line.
 */
void clip_line_buffer(
  render_object& obj,
  clip_output output_type)
{
    obj.clear_clipped_output();
    clip_line_buffer_range(
      obj,
      output_type,
      0,
      obj.indices.size(),
      obj.clipped_vertices);
    obj.use_expanded_clipped_vertices();
}

void clip_line_buffer_range(
  const render_object& obj,
  clip_output output_type,
  std::size_t index_begin,
  std::size_t index_end,
  vertex_buffer& out_vertices)
{
    /*
     * Algorithm:
     *
     *  i)   Loop over lines
     *  ii)  If the lines contains a discarded vertex, do clipping and copy resulting line to temporary buffer
     *  iii) Copy all temporary lines to the output vertex buffer.
     */

    assert((index_begin % 2) == 0);
    assert((index_end % 2) == 0);
    assert(index_begin <= index_end);
    assert(index_end <= obj.indices.size());

    out_vertices.clear();
    out_vertices.reserve(index_end - index_begin);

    for(std::size_t index_it = index_begin; index_it < index_end; index_it += 2)
    {
        const std::array<std::uint32_t, 2> indices = {
          obj.indices[index_it],
          obj.indices[index_it + 1]};

        const std::uint32_t f0 = obj.vertex_flags[indices[0]] & geom::vf_clip_discard;
        const std::uint32_t f1 = obj.vertex_flags[indices[1]] & geom::vf_clip_discard;

        const std::uint32_t any_outside = f0 | f1;
        const std::uint32_t all_outside = f0 & f1;

        if(any_outside == 0)
        {
            // trivial accept.
            if(output_type == point_list
               || output_type == line_list)
            {
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[0]));
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[1]));
            }
        }
        else if(all_outside != 0)
        {
            // trivially reject: all vertices outside at least one same plane.
        }
        else
        {
            // partially clipped.

            clipped_vertex_buffer clipped_line =
              clip_detail::clip_line_against_enabled_frustum_planes(
                clip_detail::load_line_vertices(obj, indices));

            if(output_type == point_list
               || output_type == line_list)
            {
                clip_detail::append_vertices(
                  out_vertices,
                  clipped_line);
            }
        }
    }
}

/**
 * Clip a vertex buffer/index buffer pair against the view frustum. the index buffer/vertex buffer pair is assumed
 * to contain a triangle list, i.e., if i is divisible by 3, then in_ib[i], in_ib[i+1] and in_ib[i+2] need to
 * be indices into in_vb forming a triangle.
 */
void clip_triangle_buffer(
  render_object& obj,
  clip_output output_type)
{
    obj.clear_clipped_output();
    clip_triangle_buffer_range(
      obj,
      output_type,
      0,
      obj.indices.size(),
      obj.clipped_vertices);
    obj.use_expanded_clipped_vertices();
}

void clip_triangle_buffer_range(
  const render_object& obj,
  clip_output output_type,
  std::size_t index_begin,
  std::size_t index_end,
  vertex_buffer& out_vertices)
{
    /*
     * Algorithm:
     *
     *  i)   Loop over triangles
     *  ii)  If the triangle contains a discarded vertex, do clipping and copy resulting triangles to temporary buffer
     *  iii) Copy all temporary triangles to the output vertex buffer.
     */

    assert((index_begin % 3) == 0);
    assert((index_end % 3) == 0);
    assert(index_begin <= index_end);
    assert(index_end <= obj.indices.size());

    out_vertices.clear();
    out_vertices.reserve((index_end - index_begin) * 2);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t emitted_triangles = 0;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    for(std::size_t index_it = index_begin; index_it < index_end; index_it += 3)
    {
        const std::array<std::uint32_t, 3> indices = {
          obj.indices[index_it],
          obj.indices[index_it + 1],
          obj.indices[index_it + 2]};

        const std::uint32_t f0 = obj.vertex_flags[indices[0]] & geom::vf_clip_discard;
        const std::uint32_t f1 = obj.vertex_flags[indices[1]] & geom::vf_clip_discard;
        const std::uint32_t f2 = obj.vertex_flags[indices[2]] & geom::vf_clip_discard;

        const std::uint32_t any_outside = f0 | f1 | f2;
        const std::uint32_t all_outside = f0 & f1 & f2;

        if(any_outside == 0)
        {
            // trivial accept.
            if(output_type == point_list)
            {
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[0]));
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[1]));
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[2]));
            }
            else if(output_type == line_list)
            {
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[0]));
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[1]));
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[2]));
                out_vertices.back().flags |= geom::vf_line_strip_end;
            }
            else if(output_type == triangle_list)
            {
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[0]));
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[1]));
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[2]));

#ifdef SWR_ENABLE_PIPELINE_PROFILING
                emitted_triangles += 1;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            }
        }
        else if(all_outside != 0)
        {
            // trivially reject: all vertices outside at least one same plane.
        }
        else
        {
            // partially clipped.

            clipped_vertex_buffer tri;
            clipped_vertex_buffer scratch_a;
            clipped_vertex_buffer scratch_b;
            const std::array<std::uint32_t, 3> clip_flags = {
              obj.vertex_flags[indices[0]],
              obj.vertex_flags[indices[1]],
              obj.vertex_flags[indices[2]]};

            clip_detail::load_triangle_vertices(obj, indices, tri);

            auto clipped_triangle =
              clip_detail::clip_polygon_against_enabled_frustum_planes(
                clip_flags,
                std::span<const geom::vertex>{tri.data(), tri.size()},
                scratch_a,
                scratch_b);

            if(output_type == point_list)
            {
                clip_detail::append_vertices(out_vertices, clipped_triangle);
            }
            else if(output_type == line_list
                    && clipped_triangle.size() >= 2)
            {
                clip_detail::append_vertices(out_vertices, clipped_triangle);
                out_vertices.back().flags |= geom::vf_line_strip_end;
            }
            else if(output_type == triangle_list
                    && clipped_triangle.size() >= 3)
            {
                // By construction a clipped triangle forms a convex polygon.
                // Choose the fan center that minimizes the sum of projected triangle
                // bounding-box areas to reduce raster-side overdraw/work.
                clip_detail::append_convex_polygon_as_fan_min_projected_bbox(
                  out_vertices,
                  clipped_triangle);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
                emitted_triangles += (clipped_triangle.size() - 2);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            }
        }
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    if(output_type == triangle_list)
    {
        const std::uint64_t input_triangles = static_cast<std::uint64_t>((index_end - index_begin) / 3);
        swr::impl::profile_clip_input_triangles.fetch_add(input_triangles, std::memory_order_relaxed);
        swr::impl::profile_clip_output_triangles.fetch_add(emitted_triangles, std::memory_order_relaxed);
    }
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

} /* namespace impl */

} /* namespace swr */
