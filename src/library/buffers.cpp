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

uint32_t CreateVertexBuffer(const std::vector<ml::vec4> &vb)
{
    ASSERT_INTERNAL_CONTEXT;
    int i = impl::global_context->vertex_buffers.push({});
    impl::global_context->vertex_buffers[i].reserve( vb.size() );
    for( auto it : vb )
    {
        impl::global_context->vertex_buffers[i].push_back(it);
    }
    return i;
}

uint32_t CreateIndexBuffer(const std::vector<uint32_t> &ib)
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->index_buffers.push( ib );
}

uint32_t CreateAttributeBuffer( const std::vector<ml::vec4>& attribs )
{
    ASSERT_INTERNAL_CONTEXT;
    int i = impl::global_context->vertex_attribute_buffers.push({});
    impl::global_context->vertex_attribute_buffers[i].data = attribs;
    return i;
}

void DeleteVertexBuffer( uint32_t id )
{
    ASSERT_INTERNAL_CONTEXT;

    if( id < impl::global_context->vertex_buffers.size() )
    {
        impl::global_context->vertex_buffers[id].resize(0);
        impl::global_context->vertex_buffers.free( id );
    }
    else
    {
        impl::global_context->last_error = error::invalid_value;
    }
}

void DeleteIndexBuffer( uint32_t id )
{
    ASSERT_INTERNAL_CONTEXT;

    if( id < impl::global_context->index_buffers.size() )
    {
        impl::global_context->index_buffers[id].resize(0);
        impl::global_context->index_buffers.free( id );
    }
    else
    {
        impl::global_context->last_error = error::invalid_value;
    }
}

void DeleteAttributeBuffer( uint32_t id )
{
    ASSERT_INTERNAL_CONTEXT;

    if( id < impl::global_context->vertex_attribute_buffers.size() )
    {
        impl::global_context->vertex_attribute_buffers[id].data.resize(0);
        impl::global_context->vertex_attribute_buffers.free( id );
    }
    else
    {
        impl::global_context->last_error = error::invalid_value;
    }
}

void EnableAttributeBuffer( uint32_t id, uint32_t slot )
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;

    // check if id and slot are valid.
    if( id < Context->vertex_attribute_buffers.size() && slot < Context->active_vabs.max_size() )
    {
        auto& Buf = Context->vertex_attribute_buffers[id];

        // check if we need to allocate a new slot.
        if( Context->active_vabs.size() <= slot )
        {
            Context->active_vabs.resize(slot+1, static_cast<int>(impl::vertex_attribute_index::invalid) );
        }

        Context->active_vabs[slot] = id;
        Buf.slot = slot;
    }
    else
    {
        Context->last_error = error::invalid_value;
    }
}

void DisableAttributeBuffer( uint32_t id )
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* Context = impl::global_context;

    // check that BufferId is valid.
    if( id < Context->vertex_attribute_buffers.size() )
    {
        auto& Buf = Context->vertex_attribute_buffers[id];
        if( Buf.slot >= 0 && static_cast<size_t>(Buf.slot) < Context->active_vabs.size() )
        {
            Context->active_vabs[Buf.slot] = -1;
            Buf.slot = impl::vertex_attribute_buffer::no_slot_associated;

            return;
        }
    }

    Context->last_error = error::invalid_value;
}

} /* namespace swr */
