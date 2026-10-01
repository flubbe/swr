/**
 * swr - a software rasterizer
 *
 * buffer object management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <algorithm>

/* user headers. */
#include "swr_internal.h"

namespace swr
{

namespace impl
{

void render_context::create_update_buffer_command(
  buffer_update_kind kind,
  std::uint32_t id,
  index_range range)
{
    command_list.emplace_back(impl::update_buffer_command{
      .kind = kind,
      .buffer_id = id,
      .range = range,
    });
}

}    // namespace impl

/*
 * buffer management.
 */

std::uint32_t CreateIndexBuffer(
  std::span<const std::uint32_t> data)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    auto id = context->index_buffers.push({});

    // Defer initialization.
    const std::uint32_t range_start = context->index_buffer_pool.size();
    const std::uint32_t range_size = data.size();

    auto storage = context->index_buffer_pool.allocate_range(range_size);
    std::ranges::copy(data, storage.begin());

    context->create_update_buffer_command(
      impl::buffer_update_kind::index,
      id,
      {range_start, range_size});

    return id;
}

std::uint32_t CreateAttributeBuffer(
  std::span<const ml::vec4> data)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    auto id = context->vertex_attribute_buffers.push({});

    // Defer initialization.
    const std::uint32_t range_start = context->vec4_data.size();
    const std::uint32_t range_size = data.size();

    auto storage = context->vec4_data.allocate_range(range_size);
    std::ranges::copy(data, storage.begin());

    context->create_update_buffer_command(
      impl::buffer_update_kind::attribute,
      id,
      {range_start, range_size});

    return id;
}

void UpdateIndexBuffer(
  std::uint32_t id,
  std::span<const std::uint32_t> data)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(!context->vertex_attribute_buffers.contains(id)
       || context->has_pending_deletion(impl::resource_type::index_buffer, id))
    {
        context->last_error = swr::error::invalid_value;
        return;
    }

    const std::uint32_t range_start = context->index_buffer_pool.size();
    const std::uint32_t range_size = data.size();

    auto storage = context->index_buffer_pool.allocate_range(range_size);
    std::ranges::copy(data, storage.begin());

    context->create_update_buffer_command(
      impl::buffer_update_kind::index,
      id,
      {range_start, range_size});
}

void UpdateAttributeBuffer(
  std::uint32_t id,
  std::span<const ml::vec4> data)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(!context->vertex_attribute_buffers.contains(id)
       || context->has_pending_deletion(impl::resource_type::attribute_buffer, id))
    {
        context->last_error = swr::error::invalid_value;
        return;
    }

    const std::uint32_t range_start = context->vec4_data.size();
    const std::uint32_t range_size = data.size();

    auto storage = context->vec4_data.allocate_range(range_size);
    std::ranges::copy(data, storage.begin());

    context->create_update_buffer_command(
      impl::buffer_update_kind::attribute,
      id,
      {range_start, range_size});
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

void DeleteAttributeBuffer(
  std::uint32_t id)
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

void EnableAttributeBuffer(
  std::uint32_t id,
  std::uint32_t slot)
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

void DisableAttributeBuffer(
  std::uint32_t id)
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
