/**
 * swr - a software rasterizer
 * 
 * software rasterizer interface.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

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

/** Single-threaded sweep rasterizer. */
class sweep_rasterizer_single_threaded : public rasterizer
{
    /** a geometric primitive understood by sweep_rasterizer_single_threaded */
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

#ifdef SWR_ENABLE_MULTI_THREADING
    /** worker threads. */
    utils::thread_pool rasterizer_threads;
#endif

    /**
     * Process fragments and merge outputs.
     *
     * \param x x position of fragment
     * \param y y position of fragment
     * \param states Active render states
     * \param one_over_viewport_z The fragment's viewport 1/z value.
     * \param info Fragment information.
     * 
     * \return true if a fragment was written.
     */
    bool process_fragment(int x, int y, const swr::impl::render_states& states, float one_over_viewport_z, fragment_info& info);

    /**
     * Rasterize a complete block of dimension (rasterizer_block_size, rasterizer_block_size), i.e. do not perform additional edge checks.
     *
     * \param states Active render states
     * \param attr A vertex attribute interpolator which holds pre-computed attributes. Note that its values are not preserved.
     * \param x Top left x coordinate
     * \param y Top left y coordinate
     * \param front_facing Whether the fragments come from a front facing triangle. Passed to the fragment shader.
     */
    void process_block(const swr::impl::render_states& states, triangle_interpolator& attr, int x, int y, bool front_facing);

    /**
     * Rasterize block of dimension (rasterizer_block_size, rasterizer_block_size) and check for each fragment, if it is inside the triangle
     * described by the vertex attributes.
     *
     * \param states Active render states.
     * \param attr A vertex attribute interpolator which holds pre-computed attributes. Note that its values are not preserved.
     * \param lambda_fixed Unnormalized barycentric coordinates for the triangle in fixed-point format. Used for point-in-triangle detection.
     * \param x Top left x coordinate
     * \param y Top left y coordinate
     * \param front_facing Whether the fragments come from a front facing triangle. Passed to the fragment shader.
     */
    void process_block_checked(const swr::impl::render_states& states, triangle_interpolator& attr, const geom::linear_interpolator_2d<ml::fixed_24_8_t> lambda_fixed[3], int x, int y, bool front_facing);

#ifdef SWR_ENABLE_MULTI_THREADING
    /** static block drawing functions. callable by threads. */
    static void thread_process_block(sweep_rasterizer_single_threaded* rasterizer, const swr::impl::render_states& states, triangle_interpolator attr, int x, int y, bool front_facing);

    /** static block drawing functions. callable by threads. */
    static void thread_process_block_checked(sweep_rasterizer_single_threaded* rasterizer, const swr::impl::render_states& states, triangle_interpolator attr, const geom::linear_interpolator_2d<ml::fixed_24_8_t> lambda_fixed[3], int x, int y, bool front_facing);
#endif

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

    /**
     * Draw a line. For line strips, the interior end points should be omitted by setting draw_end_point to false.
     */
    void draw_line(const swr::impl::render_states& states, bool draw_end_point, const geom::vertex& v1, const geom::vertex& v2);

    /**
     * Draw a point.
     */
    void draw_point(const swr::impl::render_states& states, const geom::vertex& v);

public:
    /** Constructor. */
    sweep_rasterizer_single_threaded(swr::impl::color_buffer* in_color_buffer, swr::impl::depth_buffer* in_depth_buffer)
    : rasterizer(in_color_buffer, in_depth_buffer)
    {
    }

    /*
     * rasterizer interface.
     */

    const std::string describe() const override
    {
        return std::string("Sweep Rasterizer");
    }
    void set_dimensions(int in_width, int in_height) override;
    void add_point(const swr::impl::render_states* S, const geom::vertex* V) override;
    void add_line(const swr::impl::render_states* S, const geom::vertex* V1, const geom::vertex* V2) override;
    void add_triangle(const swr::impl::render_states* S, bool is_front_facing, const geom::vertex* V1, const geom::vertex* V2, const geom::vertex* V3) override;
    void draw_primitives() override;
};

} /* namespace rast */
