/**
 * swr - a software rasterizer
 *
 * primitive assembly.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"
#include "culling.h"

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
 * \param vb The vertex buffer holding the vertex list.
 * \param start_index The starting vertex of the polygon.
 * \param end_index If and ending marker is detected, this holds the index of the first vertex greater or equal to start_index having an ending marker.
 * \return If an end marker is found, the function returns true.
 */
static bool next_polygon(const vertex_buffer& vb, std::size_t start_index, std::size_t& end_index)
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
static int triangle_area_sign(const ml::vec2 v1, const ml::vec2 v2, const ml::vec2 v3)
{
    // edge1 = v2-v1, edge2 = v3-v1.
    return (v2 - v1).area_sign(v3 - v1);
}

/**
 * Calculate the orientation of a convex 2d polygon given by the raster coordinates of the vertices.
 *
 * \param vb The vertex buffer holding the vertex list.
 * \param start_vertex The index of the first vertex of the polygon
 * \param end_vertex The index of the last vertex of the polygon.
 * \return Returns if the polygon is oriented clockwise, counter-clockwise, or if it is degenerate.
 *         Additionally, if the function detects non-convexity, it returns polygon_orientation::not_convex.
 */
static polygon_orientation get_polygon_orientation(const vertex_buffer& vb, const std::size_t start_vertex, const std::size_t end_vertex)
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
    for(size_t i = start_vertex + 2; i <= end_vertex; ++i)
    {
        v3 = vb[i].coords.xy();
        int sign = triangle_area_sign(v1, v2, v3);

        v1 = v2;
        v2 = v3;

        positive_corners += (sign > 0);
        negative_corners += (sign < 0);
    }

    // the above loop misses two corners, which we check here separately.
    int sign1 = triangle_area_sign(v2, v3, vb[start_vertex].coords.xy());
    int sign2 = triangle_area_sign(v3, vb[start_vertex].coords.xy(), vb[start_vertex + 1].coords.xy());

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
 * \param cull_mode current cull mode.
 * \param front_face  current front-face mode.
 * \param orientation the polygon's orientation inside the viewport.
 * \return returns true if the polygon should be culled based on the render states and the polygon's orientation.
 */
static bool face_cull_polygon(swr::cull_face_direction cull_mode, swr::front_face_orientation front_face, polygon_orientation orientation)
{
    if(cull_mode == cull_face_direction::front_and_back)
    {
        // reject all polygons.
        return true;
    }

    if(cull_mode == cull_face_direction::front)
    {
        // reject front-facing polygons.
        return (front_face == front_face_orientation::cw && orientation == polygon_orientation::cw)
               || (front_face == front_face_orientation::ccw && orientation == polygon_orientation::ccw);
    }
    else if(cull_mode == cull_face_direction::back)
    {
        // reject back-facing polygons.
        return (front_face == front_face_orientation::cw && orientation == polygon_orientation::ccw)
               || (front_face == front_face_orientation::ccw && orientation == polygon_orientation::cw);
    }

    // accept.
    return false;
}

void render_device_context::assemble_primitives(const render_states* states, vertex_buffer_mode mode, const vertex_buffer& vb)
{
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
        const int size = vb.size() & ~1;
        for(int i = 0; i < size; i += 2)
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
            const auto culling_enabled = states->culling_enabled;
            const auto cull_mode = states->cull_mode;
            const auto front_face = states->front_face;

            size_t last_index = 0;
            for(size_t first_index = 0; first_index < vb.size(); first_index = last_index + 1)
            {
                // note that LastIndex gets updated here.
                if(!next_polygon(vb, first_index, last_index))
                {
                    // no polygon found.
                    break;
                }

                // culling.
                if(culling_enabled)
                {
                    auto orientation = get_polygon_orientation(vb, first_index, last_index);
                    if(orientation == polygon_orientation::not_convex || orientation == polygon_orientation::degenerate)
                    {
                        // do not consider degenerate polygons or non-convex ones.
                        continue;
                    }

                    if(face_cull_polygon(cull_mode, front_face, orientation))
                    {
                        // don't draw.
                        continue;
                    }
                }

                // add the lines to the rasterizer.
                const auto* first_vertex = &vb[first_index];
                const auto* prev_vertex = first_vertex;

                for(size_t i = first_index + 1; i <= last_index; ++i)
                {
                    const auto* cur_vertex = &vb[i];

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
            const auto culling_enabled = states->culling_enabled;
            const auto cull_mode = states->cull_mode;
            const auto front_face = states->front_face;

            /* draw a list of triangles */
            for(size_t i = 0; i < vb.size(); i += 3)
            {
                const auto& v1 = vb[i];
                const auto& v2 = vb[i + 1];
                const auto& v3 = vb[i + 2];

                // determine if triangle is front facing.
                cull_face_direction orient = get_face_orientation(front_face, v1.coords.xy(), v2.coords.xy(), v3.coords.xy());
                bool is_front_facing = (orient == cull_face_direction::front);

                // check for face culling
                if(culling_enabled && cull_reject(cull_mode, orient))
                {
                    // reject
                    continue;
                }

                rasterizer->add_triangle(states, is_front_facing, &v1, &v2, &v3);
            }
        }
        else
        {
            // this intentionally breaks the debugger.
            assert(states->poly_mode == polygon_mode::line || states->poly_mode == polygon_mode::fill);
        }
    }
}

} /* namespace impl */

} /* namespace swr */
