/**
 * swr - a software rasterizer
 *
 * render object / draw list management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <limits>
#include <ranges>
#include <unordered_map>

/* user headers. */
#include "swr_internal.h"

namespace swr
{
namespace impl
{

namespace
{

struct index_compaction_result
{
    std::vector<std::uint32_t> remapped_indices;
    std::vector<std::uint32_t> source_indices;
};

/**
 * Build a compact local vertex numbering for the indices used by a mesh
 * subset and remap the index buffer to that numbering.
 */
index_compaction_result compact_indices(
  std::span<const std::uint32_t> indices)
{
    constexpr std::uint32_t invalid_index =
      std::numeric_limits<std::uint32_t>::max();

    index_compaction_result result;
    result.remapped_indices.reserve(indices.size());
    result.source_indices.reserve(indices.size());

    const auto max_source_index =
      *std::ranges::max_element(indices);

    const std::uint64_t dense_limit =
      static_cast<std::uint64_t>(indices.size()) * 4u + 1024u;

    if(static_cast<std::uint64_t>(max_source_index) <= dense_limit)
    {
        std::vector<std::uint32_t> local_index_for_source(
          static_cast<std::size_t>(max_source_index) + 1u,
          invalid_index);

        for(const std::uint32_t source_index: indices)
        {
            std::uint32_t& local_index =
              local_index_for_source[source_index];

            if(local_index == invalid_index)
            {
                local_index =
                  static_cast<std::uint32_t>(result.source_indices.size());

                result.source_indices.push_back(source_index);
            }

            result.remapped_indices.push_back(local_index);
        }

        return result;
    }

    std::unordered_map<std::uint32_t, std::uint32_t> local_index_for_source;
    local_index_for_source.reserve(indices.size());

    for(const std::uint32_t source_index: indices)
    {
        auto [it, inserted] =
          local_index_for_source.emplace(
            source_index,
            static_cast<std::uint32_t>(result.source_indices.size()));

        if(inserted)
        {
            result.source_indices.push_back(source_index);
        }

        result.remapped_indices.push_back(it->second);
    }

    return result;
}

}    // namespace

/*
 * render object management.
 */

template<typename TransformFn>
void copy_attributes(
  render_object& obj,
  const boost::container::static_vector<int, swr::limits::max::attributes>& active_vabs,
  const utils::slot_map<vertex_attribute_buffer>& vertex_attribute_buffers,
  TransformFn&& transform_fn)
{
    if(active_vabs.empty())
    {
        return;
    }

    int attrib_stride = active_vabs.size();
    if(attrib_stride == 0)
    {
        return;
    }

    obj.allocate_attribs(attrib_stride);
    ml::vec4* attribs = obj.attribs.data();

    for(std::size_t i = 0; i < obj.coord_count; ++i)
    {
        for(std::size_t slot = 0; slot < active_vabs.size(); ++slot)
        {
            const int& id = active_vabs[slot];

            if(id == static_cast<int>(impl::vertex_attribute_index::invalid))
            {
                continue;
            }

            attribs[slot] = vertex_attribute_buffers[id].data[transform_fn(i)];
        }

        attribs += attrib_stride;
    }
}

/*
 * create a new render object and initialize it with its vertices, the vertex buffer mode, the render states
 * and the active attributes.
 */
render_object* render_context::create_render_object(
  vertex_buffer_mode mode,
  std::size_t count)
{
    if(count == 0)
    {
        last_error = swr::error::invalid_value;
        return nullptr;
    }

    // create and initialize new object.
    auto& new_object = render_object_list.emplace_back(count, mode, states);
    copy_attributes(
      new_object,
      active_vabs,
      vertex_attribute_buffers,
      [](std::uint32_t i) -> std::uint32_t
      {
          return i;
      });

    return &new_object;
}

render_object* render_context::create_indexed_render_object(
  vertex_buffer_mode mode,
  std::size_t count,
  const std::vector<std::uint32_t>& index_buffer)
{
    if(index_buffer.empty())
    {
        last_error = swr::error::invalid_value;
        return nullptr;
    }

    if(count > index_buffer.size())
    {
        last_error = swr::error::invalid_value;
        return nullptr;
    }

    index_compaction_result compacted =
      compact_indices(
        std::span{index_buffer}.first(count));

    // create and initialize new object.
    render_object_list.emplace_back(
      std::move(compacted.remapped_indices),
      compacted.source_indices.size(),
      mode,
      states);
    auto& new_object = render_object_list.back();

    copy_attributes(
      new_object,
      active_vabs,
      vertex_attribute_buffers,
      [&compacted](std::uint32_t i) -> std::uint32_t
      {
          return compacted.source_indices[i];
      });

    return &new_object;
}

} /* namespace impl */

} /* namespace swr */
