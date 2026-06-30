/**
 * swr - a software rasterizer
 *
 * rasterizer tile cache.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include "block.h"
#include "interpolators.h"

namespace rast
{

#ifndef SWR_TILE_CACHE_PRIMITIVE_CAPACITY
#    define SWR_TILE_CACHE_PRIMITIVE_CAPACITY 1024
#endif

inline constexpr std::size_t max_small_triangle_quad_payloads = 4;
inline constexpr std::size_t max_sparse_triangle_quad_payloads =
  (swr::impl::rasterizer_block_size / 2)
  * (swr::impl::rasterizer_block_size / 2);

/** Covered-quad record for a small or sparse triangle payload. */
struct small_triangle_quad_payload
{
    unsigned int x{0};
    unsigned int y{0};
    std::uint8_t mask{0};
};

/** Inline precomputed quad coverage payload for a small checked triangle. */
struct small_triangle_payload
{
    small_triangle_interpolator attributes;
    std::array<
      small_triangle_quad_payload,
      max_small_triangle_quad_payloads>
      quads{};
    std::uint8_t quad_count{0};
};

/** Builder payload for sparse checked triangles before tile storage compaction. */
struct sparse_triangle_payload
{
    small_triangle_interpolator attributes;
    boost::container::static_vector<
      small_triangle_quad_payload,
      max_sparse_triangle_quad_payloads>
      quads;
};

/** Compacted tile-local sparse payload referencing shared sparse quad storage. */
struct sparse_triangle_tile_payload
{
    small_triangle_interpolator attributes;
    std::size_t quad_offset{0};
    std::uint16_t quad_count{0};
};

/** primitive data associated to a tile. currently only implemented for triangles. */
struct tile_info
{
    /** rasterization modes for this block. */
    enum class rasterization_mode : std::uint8_t
    {
        block = 0,          /** we unconditionally rasterize the whole block. */
        checked = 1,        /** we need to check each pixel if it belongs to the primitive. */
        thin_x_major = 2,   /** a thin primitive traced primarily along the x-axis. */
        thin_y_major = 3,   /** a thin primitive traced primarily along the y-axis. */
        small_checked = 4,  /** a small checked primitive with precomputed covered quad masks. */
        sparse_checked = 5, /** a checked primitive with precomputed sparse covered quad masks. */
    };

    /** render states. points to an entry in the context's draw list. */
    const swr::impl::render_states* states{nullptr};

    union
    {
        /** attribute interpolators for this block. */
        triangle_interpolator* attributes{nullptr};

        /** index into the mode-specific precomputed payload vector. */
        std::size_t precomputed_payload_index;
    };

    /** barycentric coordinates for checked mode; nullptr for full block mode. */
    const geom::barycentric_coordinate_block* checked_lambdas{nullptr};

    /** quad iteration bounds for checked mode. */
    quad_bounds checked_quad_bounds{};

    /** index into tile::shader_instances. */
    std::size_t shader_index{0};

    /** rasterization mode. */
    rasterization_mode mode{rasterization_mode::block};

    /** whether this corresponding triangle is front-facing. */
    bool front_facing{true};

    /** constructors. */
    tile_info() = default;
    tile_info(const tile_info&) = default;
    tile_info(tile_info&&) = default;

    tile_info& operator=(const tile_info&) = default;
    tile_info& operator=(tile_info&&) = default;

    /**
     * initializing constructor.
     */
    tile_info(
      const swr::impl::render_states* in_states,
      std::size_t in_shader_index,
      const geom::barycentric_coordinate_block* in_checked_lambdas,
      quad_bounds in_checked_quad_bounds,
      triangle_interpolator* in_attributes,
      bool in_front_facing,
      rasterization_mode in_mode)
    : states{in_states}
    , attributes{in_attributes}
    , checked_lambdas{in_checked_lambdas}
    , checked_quad_bounds{in_checked_quad_bounds}
    , shader_index{in_shader_index}
    , mode{in_mode}
    , front_facing{in_front_facing}
    {
        assert(mode == rasterization_mode::block
               || mode == rasterization_mode::small_checked
               || mode == rasterization_mode::sparse_checked
               || checked_lambdas);
    }
};

