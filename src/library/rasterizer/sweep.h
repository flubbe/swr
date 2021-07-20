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

    /** a tile waiting to be processed. currently only used for triangles. */
    struct tile
    {
        /** render states. points to an entry in the context's draw list. */
        const swr::impl::render_states* states{nullptr};

        /** attribute interpolators for this block. */
        triangle_interpolator attributes;

        /** barycentric coordinates and steps for this block. */
        geom::barycentric_coordinate_block lambdas;

        /** viewport x coordinate of the upper-left corner. */
        unsigned int x{0};

        /** viewport y coordinate of the upper-left corner. */
        unsigned int y{0};

        /** whether this corresponding triangle is front-facing. */
        bool front_facing{true};

        /** delete constructor. note that triangle_interpolator cannot have a default constructor. */
        tile() = delete;

        /** initializing constructor. */
        tile(const swr::impl::render_states* in_states, const triangle_interpolator& in_attributes, const geom::barycentric_coordinate_block& in_lambdas, unsigned int in_x, unsigned int in_y, bool in_front_facing)
        : states{in_states}
        , attributes{in_attributes}
        , lambdas{in_lambdas}
        , x{in_x}
        , y{in_y}
        , front_facing{in_front_facing}
        {
        }
    };

#ifdef SWR_ENABLE_MULTI_THREADING
    /** maximum number of cached tiles. */
    constexpr static int max_cached_tiles = 1024;

    /** list containing all tiles. */
    boost::container::static_vector<tile, max_cached_tiles> tile_cache;

    /** allocate a new tile. if the cache is full, it is rasterized and then emptied. */
    size_t allocate_tile(const swr::impl::render_states* in_states, const triangle_interpolator& in_attributes, const geom::barycentric_coordinate_block& in_lambdas, unsigned int in_x, unsigned int in_y, bool in_front_facing)
    {
        if(tile_cache.size() == tile_cache.max_size())
        {
            rasterizer_threads.run_tasks_and_wait();

            draw_list.clear();
            tile_cache.clear();
        }

        auto index = tile_cache.size();
        tile_cache.emplace_back(in_states, in_attributes, in_lambdas, in_x, in_y, in_front_facing);

        return index;
    }
#else
    /** for the single-threaded rasterizer, there only ever is a single tile active. */
    constexpr static int max_cached_tiles = 1;

    /** single tile cache. */
    tile tile_cache[max_cached_tiles];

    /** the allocation function only 0 and sets the parameters for the only tile in the tile cache. */
    size_t allocate_tile(const swr::impl::render_states* in_states, const triangle_interpolator& in_attributes, unsigned int in_x, unsigned int in_y, bool in_front_facing)
    {
        new(tile_cache) tile(in_states, in_attributes, in_x, in_y, in_front_facing);
        return 0;
    }
#endif

#ifdef SWR_ENABLE_MULTI_THREADING
    /** worker threads. */
    concurrency_utils::deferred_thread_pool<concurrency_utils::spmc_queue<std::function<void()>>> rasterizer_threads;
#endif

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
     * 
     * \param tile_index index of the block/tile in the tile cache
     */
    void process_block(unsigned int tile_index);

    /**
     * Rasterize block of dimension (rasterizer_block_size, rasterizer_block_size) and check for each fragment, if it is inside the triangle
     * described by the vertex attributes.
     * 
     * \param tile_index index of the block/tile in the tile cache
     * \param lambda0 fixed-point lambdas for the top-left corner of this block
     * \param lambda1 fixed-point lambdas for the top-left corner of this block
     * \param lambda2 fixed-point lambdas for the top-left corner of this block
     */
    void process_block_checked(unsigned int tile_index);

#ifdef SWR_ENABLE_MULTI_THREADING
    /** static block drawing functions. callable by threads. */
    static void thread_process_block(sweep_rasterizer* rasterizer, unsigned int tile_index);

    /** static block drawing functions. callable by threads. */
    static void thread_process_block_checked(sweep_rasterizer* rasterizer, unsigned int tile_index);
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
    sweep_rasterizer(std::size_t in_thread_count, swr::impl::default_framebuffer* in_framebuffer)
    : rasterizer(in_framebuffer)
#ifdef SWR_ENABLE_MULTI_THREADING
    , rasterizer_threads(in_thread_count)
#endif
    {
#ifdef SWR_ENABLE_STATS
#    ifdef SWR_ENABLE_MULTI_THREADING
        stats_rast.available_threads = rasterizer_threads.get_thread_count();
#    else
        stats_rast.available_threads = 1;
#    endif /* SWR_ENABLE_MULTI_THREADING */
#endif     /* SWR_ENABLE_STATS */
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
