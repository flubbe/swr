/**
 * swr - a software rasterizer
 *
 * software rasterizer interface.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "geometry/barycentric_coords.h"
#include "tile_cache.h"

namespace rast
{

using namespace std::literals;

/** Inclusive min/max depth range. */
struct depth_range
{
    /** Minimum depth value. */
    ml::fixed_32_t min_depth{};

    /** Maximum depth value. */
    ml::fixed_32_t max_depth{};
};

/** Allocation-free lazy callback for retrieving a depth range. */
struct depth_range_provider
{
    /** Callback signature for retrieving a depth range from an opaque context. */
    using callback_t = const depth_range& (*)(void*);

    /** No default construction: a provider must always have a callback. */
    depth_range_provider() = delete;

    /** Construct a provider from a non-null callback and its context. */
    depth_range_provider(
      callback_t in_callback,
      void* in_context)
    : callback{in_callback}
    , context{in_context}
    {
        assert(callback);
    }

    /** Copy construction preserves the callback/context pair. */
    depth_range_provider(const depth_range_provider&) = default;

    /** Move construction preserves the callback/context pair. */
    depth_range_provider(depth_range_provider&&) noexcept = default;

    /** Reassignment is not supported; make a new provider instead. */
    depth_range_provider& operator=(const depth_range_provider&) = delete;

    /** Reassignment is not supported; make a new provider instead. */
    depth_range_provider& operator=(depth_range_provider&&) = delete;

    /** Function to call when a depth range is needed. */
    callback_t const callback;

    /** Opaque context passed to callback. */
    void* const context;

    /** Retrieve the depth range. */
    [[nodiscard]]
    const depth_range& operator()() const
    {
        assert(callback);
        return callback(context);
    }
};

/**
 * Bias for application to fill rules. This is edge to the line equations if the corresponding
 * edge is a left or top one. Since this is done before any normalization took place, the fill
 * rule bias is given in 1x1-subpixel-units, where the subpixel count is given by the precision
 * of the fixed-point type used. For example, if we use ml::fixed_28_4_t, we have 4 bits of subpixel
 * precision and the fill rule bias is given in 2^(-4)-pixel-units.
 *
 * This bias is used by triangle- and point rasterization code.
 */
inline constexpr std::uint32_t FILL_RULE_EDGE_BIAS = 1;

/** Sweep rasterizer. */
class sweep_rasterizer : public rasterizer
{
    /** a geometric primitive understood by sweep_rasterizer */
    struct primitive
    {
        enum class primitive_type
        {
            point,   /** point primitive, consisting of one vertex */
            line,    /** line primitive, consisting of two vertices */
            triangle /** triangle primitive, consisting of three vertices */
        };

        /** the type of primitive to be rasterized */
        primitive_type type;

        /** whether the primitive is front-facing. only relevant for triangles (otherwise always true). */
        bool is_front_facing;

        /** the primitive's vertices. points use `v[0]`, lines use `v[0]` and `v[1]`, and triangles use `v[0]`, `v[1]` and `v[2]`. */
        std::array<geom::vertex*, 3> v{nullptr, nullptr, nullptr};

        /** Points to the active render states (which are stored in the context's draw lists). */
        const swr::impl::render_states* states{nullptr};

        /**
         * default constructor. only for compatibility with std containers.
         *
         * NOTE This does not make sense to use on its own and probably leaves the object in an undefined and unusable state.
         */
        primitive() = default;

        /** point constructor. */
        primitive(
          const swr::impl::render_states* in_states,
          geom::vertex* vertex)
        : type{primitive_type::point}
        , is_front_facing{true}
        , v{vertex, nullptr, nullptr}
        , states{in_states}
        {
        }

        /** line constructor. */
        primitive(
          const swr::impl::render_states* in_states,
          geom::vertex* v1,
          geom::vertex* v2)
        : type{primitive_type::line}
        , is_front_facing{true}
        , v{v1, v2, nullptr}
        , states{in_states}
        {
        }

        /** triangle constructor. */
        primitive(
          const swr::impl::render_states* in_states,
          bool in_is_front_facing,
          geom::vertex* v1,
          geom::vertex* v2,
          geom::vertex* v3)
        : type{primitive_type::triangle}
        , is_front_facing{in_is_front_facing}
        , v{v1, v2, v3}
        , states{in_states}
        {
        }
    };

    /** pointer to the default framebuffer. */
    swr::impl::default_framebuffer* framebuffer;