inline bool uses_checked_lambdas(tile_info::rasterization_mode mode)
{
    return mode != tile_info::rasterization_mode::block
           && mode != tile_info::rasterization_mode::small_checked
           && mode != tile_info::rasterization_mode::sparse_checked;
}

inline bool is_thin_rasterization_mode(tile_info::rasterization_mode mode)
{
    return mode == tile_info::rasterization_mode::thin_x_major
           || mode == tile_info::rasterization_mode::thin_y_major;
}

/** a tile waiting to be processed. currently only used for triangles. */
struct tile
{
    struct tile_fragment_shader_instance
    {
        const swr::impl::render_states* states{nullptr};
        swr::impl::shader_storage_buffer shader_storage;
        const swr::program_base* shader{nullptr};

        tile_fragment_shader_instance() = default;
        tile_fragment_shader_instance(
          const tile_fragment_shader_instance&) = delete;
        tile_fragment_shader_instance& operator=(
          const tile_fragment_shader_instance&) = delete;

        tile_fragment_shader_instance(
          tile_fragment_shader_instance&& other) noexcept
        : states{other.states}
        , shader_storage{std::move(other.shader_storage)}
        , shader{other.shader}
        {
            other.shader = nullptr;
            other.states = nullptr;
        }

        tile_fragment_shader_instance& operator=(
          tile_fragment_shader_instance&& other) noexcept
        {
            if(this != &other)
            {
                if(shader)
                {
                    shader->~program_base();
                }
                states = other.states;
                shader_storage = std::move(other.shader_storage);
                shader = other.shader;
                other.shader = nullptr;
                other.states = nullptr;
            }
            return *this;
        }

        explicit tile_fragment_shader_instance(
          const swr::impl::render_states* in_states)
        : states{in_states}
        , shader_storage{
            in_states->shader_info->program_size,
            in_states->shader_info->program_alignment}
        {
            assert(std::has_single_bit(in_states->shader_info->program_alignment));
            assert(
              reinterpret_cast<std::uintptr_t>(shader_storage.data())
                % in_states->shader_info->program_alignment
              == 0);
            shader = in_states->shader_info->shader->create_instance(
              shader_storage.data(),
              swr::program_instance_bindings{
                in_states->uniforms,
                in_states->texture_2d_samplers});
        }

        ~tile_fragment_shader_instance()
        {
            if(shader)
            {
                shader->~program_base();
            }
        }
    };

    /** maximum number of primitives for a tile. */
    constexpr static int max_primitive_count = SWR_TILE_CACHE_PRIMITIVE_CAPACITY;
    static_assert(
      max_primitive_count > 0,
      "SWR_TILE_CACHE_PRIMITIVE_CAPACITY must be > 0");

    /** viewport x coordinate of the upper-left corner. */
    unsigned int x{0};

    /** viewport y coordinate of the upper-left corner. */
    unsigned int y{0};

    /*
     * primitives, precomputed data and shaders associated to this tile.
     */

    boost::container::static_vector<
      tile_info,
      max_primitive_count>
      primitives;
    boost::container::static_vector<
      triangle_interpolator,
      max_primitive_count>
      primitive_attributes;
    boost::container::static_vector<
      geom::barycentric_coordinate_block,
      max_primitive_count>
      primitive_checked_lambdas;
    std::vector<
      small_triangle_payload,
      utils::allocator<small_triangle_payload>>
      primitive_small_payloads;
    std::vector<
      sparse_triangle_tile_payload,
      utils::allocator<sparse_triangle_tile_payload>>
      primitive_sparse_payloads;
    std::vector<
      small_triangle_quad_payload,
      utils::allocator<small_triangle_quad_payload>>
      primitive_sparse_quad_payloads;
    std::vector<
      tile_fragment_shader_instance,
      utils::allocator<tile_fragment_shader_instance>>
      shader_instances;

