/**
 * swr - a software rasterizer
 *
 * render objects for draw lists.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <cstdint>
#include <numeric>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace swr
{

namespace impl
{

/** Describes where post-clipping primitives should read their vertices from. */
enum class clipped_vertex_source
{
    none,
    expanded_vertices,
    original_indexed_vertices
};

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
      [[maybe_unused]] const std::span<ml::vec4> buffer)
    {
        assert(buffer.empty()
               || (reinterpret_cast<std::uintptr_t>(buffer.data()) % utils::alignment::sse) == 0);
    }

    static void assert_sse_aligned(
      [[maybe_unused]] const storage_type& buffer)
    {
        assert(buffer.empty()
               || (reinterpret_cast<std::uintptr_t>(buffer.data()) % utils::alignment::sse) == 0);
    }

public:
    /**
     * Aligned vertex attribute storage. Owned per render_object; populated at draw-call
     * time via copy_attributes and read during the vertex shader stage in Present().
     */
    storage_type attribs;

    /** Attribute count (per vertex). */
    std::size_t attrib_count{0};

    /**
     * View into the per-frame vertex data pool for coordinate storage.
     * Span is assigned at the start of Present() after pool pre-reservation.
     * Owned and lifetime-managed by render_context::vertex_data_pool.
     */
    std::span<ml::vec4> coords;

    /** Coordinate count. */
    std::size_t coord_count{0};

    /** Buffer holding all vertex flags. */
    std::vector<std::uint32_t> vertex_flags;

    /**
     * View into the per-frame vertex data pool for varying storage.
     * Span is assigned inside invoke_vertex_shader_and_clip_preprocess() during
     * Present(), after the pool has been pre-reserved for all objects.
     * Owned and lifetime-managed by render_context::vertex_data_pool.
     */
    std::span<ml::vec4> varyings;

    /** Varying count per vertex. */
    std::size_t varying_count{0};

    /** Indices into the vertex buffer. */
    std::span<std::uint32_t> indices;

    /** Drawing mode. */
    vertex_buffer_mode mode{vertex_buffer_mode::points};

    /**
     * Snapshot index into render_context::state_snapshots (set during the draw phase).
     * The corresponding pointer is resolved into `states` at the start of Present().
     */
    std::size_t state_snapshot_index{std::size_t(-1)};

    /** Pointer to the active render states, valid only during Present() processing. */
    const render_states* states{nullptr};

    /** Materialized vertices after clipping, or after assembly of original indexed vertices. */
    vertex_buffer clipped_vertices;

    /** Source selected by clipping/pre-assembly. */
    clipped_vertex_source clipped_vertices_source{clipped_vertex_source::none};

    /** Whether any vertex was marked outside the clip volume by the vertex stage. */
    bool has_clip_discard{false};

    /** Constructors */
    render_object() = default;

    /**
     * Setup state for drawing.
     *
     * Caller must fill in `indices`.
     */
    void setup(
      std::size_t vertex_count,
      vertex_buffer_mode in_mode,
      std::size_t in_state_snapshot_index)
    {
        state_snapshot_index = in_state_snapshot_index;
        states = nullptr;
        mode = in_mode;
        attrib_count = 0;
        attribs.clear();
        varying_count = 0;
        varyings = {};
        clipped_vertices.clear();
        clipped_vertices_source = clipped_vertex_source::none;
        has_clip_discard = false;

        // coord_count is set; the coords span is populated by render_context at
        // the start of Present() after pool pre-reservation.
        coord_count = vertex_count;
        vertex_flags.assign(vertex_count, 0u);
    }

    /** Clear any previous clipping output. */
    void clear_clipped_output()
    {
        clipped_vertices.clear();
        clipped_vertices_source = clipped_vertex_source::none;
    }

    /** Mark the current clipped_vertices buffer as expanded primitive output. */
    void use_expanded_clipped_vertices()
    {
        clipped_vertices_source = clipped_vertices.empty()
                                    ? clipped_vertex_source::none
                                    : clipped_vertex_source::expanded_vertices;
    }

    /** Use the original post-shader vertex/index storage as clipping output. */
    void use_original_indexed_vertices()
    {
        clipped_vertices.clear();
        clipped_vertices_source = indices.empty()
                                    ? clipped_vertex_source::none
                                    : clipped_vertex_source::original_indexed_vertices;
    }

    /** Return whether clipping/pre-assembly produced anything to assemble. */
    [[nodiscard]]
    bool has_clipped_output() const
    {
        if(clipped_vertices_source == clipped_vertex_source::original_indexed_vertices)
        {
            return !indices.empty();
        }

        return !clipped_vertices.empty();
    }

    /**
     * Bind pre-allocated attribute storage from the vertex data pool.
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
     * Bind pre-allocated coordinate storage from the vertex data pool.
     *
     * @param pool_span Span of coord_count elements from the pool.
     */
    void allocate_coords(std::span<ml::vec4> pool_span)
    {
        assert(pool_span.size() == coord_count);
        coords = pool_span;
        assert_sse_aligned(coords);
    }

    /**
     * Bind pre-allocated varying storage from the vertex data pool.
     *
     * @param count     Varying count per vertex.
     * @param pool_span Span of coord_count * count elements from the pool.
     */
    void allocate_varyings(
      std::size_t count,
      std::span<ml::vec4> pool_span)
    {
        assert(pool_span.size() == coord_count * count);
        varying_count = count;
        varyings = pool_span;
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
        return coords;
    }

    /** Access all coordinate storage. */
    [[nodiscard]]
    std::span<const ml::vec4> coord_span() const
    {
        return coords;
    }

    /** Access all varying storage. */
    [[nodiscard]]
    std::span<ml::vec4> varying_span()
    {
        return varyings;
    }

    /** Access all varying storage. */
    [[nodiscard]]
    std::span<const ml::vec4> varying_span() const
    {
        return varyings;
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
        return varyings.subspan(offset, varying_count);
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
        return varyings.subspan(offset, varying_count);
    }
};

} /* namespace impl */

} /* namespace swr */
