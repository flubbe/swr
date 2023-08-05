/**
 * swr - a software rasterizer
 *
 * render object / draw list management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

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
    if(active_vabs.size() == 0)
    {
        return;
    }

    int attrib_stride = active_vabs.size();
    if(attrib_stride == 0)
    {
        return;
    }

    obj.allocate_attribs(attrib_stride);
    ml::vec4* attribs = obj.attribs;

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
render_object* render_device_context::create_render_object(std::size_t vertex_count, vertex_buffer_mode mode)
{
    // create and initialize new object.
    render_object_list.emplace_back(vertex_count, mode, states);
    auto& new_object = render_object_list.back();

    copy_attributes(new_object, active_vabs, vertex_attribute_buffers);

    return &new_object;
}

render_object* render_device_context::create_indexed_render_object(const index_buffer& index_buffer, vertex_buffer_mode mode)
{
    // create and initialize new object.
    render_object_list.emplace_back(index_buffer.size(), mode, states);
    auto& new_object = render_object_list.back();

    copy_attributes(new_object, active_vabs, vertex_attribute_buffers,
                    [&index_buffer](uint32_t i) -> uint32_t
                    {
                        return index_buffer[i];
                    });

    return &new_object;
}

} /* namespace impl */

} /* namespace swr */
