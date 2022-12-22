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
static bool invoke_vertex_shader_and_clip_preprocess(impl::program_info* shader_info, const boost::container::static_vector<swr::uniform, geom::limits::max::uniform_locations>& uniforms, impl::vertex_buffer& vb)
{
    // check if the whole buffer should be discarded.
    bool clip_discard{true};

    // create shader
    std::vector<std::byte> shader_storage{shader_info->shader->size()};
    swr::program_base* shader = shader_info->shader->create_vertex_shader_instance(shader_storage.data(), uniforms);

    for(auto& vertex_it: vb)
    {
        // allocate space for varyings and invoke the vertex shader.
        vertex_it.varyings.resize(shader_info->varying_count);

        float gl_PointSize{0}; /* currently unused */
        shader->vertex_shader(
          0 /* gl_VertexID */, 0 /* gl_InstanceID */,
          vertex_it.attribs, vertex_it.coords,
          gl_PointSize, nullptr /* gl_ClipDistance */,
          vertex_it.varyings);

        /*
         * Set clipping markers for this vertex. A visible vertex has to satisfy the relations
         *
         *    -w <= x <= w
         *    -w <= y <= w
         *    -w <= z <= w
         *      0 < w.
         */
        if(vertex_it.coords.x < -vertex_it.coords.w || vertex_it.coords.x > vertex_it.coords.w
           || vertex_it.coords.y < -vertex_it.coords.w || vertex_it.coords.y > vertex_it.coords.w
           || vertex_it.coords.z < -vertex_it.coords.w || vertex_it.coords.z > vertex_it.coords.w
           || vertex_it.coords.w <= 0)
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

static void process_vertices(swr::impl::render_object* obj)
{
    obj->clipped_vertices.clear();

    if(obj->vertices.size() == 0 || obj->indices.size() == 0)
    {
        return;
    }

    /*
     * Invoke the vertex shaders and preprocess vertices with respect to clipping.
     * The shaders take the view coordinates as inputs and output the homogeneous clip coordinates.
     * The clip preprecessing sets a marker for each vertex outside the view frustum.
     */
    bool discard_buffer = invoke_vertex_shader_and_clip_preprocess(obj->states.shader_info, obj->states.uniforms, obj->vertices);
    if(discard_buffer)
    {
        return;
    }

    // check we have valid drawing and polygon modes.
    assert(obj->mode == vertex_buffer_mode::points || obj->mode == vertex_buffer_mode::lines || obj->mode == vertex_buffer_mode::triangles);
    assert(obj->states.poly_mode == polygon_mode::point || obj->states.poly_mode == polygon_mode::line || obj->states.poly_mode == polygon_mode::fill);

    /*
     * clip the vertex buffer.
     *
     * if we only want to draw a list of points, we already have enough clipping
     * information from the previous call to invoke_vertex_shader_and_clip_preprocess.
     *
     * Clipping pre-assembles the primitives, i.e. it creates triangles.
     */
    if(obj->mode == vertex_buffer_mode::points || obj->states.poly_mode == polygon_mode::point)
    {
        // copy the correct points.
        for(const auto& index: obj->indices)
        {
            const auto& Vertex = obj->vertices[index];
            if(!(Vertex.flags & geom::vf_clip_discard))
            {
                obj->clipped_vertices.push_back(obj->vertices[index]);
            }
        }
    }
    else if(obj->mode == vertex_buffer_mode::lines)
    {
        clip_line_buffer(obj->vertices, obj->indices, impl::line_list, obj->clipped_vertices);
    }
    else if(obj->mode == vertex_buffer_mode::triangles && obj->states.poly_mode == polygon_mode::line)
    {
        clip_triangle_buffer(obj->vertices, obj->indices, impl::line_list, obj->clipped_vertices);
    }
    else if(obj->states.poly_mode == polygon_mode::fill)
    {
        /* here we necessarily have list_it.Mode == triangles */
        clip_triangle_buffer(obj->vertices, obj->indices, impl::triangle_list, obj->clipped_vertices);
    }

    // skip the rest of the pipeline if no clipped vertices were produced.
    if(obj->clipped_vertices.size() != 0)
    {
        // perspective divide and viewport transformation.
        transform_to_viewport_coords(
            obj->clipped_vertices,
            obj->states.x, obj->states.y,
            obj->states.width, obj->states.height,
            obj->states.z_near, obj->states.z_far);
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
    ASSERT_INTERNAL_CONTEXT;
    auto context = impl::global_context;

    // immediately return if there is nothing to do.
    if(context->render_command_list.size() == 0)
    {
        return;
    }

    // process render commands.
#ifdef SWR_ENABLE_MULTI_THREADING
    for(auto it = context->render_command_list.begin(); it != context->render_command_list.end(); ++it)
    {
        context->thread_pool.push_task(process_vertices, *it);
    }
    context->thread_pool.run_tasks_and_wait();

    for(auto& it: context->render_command_list)
    {
        if(it->clipped_vertices.size() != 0)
        {
            // Assemble primitives from drawing lists. The primitives are passed on to the triangle rasterizer.
            context->assemble_primitives(&it->states, it->mode, it->clipped_vertices);
        }
    }
#else
    for(auto& it: context->render_command_list)
    {
        process_vertices(it);

        if(it->clipped_vertices.size() != 0)
        {
            // Assemble primitives from drawing lists. The primitives are passed on to the triangle rasterizer.
            context->assemble_primitives(&it->states, it->mode, it->clipped_vertices);
        }
    }
#endif

    // invoke triangle rasterizer.
    context->rasterizer->draw_primitives();

#ifdef SWR_ENABLE_STATS
    // store statistical data.
    context->stats_frag = context->rasterizer->stats_frag;
    context->stats_rast = context->rasterizer->stats_rast;
#endif

    // flush all lists.
    context->render_command_list.clear();
    context->objects.clear();
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

    impl::global_context->clear_depth_buffer();
}

void SetClearDepth(float z)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->states.set_clear_depth(z);
}

/*
 * color buffer.
 */

void ClearColorBuffer()
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->clear_color_buffer();
}

void SetClearColor(float r, float g, float b, float a)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->states.set_clear_color(r, g, b, a);
}

/*
 * scissor test.
 */

void SetScissorBox(int x, int y, int width, int height)
{
    ASSERT_INTERNAL_CONTEXT;
    auto context = impl::global_context;

    if(width < 0 || height < 0)
    {
        context->last_error = error::invalid_value;
        return;
    }

    if(context->im_declaring_primitives)
    {
        context->last_error = error::invalid_operation;
        return;
    }

    context->states.set_scissor_box(x, x + width, y, y + height);
}

/*
 * viewport transform.
 */

void SetViewport(int x, int y, unsigned int width, unsigned int height)
{
    ASSERT_INTERNAL_CONTEXT;
    auto context = impl::global_context;

    if(context->im_declaring_primitives)
    {
        context->last_error = error::invalid_operation;
        return;
    }

    context->states.set_viewport(x, y, width, height);
}

void DepthRange(float zNear, float zFar)
{
    ASSERT_INTERNAL_CONTEXT;
    auto context = impl::global_context;

    if(context->im_declaring_primitives)
    {
        context->last_error = error::invalid_operation;
        return;
    }

    context->states.set_depth_range(zNear, zFar);
}

} /* namespace swr */
