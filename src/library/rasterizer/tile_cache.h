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
struct tile_info
{
    /** rasterization modes for this block. */
    enum class rasterization_mode
    {
        block = 0,  /** we unconditionally rasterize the whole block. */
        checked = 1 /** we need to check each pixel if it belongs to the primitive. */
    };

    /** barycentric coordinates and steps for this block. */
    geom::barycentric_coordinate_block lambdas;

    /** render states. points to an entry in the context's draw list. */
    const swr::impl::render_states* states{nullptr};

    /** whether this corresponding triangle is front-facing. */
    bool front_facing{true};

    /** attribute interpolators for this block. */
    triangle_interpolator attributes;

    /** rasterization mode. */
    rasterization_mode mode{rasterization_mode::block};

    /** default constructor. */
    tile_info() = default;

    /** initializing constructor. */
    tile_info(const geom::barycentric_coordinate_block& in_lambdas, const swr::impl::render_states* in_states, const triangle_interpolator& in_attributes, bool in_front_facing, rasterization_mode in_mode)
    : lambdas{in_lambdas}
    , states{in_states}
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

    /** default constructor. */
    tile() = default;

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
    boost::container::vector<tile> entries;

    /** default constructor. */
    tile_cache() = default;

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

    /** allocate a new tile. if the cache is full, return false. */
    bool add_triangle(const swr::impl::render_states* in_states, const triangle_interpolator& in_attributes, const geom::barycentric_coordinate_block& in_lambdas, unsigned int in_x, unsigned int in_y, bool in_front_facing, tile_info::rasterization_mode in_mode)
    {
        // find the tile's coordinates.
        unsigned int tile_index = (in_y >> swr::impl::rasterizer_block_shift) * pitch + (in_x >> swr::impl::rasterizer_block_shift);
        assert(tile_index < tile_cache.size());

        auto& tile = entries[tile_index];
        if(tile.primitives.size() == tile.primitives.max_size())
        {
            // the cache is full.
            return false;
        }

        // add triangle to the primitives list.
        tile.primitives.emplace_back(in_lambdas, in_states, in_attributes, in_front_facing, in_mode);

        // set up triangle attributes.
        tile.primitives.back().attributes.setup_block_processing();

        return true;
    }
};

} /* namespace rast */