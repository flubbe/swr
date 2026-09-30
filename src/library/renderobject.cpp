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
#include <algorithm>

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

std::size_t render_context::capture_state()
{
    state_snapshots.push_back(states);
    return state_snapshots.size() - 1;
}

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

void render_context::create_draw_command(
  vertex_buffer_mode mode,
  std::size_t count)
{
    if(count == 0)
    {
        last_error = swr::error::invalid_value;
        return;
    }

    const std::uint32_t snapshot_idx = capture_state();
    const std::uint32_t index_begin = index_buffer_pool.size();

    auto indices = index_buffer_pool.allocate_range(count);
    std::ranges::iota(indices, 0);

    boost::container::static_vector<
      std::pair<int, int>,
      swr::limits::max::attributes>
      active_vab_indices;
    std::size_t attribute_slot_count = 0;
    for(std::size_t slot = 0; slot < active_vabs.size(); ++slot)
    {
        auto buffer_id = active_vabs[slot];
        if(buffer_id >= 0)
        {
            active_vab_indices.push_back(
              std::make_pair(slot, buffer_id));

            attribute_slot_count = std::max(attribute_slot_count, slot + 1);
        }
    }

    const std::size_t active_vab_begin = active_vab_indices_pool.size();
    auto active_vab_range = active_vab_indices_pool.allocate_range(
      active_vab_indices.size());
    std::ranges::copy(
      active_vab_indices,
      active_vab_range.begin());

    command_list.emplace_back(impl::draw_command{
      .mode = mode,
      .state_snapshot_index = snapshot_idx,
      .indices = {
        .begin = index_begin,
        .count = count},
      .active_vab_indices = {.begin = active_vab_begin, .count = active_vab_indices.size()},
      .attribute_slot_count = attribute_slot_count,
      .remapped = false,
      .attribute_indices = {.begin = index_begin, .count = count}});
}

void render_context::create_indexed_draw_command(
  vertex_buffer_mode mode,
  std::size_t count,
  const std::vector<std::uint32_t>& index_buffer)
{
    if(index_buffer.empty())
    {
        last_error = swr::error::invalid_value;
        return;
    }

    if(count > index_buffer.size())
    {
        last_error = swr::error::invalid_value;
        return;
    }

    const std::size_t snapshot_idx = capture_state();
    const std::size_t attribute_index_begin = index_buffer_pool.size();

    index_compaction_result compacted =
      compact_indices(
        std::span{index_buffer}.first(count));

    auto attribute_indices = index_buffer_pool.allocate_range(
      compacted.source_indices.size());
    std::ranges::copy(
      compacted.source_indices,
      attribute_indices.begin());

    const std::size_t index_begin = index_buffer_pool.size();
    auto indices = index_buffer_pool.allocate_range(
      compacted.remapped_indices.size());
    std::ranges::copy(
      compacted.remapped_indices,
      indices.begin());

    boost::container::static_vector<
      std::pair<int, int>,
      swr::limits::max::attributes>
      active_vab_indices;
    std::size_t attribute_slot_count{0};
    for(std::size_t slot = 0; slot < active_vabs.size(); ++slot)
    {
        auto buffer_id = active_vabs[slot];
        if(buffer_id >= 0)
        {
            active_vab_indices.push_back(
              std::make_pair(slot, buffer_id));

            attribute_slot_count = std::max(attribute_slot_count, slot + 1);
        }
    }

    const std::size_t active_vab_begin = active_vab_indices_pool.size();
    auto active_vab_range = active_vab_indices_pool.allocate_range(
      active_vab_indices.size());
    std::ranges::copy(
      active_vab_indices,
      active_vab_range.begin());

    // Queue draw command.
    command_list.emplace_back(impl::draw_command{
      .mode = mode,
      .state_snapshot_index = snapshot_idx,
      .indices = {
        .begin = index_begin,
        .count = indices.size()},
      .active_vab_indices = {.begin = active_vab_begin, .count = active_vab_indices.size()},
      .attribute_slot_count = attribute_slot_count,
      .remapped = true,
      .attribute_indices = {.begin = attribute_index_begin, .count = attribute_indices.size()}});
}

} /* namespace impl */

} /* namespace swr */
