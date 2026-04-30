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
    enum class rasterization_mode
    {
        block = 0,  /** we unconditionally rasterize the whole block. */
        checked = 1 /** we need to check each pixel if it belongs to the primitive. */
    };

    /** render states. points to an entry in the context's draw list. */
    const swr::impl::render_states* states{nullptr};

    /** shader. */
    const swr::program_base* shader;

    /** barycentric coordinates and steps for checked rasterization mode. */
    geom::barycentric_coordinate_block checked_lambdas{};

    /** whether this corresponding triangle is front-facing. */
    bool front_facing{true};

    /** attribute interpolators for this block. */
    triangle_interpolator attributes;

    /** rasterization mode. */
    rasterization_mode mode{rasterization_mode::block};

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
      const swr::program_base* in_shader,
      const geom::barycentric_coordinate_block& in_lambdas,
      const triangle_interpolator& in_attributes,
      bool in_front_facing,
      rasterization_mode in_mode)
    : states{in_states}
    , shader{in_shader}
    , front_facing{in_front_facing}
    , attributes{in_attributes}
    , mode{in_mode}
    {
        if(mode == rasterization_mode::checked)
        {
            checked_lambdas = in_lambdas;
        }
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
    std::vector<tile_fragment_shader_instance, utils::allocator<tile_fragment_shader_instance>> shader_instances;

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

    const swr::program_base* get_fragment_shader(const swr::impl::render_states* in_states)
    {
        for(auto& it: shader_instances)
        {
            if(it.states == in_states)
            {
                return it.shader;
            }
        }

        shader_instances.emplace_back(in_states);
        return shader_instances.back().shader;
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
        }
    }

    void clear_shader_instances()
    {
        for(auto& it: entries)
        {
            it.shader_instances.clear();
        }
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

        const swr::program_base* shader = tile.get_fragment_shader(in_states);

        // add triangle to the primitives list in-place.
        auto& triangle_ref = tile.primitives.emplace_back(
          in_states,
          shader,
          in_lambdas,
          in_attributes,
          in_front_facing,
          in_mode);

        // set up triangle attributes.
        triangle_ref.attributes.setup_block_processing();

        return tile.primitives.size() == tile.primitives.max_size();
    }
};

} /* namespace rast */
