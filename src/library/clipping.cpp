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

/* user headers. */
#include "swr_internal.h"
#include "clipping.h"

namespace swr
{

namespace impl
{

/**
 * clip against all planes (including the w-plane!)
 *
 * NOTE: if this is disabled, it may provoke segfault during fragment write,
 *       since we rely on the validity of the coordinates.
 */
#define CLIP_ALL_PLANES

/*
 * 1) A technical note: If we compile via Visual Studio, check that the compiler
 *    supports constexpr's. Support has been added in Visual Studio 2015 [1], which
 *    corresponds to the compiler version being 1900 [2].
 *
 *     [1] https://msdn.microsoft.com/de-de/library/hh567368.aspx
 *     [2] https://sourceforge.net/p/predef/wiki/Compilers/#microsoft-visual-c
 *
 * 2) From https://fabiensanglard.net/polygon_codec/:
 *    The clipping actually produces vertices with a W=0 component. That would cause a
 *    divide by zero. A way to solve this is to clip against the W=0.00001 plane.
 */
#if !defined(_MSC_VER) || _MSC_VER >= 1900
constexpr float W_CLIPPING_PLANE = 1e-5f;
#else
#    define W_CLIPPING_PLANE (1e-5f)
#endif

/**
 * We scale the calculated intersection parameter slightly to account for floating-point inaccuracies.
 * The scaling is always towards the vertex outside the clipping region.
 */
constexpr float SCALE_INTERSECTION_PARAMETER = 1.0001f;

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

/**
 * Clip vertex buffer against the x/y/z=+/- w plane.
 *
 * The indices in in_vb have to be in ascending order, i.e. the polygon has the vertices
 * in_vb.Vertices[0], in_vb.Vertices[1], etc.
 *
 * Internally, the polygon is first clipped/copied into a temporary buffer, so that in_vb and out_vb
 * are allowed to refer to the same buffer.
 */
static void clip_vertex_buffer_on_plane(const vertex_buffer& in_vb, const clip_axis axis, vertex_buffer& out_vb)
{
    // early-out for empty buffers.
    if(in_vb.size() == 0)
    {
        // empty the output buffer.
        out_vb.clear();
        return;
    }

    vertex_buffer temp;

    // special case for lines.
    if(in_vb.size() == 2)
    {
        int dot1 = (in_vb[0].coords[axis] <= in_vb[0].coords.w) ? 1 : -1;
        int dot2 = (in_vb[1].coords[axis] <= in_vb[1].coords.w) ? 1 : -1;

        if(dot1 > 0)
        {
            temp.push_back(in_vb[0]);
        }

        // do consistent clipping.
        if(dot1 * dot2 < 0)
        {
            // Need to clip against plane x/y/z = w, as specified by axis.

            auto* inside_vert = (dot1 < 0) ? &in_vb[1] : &in_vb[0];
            auto* outside_vert = (dot1 < 0) ? &in_vb[0] : &in_vb[1];

            float t = (inside_vert->coords.w - inside_vert->coords[axis]) / ((inside_vert->coords.w - inside_vert->coords[axis]) - (outside_vert->coords.w - outside_vert->coords[axis]));
            assert(t >= 0 && t <= 1);

            temp.emplace_back(lerp(SCALE_INTERSECTION_PARAMETER * t, *inside_vert, *outside_vert));
        }

        if(dot2 > 0)
        {
            temp.push_back(in_vb[1]);
        }

        out_vb.clear();
        if(temp.size())
        {
            assert(temp.size() == 2);

            int dot1 = (-temp[0].coords[axis] <= temp[0].coords.w) ? 1 : -1;
            int dot2 = (-temp[1].coords[axis] <= temp[1].coords.w) ? 1 : -1;

            if(dot1 > 0)
            {
                out_vb.push_back(temp[0]);
            }

            // do consistent clipping.
            if(dot1 * dot2 < 0)
            {
                // Need to clip against plane x/y/z = -w, as specified by axis.

                auto* inside_vert = (dot1 < 0) ? &temp[1] : &temp[0];
                auto* outside_vert = (dot1 < 0) ? &temp[0] : &temp[1];

                float t = -(inside_vert->coords.w + inside_vert->coords[axis]) / ((-inside_vert->coords.w - inside_vert->coords[axis]) + (outside_vert->coords.w + outside_vert->coords[axis]));
                assert(t >= 0 && t <= 1);

                out_vb.emplace_back(lerp(SCALE_INTERSECTION_PARAMETER * t, *inside_vert, *outside_vert));
            }

            if(dot2 > 0)
            {
                out_vb.push_back(temp[1]);
            }
        }

        return;
    }

    auto* prev_vert = &in_vb.back();
    int prev_dot = (prev_vert->coords[axis] <= prev_vert->coords.w) ? 1 : -1;

    for(size_t i = 0; i < in_vb.size(); ++i)
    {
        auto* vert = &in_vb[i];
        int dot = (vert->coords[axis] <= vert->coords.w) ? 1 : -1;

        // do consistent clipping.
        if(prev_dot * dot < 0)
        {
            // Need to clip against plane x/y/z = w, as specified by axis.

            auto* inside_vert = (prev_dot < 0) ? vert : prev_vert;
            auto* outside_vert = (prev_dot < 0) ? prev_vert : vert;

            float t = (inside_vert->coords.w - inside_vert->coords[axis]) / ((inside_vert->coords.w - inside_vert->coords[axis]) - (outside_vert->coords.w - outside_vert->coords[axis]));
            assert(t >= 0 && t <= 1);

            temp.emplace_back(lerp(SCALE_INTERSECTION_PARAMETER * t, *inside_vert, *outside_vert));
        }

        if(dot > 0)
        {
            temp.push_back(*vert);
        }

        prev_vert = vert;
        prev_dot = dot;
    }

    // clear the output buffer. the clearing cannot be done in advance, since out_vb and in_vb
    // may refer to the same object. Here, all 'active' vertices have beed copied into temp.
    out_vb.clear();

    // early-out for empty polygons.
    if(temp.size() == 0)
    {
        return;
    }

    prev_vert = &temp.back();
    prev_dot = (-prev_vert->coords[axis] <= prev_vert->coords.w) ? 1 : -1;

    for(size_t i = 0; i < temp.size(); ++i)
    {
        auto* vert = &temp[i];
        int dot = (-vert->coords[axis] <= vert->coords.w) ? 1 : -1;

        // do consistent clipping.
        if(prev_dot * dot < 0)
        {
            // Need to clip against plane x/y/z = -w, as specified by axis.

            auto* inside_vert = (prev_dot < 0) ? vert : prev_vert;
            auto* outside_vert = (prev_dot < 0) ? prev_vert : vert;

            float t = -(inside_vert->coords.w + inside_vert->coords[axis]) / ((-inside_vert->coords.w - inside_vert->coords[axis]) + (outside_vert->coords.w + outside_vert->coords[axis]));
            assert(t >= 0 && t <= 1);

            out_vb.emplace_back(lerp(SCALE_INTERSECTION_PARAMETER * t, *inside_vert, *outside_vert));
        }

        if(dot > 0)
        {
            out_vb.push_back(*vert);
        }

        prev_vert = vert;
        prev_dot = dot;
    }
}

/**
 * Clip a line against the w plane.
 *
 * Recall that a visible vertex has to satisfy the relations
 *
 *    -w <= x <= w
 *    -w <= y <= w
 *    -w <= z <= w
 *      0 < w.
 *
 * in_vb and out_vb are not allowed to refer to the same buffer.
 *
 * if in_vb does not contain a line (i.e., 2 vertices), we empty the output buffer and return.
 */
static void clip_line_on_w_plane(const vertex_buffer& in_line, vertex_buffer& out_vb)
{
    // ensure the output buffer is empty.
    out_vb.clear();

    // check that in_vb contains a line.
    if(in_line.size() != 2)
    {
        return;
    }

    int dots[2] = {
      (in_line[0].coords.w < W_CLIPPING_PLANE) ? -1 : 1,
      (in_line[1].coords.w < W_CLIPPING_PLANE) ? -1 : 1};

    if(dots[0] > 0)
    {
        out_vb.push_back(in_line[0]);
    }

    // do consistent clipping.
    if(dots[0] * dots[1] < 0)
    {
        // Need to clip against plane w=0.
        //
        // to avoid dividing by zero when converting to NDC, we clip
        // against w=W_CLIPPING_PLANE.

        // FIXME ? this selection could be condensed into a single comparison, since dots[0]*dots[1]<0 implies that exactly one of the dots[i] is positive.
        auto* inside_vert = (dots[0] < 0) ? &in_line[0] : &in_line[1];
        auto* outside_vert = (dots[1] < 0) ? &in_line[0] : &in_line[1];

        float t = (inside_vert->coords.w - W_CLIPPING_PLANE) / (inside_vert->coords.w - outside_vert->coords.w);
        assert(t >= 0 && t <= 1);

        out_vb.emplace_back(lerp(t, *inside_vert, *outside_vert));
    }

    if(dots[1] > 0)
    {
        out_vb.push_back(in_line[1]);
    }
}

/**
 * Clip a vertex buffer/index buffer pair against the view frustum. the index buffer/vertex buffer pair is assumed
 * to contain a line list, i.e., if i is divisible by 2, then in_ib[i] and in_ib[i+1] need to be indices into in_vb
 * forming a line.
 */
void clip_line_buffer(render_object& obj, clip_output output_type)
{
    vertex_buffer clipped_line{2};
    vertex_buffer temp_line{2};

    /*
     * Algorithm:
     *
     *  i)   Loop over lines
     *  ii)  If the lines contains a discarded vertex, do clipping and copy resulting line to temporary buffer
     *  iii) Copy all temporary lines to the output vertex buffer.
     */

    obj.clipped_vertices.clear();
    obj.clipped_vertices.reserve(obj.coord_count);

    for(size_t index_it = 0; index_it < obj.indices.size(); index_it += 2)
    {
        const std::uint32_t indices[2] = {
          obj.indices[index_it],
          obj.indices[index_it + 1]};

        // perform clipping.
        if((obj.flags[indices[0]] & geom::vf_clip_discard)
           || (obj.flags[indices[1]] & geom::vf_clip_discard))
        {
            // fill temporary vertex buffer.
            temp_line.clear();

            // TODO temporary
            geom::vertex v;
            v.varyings.reserve(obj.states.shader_info->varying_count);
            for(std::size_t i = 0; i < 2; ++i)
            {
                v.coords = obj.coords[indices[i]];

                v.varyings.clear();
                for(std::uint32_t j = 0; j < obj.states.shader_info->varying_count; ++j)
                {
                    v.varyings.emplace_back(obj.varyings[indices[i] * obj.states.shader_info->varying_count + j]);
                }

                v.flags = obj.flags[indices[i]];

                temp_line.push_back(v);
            }

            // perform clipping.
            clipped_line.clear();
            clip_line_on_w_plane(temp_line, clipped_line);
#ifdef CLIP_ALL_PLANES
            clip_vertex_buffer_on_plane(clipped_line, x_axis, clipped_line);
            clip_vertex_buffer_on_plane(clipped_line, y_axis, clipped_line);
#endif
            clip_vertex_buffer_on_plane(clipped_line, z_axis, clipped_line);

            // copy clipped vertices to output buffer.
            if(output_type == point_list)
            {
                // write a list of points.
                obj.clipped_vertices.insert(std::end(obj.clipped_vertices), std::begin(clipped_line), std::end(clipped_line));
            }
            else if(output_type == line_list)
            {
                // store vertex list.
                obj.clipped_vertices.insert(std::end(obj.clipped_vertices), std::begin(clipped_line), std::end(clipped_line));
            }
        }
        else
        {
            // copy clipped vertices to output buffer.
            if(output_type == point_list)
            {
                // write a list of points.

                // TODO temporary
                geom::vertex v;
                v.varyings.reserve(obj.states.shader_info->varying_count);
                for(std::size_t i = 0; i < 2; ++i)
                {
                    v.coords = obj.coords[indices[i]];

                    v.varyings.clear();
                    for(std::uint32_t j = 0; j < obj.states.shader_info->varying_count; ++j)
                    {
                        v.varyings.emplace_back(obj.varyings[indices[i] * obj.states.shader_info->varying_count + j]);
                    }

                    v.flags = obj.flags[indices[i]];

                    obj.clipped_vertices.push_back(v);
                }
            }
            else if(output_type == line_list)
            {
                // construct lines.

                // TODO temporary
                geom::vertex v;
                v.varyings.reserve(obj.states.shader_info->varying_count);
                for(std::size_t i = 0; i < 2; ++i)
                {
                    v.coords = obj.coords[indices[i]];

                    v.varyings.clear();
                    for(std::uint32_t j = 0; j < obj.states.shader_info->varying_count; ++j)
                    {
                        v.varyings.emplace_back(obj.varyings[indices[i] * obj.states.shader_info->varying_count + j]);
                    }

                    v.flags = obj.flags[indices[i]];

                    obj.clipped_vertices.push_back(v);
                }
            }
        }
    }
}

/**
 * Clip a triangle against the w plane.
 *
 * Recall that a visible vertex has to satisfy the relations
 *
 *    -w <= x <= w
 *    -w <= y <= w
 *    -w <= z <= w
 *      0 < w.
 *
 * in_vb and out_vb are not allowed to refer to the same buffer.
 *
 * if in_vb does not contain a triangle (i.e., 3 vertices), we empty the output buffer and return.
 */
static void clip_triangle_on_w_plane(const vertex_buffer& in_triangle, vertex_buffer& out_vb)
{
    // ensure the output buffer is empty.
    out_vb.clear();

    // check that in_vb contains a triangle.
    if(in_triangle.size() != 3)
    {
        return;
    }

    auto* prev_vert = &in_triangle[2]; /* last triangle vertex */
    int prev_dot = (prev_vert->coords.w < W_CLIPPING_PLANE) ? -1 : 1;

    for(int i = 0; i < 3; ++i)
    {
        auto* vert = &in_triangle[i];
        int dot = (vert->coords.w < W_CLIPPING_PLANE) ? -1 : 1;

        // do consistent clipping.
        if(prev_dot * dot < 0)
        {
            // Need to clip against plane w=0.
            //
            // to avoid dividing by zero when converting to NDC, we clip
            // against w=W_CLIPPING_PLANE.

            auto* inside_vert = (prev_dot < 0) ? vert : prev_vert;
            auto* outside_vert = (prev_dot < 0) ? prev_vert : vert;

            float t = (inside_vert->coords.w - W_CLIPPING_PLANE) / (inside_vert->coords.w - outside_vert->coords.w);
            assert(t >= 0 && t <= 1);

            out_vb.emplace_back(lerp(t, *inside_vert, *outside_vert));
        }

        if(dot > 0)
        {
            out_vb.push_back(*vert);
        }

        prev_vert = vert;
        prev_dot = dot;
    }
}

/**
 * Clip a vertex buffer/index buffer pair against the view frustum. the index buffer/vertex buffer pair is assumed
 * to contain a triangle list, i.e., if i is divisible by 3, then in_ib[i], in_ib[i+1] and in_ib[i+2] need to
 * be indices into in_vb forming a triangle.
 */
void clip_triangle_buffer(render_object& obj, clip_output output_type)
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

