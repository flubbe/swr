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

static void copy_attributes(render_object& obj, const boost::container::static_vector<int, geom::limits::max::attributes>& active_vabs, const utils::slot_map<vertex_attribute_buffer>& vertex_attribute_buffers)
{
    const auto vertex_count = obj.vertices.size();
    int slot = -1;

    // copy the active attribute slots.
    for(auto id: active_vabs)
    {
        ++slot;

        // skip empty attribute slots.
        if(id == static_cast<int>(impl::vertex_attribute_index::invalid))
        {
            continue;
        }

        // copy attributes.
        for(size_t i = 0; i < vertex_count; ++i)
        {
            if(obj.vertices[i].attribs.size() <= static_cast<std::size_t>(slot))
            {
                obj.vertices[i].attribs.resize(slot + 1);
            }

            obj.vertices[i].attribs[slot] = vertex_attribute_buffers[id].data[i];
        }
    }
}

static void copy_indexed_attributes(render_object& obj, const index_buffer& index_buffer, const boost::container::static_vector<int, geom::limits::max::attributes>& active_vabs, const utils::slot_map<vertex_attribute_buffer>& vertex_attribute_buffers)
{
    const auto vertex_count = index_buffer.size();
    int slot = -1;

    // copy the active attribute slots.
    for(auto id: active_vabs)
    {
        ++slot;

        // skip empty attribute slots.
        if(id == static_cast<int>(impl::vertex_attribute_index::invalid))
        {
            continue;
        }

        // copy attributes.
        for(size_t i = 0; i < vertex_count; ++i)
        {
            if(obj.vertices[i].attribs.size() <= static_cast<std::size_t>(slot))
            {
                obj.vertices[i].attribs.resize(slot + 1);
            }

            auto index = index_buffer[i];
            obj.vertices[i].attribs[slot] = vertex_attribute_buffers[id].data[index];
        }
    }
}

/*
 * create a new render object and initialize it with its vertices, the vertex buffer mode, the render states
 * and the active attributes.
 */
render_object* render_device_context::CreateRenderObject(std::size_t vertex_count, vertex_buffer_mode mode)
{
    // create and initialize new object.
    objects.emplace_back(vertex_count, mode, states);
    auto& new_object = objects.back();

    copy_attributes(new_object, active_vabs, vertex_attribute_buffers);

    return &new_object;
}

render_object* render_device_context::CreateIndexedRenderObject(const index_buffer& index_buffer, vertex_buffer_mode mode)
{
    // create and initialize new object.
    objects.emplace_back(index_buffer.size(), mode, states);
    auto& new_object = objects.back();

    copy_indexed_attributes(new_object, index_buffer, active_vabs, vertex_attribute_buffers);

    return &new_object;
}

} /* namespace impl */

} /* namespace swr */
