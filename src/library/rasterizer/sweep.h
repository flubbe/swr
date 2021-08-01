/**
 * swr - a software rasterizer
 * 
 * software rasterizer interface.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "geometry/barycentric_coords.h"

namespace rast
{

#define SWR_ENABLE_MULTI_THREADING

/**
 * Bias for application to fill rules. This is edge to the line equations if the corresponding
 * edge is a left or top one. Since this is done before any normalization took place, the fill
 * rule bias is given in 1x1-subpixel-units, where the subpixel count is given by the precision
 * of the fixed-point type used. For example, if we use ml::fixed_28_4_t, we have 4 bits of subpixel
 * precision and the fill rule bias is given in 2^(-4)-pixel-units.
 *
 * This bias is used by triangle- and point rasterization code.
 */
#define FILL_RULE_EDGE_BIAS 1

/** Sweep rasterizer. */
class sweep_rasterizer : public rasterizer
{
    /** a geometric primitive understood by sweep_rasterizer */
    struct primitive
    {
        enum primitive_type
        {
            point,   /** point primitive, consisting of one vertex */
            line,    /** line primitive, consisting of two vertices */
            triangle /** triangle primitive, consisting of three vertices */
        };

        /** the type of primitive to be rasterized */
        primitive_type type;

        /** whether the primitive is front-facing. only relevant for triangles (otherwise always true). */
        bool is_front_facing;

        /** the primitive's vertices. points use v[0], lines use v[0] and v[1], and triangles use v[0], v[1] and v[2]. */
        const geom::vertex* v[3];

        /** Points to the active render states (which are stored in the context's draw lists). */
        const swr::impl::render_states* states{nullptr};

        /** 
         * default constructor. only for compatibility with std containers. 
         * 
         * NOTE: this does not make sense to use on its own and probably leaves the object in an undefined and unusable state. 
         */
        primitive() = default;

        /** point constructor. */
        primitive(const swr::impl::render_states* in_states, const geom::vertex* vertex)
        : type(point)
        , is_front_facing(true)
        , v{vertex, nullptr, nullptr}
        , states(in_states)
        {
        }

        /** line constructor. */
        primitive(const swr::impl::render_states* in_states, const geom::vertex* v1, const geom::vertex* v2)
        : type(line)
        , is_front_facing(true)
        , v{v1, v2, nullptr}
        , states(in_states)
        {
        }

        /** triangle constructor. */
        primitive(const swr::impl::render_states* in_states, bool in_is_front_facing, const geom::vertex* v1, const geom::vertex* v2, const geom::vertex* v3)
        : type(triangle)
        , is_front_facing(in_is_front_facing)
        , v{v1, v2, v3}
        , states(in_states)
        {
        }
    };

    /** list containing all primitives which are to be rasterized. */
    std::vector<primitive> draw_list;

    /** primitive data associated to a tile. currently only implemented for triangles. */
    struct primitive_data
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
        primitive_data() = default;

        /** initializing constructor. */
        primitive_data(const geom::barycentric_coordinate_block& in_lambdas, const swr::impl::render_states* in_states, const triangle_interpolator& in_attributes, bool in_front_facing, rasterization_mode in_mode)
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
        boost::container::static_vector<primitive_data, max_primitive_count> primitives;
    };

    /** tile cache width. */
    int tiles_x{0};

    /** tile cache height. */
    int tiles_y{0};

    /** tile cache. */
    boost::container::vector<tile> tile_cache;

    /** reset tile cache. */
    void reset_tile_cache(int in_tiles_x, int in_tiles_y)
    {
        tile_cache.clear();
        tiles_x = 0;
        tiles_y = 0;

        if(in_tiles_x > 0 && in_tiles_y > 0)
        {
            tile_cache.resize(in_tiles_x * in_tiles_y);
            tiles_x = in_tiles_x;
            tiles_y = in_tiles_y;

            for(int y = 0; y < in_tiles_y; ++y)
            {
                for(int x = 0; x < in_tiles_x; ++x)
                {
                    tile_cache[y * in_tiles_x + x].x = x * swr::impl::rasterizer_block_size;
                    tile_cache[y * in_tiles_x + x].y = y * swr::impl::rasterizer_block_size;
                }
            }
        }
    }

    /** mark each tile in the cache as clear. */
    void clear_tile_cache()
    {
        for(auto& it: tile_cache)
        {
            it.primitives.clear();
        }
    }

#ifdef SWR_ENABLE_MULTI_THREADING
    /** process all tiles stored in the tile cache. */
    void process_tile_cache()
    {
        // for each non-empty tile, add a job to the thread pool.
        for(std::size_t i = 0; i < tile_cache.size(); ++i)
        {
            if(tile_cache[i].primitives.size())
            {
                rasterizer_threads.push_task(process_tile_static, this, &tile_cache[i]);
            }
        }

        rasterizer_threads.run_tasks_and_wait();
        clear_tile_cache();
    }
#else /* SWR_ENABLE_MULTI_THREADING */
    /** process all tiles stored in the tile cache. */
    void process_tile_cache()
    {
        // for each non-empty tile, add a job to the thread pool.
        for(std::size_t i = 0; i < tile_cache.size(); ++i)
        {
            process_tile(i);
        }

        clear_tile_cache();
    }
