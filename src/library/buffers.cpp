/**
 * swr - a software rasterizer
 *
 * buffer object management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{

/*
 * buffer management.
 */

std::uint32_t CreateIndexBuffer(const std::vector<std::uint32_t>& ib)
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->index_buffers.push(ib);
}

std::uint32_t CreateAttributeBuffer(const std::vector<ml::vec4>& attribs)
{
    ASSERT_INTERNAL_CONTEXT;
    return impl::global_context->vertex_attribute_buffers.push(attribs);
}

void UpdateIndexBuffer(
  std::uint32_t id,
  const std::vector<std::uint32_t>& data)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(!context->index_buffers.contains(id)
       || context->has_pending_deletion(impl::resource_type::index_buffer, id))
    {
        impl::global_context->last_error = swr::error::invalid_value;
        return;
    }

    context->index_buffers[id] = data;
}

void UpdateAttributeBuffer(std::uint32_t id, const std::vector<ml::vec4>& data)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(!context->vertex_attribute_buffers.contains(id)
       || context->has_pending_deletion(impl::resource_type::attribute_buffer, id))
    {
        context->last_error = swr::error::invalid_value;
        return;
    }

    context->vertex_attribute_buffers[id] = data;
}

void DeleteIndexBuffer(
  std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    auto* context = impl::global_context;

    if(!context->index_buffers.contains(id))
    {
        context->last_error = error::invalid_value;
        return;
    }

    // Mark buffer for deletion.
    // Duplications are resolved when processing deletions.
    context->pending_resource_deletions.push_back(
      {.type = impl::resource_type::index_buffer,
       .id = id});
}

void DeleteAttributeBuffer(std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    auto* context = impl::global_context;

    if(!context->vertex_attribute_buffers.contains(id))
    {
        context->last_error = error::invalid_value;
        return;
    }

    // Mark buffer for deletion.
    // Duplications are resolved when processing deletions.
    context->pending_resource_deletions.push_back(
      {.type = impl::resource_type::attribute_buffer,
       .id = id});
}

void EnableAttributeBuffer(std::uint32_t id, std::uint32_t slot)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(!context->vertex_attribute_buffers.contains(id)
       || context->has_pending_deletion(impl::resource_type::attribute_buffer, id)
       || slot >= context->active_vabs.max_size())
    {
        context->last_error = error::invalid_value;
        return;
    }

    if(slot >= context->active_vabs.size())
    {
        context->active_vabs.resize(
          slot + 1,
          static_cast<int>(impl::vertex_attribute_index::invalid));
    }

    context->active_vabs[slot] = id;
    context->vertex_attribute_buffers[id].slot = slot;
}

void DisableAttributeBuffer(std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(!context->vertex_attribute_buffers.contains(id)
       || context->has_pending_deletion(impl::resource_type::attribute_buffer, id))
    {
        context->last_error = error::invalid_value;
        return;
    }

    auto& buf = context->vertex_attribute_buffers[id];
    if(buf.slot < 0
       || static_cast<std::size_t>(buf.slot) >= context->active_vabs.size())
    {
        context->last_error = error::invalid_value;
        return;
    }

    context->active_vabs[buf.slot] = -1;
    buf.slot = impl::vertex_attribute_buffer::no_slot_associated;
}

} /* namespace swr */
