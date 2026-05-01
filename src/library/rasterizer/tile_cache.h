/**
 * swr - a software rasterizer
 *
 * rasterizer tile cache.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace rast
{

#ifndef SWR_TILE_CACHE_PRIMITIVE_CAPACITY
#    define SWR_TILE_CACHE_PRIMITIVE_CAPACITY 1024
#endif

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

    /** index into tile::shader_instances. */
    std::size_t shader_index{0};

    /** rasterization mode. */
    rasterization_mode mode{rasterization_mode::block};

    /** whether this corresponding triangle is front-facing. */
    bool front_facing{true};

    /** constructors. */
    tile_info() = default;
    tile_info(tile_info&&) = default;

    tile_info(const tile_info&) = default;
    tile_info& operator=(const tile_info&) = default;
    tile_info& operator=(tile_info&&) = default;

    /**
     * initializing constructor.
     *
     * NOTE This instantiates the shader.
     */
    tile_info(
      const swr::impl::render_states* in_states,
      std::size_t in_shader_index,
      const geom::barycentric_coordinate_block* in_checked_lambdas,
      triangle_interpolator* in_attributes,
      bool in_front_facing,
      rasterization_mode in_mode)
    : states{in_states}
    , attributes{in_attributes}
    , checked_lambdas{in_checked_lambdas}
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
        std::vector<std::byte> shader_storage;
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
        , shader_storage{in_states->shader_info->shader->size()}
        , shader{in_states->shader_info->shader->create_fragment_shader_instance(
            shader_storage.data(),
            in_states->uniforms,
            in_states->texture_2d_samplers)}
        {
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
#ifdef DO_BENCHMARKING
            swr::impl::profile_tile_shader_instance_probe_steps.fetch_add(1, std::memory_order_relaxed);
#endif
            return last_shader_index;
        }

        for(std::size_t i = 0; i < shader_instances.size(); ++i)
        {
#ifdef DO_BENCHMARKING
            swr::impl::profile_tile_shader_instance_probe_steps.fetch_add(1, std::memory_order_relaxed);
#endif
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
        for(auto& it: entries)
        {
            it.primitives.clear();
            it.primitive_attributes.clear();
            it.primitive_checked_lambdas.clear();
        }
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

        const std::size_t shader_index = tile.get_fragment_shader_index(in_states);
        auto& attributes_ref = tile.primitive_attributes.emplace_back(in_attributes);
        auto& checked_lambdas_ref = tile.primitive_checked_lambdas.emplace_back(in_lambdas);

        // add triangle to the primitives list in-place.
        auto& triangle_ref = tile.primitives.emplace_back(
          in_states,
          shader_index,
          &checked_lambdas_ref,
          &attributes_ref,
          in_front_facing,
          tile_info::rasterization_mode::checked);

        // set up triangle attributes.
        triangle_ref.attributes->setup_block_processing();

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

        const std::size_t shader_index = tile.get_fragment_shader_index(in_states);
        auto& attributes_ref = tile.primitive_attributes.emplace_back(in_attributes);

        // add triangle to the primitives list in-place.
        auto& triangle_ref = tile.primitives.emplace_back(
          in_states,
          shader_index,
          nullptr,
          &attributes_ref,
          in_front_facing,
          in_mode);

        // set up triangle attributes.
        triangle_ref.attributes->setup_block_processing();

        return tile.primitives.size() == tile.primitives.max_size();
    }
};

} /* namespace rast */
