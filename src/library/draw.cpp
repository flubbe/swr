/**
 * swr - a software rasterizer
 *
 * buffer drawing functions.
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
 * drawing.
 */

void DrawElements(std::size_t vertex_count, vertex_buffer_mode mode)
{
    ASSERT_INTERNAL_CONTEXT;

    // add draw command to command list.
    impl::global_context->create_render_object(vertex_count, mode);
}

void DrawIndexedElements(uint32_t index_buffer_id, vertex_buffer_mode mode)
{
    ASSERT_INTERNAL_CONTEXT;

    auto* context = impl::global_context;
    if(index_buffer_id < context->index_buffers.size())
    {
        // add draw command to the command list.
        context->create_indexed_render_object(context->index_buffers[index_buffer_id], mode);
    }
}

} /* namespace swr */