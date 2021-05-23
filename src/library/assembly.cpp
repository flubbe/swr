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

enum EPolyOrientation
{
    PO_NotConvex,
    PO_Degenerate,
    PO_Clockwise,
    PO_CounterClockwise
};

/**
 * Extract the polygon information out of a line loop, which in turn consists of vertices.
 * Some vertices have markers to indicate where a polygon ends (and thus, where the next starts).
 *
 * \param InBuffer The vertex buffer holding the vertex list.
 * \param InStartVertex The starting vertex of the polygon.
 * \param OutEndVertex The end vertex.
 * \return If a valid polygon is extracted (i.e. if an end vertex found before the buffer is empty), the function returns true.
 */
static bool GetNextPolygon(const vertex_buffer& InBuffer, size_t InStartVertex, size_t& OutEndVertex)
{
    for(size_t i = InStartVertex; i < InBuffer.size(); ++i)
    {
        // check for end vertex.
        if(InBuffer[i].flags & geom::vf_line_strip_end)
        {
            OutEndVertex = i;
            return true;
        }
    }

    // we did not find an ending marker.
    return false;
}

static int GetAreaSign(const ml::vec2 V1, const ml::vec2 V2, const ml::vec2 V3)
{
    // Edge1 = V2-V1, Edge2 = V3-V1.
    return (V2 - V1).area_sign(V3 - V1);
}

/**
 * Calculate the orientation of a convex 2d polygon given by the raster coordinates of the vertices.
 *
 * \param InBuffer The vertex buffer holding the vertex list.
 * \param InStartVertex The index of the first vertex of the polygon
 * \param InEndVertex The index of the last vertex of the polygon.
 * \return Returns if the polygon is oriented clockwise, counter-clockwise, or if it is degenerate.
 *         Additionally, if the function detects non-convexity, it returns PO_NotConvex.
 */
static EPolyOrientation GetConvexPolygonOrientation(const vertex_buffer& InBuffer, size_t InStartVertex, size_t InEndVertex)
{
    assert(InEndVertex < InBuffer.size());

    // a non-negenerate convex polygon needs to have at least 3 vertices.
    if(InStartVertex + 2 > InEndVertex)
    {
        return PO_Degenerate;
    }

    int PositiveCorners = 0;
    int NegativeCorners = 0;

    // loop through the vertex list and calculate the orientation at each corner.
    for(size_t i = InStartVertex; i <= InEndVertex - 2; ++i)
    {
        int Sign = GetAreaSign(InBuffer[i].coords.xy(), InBuffer[i + 1].coords.xy(), InBuffer[i + 2].coords.xy());

        PositiveCorners += (Sign > 0);
        NegativeCorners += (Sign < 0);
    }

    // the above loop misses two corners, which we check here separately.
    int Sign1 = GetAreaSign(InBuffer[InEndVertex - 1].coords.xy(), InBuffer[InEndVertex].coords.xy(), InBuffer[InStartVertex].coords.xy());
    int Sign2 = GetAreaSign(InBuffer[InEndVertex].coords.xy(), InBuffer[InStartVertex].coords.xy(), InBuffer[InStartVertex + 1].coords.xy());

    PositiveCorners += (Sign1 > 0) + (Sign2 > 0);
    NegativeCorners += (Sign1 < 0) + (Sign2 < 0);

    if(PositiveCorners > 0 && NegativeCorners == 0)
    {
        return PO_Clockwise;
    }
    else if(PositiveCorners == 0 && NegativeCorners > 0)
    {
        return PO_CounterClockwise;
    }
    else if(PositiveCorners > 0 && NegativeCorners > 0)
    {
        return PO_NotConvex;
    }

    return PO_Degenerate;
}

/**
 * Decide if we should face-cull a polygon with a known orientation.
 *
 * \param States Active render states, includeing the cull mode and front-face mode.
 * \param Orientation The polygon's orientation inside the viewport.
 * \return Returns true if the polygon should be culled based on the render states and the polygon's orientation.
 */
