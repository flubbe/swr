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

    auto* Context = impl::global_context;
    if(Context->im_declaring_primitives)
    {
        Context->last_error = error::invalid_operation;
        return;
    }

    // add the object to the draw list.
    Context->DrawList.push_back(Context->CreateRenderObject(vertex_count, mode));
}

void DrawIndexedElements(uint32_t IndexBufferId, vertex_buffer_mode Mode)
{
    ASSERT_INTERNAL_CONTEXT;

    auto* Context = impl::global_context;
    if(Context->im_declaring_primitives)
    {
        Context->last_error = error::invalid_operation;
        return;
    }

    if(IndexBufferId < Context->index_buffers.size())
    {
        // add the object to the draw list.
        auto& ib = Context->index_buffers[IndexBufferId];
        Context->DrawList.push_back(Context->CreateIndexedRenderObject(ib, Mode));
    }
}

} /* namespace swr */