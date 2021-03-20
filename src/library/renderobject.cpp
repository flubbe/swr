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

static void copy_attributes(render_object& NewObject, const boost::container::static_vector<int, geom::limits::max::attributes>& active_vabs, const utils::slot_map<vertex_attribute_buffer>& vertex_attribute_buffers)
{
    int slot = 0;

    // copy the active attribute slots.
    for(auto it: active_vabs)
    {
        // skip empty attribute slots.
        if(it == static_cast<int>(impl::vertex_attribute_index::invalid))
        {
            ++slot;
            continue;
        }

        // copy attributes.
        auto& buf = vertex_attribute_buffers[it];
        for(size_t i = 0; i < NewObject.vertices.size(); ++i)
        {
            // copies vec4 into VertexAttribute.
            NewObject.vertices[i].attribs.push_back(buf.data[i]);
        }

        ++slot;
    }
}

/*
 * create a new render object and initialize it with its vertices, the vertex buffer mode, the render states
 * and the active attributes.
 */
render_object* render_device_context::CreateRenderObject(std::size_t vertex_count, vertex_buffer_mode mode)
{
    // create and initialize new object.
    objects.emplace_back(vertex_count, mode, RenderStates);
    auto& NewObject = objects.back();

    copy_attributes(NewObject, active_vabs, vertex_attribute_buffers);

    return &NewObject;
}

render_object* render_device_context::CreateIndexedRenderObject(const index_buffer& index_buffer, vertex_buffer_mode mode)
{
    // create and initialize new object.
    objects.emplace_back(index_buffer, mode, RenderStates);
    auto& NewObject = objects.back();

    copy_attributes(NewObject, active_vabs, vertex_attribute_buffers);

    return &NewObject;
}

void render_device_context::ReleaseRenderObjects()
{
    objects.resize(0);
}

} /* namespace impl */

} /* namespace swr */
