/**
 * swr - a software rasterizer
 *
 * vertex-, index- and attribute buffers.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

namespace swr
{

namespace impl
{

/** vertex buffer. */
typedef std::vector<geom::vertex, utils::allocator<geom::vertex>> vertex_buffer;

/**
 * vertex attribute buffer.
 *
 * each attribute seems to have 4 entries (some of them may be unused),
 * see https://www.khronos.org/opengl/wiki/GLAPI/glBindAttribLocation .
 */
struct vertex_attribute_buffer
{
    enum no_slot_associated
    {
        no_slot_associated = -1
    };

    /** The slot this buffer is bound to; no_slot_associated if none. */
    int slot{no_slot_associated};

    /** buffer data. */
    std::vector<ml::vec4> data;

    /** default constructor. */
    vertex_attribute_buffer() = default;

    /** constructor. */
    vertex_attribute_buffer(const std::vector<ml::vec4>& in_data)
    : data(in_data)
    {
    }
};

} /* namespace impl */

} /* namespace swr */
