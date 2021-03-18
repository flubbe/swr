/**
 * swr - a software rasterizer
 * 
 * immediate mode for drawing primitives.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{

/*
 * immediate mode implementation.
 */

void BeginPrimitives( vertex_buffer_mode mode )
{
    ASSERT_INTERNAL_CONTEXT;
    auto context = impl::global_context;

    if(context->im_declaring_primitives)
    {
        // we are already inside a primitive declaration
        context->last_error = error::invalid_operation;
        return;
    }

    // make sure all buffers are empty and set up mode.
    context->im_vertex_buf.resize(0);
    context->im_color_buf.resize(0);
    context->im_tex_coord_buf.resize(0);
    context->im_normal_buf.resize(0);

    context->im_mode = mode;

    context->im_declaring_primitives = true;
}

void EndPrimitives()
{
    ASSERT_INTERNAL_CONTEXT;
    auto* context = impl::global_context;

    if(!context->im_declaring_primitives)
    {
        // we did not declare any primitive.
        context->im_vertex_buf.resize(0);
        context->im_color_buf.resize(0);
        context->im_tex_coord_buf.resize(0);
        context->im_normal_buf.resize(0);

        context->last_error = error::invalid_operation;

        return;
    }

    // check that the buffer sizes match.
    auto ref_size = context->im_vertex_buf.size();
    if( ref_size != context->im_color_buf.size() 
     || ref_size != context->im_tex_coord_buf.size()
     || ref_size != context->im_normal_buf.size() )
    {
        // inconsistent declaration.
        context->im_vertex_buf.resize(0);
        context->im_color_buf.resize(0);
        context->im_tex_coord_buf.resize(0);
        context->im_normal_buf.resize(0);

        context->im_declaring_primitives = false;

        context->last_error = error::invalid_value;

        return;
    }

    // only add non-empty declarations.
    if(ref_size != 0)
    {
        vertex_buffer_mode mode = context->im_mode;

        /*
         * check if we constructed triangles out of more complex primitives during.
         */
        if( mode != vertex_buffer_mode::points && mode != vertex_buffer_mode::lines && mode != vertex_buffer_mode::triangles )
        {
            // here BufferMode is one of triangle_tan, triangle_strip, quads, polygon.
            // these were divided into triangles during vertex insertion, so we need to change the buffer's mode.
            mode = vertex_buffer_mode::triangles;
        }

        // make sure attribute buffers are disabled.
        context->active_vabs.resize(0);

        // create temporary buffers.
        auto vertex_buffer_id = CreateAttributeBuffer(context->im_vertex_buf);
        auto color_id = CreateAttributeBuffer(context->im_color_buf);
        auto tex_coord_id = CreateAttributeBuffer(context->im_tex_coord_buf);
        auto normal_id = CreateAttributeBuffer(context->im_normal_buf);

        // enable attribute buffers.
        EnableAttributeBuffer(vertex_buffer_id, default_index::position);
        EnableAttributeBuffer(color_id, default_index::color);
        EnableAttributeBuffer(tex_coord_id, default_index::tex_coord);
        EnableAttributeBuffer(normal_id, default_index::normal);

        // add the object to the draw list.
        auto* NewObject = context->CreateRenderObject( context->im_vertex_buf.size(), mode );
        if( NewObject != nullptr )
        {
            context->DrawList.push_back( NewObject );
        }

        // disable attribute buffers.
        DisableAttributeBuffer(normal_id);
        DisableAttributeBuffer(tex_coord_id);
        DisableAttributeBuffer(color_id);
        DisableAttributeBuffer(vertex_buffer_id);

        // destroy temporary buffers.
        DeleteAttributeBuffer(normal_id);
        DeleteAttributeBuffer(tex_coord_id);
        DeleteAttributeBuffer(color_id);
        DeleteAttributeBuffer(vertex_buffer_id);
    }

    // empty the buffer.
    context->im_vertex_buf.resize(0);
    context->im_color_buf.resize(0);
    context->im_tex_coord_buf.resize(0);
    context->im_normal_buf.resize(0);

    context->im_declaring_primitives = false;
}

/*
 * MSDN states:
 *
 *     Neither floating - point nor signed integer values are clamped to the range [0, 1]
 *     before the current color is updated. However, color components are clamped to this
 *     range before they are interpolated or written into a color buffer.
 *
 * [see https://msdn.microsoft.com/en-us/library/windows/desktop/dd318429(v=vs.85).aspx]
 *
 * Here, we immediately update the current color and thus clamp the values on insertion.
 */

void SetColor( float r, float g, float b, float a )
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->im_color = ml::clamp_to_unit_interval( {r,g,b,a} );
}

void SetTexCoord( float u, float v )
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->im_tex_coord = { u, v, 0.f, 0.f };
}