    /** list containing all primitives which are to be rasterized. */
    std::vector<primitive> draw_list;

    /** tile cache. */
    tile_cache tiles;

#ifdef SWR_ENABLE_MULTI_THREADING
    /** thread pool. */
    swr::impl::render_context::thread_pool_type* thread_pool{nullptr};

    /** process all tiles stored in the tile cache. */
    void process_tile_cache()
    {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_scan = 0;
        std::uint64_t stage_process = 0;
        std::uint64_t stage_clear = 0;
        std::uint64_t nonempty_tiles = 0;
        std::uint64_t primitive_count = 0;
        std::uint64_t max_tile_prims = 0;
        std::uint64_t near_full_tiles = 0;
        const std::uint64_t scanned_tiles = tiles.active_tile_indices.size();
        utils::clock(stage_scan);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */

        // for each non-empty tile, add a job to the thread pool.
        for(const auto tile_index: tiles.active_tile_indices)
        {
            auto& entry = tiles.entries[tile_index];

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
            ++nonempty_tiles;
            const std::uint64_t prims = entry.primitives.size();
            primitive_count += prims;
            max_tile_prims = std::max(max_tile_prims, prims);
            if(prims + 16 >= entry.primitives.max_size())
            {
                ++near_full_tiles;
            }
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */

            thread_pool->push_task(
              process_tile_static,
              this,
              &entry);
        }

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_scan);
        utils::clock(stage_process);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */

