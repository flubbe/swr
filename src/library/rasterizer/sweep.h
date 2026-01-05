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
#include "tile_cache.h"

namespace rast
{

/**
 * Bias for application to fill rules. This is edge to the line equations if the corresponding
 * edge is a left or top one. Since this is done before any normalization took place, the fill
 * rule bias is given in 1x1-subpixel-units, where the subpixel count is given by the precision
 * of the fixed-point type used. For example, if we use ml::fixed_28_4_t, we have 4 bits of subpixel
 * precision and the fill rule bias is given in 2^(-4)-pixel-units.
 *
 * This bias is used by triangle- and point rasterization code.
 */
constexpr std::uint32_t FILL_RULE_EDGE_BIAS = 1;

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
        geom::vertex* v[3];

        /** Points to the active render states (which are stored in the context's draw lists). */
        const swr::impl::render_states* states{nullptr};

        /**
         * default constructor. only for compatibility with std containers.
         *
         * NOTE: this does not make sense to use on its own and probably leaves the object in an undefined and unusable state.
         */
        primitive() = default;

        /** point constructor. */
        primitive(const swr::impl::render_states* in_states, geom::vertex* vertex)
        : type(point)
        , is_front_facing(true)
        , v{vertex, nullptr, nullptr}
        , states(in_states)
        {
        }

        /** line constructor. */
        primitive(const swr::impl::render_states* in_states, geom::vertex* v1, geom::vertex* v2)
        : type(line)
        , is_front_facing(true)
        , v{v1, v2, nullptr}
        , states(in_states)
        {
        }

        /** triangle constructor. */
        primitive(const swr::impl::render_states* in_states, bool in_is_front_facing, geom::vertex* v1, geom::vertex* v2, geom::vertex* v3)
        : type(triangle)
        , is_front_facing(in_is_front_facing)
        , v{v1, v2, v3}
        , states(in_states)
        {
        }
    };

    /** list containing all primitives which are to be rasterized. */
    std::vector<primitive> draw_list;

    /** tile cache. */
    tile_cache tiles;

#ifdef SWR_ENABLE_MULTI_THREADING
    /** thread pool. */
    swr::impl::render_device_context::thread_pool_type* thread_pool{nullptr};

    /** process all tiles stored in the tile cache. */
    void process_tile_cache()
    {
        // for each non-empty tile, add a job to the thread pool.
        for(std::size_t i = 0; i < tiles.entries.size(); ++i)
        {
            if(tiles.entries[i].primitives.size())
            {
                thread_pool->push_task(process_tile_static, this, &tiles.entries[i]);
            }
        }

        thread_pool->run_tasks_and_wait();
        tiles.clear_tiles();
    }
#else  /* SWR_ENABLE_MULTI_THREADING */
    /** process all tiles stored in the tile cache. */
    void process_tile_cache()
    {
        // for each non-empty tile, add a job to the thread pool.
        for(std::size_t i = 0; i < tiles.entries.size(); ++i)
        {
            if(tiles.entries[i].primitives.size())
            {
                process_tile(tiles.entries[i]);
            }
        }

        tiles.clear_tiles();
    }
#endif /* SWR_ENABLE_MULTI_THREADING */

    /*
     * fragment processing.
     */

    /** generate a color value along with depth- and stencil flags for a single fragment. writes to the depth buffer. */
    void process_fragment(
      int x,
      int y,
      const swr::impl::render_states& states,
      const swr::program_base* in_shader,
      float one_over_viewport_z,
      fragment_info& info,
      swr::impl::fragment_output& out);

    /** generate color values along with depth- and stencil masks for a 2x2 block of fragments. writes to the depth buffer. */
    void process_fragment_block(
      int x,
      int y,
      const swr::impl::render_states& states,
      const swr::program_base* in_shader,
      const ml::vec4& one_over_viewport_z,
      fragment_info info[4],
      swr::impl::fragment_output_block& out);

    /**
     * generate color values along with depth- and stencil masks for a 2x2 block of fragments. writes to the depth buffer.
     *
     * @param x x-coordinate of the top-left corner of the block.
     * @param y y-coordinate of the top-left corner of the block.
     * @param mask block mask `tl|tr|bl|br`, i.e., `tl` corresponds to mask `0x8`, `tr` to `0x4`, `bl` to `0x2`, `br` to `0x1`.
     * @param states active render states.
     * @param one_over_viewport_z `1/z` for viewport `z` for each fragment (order `tl`, `tr`, `bl`, `br`).
     * @param info fragment infos (order `tl`, `tr`, `bl`, `br`).
     * @param out output fragment block.
     */
    void process_fragment_block(
      int x,
      int y,
      std::uint8_t mask,
      const swr::impl::render_states& states,
      const swr::program_base* in_shader,
      const ml::vec4& one_over_viewport_z,
      fragment_info info[4],
      swr::impl::fragment_output_block& out);

    /*
     * fragment block processing.
     */

    /**
     * Rasterize a complete block of dimension (rasterizer_block_size, rasterizer_block_size), i.e. do not perform additional edge checks.
     */
    void process_block(unsigned int in_x, unsigned int in_y, tile_info& in_data);

    /**
     * Rasterize block of dimension (rasterizer_block_size, rasterizer_block_size) and check for each fragment, if it is inside the triangle
     * described by the vertex attributes.
     */
    void process_block_checked(unsigned int in_x, unsigned int in_y, tile_info& in_data);

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
     * NOTE Depending on the render states, the vertices may be modified.
     *
     * \param states Active render states for this triangle.
     * \param is_front_facing Whether this triangle is front facing. Passed to the fragment shader.
     * \param v1 First triangle vertex.
     * \param v2 Second triangle vertex.
     * \param v3 Third triangle vertex.
     */
    void draw_filled_triangle(const swr::impl::render_states& states, bool is_front_facing, geom::vertex& v1, geom::vertex& v2, geom::vertex& v3);

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
    sweep_rasterizer([[maybe_unused]] swr::impl::render_device_context::thread_pool_type* in_thread_pool, swr::impl::default_framebuffer* in_framebuffer)
    : rasterizer{in_framebuffer}
#ifdef SWR_ENABLE_MULTI_THREADING
    , thread_pool{in_thread_pool}
#endif
    {
        assert(framebuffer);

        /*
         * set up tile cache.
         */

        unsigned int tiles_x = (framebuffer->properties.width >> swr::impl::rasterizer_block_shift) + 1;
        unsigned int tiles_y = (framebuffer->properties.height >> swr::impl::rasterizer_block_shift) + 1;

        tiles.reset(tiles_x, tiles_y);
    }

    /*
     * rasterizer interface.
     */

    std::string describe() const override
    {
        return "Sweep Rasterizer";
    }
    void add_point(const swr::impl::render_states* states, geom::vertex* v) override;
    void add_line(const swr::impl::render_states* states, geom::vertex* v1, geom::vertex* v2) override;
    void add_triangle(const swr::impl::render_states* states, bool is_front_facing, geom::vertex* v1, geom::vertex* v2, geom::vertex* v3) override;
    void draw_primitives() override;
};

} /* namespace rast */