#endif

    /** allocate a new tile. if the cache is full, it is rasterized and then emptied. */
    void cache_triangle_data(const swr::impl::render_states* in_states, const triangle_interpolator& in_attributes, const geom::barycentric_coordinate_block& in_lambdas, unsigned int in_x, unsigned int in_y, bool in_front_facing, primitive_data::rasterization_mode in_mode)
    {
        // find the tile's coordinates.
        unsigned int tile_index = (in_y >> swr::impl::rasterizer_block_shift) * tiles_x + (in_x >> swr::impl::rasterizer_block_shift);
        assert(tile_index < tile_cache.size());

        auto& tile = tile_cache[tile_index];
        if(tile.primitives.size() == tile.primitives.max_size())
        {
            process_tile_cache();
        }

        tile.primitives.emplace_back(in_lambdas, in_states, in_attributes, in_front_facing, in_mode);
    }

#ifdef SWR_ENABLE_MULTI_THREADING
    /** worker threads. */
    concurrency_utils::deferred_thread_pool<concurrency_utils::spmc_blocking_queue<std::function<void()>>> rasterizer_threads;
#endif /* SWR_ENABLE_MULTI_THREADING */

    /*
     * fragment processing.
     */

    /** generate a color value along with depth- and stencil flags for a single fragment. writes to the depth buffer. */
    void process_fragment(int x, int y, const swr::impl::render_states& states, float one_over_viewport_z, fragment_info& info, swr::impl::fragment_output& out);

    /** generate color values along with depth- and stencil masks for a 2x2 block of fragments. writes to the depth buffer. */
    void process_fragment_block(int x, int y, const swr::impl::render_states& states, float one_over_viewport_z[4], fragment_info info[4], swr::impl::fragment_output_block& out);

    /*
     * fragment block processing.
     */

    /**
     * Rasterize a complete block of dimension (rasterizer_block_size, rasterizer_block_size), i.e. do not perform additional edge checks.
     */
    void process_block(unsigned int in_x, unsigned int in_y, primitive_data& in_data);

    /**
     * Rasterize block of dimension (rasterizer_block_size, rasterizer_block_size) and check for each fragment, if it is inside the triangle
     * described by the vertex attributes.
     */
    void process_block_checked(unsigned int in_x, unsigned int in_y, primitive_data& in_data);

    /** process a tile. */
    void process_tile(tile& in_tile);

#ifdef SWR_ENABLE_MULTI_THREADING
    /** calls rasterizer->process_tile. */
    static void process_tile_static(sweep_rasterizer* rasterizer, tile* in_tile);
#endif

    /*
     * drawing functions.
     */

    /**
     * Draw the triangle (v1,v2,v3) using a sweep algorithm with blocks of size rasterizer_block_size.
     * The triangle is rasterized regardless of its orientation.
     *
     * \param states Active render states for this triangle.
     * \param is_front_facing Whether this triangle is front facing. Passed to the fragment shader.
     * \param v1 First triangle vertex.
     * \param v2 Second triangle vertex.
     * \param v3 Third triangle vertex.
     */
    void draw_filled_triangle(const swr::impl::render_states& states, bool is_front_facing, const geom::vertex& v1, const geom::vertex& v2, const geom::vertex& v3);

    /** draw a line. For line strips, the interior end points should be omitted by setting draw_end_point to false. */
    void draw_line(const swr::impl::render_states& states, bool draw_end_point, const geom::vertex& v1, const geom::vertex& v2);

    /** draw a point. */
    void draw_point(const swr::impl::render_states& states, const geom::vertex& v);

    /** draw the primitives in the list sequentially. */
    void draw_primitives_sequentially();

#ifdef SWR_ENABLE_MULTI_THREADING
    /** draw the primitives in the list in parallel. */
    void draw_primitives_parallel();
#endif

public:
    /** Constructor. */
    sweep_rasterizer([[maybe_unused]] std::size_t in_thread_count, swr::impl::default_framebuffer* in_framebuffer)
    : rasterizer(in_framebuffer)
#ifdef SWR_ENABLE_MULTI_THREADING
    , rasterizer_threads(in_thread_count)
#endif
    {
        assert(framebuffer);

        /*
         * set up tile cache.
         */

        unsigned int tiles_x = (framebuffer->properties.width >> swr::impl::rasterizer_block_shift) + 1;
        unsigned int tiles_y = (framebuffer->properties.height >> swr::impl::rasterizer_block_shift) + 1;

        reset_tile_cache(tiles_x, tiles_y);
    }

    /*
     * rasterizer interface.
     */

    const std::string describe() const override
    {
        return std::string("Sweep Rasterizer");
    }
    void add_point(const swr::impl::render_states* states, const geom::vertex* v) override;
    void add_line(const swr::impl::render_states* states, const geom::vertex* v1, const geom::vertex* v2) override;
    void add_triangle(const swr::impl::render_states* states, bool is_front_facing, const geom::vertex* v1, const geom::vertex* v2, const geom::vertex* v3) override;
    void draw_primitives() override;
};

} /* namespace rast */