    /*
     * Algorithm:
     *
     *  i)   Loop over triangles
     *  ii)  If the triangle contains a discarded vertex, do clipping and copy resulting triangles to temporary buffer
     *  iii) Copy all temporary triangles to the output vertex buffer.
     */

    obj.clipped_vertices.clear();
    obj.clipped_vertices.reserve(obj.clipped_vertices.size());

    for(size_t index_it = 0; index_it < obj.indices.size(); index_it += 3)
    {
        const std::uint32_t indices[3] = {
          obj.indices[index_it],
          obj.indices[index_it + 1],
          obj.indices[index_it + 2]};

        // perform clipping.
        if((obj.flags[indices[0]] & geom::vf_clip_discard)
           || (obj.flags[indices[1]] & geom::vf_clip_discard)
           || (obj.flags[indices[2]] & geom::vf_clip_discard))
        {
            // fill temporary vertex buffer.
            temp_triangle.clear();

            // TODO temporary
            geom::vertex v;
            v.varyings.reserve(obj.states.shader_info->varying_count);
            for(std::size_t i = 0; i < 3; ++i)
            {
                v.coords = obj.coords[indices[i]];

                v.varyings.clear();
                for(std::uint32_t j = 0; j < obj.states.shader_info->varying_count; ++j)
                {
                    v.varyings.emplace_back(obj.varyings[indices[i] * obj.states.shader_info->varying_count + j]);
                }

                v.flags = obj.flags[indices[i]];

                temp_triangle.push_back(v);
            }

            // perform clipping.
            clipped_triangle.clear();
            clip_triangle_on_w_plane(temp_triangle, clipped_triangle);
#ifdef CLIP_ALL_PLANES
            clip_vertex_buffer_on_plane(clipped_triangle, x_axis, clipped_triangle);
            clip_vertex_buffer_on_plane(clipped_triangle, y_axis, clipped_triangle);
#endif
            clip_vertex_buffer_on_plane(clipped_triangle, z_axis, clipped_triangle);

            // copy clipped vertices to output buffer.
            if(output_type == point_list)
            {
                // write a list of points.
                obj.clipped_vertices.insert(std::end(obj.clipped_vertices), std::begin(clipped_triangle), std::end(clipped_triangle));
            }
            else if(output_type == line_list
                    && clipped_triangle.size() >= 2)
            {
                // store vertex list. mark last vertex of the line,
                // so that the polygons can all be reconstructed.
                obj.clipped_vertices.insert(std::end(obj.clipped_vertices), std::begin(clipped_triangle), std::end(clipped_triangle));
                obj.clipped_vertices.back().flags |= geom::vf_line_strip_end;
            }
            else if(output_type == triangle_list && clipped_triangle.size() >= 3)
            {
                // By construction a clipped triangle forms a convex polygon.
                // Thus, we can construct it as a triangle fan by selecting an arbitrary vertex as its center.

                const geom::vertex& center = clipped_triangle.front();
                const geom::vertex* previous = &clipped_triangle[1];

                for(size_t i = 2; i < clipped_triangle.size(); ++i)
                {
                    const geom::vertex* current = &clipped_triangle[i];

                    obj.clipped_vertices.push_back(center);
                    obj.clipped_vertices.push_back(*previous);
                    obj.clipped_vertices.push_back(*current);

                    previous = current;
                }
            }
        }
        else
        {
            // copy clipped vertices to output buffer.
            if(output_type == point_list)
            {
                // TODO temporary
                geom::vertex v;
                v.varyings.reserve(obj.states.shader_info->varying_count);
                for(std::size_t i = 0; i < 3; ++i)
                {
                    v.coords = obj.coords[indices[i]];

                    v.varyings.clear();
                    for(std::uint32_t j = 0; j < obj.states.shader_info->varying_count; ++j)
                    {
                        v.varyings.emplace_back(obj.varyings[indices[i] * obj.states.shader_info->varying_count + j]);
                    }

                    v.flags = obj.flags[indices[i]];

                    obj.clipped_vertices.push_back(v);
                }
            }
            else if(output_type == line_list)
            {
                // construct lines.
                // TODO temporary
                geom::vertex v;
                v.varyings.reserve(obj.states.shader_info->varying_count);
                for(std::size_t i = 0; i < 3; ++i)
                {
                    v.coords = obj.coords[indices[i]];

                    v.varyings.clear();
                    for(std::uint32_t j = 0; j < obj.states.shader_info->varying_count; ++j)
                    {
                        v.varyings.emplace_back(obj.varyings[indices[i] * obj.states.shader_info->varying_count + j]);
                    }

                    v.flags = obj.flags[indices[i]];

                    obj.clipped_vertices.push_back(v);
                }

                // mark last index as end of line strip.
                obj.clipped_vertices.back().flags |= geom::vf_line_strip_end;
            }
            else if(output_type == triangle_list)
            {
                // copy triangle.
                // TODO temporary
                geom::vertex v;
                v.varyings.reserve(obj.states.shader_info->varying_count);
                for(std::size_t i = 0; i < 3; ++i)
                {
                    v.coords = obj.coords[indices[i]];

                    v.varyings.clear();
                    for(std::uint32_t j = 0; j < obj.states.shader_info->varying_count; ++j)
                    {
                        v.varyings.emplace_back(obj.varyings[indices[i] * obj.states.shader_info->varying_count + j]);
                    }

                    v.flags = obj.flags[indices[i]];

                    obj.clipped_vertices.push_back(v);
                }
            }
        }
    }
}

} /* namespace impl */

} /* namespace swr */