    const swr::impl::render_states* last_shader_state{nullptr};
    std::size_t last_shader_index{0};

    /** constructors. */
    tile() = default;
    tile(const tile&) = default;
    tile(tile&&) = default;

    tile& operator=(const tile&) = default;
    tile& operator=(tile&&) = default;

    /** initialize tile position. */
    tile(unsigned int in_x, unsigned int in_y)
    : x{in_x}
    , y{in_y}
    {
    }

    std::size_t get_fragment_shader_index(
      const swr::impl::render_states* in_states)
    {
        if(last_shader_state == in_states
           && last_shader_index < shader_instances.size()
           && shader_instances[last_shader_index].states == in_states)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_tile_shader_instance_probe_steps.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            return last_shader_index;
        }

        for(std::size_t i = 0; i < shader_instances.size(); ++i)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_tile_shader_instance_probe_steps.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            if(shader_instances[i].states == in_states)
            {
                last_shader_state = in_states;
                last_shader_index = i;
                return i;
            }
        }

        shader_instances.emplace_back(in_states);
        last_shader_state = in_states;
        last_shader_index = shader_instances.size() - 1;
        return last_shader_index;
    }
};

/** tile cache. */
struct tile_cache
{
    /** tile cache width. */
    int pitch{0};

    /** tiles. */
    std::vector<
      tile,
      utils::allocator<tile>>
      entries;
    std::vector<std::uint32_t> active_tile_indices;

    [[nodiscard]]
    bool try_get_tile(
      unsigned int in_x,
      unsigned int in_y,
      std::size_t& out_tile_index,
      tile*& out_tile)
    {
        out_tile_index = 0;
        out_tile = nullptr;

        if(pitch <= 0
           || entries.empty())
        {
            return false;
        }

        const unsigned int tile_x =
          in_x >> swr::impl::rasterizer_block_shift;
        const unsigned int tile_y =
          in_y >> swr::impl::rasterizer_block_shift;

        const std::size_t tile_index =
          static_cast<std::size_t>(tile_y) * static_cast<std::size_t>(pitch)
          + static_cast<std::size_t>(tile_x);
        if(tile_index >= entries.size())
        {
            return false;
        }

        out_tile_index = tile_index;
        out_tile = &entries[tile_index];
        return true;
    }

    /** constructors. */
    tile_cache() = default;
    tile_cache(const tile_cache&) = delete;
    tile_cache(tile_cache&&) = delete;

    tile_cache& operator=(const tile_cache&) = delete;
    tile_cache& operator=(tile_cache&&) = delete;

    /** reset tile cache. */
    void reset(
      unsigned int in_tiles_x = 0,
      unsigned int in_tiles_y = 0)
    {
        entries.clear();
        active_tile_indices.clear();
        pitch = 0;

        if(in_tiles_x > 0
           && in_tiles_y > 0)
        {
            // allocate tile cache.
            entries.resize(in_tiles_x * in_tiles_y);
            pitch = in_tiles_x;

            // initialize tile positions.
            for(unsigned int y = 0; y < in_tiles_y; ++y)
            {
                for(unsigned int x = 0; x < in_tiles_x; ++x)
                {
                    entries[y * pitch + x] = {
                      x * swr::impl::rasterizer_block_size,
                      y * swr::impl::rasterizer_block_size};
                }
            }
        }
    }

    /** mark each tile in the cache as clear. */
    void clear_tiles()
    {
        for(const auto tile_index: active_tile_indices)
        {
            auto& it = entries[tile_index];
            it.primitives.clear();
            it.primitive_attributes.clear();
            it.primitive_checked_lambdas.clear();
            it.primitive_small_payloads.clear();
            it.primitive_sparse_payloads.clear();
            it.primitive_sparse_quad_payloads.clear();
        }
        active_tile_indices.clear();
    }

