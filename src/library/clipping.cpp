/**
 * swr - a software rasterizer
 *
 * triangle clipping in homogeneous coordinates.
 *
 * references:
 *  [1] http://fabiensanglard.net/polygon_codec/
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <span>
#include <vector>

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

inline void load_vertex_from_render_object(
  const render_object& obj,
  std::uint32_t vertex_index,
  std::uint32_t varying_count,
  geom::vertex& out_vertex)
{
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    constexpr std::uint64_t coord_bytes = sizeof(ml::vec4);
    constexpr std::uint64_t flag_bytes = sizeof(std::uint32_t);
    const std::uint64_t varying_bytes = static_cast<std::uint64_t>(varying_count) * sizeof(ml::vec4);
    swr::impl::profile_clip_vertex_read_bytes.fetch_add(coord_bytes + flag_bytes + varying_bytes, std::memory_order_relaxed);
    swr::impl::profile_clip_vertex_write_bytes.fetch_add(coord_bytes + flag_bytes + varying_bytes, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    out_vertex.coords = obj.coords[vertex_index];
    out_vertex.flags = obj.vertex_flags[vertex_index];

    const std::size_t varying_offset = static_cast<std::size_t>(vertex_index) * varying_count;
    out_vertex.varyings.clear();
    for(std::uint32_t i = 0; i < varying_count; ++i)
    {
        out_vertex.varyings.emplace_back(obj.varyings[varying_offset + i]);
    }
}

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

    // Preserve previous behavior: only axis-plane clipping gets the small outward bias.
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
static std::vector<geom::vertex> clip_line_segment_against_plane(
  std::span<const geom::vertex> in_line,
  const plane p)
{
    std::vector<geom::vertex> out;

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
static std::vector<geom::vertex> clip_closed_polygon_against_plane(
  std::span<const geom::vertex> in_poly,
  const plane p)
{
    std::vector<geom::vertex> out;

    if(in_poly.empty())
    {
        return out;
    }

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

    return out;
}

static std::vector<geom::vertex> clip_polygon_against_enabled_frustum_planes(std::span<const geom::vertex> poly)
{
    std::vector<geom::vertex> result = clip_closed_polygon_against_plane(poly, plane{plane_kind::w_min, z_axis});

    result = clip_closed_polygon_against_plane(result, plane{plane_kind::axis_positive, x_axis});
    result = clip_closed_polygon_against_plane(result, plane{plane_kind::axis_negative, x_axis});
    result = clip_closed_polygon_against_plane(result, plane{plane_kind::axis_positive, y_axis});
    result = clip_closed_polygon_against_plane(result, plane{plane_kind::axis_negative, y_axis});
    result = clip_closed_polygon_against_plane(result, plane{plane_kind::axis_positive, z_axis});
    result = clip_closed_polygon_against_plane(result, plane{plane_kind::axis_negative, z_axis});

    return result;
}

static std::vector<geom::vertex> clip_line_against_enabled_frustum_planes(std::span<const geom::vertex> line)
{
    std::vector<geom::vertex> result = clip_line_segment_against_plane(line, plane{plane_kind::w_min, z_axis});

    result = clip_line_segment_against_plane(result, plane{plane_kind::axis_positive, x_axis});
    result = clip_line_segment_against_plane(result, plane{plane_kind::axis_negative, x_axis});
    result = clip_line_segment_against_plane(result, plane{plane_kind::axis_positive, y_axis});
    result = clip_line_segment_against_plane(result, plane{plane_kind::axis_negative, y_axis});
    result = clip_line_segment_against_plane(result, plane{plane_kind::axis_positive, z_axis});
    result = clip_line_segment_against_plane(result, plane{plane_kind::axis_negative, z_axis});

    return result;
}

static geom::vertex load_vertex(
  const render_object& obj,
  const std::uint32_t index)
{
    geom::vertex v;
    v.coords = obj.coords[index];
    v.varyings.reserve(obj.states.shader_info->varying_count);

    for(std::uint32_t j = 0; j < obj.states.shader_info->varying_count; ++j)
    {
        v.varyings.emplace_back(obj.varyings[index * obj.states.shader_info->varying_count + j]);
    }

    v.flags = obj.vertex_flags[index];
    return v;
}

static std::vector<geom::vertex> load_line_vertices(
  const render_object& obj,
  const std::array<std::uint32_t, 2>& indices)
{
    std::vector<geom::vertex> line;
    line.reserve(2);
    line.emplace_back(load_vertex(obj, indices[0]));
    line.emplace_back(load_vertex(obj, indices[1]));
    return line;
}

static std::vector<geom::vertex> load_triangle_vertices(
  const render_object& obj,
  const std::array<std::uint32_t, 3> indices)
{
    std::vector<geom::vertex> tri;
    tri.reserve(3);
    tri.emplace_back(load_vertex(obj, indices[0]));
    tri.emplace_back(load_vertex(obj, indices[1]));
    tri.emplace_back(load_vertex(obj, indices[2]));
    return tri;
}

static void assign_vertex_buffer(
  vertex_buffer& dst,
  const std::vector<geom::vertex>& src)
{
    dst.clear();
    dst.insert(std::end(dst), std::begin(src), std::end(src));
}

static void append_vertices(
  vertex_buffer& dst,
  const std::vector<geom::vertex>& src)
{
    dst.insert(std::end(dst), std::begin(src), std::end(src));
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
    clip_line_buffer_range(
      obj,
      output_type,
      0,
      obj.indices.size(),
      obj.clipped_vertices);
}

void clip_line_buffer_range(
  const render_object& obj,
  clip_output output_type,
  std::size_t index_begin,
  std::size_t index_end,
  vertex_buffer& out_vertices)
{
    vertex_buffer clipped_line{2};
    vertex_buffer temp_line{2};
    const std::uint32_t varying_count = obj.states.shader_info->varying_count;

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

    geom::vertex v;
    v.varyings.reserve(varying_count);

    for(std::size_t index_it = index_begin; index_it < index_end; index_it += 2)
    {
        const std::array<std::uint32_t, 2> indices = {
          obj.indices[index_it],
          obj.indices[index_it + 1]};

        const bool needs_clipping =
          (obj.vertex_flags[indices[0]] & geom::vf_clip_discard)
          || (obj.vertex_flags[indices[1]] & geom::vf_clip_discard);

        if(needs_clipping)
        {
            std::vector<geom::vertex> clipped_line =
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
        else
        {
            if(output_type == point_list
               || output_type == line_list)
            {
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[0]));
                out_vertices.emplace_back(
                  clip_detail::load_vertex(obj, indices[1]));
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
    clip_triangle_buffer_range(
      obj,
      output_type,
      0,
      obj.indices.size(),
      obj.clipped_vertices);
}

void clip_triangle_buffer_range(
  const render_object& obj,
  clip_output output_type,
  std::size_t index_begin,
  std::size_t index_end,
  vertex_buffer& out_vertices)
{
    /*
     * temporary buffers.
     *
     * a note on the initial buffer size: if a large triangle is intersected with a small enough cube,
     * it can produce a hexagonal-type polygon, i.e., 6 vertices. now if one vertex is inside
     * the cube and the triangle is "flat enough", two additional vertices appear, so 8 seems
     * to be a good guess as an initial buffer size for a clipped triangle.
     */
    vertex_buffer clipped_triangle{8};
    vertex_buffer temp_triangle{3};
    const std::uint32_t varying_count = obj.states.shader_info->varying_count;

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

    geom::vertex v;
    v.varyings.reserve(varying_count);

    for(std::size_t index_it = index_begin; index_it < index_end; index_it += 3)
    {
        const std::array<std::uint32_t, 3> indices = {
          obj.indices[index_it],
          obj.indices[index_it + 1],
          obj.indices[index_it + 2]};

        const bool needs_clipping =
          (obj.vertex_flags[indices[0]] & geom::vf_clip_discard)
          || (obj.vertex_flags[indices[1]] & geom::vf_clip_discard)
          || (obj.vertex_flags[indices[2]] & geom::vf_clip_discard);

        if(needs_clipping)
        {
            std::vector<geom::vertex> clipped_triangle =
              clip_detail::clip_polygon_against_enabled_frustum_planes(
                clip_detail::load_triangle_vertices(obj, indices));

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
                // Thus, we can construct it as a triangle fan by selecting an arbitrary vertex as its center.
                const geom::vertex& center = clipped_triangle.front();
                const geom::vertex* previous = &clipped_triangle[1];

                for(std::size_t i = 2; i < clipped_triangle.size(); ++i)
                {
                    const geom::vertex* current = &clipped_triangle[i];

                    out_vertices.emplace_back(center);
                    out_vertices.emplace_back(*previous);
                    out_vertices.emplace_back(*current);

                    previous = current;
                }
            }
        }
        else
        {
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
            }
        }
    }
}

} /* namespace impl */

} /* namespace swr */