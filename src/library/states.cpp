/**
 * swr - a software rasterizer
 * 
 * render pipeline state management.
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
 * render context state setter and getter.
 */

void SetState(state s, bool enable)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;

    if (s == state::blend)
    {
        Context->RenderStates.blending_enabled = enable;
    }
    else if (s == state::cull_face)
    {
        Context->RenderStates.culling_enabled = enable;
    }
    else if (s == state::depth_test)
    {
        Context->RenderStates.depth_test_enabled = enable;
    }
    else if (s == state::depth_write)
    {
        Context->RenderStates.write_depth = enable;
    }
    else if (s == state::scissor_test)
    {
        Context->RenderStates.scissor_test_enabled = enable;
    }
}

bool GetState(state s)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;

    if (s == state::blend)
    {
        return Context->RenderStates.blending_enabled;
    }
    else if (s == state::cull_face)
    {
        return Context->RenderStates.culling_enabled;
    }
    else if (s == state::depth_test)
    {
        return Context->RenderStates.depth_test_enabled;
    }
    else if (s == state::depth_write)
    {
        return Context->RenderStates.write_depth;
    }
    else if (s == state::scissor_test)
    {
        return Context->RenderStates.scissor_test_enabled;
    }

    return false;
}

/*
 * blending.
 */

void SetBlendFunc( blend_func sfactor, blend_func dfactor )
{
    ASSERT_INTERNAL_CONTEXT;
    auto* Context = impl::global_context;

    if( Context->im_declaring_primitives )
    {
        Context->last_error = error::invalid_operation;
        return;
    }

    Context->RenderStates.blend_src = sfactor;
    Context->RenderStates.blend_dst = dfactor;
}

blend_func GetSourceBlendFunc()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->RenderStates.blend_src;
}

blend_func GetDestinationBlendFunc()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->RenderStates.blend_dst;
}

/*
 * depth test. 
 */

void SetDepthTest( comparison_func func )
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;
    Context->RenderStates.depth_func = func;
}

comparison_func GetDepthTest()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->RenderStates.depth_func;
}

/*
 * cull mode. 
 */

void SetFrontFace(front_face_orientation ffo)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->RenderStates.front_face = ffo;
}

front_face_orientation GetFrontFace()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->RenderStates.front_face;
}

void SetCullMode(cull_face_direction cfd)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->RenderStates.cull_mode = cfd;
}

cull_face_direction GetCullMode()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->RenderStates.cull_mode;
}

/*
 * polygon mode. 
 */

void SetPolygonMode(polygon_mode Mode)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->RenderStates.poly_mode = Mode;
}

polygon_mode GetPolygonMode()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->RenderStates.poly_mode;
}

/*
 * debugging. 
 */

void SetDebugState(debug_state State, bool Enable)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;

    if(State == debug_state::show_depth_buffer)
    {
        Context->CopyDepthToColor = Enable;
    }
}

bool GetDebugState(debug_state State)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;

    if(State == debug_state::show_depth_buffer)
    {
        return Context->CopyDepthToColor;
    }

    return false;
}

} /* namespace swr */
