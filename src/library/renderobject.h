/**
 * swr - a software rasterizer
 *
 * render objects for draw lists.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

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
    using storage_type = std::vector<
      ml::vec4,
      utils::aligned_default_init_allocator<
        ml::vec4,
        utils::alignment::sse>>;

    static void assert_sse_aligned(
      const storage_type& buffer)
    {
        assert(buffer.empty()
               || (reinterpret_cast<std::uintptr_t>(buffer.data()) % utils::alignment::sse) == 0);
    }

public:
    /** Aligned vertex attribute storage. */
    storage_type attribs;

    /** Attribute count (per vertex). */
    std::size_t attrib_count{0};

    /** Aligned vertex coordinate storage. */
    storage_type coords;

    /** Coordinate count. */
    std::size_t coord_count{0};

    /** Buffer holding all vertex flags. */
    std::vector<std::uint32_t> vertex_flags;

    /** Aligned varying storage. */
    storage_type varyings;

    /** Varying count per vertex. */
    std::size_t varying_count{0};

    /** Indices into the vertex buffer. */
    std::vector<std::uint32_t> indices;

    /** Drawing mode. */
    vertex_buffer_mode mode{vertex_buffer_mode::points};

    /** Active render states for this object. */
    render_states states;

    /** Ordered vertices after clipping. */
    vertex_buffer clipped_vertices;

    /** Constructors */
    render_object() = default;

    /** Initialize the object with vertices in sequential order. */
    render_object(
      std::size_t count,
      vertex_buffer_mode in_mode,
      const render_states& in_states)
    : indices{std::views::iota(std::size_t{0}, count)
              | std::ranges::to<std::vector<std::uint32_t>>()}
    , mode{in_mode}
    , states{in_states}
    {
        allocate_coords(count);
        vertex_flags.resize(count);
    }

    /** Initialize the object with vertices and indices. */
    render_object(
      const std::vector<std::uint32_t>& in_indices,
      vertex_buffer_mode in_mode,
      const render_states& in_states)
    : indices{in_indices}
    , mode{in_mode}
    , states{in_states}
    {
        allocate_coords(in_indices.size());
        vertex_flags.resize(in_indices.size());
    }

    /**
     * Allocate attributes.
     *
     * @param count Attribute count per vertex.
     */
    void allocate_attribs(
      std::size_t count)
    {
        attrib_count = count;
        attribs.resize(coord_count * attrib_count);
        assert_sse_aligned(attribs);
    }

    /**
     * Allocate coordinates.
     *
     * @param count Vertex count.
     */
    void allocate_coords(
      std::size_t count)
    {
        coord_count = count;
        coords.resize(coord_count);
        assert_sse_aligned(coords);
    }

    /**
     * Allocate varying storage.
     *
     * @param count Varying count per vertex.
     */
    void allocate_varyings(
      std::size_t count)
    {
        varying_count = count;
        varyings.resize(coord_count * varying_count);
        assert_sse_aligned(varyings);
    }

    /** Access all attribute storage. */
    [[nodiscard]]
    std::span<ml::vec4> attrib_span()
    {
        return {attribs.data(), attribs.size()};
    }

    /** Access all attribute storage. */
    [[nodiscard]]
    std::span<const ml::vec4> attrib_span() const
    {
        return {attribs.data(), attribs.size()};
    }

    /** Access the attributes belonging to a single vertex. */
    [[nodiscard]]
    std::span<ml::vec4> attribs_for_vertex(
      std::size_t vertex)
    {
        assert(vertex < coord_count);

        if(attrib_count == 0)
        {
            return {};
        }

        const std::size_t offset = vertex * attrib_count;
        assert(offset + attrib_count <= attribs.size());
        return {attribs.data() + offset, attrib_count};
    }

    /** Access the attributes belonging to a single vertex. */
    [[nodiscard]]
    std::span<const ml::vec4> attribs_for_vertex(
      std::size_t vertex) const
    {
        assert(vertex < coord_count);

        if(attrib_count == 0)
        {
            return {};
        }

        const std::size_t offset = vertex * attrib_count;
        assert(offset + attrib_count <= attribs.size());
        return {attribs.data() + offset, attrib_count};
    }

    /** Access all coordinate storage. */
    [[nodiscard]]
    std::span<ml::vec4> coord_span()
    {
        return {coords.data(), coords.size()};
    }

    /** Access all coordinate storage. */
    [[nodiscard]]
    std::span<const ml::vec4> coord_span() const
    {
        return {coords.data(), coords.size()};
    }

    /** Access all varying storage. */
    [[nodiscard]]
    std::span<ml::vec4> varying_span()
    {
        return {varyings.data(), varyings.size()};
    }

    /** Access all varying storage. */
    [[nodiscard]]
    std::span<const ml::vec4> varying_span() const
    {
        return {varyings.data(), varyings.size()};
    }

    /** Access the varyings belonging to a single vertex. */
    [[nodiscard]]
    std::span<ml::vec4> varyings_for_vertex(std::size_t vertex)
    {
        assert(vertex < coord_count);

        if(varying_count == 0)
        {
            return {};
        }

        const std::size_t offset = vertex * varying_count;
        assert(offset + varying_count <= varyings.size());
        return {varyings.data() + offset, varying_count};
    }

    /** Access the varyings belonging to a single vertex. */
    [[nodiscard]]
    std::span<const ml::vec4> varyings_for_vertex(
      std::size_t vertex) const
    {
        assert(vertex < coord_count);

        if(varying_count == 0)
        {
            return {};
        }

        const std::size_t offset = vertex * varying_count;
        assert(offset + varying_count <= varyings.size());
        return {varyings.data() + offset, varying_count};
    }
};

} /* namespace impl */

} /* namespace swr */
