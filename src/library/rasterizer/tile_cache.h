/**
 * swr - a software rasterizer
 *
 * rasterizer tile cache.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace rast
{

#ifndef SWR_TILE_CACHE_PRIMITIVE_CAPACITY
#    define SWR_TILE_CACHE_PRIMITIVE_CAPACITY 1024
#endif

/** Bounds for 2x2 quad iteration within one rasterizer block. */
struct quad_bounds
{
    unsigned int start_x{0};
    unsigned int start_y{0};
    unsigned int end_x{0};
    unsigned int end_y{0};

    [[nodiscard]]
    bool empty() const
    {
        return start_x >= end_x
               || start_y >= end_y;
    }
};

inline quad_bounds full_block_quad_bounds(
  unsigned int block_x,
  unsigned int block_y)
{
    return {
      block_x,
      block_y,
      block_x + swr::impl::rasterizer_block_size,
      block_y + swr::impl::rasterizer_block_size};
}

/** primitive data associated to a tile. currently only implemented for triangles. */
class tile_info
{
public:
    /** rasterization modes for this block. */
    enum class rasterization_mode : unsigned char
    {
        block = 0,  /** we unconditionally rasterize the whole block. */
        checked = 1 /** we need to check each pixel if it belongs to the primitive. */
    };

    /** render states. points to an entry in the context's draw list. */
    const swr::impl::render_states* states{nullptr};

    /** attribute interpolators for this block. */
    triangle_interpolator* attributes{nullptr};

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
        assert(mode == rasterization_mode::block || checked_lambdas);
    }
};

/** a tile waiting to be processed. currently only used for triangles. */
struct tile
{
    struct tile_fragment_shader_instance
    {
        const swr::impl::render_states* states{nullptr};
        swr::impl::shader_storage_buffer shader_storage;
        const swr::program_base* shader{nullptr};

        tile_fragment_shader_instance() = default;
        tile_fragment_shader_instance(const tile_fragment_shader_instance&) = delete;
        tile_fragment_shader_instance& operator=(const tile_fragment_shader_instance&) = delete;

        tile_fragment_shader_instance(tile_fragment_shader_instance&& other) noexcept
        : states{other.states}
        , shader_storage{std::move(other.shader_storage)}
        , shader{other.shader}
        {
            other.shader = nullptr;
            other.states = nullptr;
        }

        tile_fragment_shader_instance& operator=(tile_fragment_shader_instance&& other) noexcept
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

        explicit tile_fragment_shader_instance(const swr::impl::render_states* in_states)
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
    static_assert(max_primitive_count > 0, "SWR_TILE_CACHE_PRIMITIVE_CAPACITY must be > 0");

    /** viewport x coordinate of the upper-left corner. */
    unsigned int x{0};

    /** viewport y coordinate of the upper-left corner. */
    unsigned int y{0};

    /** primitives associated to this tile. */
    boost::container::static_vector<tile_info, max_primitive_count> primitives;
    boost::container::static_vector<triangle_interpolator, max_primitive_count> primitive_attributes;
    boost::container::static_vector<geom::barycentric_coordinate_block, max_primitive_count> primitive_checked_lambdas;
    std::vector<tile_fragment_shader_instance, utils::allocator<tile_fragment_shader_instance>> shader_instances;
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

    std::size_t get_fragment_shader_index(const swr::impl::render_states* in_states)
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
    std::vector<tile, utils::allocator<tile>> entries;
    std::vector<std::uint32_t> active_tile_indices;

    /** constructors. */
    tile_cache() = default;
    tile_cache(const tile_cache&) = delete;
    tile_cache(tile_cache&&) = delete;

    tile_cache& operator=(const tile_cache&) = delete;
    tile_cache& operator=(tile_cache&&) = delete;

    /** reset tile cache. */
    void reset(unsigned int in_tiles_x = 0, unsigned int in_tiles_y = 0)
    {
        entries.clear();
        active_tile_indices.clear();
        pitch = 0;

        if(in_tiles_x > 0 && in_tiles_y > 0)
        {
            // allocate tile cache.
            entries.resize(in_tiles_x * in_tiles_y);
            pitch = in_tiles_x;

            // initialize tile positions.
            for(unsigned int y = 0; y < in_tiles_y; ++y)
            {
                for(unsigned int x = 0; x < in_tiles_x; ++x)
                {
                    entries[y * pitch + x] = tile(x * swr::impl::rasterizer_block_size, y * swr::impl::rasterizer_block_size);
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
      bool in_front_facing)
    {
        // find the tile's coordinates.
        unsigned int tile_index = (in_y >> swr::impl::rasterizer_block_shift) * pitch + (in_x >> swr::impl::rasterizer_block_shift);
        assert(tile_index < entries.size());

        auto& tile = entries[tile_index];
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
          tile_info::rasterization_mode::checked);

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
        if(in_mode == tile_info::rasterization_mode::checked)
        {
            return add_triangle_checked(
              in_x,
              in_y,
              in_states,
              in_lambdas,
              full_block_quad_bounds(in_x, in_y),
              in_attributes,
              in_front_facing);
        }

        // find the tile's coordinates.
        unsigned int tile_index = (in_y >> swr::impl::rasterizer_block_shift) * pitch + (in_x >> swr::impl::rasterizer_block_shift);
        assert(tile_index < entries.size());

        auto& tile = entries[tile_index];
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
