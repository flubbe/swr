/**
 * swr - a software rasterizer
 * 
 * the graphics pipeline.
 * 
 * most of the actual work (e.g. clipping, primitive assembly and rasterization) is delegated to subroutines implemented elsewhere.
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

/*
 * rendering pipeline.
 */

/** Call vertex shaders and set clipping markers. */
static bool invoke_vertex_shader_and_clip_preprocess(impl::program_info* shader_info, impl::vertex_buffer& vb)
{
    // check if the whole buffer should be discarded.
    bool clip_discard{true};

    for(auto& vertex_it: vb)
    {
        // allocate space for varyings and invoke the vertex shader.
        vertex_it.varyings.resize(shader_info->varying_count);

        float gl_PointSize{0}; /* currently unused */
        shader_info->shader->vertex_shader(
          0 /* gl_VertexID */, 0 /* gl_InstanceID */,
          vertex_it.attribs, vertex_it.coords,
          gl_PointSize, nullptr /* gl_ClipDistance */,
          vertex_it.varyings);

        // Set clipping markers for this vertex. A visible vertex has to satisfy the relations
        //
        //    -w <= x <= w
        //    -w <= y <= w
        //    -w <= z <= w
        //      0 < w.
        if(vertex_it.coords.x < -vertex_it.coords.w || vertex_it.coords.x > vertex_it.coords.w || vertex_it.coords.y < -vertex_it.coords.w || vertex_it.coords.y > vertex_it.coords.w || vertex_it.coords.z < -vertex_it.coords.w || vertex_it.coords.z > vertex_it.coords.w || vertex_it.coords.w <= 0)
        {
            vertex_it.flags |= geom::vf_clip_discard;
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
static void transform_to_viewport_coords(impl::vertex_buffer& vb, float x, float y, float width, float height, float z_near, float z_far)
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
    auto context = static_cast<impl::render_device_context*>(impl::global_context);

    // return if there is nothing to draw.
    if(context->DrawList.size() == 0)
    {
        return;
    }

    // Raster vertices in draw lists.
    for(auto& it: context->DrawList)
    {
        // a draw list entry may be null.
        if(!it)
        {
            continue;
        }

        if(it->vertices.size() == 0
           || it->indices.size() == 0)
        {
            continue;
        }

        /*
         * Let the (vertex-)shader know the active render states.
         */
        it->states.shader_info->shader->update_uniforms(&it->states.uniforms);

        /*
         * Invoke the vertex shaders and preprocess vertices with respect to clipping.
         * The shaders take the view coordinates as inputs and output the homogeneous clip coordinates.
         * The clip preprecessing sets a marker for each vertex outside the view frustum.
         */
        bool discard_buffer = invoke_vertex_shader_and_clip_preprocess(it->states.shader_info, it->vertices);
        if(discard_buffer)
        {
            continue;
        }

        // check we have valid drawing and polygon modes.
        assert(it->mode == vertex_buffer_mode::points || it->mode == vertex_buffer_mode::lines || it->mode == vertex_buffer_mode::triangles);
        assert(it->states.poly_mode == polygon_mode::point || it->states.poly_mode == polygon_mode::line || it->states.poly_mode == polygon_mode::fill);

        /*
         * clip the vertex buffer.
         *
         * if we only want to draw a list of points, we already have enough clipping
         * information from the previous call to invoke_vertex_shader_and_clip_preprocess.
         *
         * Clipping pre-assembles the primitives, i.e. it creates triangles.
         */
        it->clipped_vertices.clear();
        if(it->mode == vertex_buffer_mode::points || it->states.poly_mode == polygon_mode::point)
        {
            // copy the correct points.
            for(const auto& index: it->indices)
            {
                const auto& Vertex = it->vertices[index];
                if(!(Vertex.flags & geom::vf_clip_discard))
                {
                    it->clipped_vertices.push_back(it->vertices[index]);
                }
            }
        }
        else if(it->mode == vertex_buffer_mode::lines || it->states.poly_mode == polygon_mode::line)
        {
            clip_triangle_buffer(it->vertices, it->indices, impl::line_list, it->clipped_vertices);
        }
        else if(it->states.poly_mode == polygon_mode::fill)
        {
            /* here we necessarily have list_it.Mode == triangles */
            clip_triangle_buffer(it->vertices, it->indices, impl::triangle_list, it->clipped_vertices);
        }

        // skip the rest of the pipeline if no clipped vertices were produced.
        if(it->clipped_vertices.size() == 0)
        {
            continue;
        }

        // perspective divide and viewport transformation.
        transform_to_viewport_coords(
          it->clipped_vertices,
          it->states.x, it->states.y,
          it->states.width, it->states.height,
          it->states.z_near, it->states.z_far);

        // Assemble primitives from drawing lists. The primitives are passed on to the triangle rasterizer.
        context->AssemblePrimitives(&it->states, it->mode, it->clipped_vertices);
    }

    // invoke triangle rasterizer.
    context->rasterizer->draw_primitives();

#ifdef SWR_ENABLE_STATS
    // store statistical data.
    context->stats_frag = context->rasterizer->stats_frag;
    context->stats_rast = context->rasterizer->stats_rast;
#endif

    // flush drawing lists.
    context->DrawList.resize(0);
    context->ReleaseRenderObjects();

    // debugging.
    if(context->CopyDepthToColor)
    {
        context->DisplayDepthBuffer();
    }
}

/*
 * depth buffer.
 */

void ClearDepthBuffer()
{
    ASSERT_INTERNAL_CONTEXT;

    if(impl::global_context->im_declaring_primitives)
    {
        impl::global_context->last_error = error::invalid_operation;
        return;
    }

    impl::global_context->ClearDepthBuffer();
}

void SetClearDepth(float z)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->SetClearDepth(boost::algorithm::clamp(z, 0.f, 1.f));
}

/*
 * color buffer.
 */

void ClearColorBuffer()
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->ClearColorBuffer();
}

void SetClearColor(float r, float g, float b, float a)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->SetClearColor(r, g, b, a);
}

/*
 * scissor test.
 */

void SetScissorBox(int x, int y, int width, int height)
{
    ASSERT_INTERNAL_CONTEXT;
    auto* Context = impl::global_context;
    assert(Context);

    if(width < 0 || height < 0)
    {
        Context->last_error = error::invalid_value;
        return;
    }

    if(Context->im_declaring_primitives)
    {
        Context->last_error = error::invalid_operation;
        return;
    }

    Context->RenderStates.scissor_box = {x, x + width, y, y + height};
}

/*
 * viewport transform.
 */

void SetViewport(int x, int y, unsigned int width, unsigned int height)
{
    ASSERT_INTERNAL_CONTEXT;
    auto* Context = impl::global_context;
    assert(Context);

    if(width < 0 || height < 0)
    {
        Context->last_error = error::invalid_value;
        return;
    }

    if(Context->im_declaring_primitives)
    {
        Context->last_error = error::invalid_operation;
        return;
    }

    Context->RenderStates.x = x;
    Context->RenderStates.y = y;
    Context->RenderStates.width = width;
    Context->RenderStates.height = height;
}

void DepthRange(float zNear, float zFar)
{
    ASSERT_INTERNAL_CONTEXT;
    auto* Context = impl::global_context;
    assert(Context);

    if(Context->im_declaring_primitives)
    {
        Context->last_error = error::invalid_operation;
        return;
    }

    Context->RenderStates.z_near = boost::algorithm::clamp(zNear, 0.f, 1.f);
    Context->RenderStates.z_far = boost::algorithm::clamp(zFar, 0.f, 1.f);
}

} /* namespace swr */
