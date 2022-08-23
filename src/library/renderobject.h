/**
 * swr - a software rasterizer
 *
 * render objects for draw lists.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace swr
{

namespace impl
{

/**
 * A render object is the representation of an object (consisting of vertices)
 * during the render stages inside the rendering pipeline.
 */
struct render_object
{
    /** Buffer holding the object's vertex information. */
    vertex_buffer vertices;

    /** Indices into the vertex buffer. */
    index_buffer indices;

    /** Drawing mode. */
    vertex_buffer_mode mode{vertex_buffer_mode::points};

    /** Active render states for this object. */
    render_states states;

    /** Ordered vertices after clipping. */
    vertex_buffer clipped_vertices;

    /** Constructors */
    render_object()
    {
    }

    /** Initialize the object with vertices in sequential order. */
    render_object(std::size_t count, vertex_buffer_mode in_mode, const render_states& in_states)
    : vertices(count)
    , indices(count)
    , mode(in_mode)
    , states(in_states)
    {
        // populate index buffer with consecutive numbers.
        std::iota(std::begin(indices), std::end(indices), 0);
    }

    /** Initialize the object with vertices and indices. */
    render_object(const index_buffer& in_indices, vertex_buffer_mode in_mode, const render_states& in_states)
    : vertices(in_indices.size())
    , indices(in_indices)
    , mode(in_mode)
    , states(in_states)
    {
    }
};

} /* namespace impl */

} /* namespace swr */
