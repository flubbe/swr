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
 * sort the vertices when clipping to have consistent intersection points for
 * triangles sharing an edge.
 */
#define CLIP_SORT_VERTICES

/**
 * clip against all planes (including the w-plane!)
 * 
 * NOTE: if this is disabled, it may provoke segfault during fragment write,
 *       since we rely on the validity of the coordinates.
 */
#define CLIP_ALL_PLANES

/*
 * If we compile via Visual Studio, check that the compiler supports constexpr's.
 * Support has been added in Visual Studio 2015 [1], which corresponds to the
 * compiler version being 1900 [2].
 *
 * [1] https://msdn.microsoft.com/de-de/library/hh567368.aspx
 * [2] https://sourceforge.net/p/predef/wiki/Compilers/#microsoft-visual-c
 */
#if !defined(_MSC_VER) || _MSC_VER >= 1900
constexpr float W_CLIPPING_PLANE = 1.0f;
#else
#    define W_CLIPPING_PLANE (1.0f)
#endif

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
 * if in_vb does not contain 3 indices, we empty the output buffer and return.
 * 
 * for indexed polygons, add an index_buffer input in_ib, set prev_vert=&in_vb[in_ib.back()] and
 * replace the for loop header by "for( auto i : in_ib )".
 */
static void clip_triangle_on_w_plane(const vertex_buffer& in_vb, vertex_buffer& out_vb)
{
    // ensure the output buffer is empty.
    out_vb.resize(0);

    // check that in_vb contains a triangle.
    if(in_vb.size() != 3)
    {
        return;
    }

    auto* prev_vert = &in_vb[2]; /* last triangle vertex */
    int prev_dot = (prev_vert->coords.w < W_CLIPPING_PLANE) ? -1 : 1;

    for(int i = 0; i < 3; ++i)
    {
        auto* vert = &in_vb[i];
        int dot = (vert->coords.w < W_CLIPPING_PLANE) ? -1 : 1;

#ifdef CLIP_SORT_VERTICES
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

            geom::vertex new_vert = lerp(t, *inside_vert, *outside_vert);
            out_vb.push_back(new_vert);
        }
#else
        if(prev_dot * dot < 0)
        {
            // Need to clip against plane w=0.
            //
            // to avoid dividing by zero when converting to NDC, we clip
            // against w=W_CLIPPING_PLANE.

            float t = (prev_vert->coords.w - W_CLIPPING_PLANE) / (prev_vert->coords.w - vert->coords.w);
            assert(t >= 0 && t <= 1);

            geom::vertex new_vert = lerp(t, *prev_vert, *vert);
            out_vb.push_back(new_vert);
        }
#endif

        if(dot > 0)
        {
            out_vb.push_back(*vert);
        }

        prev_vert = vert;
        prev_dot = dot;
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

/**
 * Clip vertex buffer against the x/y/z=+/- w plane.
 *
 * The indices in in_vb have to be in ascending order, i.e. the polygon has the vertices
 * in_vb.Vertices[0], in_vb.Vertices[1], etc.
 *
 * Internally, the polygon is first clipped/copied into a buffer, so that in_vb and out_vb
 * are allowed to refer to the same veriable.
 */
static void clip_vertex_buffer_on_plane(const vertex_buffer& in_vb, const clip_axis axis, vertex_buffer& out_vb)
{
    // early-out for empty buffers.
    if(in_vb.size() == 0)
    {
        // empty the output buffer.
        out_vb.resize(0);
        return;
    }

    // use static temporary vertex buffer to remove the need for permanent reallocation.
    static vertex_buffer temp;
    temp.resize(0);

    auto* prev_vert = &in_vb.back();
    int prev_dot = (prev_vert->coords[axis] <= prev_vert->coords.w) ? 1 : -1;

    for(size_t i = 0; i < in_vb.size(); ++i)
    {
        auto* vert = &in_vb[i];
        auto dot = (vert->coords[axis] <= vert->coords.w) ? 1 : -1;

#ifdef CLIP_SORT_VERTICES
        // do consistent clipping.
        if(prev_dot * dot < 0)
        {
            // Need to clip against plane w=0.
            //
            // to avoid dividing by zero when converting to NDC, we clip
            // against w=W_CLIPPING_PLANE.

            auto* inside_vert = (prev_dot < 0) ? vert : prev_vert;
            auto* outside_vert = (prev_dot < 0) ? prev_vert : vert;

            float t = (inside_vert->coords.w - inside_vert->coords[axis]) / ((inside_vert->coords.w - inside_vert->coords[axis]) - (outside_vert->coords.w - outside_vert->coords[axis]));
            assert(t >= 0 && t <= 1);

            geom::vertex new_vert = lerp(t, *inside_vert, *outside_vert);
            temp.push_back(new_vert);
        }
#else
        if(prev_dot * dot < 0)
        {
            // Need to clip against plane x/y/z = w, as specified by axis.

            float t = (prev_vert->coords.w - prev_vert->coords[axis]) / ((prev_vert->coords.w - prev_vert->coords[axis]) - (vert->coords.w - vert->coords[axis]));
            assert(t >= 0 && t <= 1);

            geom::vertex new_vert = lerp(t, *prev_vert, *vert);
            temp.push_back(new_vert);
        }
#endif

        if(dot > 0)
        {
            temp.push_back(*vert);
        }

        prev_vert = vert;
        prev_dot = dot;

        ++vert;
    }

    // clear the output buffer. the clearing cannot be done in advance, since out_vb and in_vb
    // may refer to the same object. Here, all 'active' vertices have beed copied into temp.
    out_vb.resize(0);

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
        auto dot = (-vert->coords[axis] <= vert->coords.w) ? 1 : -1;

#ifdef CLIP_SORT_VERTICES
        // do consistent clipping.
        if(prev_dot * dot < 0)
        {
            // Need to clip against plane w=0.
            //
            // to avoid dividing by zero when converting to NDC, we clip
            // against w=W_CLIPPING_PLANE.

            auto* inside_vert = (prev_dot < 0) ? vert : prev_vert;
            auto* outside_vert = (prev_dot < 0) ? prev_vert : vert;

            float t = -(inside_vert->coords.w + inside_vert->coords[axis]) / ((-inside_vert->coords.w - inside_vert->coords[axis]) + (outside_vert->coords.w + outside_vert->coords[axis]));
            assert(t >= 0 && t <= 1);

            geom::vertex new_vert = lerp(t, *inside_vert, *outside_vert);
            out_vb.push_back(new_vert);
        }
#else
        if(prev_dot * dot < 0)
        {
            // Need to clip against plane x/y/z = -w, as specified by axis.
            float t = -(prev_vert->coords.w + prev_vert->coords[axis]) / ((-prev_vert->coords.w - prev_vert->coords[axis]) + (vert->coords.w + vert->coords[axis]));
            assert(t >= 0 && t <= 1);

            geom::vertex new_vert = lerp(t, *prev_vert, *vert);
            out_vb.push_back(new_vert);
        }
#endif

        if(dot > 0)
        {
            out_vb.push_back(*vert);
        }

        prev_vert = vert;
        prev_dot = dot;

        ++vert;
    }
}