    void clear_shader_instances()
    {
        for(auto& it: entries)
        {
            it.shader_instances.clear();
            it.last_shader_state = nullptr;
            it.last_shader_index = 0;
        }
    }

    /** allocate a new tile. returns true if the cache was full or the added triangle filled the cache. */
    bool add_triangle_checked(
      unsigned int in_x,
      unsigned int in_y,
      const swr::impl::render_states* in_states,
      const geom::barycentric_coordinate_block& in_lambdas,
      quad_bounds in_quad_bounds,
      const triangle_interpolator& in_attributes,
      bool in_front_facing,
      tile_info::rasterization_mode in_mode = tile_info::rasterization_mode::checked)
    {
        assert(uses_checked_lambdas(in_mode));

        // find the tile's coordinates.
        std::size_t tile_index = 0;
        tile* tile_ptr = nullptr;
        if(!try_get_tile(in_x, in_y, tile_index, tile_ptr))
        {
            return false;
        }

        auto& tile = *tile_ptr;
        if(tile.primitives.size() == tile.primitives.max_size())
        {
            // the cache was full.
            // FIXME this should not happen
            return true;
        }
        if(tile.primitives.empty())
        {
            active_tile_indices.push_back(tile_index);
        }

        const std::size_t shader_index = tile.get_fragment_shader_index(in_states);
        auto& attributes_ref = tile.primitive_attributes.emplace_back(in_attributes);
        auto& checked_lambdas_ref = tile.primitive_checked_lambdas.emplace_back(in_lambdas);

        // add triangle to the primitives list in-place.
        tile.primitives.emplace_back(
          in_states,
          shader_index,
          &checked_lambdas_ref,
          in_quad_bounds,
          &attributes_ref,
          in_front_facing,
          in_mode);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        constexpr std::uint64_t tile_info_bytes = sizeof(tile_info);
        constexpr std::uint64_t interp_bytes = sizeof(triangle_interpolator);
        constexpr std::uint64_t checked_lambda_bytes = sizeof(geom::barycentric_coordinate_block);
        const std::uint64_t checked_payload_bytes =
          static_cast<std::uint64_t>(tile_info_bytes + interp_bytes + checked_lambda_bytes);
        swr::impl::profile_raster_tile_payload_write_bytes.fetch_add(
          checked_payload_bytes,
          std::memory_order_relaxed);
        swr::impl::profile_raster_tile_payload_checked_write_bytes.fetch_add(
          checked_payload_bytes,
          std::memory_order_relaxed);
        swr::impl::profile_raster_tile_info_write_bytes.fetch_add(tile_info_bytes, std::memory_order_relaxed);
        swr::impl::profile_raster_interp_write_bytes.fetch_add(interp_bytes, std::memory_order_relaxed);
        swr::impl::profile_raster_checked_lambda_write_bytes.fetch_add(checked_lambda_bytes, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        return tile.primitives.size() == tile.primitives.max_size();
    }

    bool add_small_triangle_checked(
      unsigned int in_x,
      unsigned int in_y,
      const swr::impl::render_states* in_states,
      const geom::barycentric_coordinate_block& in_lambdas,
      quad_bounds in_quad_bounds,
      const triangle_interpolator& in_attributes,
      bool in_front_facing,
      bool& out_emitted)
    {
        out_emitted = false;

        // if we cannot store without allocation, add a checked triangle.
        if(!small_triangle_interpolator::can_store_without_allocation(in_attributes))
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_small_quad_fallback_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            out_emitted = true;
            return add_triangle_checked(
              in_x,
              in_y,
              in_states,
              in_lambdas,
              in_quad_bounds,
              in_attributes,
              in_front_facing);
        }

        const unsigned int block_end_x = in_x + swr::impl::rasterizer_block_size;
        const unsigned int block_end_y = in_y + swr::impl::rasterizer_block_size;

        in_quad_bounds.start_x = std::max(in_quad_bounds.start_x, in_x);
        in_quad_bounds.start_y = std::max(in_quad_bounds.start_y, in_y);
        in_quad_bounds.end_x = std::min(in_quad_bounds.end_x, block_end_x);
        in_quad_bounds.end_y = std::min(in_quad_bounds.end_y, block_end_y);

        if(in_quad_bounds.empty())
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_small_quad_empty_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            return false;
        }