void InsertVertex( float x, float y, float z, float w )
{
    ASSERT_INTERNAL_CONTEXT;
    auto* context = impl::global_context;

    const size_t buffer_size = context->im_vertex_buf.size();
    assert(buffer_size==context->im_color_buf.size());
    assert(buffer_size==context->im_tex_coord_buf.size());
    assert(buffer_size==context->im_normal_buf.size());

    if (context->im_mode == vertex_buffer_mode::triangle_strip)
	{
		if (buffer_size >= 3)
		{
			const auto num_tris = buffer_size / 3;
			
			// every second triangle has reversed orientation (vertex ordering checked).
			if (num_tris & 2)
			{
                const auto v1 = context->im_vertex_buf[buffer_size - 2];
                const auto v2 = context->im_vertex_buf[buffer_size - 1];
                context->im_vertex_buf.push_back(v2);
                context->im_vertex_buf.push_back(v1);

                const auto c1 = context->im_color_buf[buffer_size - 2];
                const auto c2 = context->im_color_buf[buffer_size - 1];
                context->im_color_buf.push_back(c2);
                context->im_color_buf.push_back(c1);

                const auto t1 = context->im_tex_coord_buf[buffer_size - 2];
                const auto t2 = context->im_tex_coord_buf[buffer_size - 1];
                context->im_tex_coord_buf.push_back(t2);
                context->im_tex_coord_buf.push_back(t1);

                const auto n1 = context->im_normal_buf[buffer_size - 2];
                const auto n2 = context->im_normal_buf[buffer_size - 1];
                context->im_normal_buf.push_back(n2);
                context->im_normal_buf.push_back(n1);
			}
			else
			{
                const auto v1 = context->im_vertex_buf[buffer_size - 2]; 
                const auto v2 = context->im_vertex_buf[buffer_size - 1];
                context->im_vertex_buf.push_back(v1);
                context->im_vertex_buf.push_back(v2);

                const auto c1 = context->im_color_buf[buffer_size - 2];
                const auto c2 = context->im_color_buf[buffer_size - 1];
                context->im_color_buf.push_back(c1);
                context->im_color_buf.push_back(c2);

                const auto t1 = context->im_tex_coord_buf[buffer_size - 2];
                const auto t2 = context->im_tex_coord_buf[buffer_size - 1];
                context->im_tex_coord_buf.push_back(t1);
                context->im_tex_coord_buf.push_back(t2);

                const auto n1 = context->im_normal_buf[buffer_size - 2];
                const auto n2 = context->im_normal_buf[buffer_size - 1];
                context->im_normal_buf.push_back(n1);
                context->im_normal_buf.push_back(n2);
			}
		}
	}
    else if (context->im_mode == vertex_buffer_mode::triangle_fan)
	{
		if (buffer_size >= 3)
		{
			// insert the previous two vertices to convert fan to triangles (vertex ordering checked).
            const auto v1 = context->im_vertex_buf[0];
            const auto v2 = context->im_vertex_buf[buffer_size-1];
            context->im_vertex_buf.push_back(v1);
            context->im_vertex_buf.push_back(v2);

            const auto c1 = context->im_color_buf[0];
            const auto c2 = context->im_color_buf[buffer_size - 1];
            context->im_color_buf.push_back(c1);
            context->im_color_buf.push_back(c2);

            const auto t1 = context->im_tex_coord_buf[0];
            const auto t2 = context->im_tex_coord_buf[buffer_size - 1];
            context->im_tex_coord_buf.push_back(t1);
            context->im_tex_coord_buf.push_back(t2);

            const auto n1 = context->im_normal_buf[0];
            const auto n2 = context->im_normal_buf[buffer_size - 1];
            context->im_normal_buf.push_back(n1);
            context->im_normal_buf.push_back(n2);
		}
	}
    else if (context->im_mode == vertex_buffer_mode::quads)
	{
        // insert additional vertices to convert quads to triangles.
        if((buffer_size % 6) / 3 == 1)
        {
            const auto v1 = context->im_vertex_buf[buffer_size-3];
            const auto v2 = context->im_vertex_buf[buffer_size-1];
            context->im_vertex_buf.push_back(v1);
            context->im_vertex_buf.push_back(v2);

            const auto c1 = context->im_color_buf[buffer_size - 3];
            const auto c2 = context->im_color_buf[buffer_size - 1];
            context->im_color_buf.push_back(c1);
            context->im_color_buf.push_back(c2);

            const auto t1 = context->im_tex_coord_buf[buffer_size - 3];
            const auto t2 = context->im_tex_coord_buf[buffer_size - 1];
            context->im_tex_coord_buf.push_back(t1);
            context->im_tex_coord_buf.push_back(t2);

            const auto n1 = context->im_normal_buf[buffer_size - 3];
            const auto n2 = context->im_normal_buf[buffer_size - 1];
            context->im_normal_buf.push_back(n1);
            context->im_normal_buf.push_back(n2);
        }
    }
    else if (context->im_mode == vertex_buffer_mode::polygon)
	{
		if (buffer_size >= 3)
		{
			// insert additional vertices to convert polygons to triangles (vertex ordering checked).
            const auto v1 = context->im_vertex_buf[0];
            const auto v2 = context->im_vertex_buf[buffer_size - 1];
            context->im_vertex_buf.push_back(v1);
            context->im_vertex_buf.push_back(v2);

            const auto c1 = context->im_color_buf[0];
            const auto c2 = context->im_color_buf[buffer_size - 1];
            context->im_color_buf.push_back(c1);
            context->im_color_buf.push_back(c2);

            const auto t1 = context->im_tex_coord_buf[0];
            const auto t2 = context->im_tex_coord_buf[buffer_size - 1];
            context->im_tex_coord_buf.push_back(t1);
            context->im_tex_coord_buf.push_back(t2);

            const auto n1 = context->im_normal_buf[0];
            const auto n2 = context->im_normal_buf[buffer_size - 1];
            context->im_normal_buf.push_back(n1);
            context->im_normal_buf.push_back(n2);
		}
	}

    context->im_vertex_buf.push_back( ml::vec4{ x, y, z, w } );
    context->im_color_buf.push_back( context->im_color );
    context->im_tex_coord_buf.push_back( context->im_tex_coord );
    context->im_normal_buf.push_back( context->im_normal );
}

} /* namespace swr */