/**
 * Clip a vertex buffer/index buffer pair against the view frustum. the index buffer/vertex buffer pair is assumed
 * to contain a triangle list, i.e., if i is divisible by 3, then in_ib[i], in_ib[i+1] and in_ib[i+2] need to
 * be indices into in_vb forming a triangle.
 */
void clip_triangle_buffer(const vertex_buffer& in_vb, const index_buffer& in_ib, clip_output output_type, vertex_buffer& out_vb)
{
    /*
     * temporary buffers. they are static in order to do memory allocation only about once.
     * 
     * a note on the initial buffer size: if a large triangle is intersected with a small enough cube,
     * it can produce a hexagonal-type polygon, i.e., 6 vertices. now if one vertex is inside
     * the cube and the triangle is "flat enough", two additional vertices appear, so 8 seems
     * to be a good guess as an initial buffer size for a clipped triangle.
     */
    static vertex_buffer clipped_triangle(8);
    static vertex_buffer temp_triangle(3);

    /*
     * Algorithm:
     *
     *  i)   Loop over triangles
     *  ii)  If the triangle contains a discarded vertex, do clipping and copy resulting triangles to buffer
     *  iii) Copy all triangles to the output vertex buffer.
     */

    out_vb.resize(0);
    out_vb.reserve(in_vb.size());

    for(size_t index_it = 0; index_it < in_ib.size(); index_it += 3)
    {
        const auto i1 = in_ib[index_it];
        const auto i2 = in_ib[index_it + 1];
        const auto i3 = in_ib[index_it + 2];

        const auto& v1 = in_vb[i1];
        const auto& v2 = in_vb[i2];
        const auto& v3 = in_vb[i3];

        // perform clipping.
        if((v1.flags & geom::vf_clip_discard)
           || (v2.flags & geom::vf_clip_discard)
           || (v3.flags & geom::vf_clip_discard))
        {
            // fill temporary vertex buffer.
            temp_triangle.resize(0);
            temp_triangle.push_back(v1);
            temp_triangle.push_back(v2);
            temp_triangle.push_back(v3);

            // perform clipping.
            clipped_triangle.resize(0);
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
                out_vb.insert(std::end(out_vb), std::begin(clipped_triangle), std::end(clipped_triangle));
            }
            else if(output_type == line_list
                    && clipped_triangle.size() >= 2)
            {
                // store vertex list. mark last vertex of the line,
                // so that the polygons can all be reconstructed.
                out_vb.insert(std::end(out_vb), std::begin(clipped_triangle), std::end(clipped_triangle));
                out_vb.back().flags |= geom::vf_line_strip_end;
            }
            else if(output_type == triangle_list && clipped_triangle.size() >= 3)
            {
                // Note, that by construction a clipped triangle forms a convex polygon.
                // Thus, we can construct it as a triangle fan by selecting an arbitrary vertex as its center.

                const auto& CenterVertex = clipped_triangle.front();
                const auto* PreviousVertex = &clipped_triangle[1];

                for(size_t i = 1; i < clipped_triangle.size(); ++i)
                {
                    const auto* CurrentVertex = &clipped_triangle[i];

                    out_vb.push_back(CenterVertex);
                    out_vb.push_back(*PreviousVertex);
                    out_vb.push_back(*CurrentVertex);

                    PreviousVertex = CurrentVertex;
                }
            }
        }
        else
        {
            // copy clipped vertices to output buffer.
            if(output_type == point_list)
            {
                // write a list of points.
                out_vb.push_back(v1);
                out_vb.push_back(v2);
                out_vb.push_back(v3);
            }
            else if(output_type == line_list)
            {
                // construct lines.
                out_vb.push_back(v1);
                out_vb.push_back(v2);
                out_vb.push_back(v3);

                // mark last index as end of line strip.
                out_vb.back().flags |= geom::vf_line_strip_end;
            }
            else if(output_type == triangle_list)
            {
                // copy triangle.
                out_vb.push_back(v1);
                out_vb.push_back(v2);
                out_vb.push_back(v3);
            }
        }
    }
}

} /* namespace impl */

} /* namespace swr */