        geom::barycentric_coordinate_block lambdas = in_lambdas;
        lambdas.setup(1, 1);
        lambdas.step_y(static_cast<int>(in_quad_bounds.start_y - in_y));
        lambdas.step_x(static_cast<int>(in_quad_bounds.start_x - in_x));

        small_triangle_payload payload{
          .attributes = small_triangle_interpolator{in_attributes}};

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t quad_tests = 0;
        std::uint64_t empty_quads = 0;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        for(unsigned int y = in_quad_bounds.start_y; y < in_quad_bounds.end_y; y += 2)
        {
            geom::barycentric_coordinate_block::fixed_24_8_array_4 row_start[3];
            lambdas.store_position(row_start[0], row_start[1], row_start[2]);

            for(unsigned int x = in_quad_bounds.start_x; x < in_quad_bounds.end_x; x += 2)
            {
                const int mask = geom::reduce_coverage_mask(lambdas.get_coverage_mask());
#ifdef SWR_ENABLE_PIPELINE_PROFILING
                ++quad_tests;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

                if(mask)
                {
                    assert(payload.quad_count < payload.quads.size());
                    payload.quads[payload.quad_count++] = {
                      x,
                      y,
                      static_cast<std::uint8_t>(mask)};
                }
#ifdef SWR_ENABLE_PIPELINE_PROFILING
                else
                {
                    ++empty_quads;
                }
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

                lambdas.step_x(2);
            }

            lambdas.load_position(row_start[0], row_start[1], row_start[2]);
            lambdas.step_y(2);
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        swr::impl::profile_checked_quad_tests.fetch_add(quad_tests, std::memory_order_relaxed);
        swr::impl::profile_checked_empty_quads.fetch_add(empty_quads, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if(payload.quad_count == 0)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            swr::impl::profile_raster_small_quad_empty_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            return false;
        }

        return add_small_triangle_checked_payload(
          in_x,
          in_y,
          in_states,
          in_quad_bounds,
          std::move(payload),
          in_front_facing,
          out_emitted);
    }

    bool add_precomputed_triangle_checked_payload(
      unsigned int in_x,
      unsigned int in_y,
      const swr::impl::render_states* in_states,
      quad_bounds in_quad_bounds,
      small_triangle_payload payload,
      bool in_front_facing,
      bool& out_emitted,
      tile_info::rasterization_mode in_mode)
    {
        assert(in_mode == tile_info::rasterization_mode::small_checked);
        out_emitted = false;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_store = 0;
        utils::clock(stage_store);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if(payload.quad_count == 0)
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            utils::unclock(stage_store);
            swr::impl::profile_raster_small_quad_store_cycles.fetch_add(
              stage_store,
              std::memory_order_relaxed);
            swr::impl::profile_raster_small_quad_empty_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            return false;
        }

        std::size_t tile_index = 0;
        tile* tile_ptr = nullptr;
        if(!try_get_tile(in_x, in_y, tile_index, tile_ptr))
        {
            return false;
        }

