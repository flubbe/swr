/**
 * swr - a software rasterizer
 *
 * rasterizer tile cache.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace rast
{

/** primitive data associated to a tile. currently only implemented for triangles. */
class tile_info
{
    /** shader storage */
    std::vector<std::byte> shader_storage;

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

    /** barycentric coordinates and steps for this block. */
    geom::barycentric_coordinate_block lambdas;

    /** whether this corresponding triangle is front-facing. */
    bool front_facing{true};

    /** attribute interpolators for this block. */
    triangle_interpolator attributes;

    /** rasterization mode. */
    rasterization_mode mode{rasterization_mode::block};

    /** constructors. */
    tile_info() = default;
    tile_info(tile_info&&) = default;

    tile_info(const tile_info& other)
    : shader_storage{other.shader->size()}
    , states{other.states}
    , shader{other.shader->create_fragment_shader_instance(shader_storage.data(), other.states->uniforms, other.states->texture_2d_samplers)}
    , lambdas{other.lambdas}
    , front_facing{other.front_facing}
    , attributes{other.attributes}
    , mode{other.mode}
    {
    }

    ~tile_info()
    {
        shader->~program_base();
    }

    tile_info& operator=(const tile_info&) = default;
    tile_info& operator=(tile_info&&) = default;

    /**
     * initializing constructor.
     *
     * NOTE This instantiates the shader.
     */
    tile_info(
      const swr::impl::render_states* in_states,
      const geom::barycentric_coordinate_block& in_lambdas,
      const triangle_interpolator& in_attributes,
      bool in_front_facing,
      rasterization_mode in_mode)
    : shader_storage{in_states->shader_info->shader->size()}
    , states{in_states}
    , shader{in_states->shader_info->shader->create_fragment_shader_instance(shader_storage.data(), in_states->uniforms, in_states->texture_2d_samplers)}
    , lambdas{in_lambdas}
    , front_facing{in_front_facing}
    , attributes{in_attributes}
    , mode{in_mode}
    {
    }
};

/** a tile waiting to be processed. currently only used for triangles. */
struct tile
{
    /** maximum number of primitives for a tile. */
    constexpr static int max_primitive_count = 32;

    /** viewport x coordinate of the upper-left corner. */
    unsigned int x{0};

    /** viewport y coordinate of the upper-left corner. */
    unsigned int y{0};

    /** primitives associated to this tile. */
    boost::container::static_vector<tile_info, max_primitive_count> primitives;

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
};

/** tile cache. */
struct tile_cache
{
    /** tile cache width. */
    int pitch{0};

    /** tiles. */
    std::vector<tile> entries;

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

    /** allocate a new tile. returns true if the cache was full or the added triangle filled the cache. */
    bool add_triangle(unsigned int in_x, unsigned int in_y, const tile_info& info)
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

        // add triangle to the primitives list.
        // NOTE no std::move here, since the copy creates the shader instance
        auto& triangle_ref = tile.primitives.emplace_back(info);

        // set up triangle attributes.
        triangle_ref.attributes.setup_block_processing();

        return tile.primitives.size() == tile.primitives.max_size();
    }
};

} /* namespace rast */