static bool FaceCullPolygon(const render_states* States, EPolyOrientation Orientation)
{
    if(States->cull_mode == cull_face_direction::front_and_back)
    {
        // reject all polygons.
        return true;
    }

    if(States->cull_mode == cull_face_direction::front)
    {
        // reject front-facing polygons.
        return (States->front_face == front_face_orientation::cw && Orientation == PO_Clockwise)
               || (States->front_face == front_face_orientation::ccw && Orientation == PO_CounterClockwise);
    }
    else if(States->cull_mode == cull_face_direction::back)
    {
        // reject back-facing polygons.
        return (States->front_face == front_face_orientation::cw && Orientation == PO_CounterClockwise)
               || (States->front_face == front_face_orientation::ccw && Orientation == PO_Clockwise);
    }

    // accept.
    return false;
}

void render_device_context::AssemblePrimitives(const render_states* States, vertex_buffer_mode Mode, const vertex_buffer& Buffer)
{
    // choose drawing mode.
    if(Mode == vertex_buffer_mode::points
       || States->poly_mode == polygon_mode::point)
    {
        /* draw a list of points */
        for(auto& vertex_it: Buffer)
        {
            rasterizer->add_point(States, &vertex_it);
        }
    }
    else if(Mode == vertex_buffer_mode::lines)
    {
        /* draw a list of lines */
        int size = Buffer.size() & ~1;
        for(int i = 0; i < size; i += 2)
        {
            rasterizer->add_line(States, &Buffer[i], &Buffer[i + 1]);
        }
    }
    else if(Mode == vertex_buffer_mode::triangles)
    {
        if(Buffer.size() < 3)
        {
            return;
        }

        // depending on the polygon mode, the vertex buffer either holds a list of triangles or a list of points.
        if(States->poly_mode == polygon_mode::line)
        {
            size_t LastIndex = 0;
            for(size_t FirstIndex = 0; FirstIndex < Buffer.size(); FirstIndex = LastIndex + 1)
            {
                // note that LastIndex gets updated here.
                if(!GetNextPolygon(Buffer, FirstIndex, LastIndex))
                {
                    // no polygon found.
                    break;
                }

                // culling.
                if(States->culling_enabled)
                {
                    EPolyOrientation Orientation = GetConvexPolygonOrientation(Buffer, FirstIndex, LastIndex);
                    if(Orientation == PO_NotConvex || Orientation == PO_Degenerate)
                    {
                        // do not consider degenerate polygons or non-convex ones.
                        continue;
                    }

                    if(FaceCullPolygon(States, Orientation))
                    {
                        // don't draw.
                        continue;
                    }
                }

                // add the lines to the rasterizer.
                const auto* FirstVertex = &Buffer[FirstIndex];
                const auto* PreviousVertex = FirstVertex;

                for(size_t i = FirstIndex + 1; i <= LastIndex; ++i)
                {
                    const auto* CurrentVertex = &Buffer[i];

                    // Add the current line to the rasterizer.
                    rasterizer->add_line(States, PreviousVertex, CurrentVertex);

                    PreviousVertex = CurrentVertex;
                }
                // close the strip.
                rasterizer->add_line(States, PreviousVertex, FirstVertex);
            }
        }
        else if(States->poly_mode == polygon_mode::fill)
        {
            // Pre-calculate triangle count. Note that always 3*TriangeCount <= Buffer.Vertices.size().
            const size_t TriangleCount = Buffer.size() / 3;
            const auto* BufferData = Buffer.data();

            /* draw a list of triangles */
            for(size_t i = 0; i < TriangleCount; ++i)
            {
                const auto& V1 = BufferData[0];
                const auto& V2 = BufferData[1];
                const auto& V3 = BufferData[2];
                BufferData += 3;

                // determine if triangle is front facing.
                cull_face_direction orient = get_face_orientation(States->front_face, V1.coords.xy(), V2.coords.xy(), V3.coords.xy());
                bool is_front_facing = (orient == cull_face_direction::front);

                // check for face culling
                if(States->culling_enabled && cull_reject(States->cull_mode, orient))
                {
                    // reject
                    continue;
                }

                rasterizer->add_triangle(States, is_front_facing, &V1, &V2, &V3);
            }
        }
        else
        {
            // this intentionally breaks the debugger.
            assert(States->poly_mode == polygon_mode::line || States->poly_mode == polygon_mode::fill);
        }
    }
}

} /* namespace impl */

} /* namespace swr */