        auto& tile = *tile_ptr;
        if(tile.primitives.size() == tile.primitives.max_size())
        {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            utils::unclock(stage_store);
            swr::impl::profile_raster_small_quad_store_cycles.fetch_add(
              stage_store,
              std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            return true;
        }
        if(tile.primitives.empty())
        {
            active_tile_indices.push_back(tile_index);
        }

        const std::size_t shader_index = tile.get_fragment_shader_index(in_states);
        const std::size_t small_payload_index = tile.primitive_small_payloads.size();
        tile.primitive_small_payloads.emplace_back(std::move(payload));

        auto& primitive = tile.primitives.emplace_back();
        primitive.states = in_states;
        primitive.shader_index = shader_index;
        primitive.checked_lambdas = nullptr;
        primitive.checked_quad_bounds = in_quad_bounds;
        primitive.precomputed_payload_index = small_payload_index;
        primitive.front_facing = in_front_facing;
        primitive.mode = in_mode;

        out_emitted = true;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_store);
        swr::impl::profile_raster_small_quad_store_cycles.fetch_add(
          stage_store,
          std::memory_order_relaxed);
        constexpr std::uint64_t tile_info_bytes = sizeof(tile_info);
        constexpr std::uint64_t small_payload_bytes = sizeof(small_triangle_payload);
        const std::uint64_t checked_payload_bytes =
          static_cast<std::uint64_t>(tile_info_bytes + small_payload_bytes);
        if(in_mode == tile_info::rasterization_mode::small_checked)
        {
            swr::impl::profile_raster_small_quad_queued_primitives.fetch_add(1, std::memory_order_relaxed);
        }
        swr::impl::profile_raster_tile_payload_write_bytes.fetch_add(
          checked_payload_bytes,
          std::memory_order_relaxed);
        swr::impl::profile_raster_tile_payload_checked_write_bytes.fetch_add(
          checked_payload_bytes,
          std::memory_order_relaxed);
        swr::impl::profile_raster_tile_info_write_bytes.fetch_add(tile_info_bytes, std::memory_order_relaxed);
        swr::impl::profile_raster_interp_write_bytes.fetch_add(small_payload_bytes, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        return tile.primitives.size() == tile.primitives.max_size();
    }

    bool add_small_triangle_checked_payload(
      unsigned int in_x,
      unsigned int in_y,
      const swr::impl::render_states* in_states,
      quad_bounds in_quad_bounds,
      small_triangle_payload payload,
      bool in_front_facing,
      bool& out_emitted)
    {
        return add_precomputed_triangle_checked_payload(
          in_x,
          in_y,
          in_states,
          in_quad_bounds,
          std::move(payload),
          in_front_facing,
          out_emitted,
          tile_info::rasterization_mode::small_checked);
    }

