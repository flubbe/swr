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
    impl::render_device_context* context = impl::global_context;

    if(s == state::blend)
    {
        context->states.blending_enabled = enable;
    }
    else if(s == state::cull_face)
    {
        context->states.culling_enabled = enable;
    }
    else if(s == state::depth_test)
    {
        context->states.depth_test_enabled = enable;
    }
    else if(s == state::depth_write)
    {
        context->states.write_depth = enable;
    }
    else if(s == state::scissor_test)
    {
        context->states.scissor_test_enabled = enable;
    }
}

bool GetState(state s)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(s == state::blend)
    {
        return context->states.blending_enabled;
    }
    else if(s == state::cull_face)
    {
        return context->states.culling_enabled;
    }
    else if(s == state::depth_test)
    {
        return context->states.depth_test_enabled;
    }
    else if(s == state::depth_write)
    {
        return context->states.write_depth;
    }
    else if(s == state::scissor_test)
    {
        return context->states.scissor_test_enabled;
    }

    return false;
}

/*
 * blending.
 */

void SetBlendFunc(blend_func sfactor, blend_func dfactor)
{
    ASSERT_INTERNAL_CONTEXT;
    auto* context = impl::global_context;

    if(context->im_declaring_primitives)
    {
        context->last_error = error::invalid_operation;
        return;
    }

    context->states.blend_src = sfactor;
    context->states.blend_dst = dfactor;
}

blend_func GetSourceBlendFunc()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->states.blend_src;
}

blend_func GetDestinationBlendFunc()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->states.blend_dst;
}

/*
 * depth test.
 */

void SetDepthTest(comparison_func func)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->states.depth_func = func;
}

comparison_func GetDepthTest()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->states.depth_func;
}

/*
 * cull mode.
 */

void SetFrontFace(front_face_orientation ffo)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->states.front_face = ffo;
}

front_face_orientation GetFrontFace()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->states.front_face;
}

void SetCullMode(cull_face_direction cfd)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->states.cull_mode = cfd;
}

cull_face_direction GetCullMode()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->states.cull_mode;
}

/*
 * polygon mode.
 */

void SetPolygonMode(polygon_mode Mode)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::global_context->states.poly_mode = Mode;
}

polygon_mode GetPolygonMode()
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->states.poly_mode;
}

} /* namespace swr */
