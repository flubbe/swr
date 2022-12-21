/**
 * swr - a software rasterizer
 *
 * render object / draw list management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* boost headers */
#include <boost/range/adaptor/indexed.hpp>

/* user headers. */
#include "swr_internal.h"

namespace swr
{
namespace impl
{

/*
 * render object management.
 */

static void copy_attributes(
  render_object& obj,
  const boost::container::static_vector<int, geom::limits::max::attributes>& active_vabs,
  const utils::slot_map<vertex_attribute_buffer>& vertex_attribute_buffers,
  std::function<uint32_t(uint32_t)> transform_fn =
    [](uint32_t i) -> uint32_t
  { return i; })
{
    // copy the active attribute slots.
    for(auto [slot, id]: active_vabs | boost::adaptors::indexed())
    {
        // skip empty attribute slots.
        if(id == static_cast<int>(impl::vertex_attribute_index::invalid))
        {
            continue;
        }

        // copy attributes.
        for(auto [i, vertex]: obj.vertices | boost::adaptors::indexed())
        {
            if(vertex.attribs.size() <= static_cast<std::size_t>(slot))
            {
                vertex.attribs.resize(slot + 1);
            }

            vertex.attribs[slot] = vertex_attribute_buffers[id].data[transform_fn(i)];
        }
    }
}

/*
 * create a new render object and initialize it with its vertices, the vertex buffer mode, the render states
 * and the active attributes.
 */
render_object* render_device_context::create_render_object(std::size_t vertex_count, vertex_buffer_mode mode)
{
    // create and initialize new object.
    objects.emplace_back(vertex_count, mode, states);
    auto& new_object = objects.back();

    copy_attributes(new_object, active_vabs, vertex_attribute_buffers);

    return &new_object;
}

render_object* render_device_context::create_indexed_render_object(const index_buffer& index_buffer, vertex_buffer_mode mode)
{
    // create and initialize new object.
    objects.emplace_back(index_buffer.size(), mode, states);
    auto& new_object = objects.back();

    copy_attributes(new_object, active_vabs, vertex_attribute_buffers,
                    [&index_buffer](uint32_t i) -> uint32_t
                    {
                        return index_buffer[i];
                    });

    return &new_object;
}

} /* namespace impl */

} /* namespace swr */
