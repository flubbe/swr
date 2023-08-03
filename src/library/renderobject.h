/**
 * swr - a software rasterizer
 *
 * render objects for draw lists.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
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
class render_object
{
    using storage_type = std::vector<std::byte, utils::allocator<std::byte>>;

    /** Storage for the object's vertex attributes. */
    storage_type attrib_storage;

    /** Storage for the vertex coordinates. */
    storage_type coord_storage;

    /** Storage for the varyings. */
    storage_type varying_storage;

    /** Allocate a buffer. */
    void allocate_buffer(std::size_t count, storage_type& storage, ml::vec4** buffer)
    {
        *buffer = reinterpret_cast<ml::vec4*>(utils::align_vector(utils::alignment::sse, count * sizeof(ml::vec4), storage));
        assert(*buffer != nullptr);
    }

public:
    /** Aligned pointer into the attribute storage. */
    ml::vec4* attribs{nullptr};

    /** Attribute count (per vertex). */
    std::size_t attrib_count{0};

    /** Aligned pointer into the attribute storage. */
    ml::vec4* coords{nullptr};

    /** Coordinate count. */
    std::size_t coord_count{0};

    /** Buffer holding all vertex flags. */
    std::vector<uint32_t> flags;

    /** Aligned pointer into the varying storage. */
    ml::vec4* varyings{nullptr};

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
    : mode{in_mode}
    , states{in_states}
    {
        allocate_coords(count);
        flags.resize(count);

        // populate index buffer with consecutive numbers.
        indices.reserve(count);
        for(std::size_t i=0; i < count; ++i)
        {
            indices.emplace_back(i);
        }
    }

    /** Initialize the object with vertices and indices. */
    render_object(const index_buffer& in_indices, vertex_buffer_mode in_mode, const render_states& in_states)
    : indices{in_indices}
    , mode{in_mode}
    , states{in_states}
    {
        allocate_coords(in_indices.size());
        flags.resize(in_indices.size());
    }

    /**
     * Allocate attributes. 
     * 
     * @param count Attribute count per vertex.
     */
    void allocate_attribs(std::size_t count)
    {
        allocate_buffer(coord_count * count, attrib_storage, &attribs);
        attrib_count = count;
    }

    /** 
     * Allocate coordinates. 
     * 
     * @param count Vertex count.
     */
    void allocate_coords(std::size_t count)
    {
        allocate_buffer(count, coord_storage, &coords);
        coord_count = count;
    }

    /**
     * Allocate varying storage. 
     * 
     * @param count Varying count per vertex.
     */
    void allocate_varyings(std::size_t count)
    {
        allocate_buffer(coord_count * count, varying_storage, &varyings);
    }
};

} /* namespace impl */

} /* namespace swr */