        thread_pool->run_tasks_and_wait();

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_process);
        utils::clock(stage_clear);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */

        tiles.clear_tiles();

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_clear);
        swr::impl::profile_raster_flush_scan_cycles.fetch_add(stage_scan, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_process_cycles.fetch_add(stage_process, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_clear_cycles.fetch_add(stage_clear, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_nonempty_tiles.fetch_add(nonempty_tiles, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_primitives.fetch_add(primitive_count, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_count.fetch_add(1, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_scanned_tiles.fetch_add(scanned_tiles, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_max_tile_prims.fetch_add(max_tile_prims, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_near_full_tiles.fetch_add(near_full_tiles, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
    }
#else /* SWR_ENABLE_MULTI_THREADING */
    /** process all tiles stored in the tile cache. */
    void process_tile_cache()
    {
#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_scan = 0;
        std::uint64_t stage_process = 0;
        std::uint64_t stage_clear = 0;
        std::uint64_t nonempty_tiles = 0;
        std::uint64_t primitive_count = 0;
        std::uint64_t max_tile_prims = 0;
        std::uint64_t near_full_tiles = 0;
        const std::uint64_t scanned_tiles = tiles.active_tile_indices.size();
        bool process_started = false;
        utils::clock(stage_scan);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */

        // for each non-empty tile, add a job to the thread pool.
        for(const auto tile_index: tiles.active_tile_indices)
        {
            auto& entry = tiles.entries[tile_index];

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
            ++nonempty_tiles;
            const std::uint64_t prims = entry.primitives.size();
            primitive_count += prims;
            max_tile_prims = std::max(max_tile_prims, prims);
            if(prims + 16 >= entry.primitives.max_size())
            {
                ++near_full_tiles;
            }
            if(!process_started)
            {
                utils::unclock(stage_scan);
                process_started = true;
            }
            utils::clock(stage_process);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */

            process_tile(entry);

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
            utils::unclock(stage_process);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
        }

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        if(!process_started)
        {
            utils::unclock(stage_scan);
        }
        utils::clock(stage_clear);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */

        tiles.clear_tiles();

#    ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_clear);
        swr::impl::profile_raster_flush_scan_cycles.fetch_add(stage_scan, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_process_cycles.fetch_add(stage_process, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_clear_cycles.fetch_add(stage_clear, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_nonempty_tiles.fetch_add(nonempty_tiles, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_primitives.fetch_add(primitive_count, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_count.fetch_add(1, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_scanned_tiles.fetch_add(scanned_tiles, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_max_tile_prims.fetch_add(max_tile_prims, std::memory_order_relaxed);
        swr::impl::profile_raster_flush_near_full_tiles.fetch_add(near_full_tiles, std::memory_order_relaxed);
#    endif /* SWR_ENABLE_PIPELINE_PROFILING */
    }
#endif     /* SWR_ENABLE_MULTI_THREADING */

    /*
     * fragment processing.
     */

    /**
     * Generate a color value along with depth- and stencil flags for a single fragment.
     * Writes to the depth buffer.
     *
     * @param x Raster `x` coordinate of the fragment.
     * @param y Raster `y` coordinate of the fragment.
     * @param states Active render states.
     * @param in_shader Fragment shader to execute.
     * @param one_over_viewport_z Reciprocal viewport depth value for the fragment.
     * @param info Per-fragment interpolator information.
     * @param out Output buffer for the fragment.
     */
    void process_fragment(
      int x,
      int y,
      const swr::impl::render_states& states,
      const swr::program_base* in_shader,
      float one_over_viewport_z,
      fragment_info& info,
      swr::impl::fragment_output& out);

    /**
     * Generate color values along with depth- and stencil masks for a 2x2 fragment block.
     * Writes to the depth buffer.
     *
     * @param x Left raster coordinate of the fragment block.
     * @param y Top raster coordinate of the fragment block.
     * @param states Active render states.
     * @param in_shader Fragment shader to execute.
     * @param one_over_viewport_z Reciprocal viewport depth values for the fragment block.
     * @param info Per-fragment interpolator information.
     * @param out Output buffers for the fragment block.
     */
    void process_fragment_block(
      int x,
      int y,
      const swr::impl::render_states& states,
      const swr::program_base* in_shader,
      const ml::vec4& one_over_viewport_z,
      std::array<fragment_info, 4>& info,
      swr::impl::fragment_output_block& out);

    /**
     * Generate color values along with depth- and stencil masks for a masked 2x2 fragment block.
     * Writes to the depth buffer. Only fragments selected by the mask are shaded and depth-tested.
     *
     * @param x Left raster coordinate of the fragment block.
     * @param y Top raster coordinate of the fragment block.
     * @param mask Coverage mask for the 2x2 fragment block.
     * @param states Active render states.
     * @param in_shader Fragment shader to execute.
     * @param one_over_viewport_z Reciprocal viewport depth values for the fragment block.
     * @param info Per-fragment interpolator information.
     * @param out Output buffers for the fragment block.
     */
    void process_fragment_block(
      int x,
      int y,
      std::uint8_t mask,
      const swr::impl::render_states& states,
      const swr::program_base* in_shader,
      const ml::vec4& one_over_viewport_z,
      std::array<fragment_info, 4>& info,
      swr::impl::fragment_output_block& out);

    /*
     * block processing.
     */

    /**
     * Rasterize a complete block of dimension `(rasterizer_block_size, rasterizer_block_size)`,
     * i.e. do not perform additional edge checks.
     *
     * @param block_x Left raster coordinate of the block.
     * @param block_y Top raster coordinate of the block.
     * @param data Tile data.
     * @param range_provider Optional lazy min/max depth range provider for the current tile.
     * @return false if the block was fully rejected before fragment processing, true otherwise.
     */
    bool process_block(
      unsigned int block_x,
      unsigned int block_y,
      tile_info& data,
      const depth_range_provider* range_provider = nullptr);

    /**
     * Rasterize block of dimension `(rasterizer_block_size, rasterizer_block_size)`
     * and check for each fragment, if it is inside the triangle described by the vertex attributes.
     *
     * @param block_x Left raster coordinate of the block.
     * @param block_y Top raster coordinate of the block.
     * @param data Tile data.
     * @param range_provider Optional lazy min/max depth range provider for the current tile.
     * @return false if the block was fully rejected before fragment processing, true otherwise.
     */
    bool process_block_checked(
      unsigned int block_x,
      unsigned int block_y,
      tile_info& data,
      const depth_range_provider* range_provider = nullptr);

    /**
     * Rasterize a block using precomputed small-triangle attributes and a set of quads.
     * This path is optimized for small triangles where the quad payloads are already available.
     *
     * @param block_x Left raster coordinate of the block.
     * @param block_y Top raster coordinate of the block.
     * @param data Tile data.
     * @param attributes Precomputed interpolator state for the small triangle.
     * @param quads Precomputed payloads for the 2x2 quads covered by the triangle.
     */
    void process_block_precomputed_checked(
      unsigned int block_x,
      unsigned int block_y,
      tile_info& data,
      const small_triangle_interpolator& attributes,
      std::span<const small_triangle_quad_payload> quads);

    /**
     * Rasterize a small triangle block and perform per-pixel checks.
     * This path is used for tightly bounded triangles where a compact payload is sufficient.
     *
     * @param block_x Left raster coordinate of the block.
     * @param block_y Top raster coordinate of the block.
     * @param data Tile data.
     * @param payload Payload describing the small triangle.
     */
    void process_block_small_checked(
      unsigned int block_x,
      unsigned int block_y,
      tile_info& data,
      const small_triangle_payload& payload);

    /**
     * Rasterize a sparse triangle block using a compact sparse payload.
     * This path is optimized for triangles that cover very few pixels within the block.
     *
     * @param block_x Left raster coordinate of the block.
     * @param block_y Top raster coordinate of the block.
     * @param data Tile data.
     * @param payload Sparse triangle payload for the block.
     */
    void process_block_sparse_checked(
      unsigned int block_x,
      unsigned int block_y,
      tile_info& data,
      const sparse_triangle_payload& payload);

    /**
     * Rasterize a sparse triangle block with additional quad payloads precomputed.
     * This overload supports sparse blocks where quad-level coverage information is available.
     *
     * @param block_x Left raster coordinate of the block.
     * @param block_y Top raster coordinate of the block.
     * @param data Tile data.
     * @param payload Sparse triangle tile payload.
     * @param quads Precomputed payloads for the 2x2 quads in the block.
     */
    void process_block_sparse_checked(
      unsigned int block_x,
      unsigned int block_y,
      tile_info& data,
      const sparse_triangle_tile_payload& payload,
      std::span<const small_triangle_quad_payload> quads);

    /**
     * Process all primitives stored in a tile and rasterize them into the framebuffer.
     *
     * @param in_tile Tile data containing primitives and associated state.
     */
    void process_tile(tile& in_tile);

#ifdef SWR_ENABLE_MULTI_THREADING
    /**
     * Static thread entry point that forwards tile processing to the rasterizer instance.
     *
     * @param rasterizer Sweep rasterizer instance.
     * @param in_tile Tile to process.
     */
    static void process_tile_static(
      sweep_rasterizer* rasterizer,
      tile* in_tile);
#endif

    /*
     * drawing functions.
     */

    /**
     * Draw the triangle `(v1,v2,v3)` using a sweep algorithm with blocks of size `rasterizer_block_size`.
     * The triangle is rasterized regardless of its orientation.
     *
     * @param states Active render states for this triangle.
     * @param is_front_facing Whether this triangle is front facing. Passed to the fragment shader.
     * @param v0 First triangle vertex.
     * @param v1 Second triangle vertex.
     * @param v2 Third triangle vertex.
     */
    void draw_filled_triangle(
      const swr::impl::render_states& states,
      bool is_front_facing,
      const geom::vertex& v0,
      const geom::vertex& v1,
      const geom::vertex& v2);

    /** draw a line. For line strips, the interior end points should be omitted by setting draw_end_point to false. */
    void draw_line(
      const swr::impl::render_states& states,
      bool draw_end_point,
      const geom::vertex& v0,
      const geom::vertex& v1);

    /** draw a point. */
    void draw_point(
      const swr::impl::render_states& states,
      const geom::vertex& v);

    /** draw the primitives in the list sequentially. */
    void draw_primitives_sequential();

#ifdef SWR_ENABLE_MULTI_THREADING
    /** draw the primitives in the list in parallel. */
    void draw_primitives_parallel();
#endif

public:
    /** Constructor. */
    sweep_rasterizer(
      [[maybe_unused]] swr::impl::render_context::thread_pool_type* in_thread_pool,
      swr::impl::default_framebuffer* in_framebuffer)
    : framebuffer{in_framebuffer}
#ifdef SWR_ENABLE_MULTI_THREADING
    , thread_pool{in_thread_pool}
#endif
    {
        assert(framebuffer);
#ifdef SWR_ENABLE_MULTI_THREADING
        assert(thread_pool);
#endif

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

    [[nodiscard]]
    std::string_view describe() const override
    {
        return "Sweep Rasterizer"sv;
    }

    void add_point(
      const swr::impl::render_states* states,
      geom::vertex* v) override;
    void add_line(
      const swr::impl::render_states* states,
      geom::vertex* v1,
      geom::vertex* v2) override;
    void add_triangle(
      const swr::impl::render_states* states,
      bool is_front_facing,
      geom::vertex* v1,
      geom::vertex* v2,
      geom::vertex* v3) override;
    void draw_primitives() override;
};

} /* namespace rast */