    bool add_sparse_triangle_checked_payload(
      unsigned int in_x,
      unsigned int in_y,
      const swr::impl::render_states* in_states,
      quad_bounds in_quad_bounds,
      const sparse_triangle_payload& payload,
      bool in_front_facing,
      bool& out_emitted)
    {
        out_emitted = false;

        if(payload.quads.empty())
        {
            return false;
        }

        std::size_t tile_index = 0;
        tile* tile_ptr = nullptr;
        if(!try_get_tile(in_x, in_y, tile_index, tile_ptr))
        {
            return false;
        }

        auto& tile = *tile_ptr;
        if(tile.primitives.size() == tile.primitives.max_size())
        {
            return true;
        }
        if(tile.primitives.empty())
        {
            active_tile_indices.push_back(tile_index);
        }

        static_assert(max_sparse_triangle_quad_payloads <= 0xffff);

        const std::size_t shader_index = tile.get_fragment_shader_index(in_states);
        const std::size_t sparse_payload_index = tile.primitive_sparse_payloads.size();
        const std::size_t quad_offset = tile.primitive_sparse_quad_payloads.size();

        tile.primitive_sparse_quad_payloads.insert(
          tile.primitive_sparse_quad_payloads.end(),
          payload.quads.begin(),
          payload.quads.end());
        tile.primitive_sparse_payloads.emplace_back(
          sparse_triangle_tile_payload{
            payload.attributes,
            quad_offset,
            static_cast<std::uint16_t>(payload.quads.size())});

        auto& primitive = tile.primitives.emplace_back();
        primitive.states = in_states;
        primitive.shader_index = shader_index;
        primitive.checked_lambdas = nullptr;
        primitive.checked_quad_bounds = in_quad_bounds;
        primitive.precomputed_payload_index = sparse_payload_index;
        primitive.front_facing = in_front_facing;
        primitive.mode = tile_info::rasterization_mode::sparse_checked;

        out_emitted = true;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        constexpr std::uint64_t tile_info_bytes = sizeof(tile_info);
        constexpr std::uint64_t sparse_payload_bytes = sizeof(sparse_triangle_tile_payload);
        const std::uint64_t quad_payload_bytes =
          static_cast<std::uint64_t>(payload.quads.size() * sizeof(small_triangle_quad_payload));
        const std::uint64_t checked_payload_bytes =
          static_cast<std::uint64_t>(tile_info_bytes + sparse_payload_bytes + quad_payload_bytes);
        swr::impl::profile_raster_tile_payload_write_bytes.fetch_add(
          checked_payload_bytes,
          std::memory_order_relaxed);
        swr::impl::profile_raster_tile_payload_checked_write_bytes.fetch_add(
          checked_payload_bytes,
          std::memory_order_relaxed);
        swr::impl::profile_raster_tile_info_write_bytes.fetch_add(tile_info_bytes, std::memory_order_relaxed);
        swr::impl::profile_raster_interp_write_bytes.fetch_add(
          sparse_payload_bytes + quad_payload_bytes,
          std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        return tile.primitives.size() == tile.primitives.max_size();
    }

    /** allocate a new tile. returns true if the cache was full or the added triangle filled the cache. */
    bool add_triangle(
      unsigned int in_x,
      unsigned int in_y,
      const swr::impl::render_states* in_states,
      const geom::barycentric_coordinate_block& in_lambdas,
      const triangle_interpolator& in_attributes,
      bool in_front_facing,
      tile_info::rasterization_mode in_mode)
    {
        if(uses_checked_lambdas(in_mode))
        {
            return add_triangle_checked(
              in_x,
              in_y,
              in_states,
              in_lambdas,
              full_block_quad_bounds(in_x, in_y),
              in_attributes,
              in_front_facing,
              in_mode);
        }

        assert(in_mode == tile_info::rasterization_mode::block);

        // find the tile's coordinates.
        std::size_t tile_index = 0;
        tile* tile_ptr = nullptr;
        if(!try_get_tile(in_x, in_y, tile_index, tile_ptr))
        {
            return false;
        }

        auto& tile = *tile_ptr;
        if(tile.primitives.size() == tile.primitives.max_size())
        {
            // the cache was full.
            // FIXME this should not happen
            return true;
        }
        if(tile.primitives.empty())
        {
            active_tile_indices.push_back(tile_index);
        }

        const std::size_t shader_index = tile.get_fragment_shader_index(in_states);
        auto& attributes_ref = tile.primitive_attributes.emplace_back(in_attributes);

        // add triangle to the primitives list in-place.
        tile.primitives.emplace_back(
          in_states,
          shader_index,
          nullptr,
          full_block_quad_bounds(in_x, in_y),
          &attributes_ref,
          in_front_facing,
          in_mode);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        constexpr std::uint64_t tile_info_bytes = sizeof(tile_info);
        constexpr std::uint64_t interp_bytes = sizeof(triangle_interpolator);
        const std::uint64_t block_payload_bytes =
          static_cast<std::uint64_t>(tile_info_bytes + interp_bytes);
        swr::impl::profile_raster_tile_payload_write_bytes.fetch_add(
          block_payload_bytes,
          std::memory_order_relaxed);
        swr::impl::profile_raster_tile_payload_block_write_bytes.fetch_add(
          block_payload_bytes,
          std::memory_order_relaxed);
        swr::impl::profile_raster_tile_info_write_bytes.fetch_add(tile_info_bytes, std::memory_order_relaxed);
        swr::impl::profile_raster_interp_write_bytes.fetch_add(interp_bytes, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        return tile.primitives.size() == tile.primitives.max_size();
    }
};

} /* namespace rast */
