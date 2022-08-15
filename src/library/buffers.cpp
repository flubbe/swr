/**
 * swr - a software rasterizer
 *
 * buffer object management.
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
 * buffer management.
 */

uint32_t CreateVertexBuffer(const std::vector<ml::vec4>& vb)
{
    ASSERT_INTERNAL_CONTEXT;
    int i = impl::global_context->vertex_buffers.push({});
    impl::global_context->vertex_buffers[i].reserve(vb.size());
    for(auto it: vb)
    {
        impl::global_context->vertex_buffers[i].push_back(it);
    }
    return i;
}

uint32_t CreateIndexBuffer(const std::vector<uint32_t>& ib)
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->index_buffers.push(ib);
}

uint32_t CreateAttributeBuffer(const std::vector<ml::vec4>& attribs)
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->vertex_attribute_buffers.push(attribs);
}

template<typename T>
static void delete_buffer(uint32_t id, utils::slot_map<T>& buffers, error& last_error)
{
    if(id < buffers.size())
    {
        buffers[id].clear();
        buffers.free(id);
    }
    else
    {
        last_error = error::invalid_value;
    }
}

void DeleteVertexBuffer(uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    delete_buffer(id, impl::global_context->vertex_buffers, impl::global_context->last_error);
}

void DeleteIndexBuffer(uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    delete_buffer(id, impl::global_context->index_buffers, impl::global_context->last_error);
}

void DeleteAttributeBuffer(uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;

    if(id < impl::global_context->vertex_attribute_buffers.size())
    {
        impl::global_context->vertex_attribute_buffers[id].data.clear(); /* the .data member access here prevents more unification with the delete_buffer function above? */
        impl::global_context->vertex_attribute_buffers.free(id);
    }
    else
    {
        impl::global_context->last_error = error::invalid_value;
    }
}

void EnableAttributeBuffer(uint32_t id, uint32_t slot)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    // check if id and slot are valid.
    if(id < context->vertex_attribute_buffers.size() && slot < context->active_vabs.max_size())
    {
        // check if we need to allocate a new slot.
        if(slot >= context->active_vabs.size())
        {
            context->active_vabs.resize(slot + 1, static_cast<int>(impl::vertex_attribute_index::invalid));
        }

        context->active_vabs[slot] = id;
        context->vertex_attribute_buffers[id].slot = slot;
    }
    else
    {
        context->last_error = error::invalid_value;
    }
}

void DisableAttributeBuffer(uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    // check that BufferId is valid.
    if(id < context->vertex_attribute_buffers.size())
    {
        auto& buf = context->vertex_attribute_buffers[id];
        if(buf.slot >= 0 && static_cast<size_t>(buf.slot) < context->active_vabs.size())
        {
            context->active_vabs[buf.slot] = -1;
            buf.slot = impl::vertex_attribute_buffer::no_slot_associated;

            return;
        }
    }

    context->last_error = error::invalid_value;
}

} /* namespace swr */
