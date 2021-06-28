/**
 * swr - a software rasterizer
 * 
 * abstract rasterizer that queues up points, lines and triangles and finally draws them.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "swr/stats.h"

namespace rast
{

/** abstract rasterizer interface. */
struct rasterizer
{
    /*
     * rasterizer states. 
     */

    /** pixel center. */
    ml::vec2 pixel_center{0.5f, 0.5f};

    /** width and height of rasterized area. */
    int raster_width{0}, raster_height{0};

    /** pointer to the default framebuffer. */
    swr::impl::default_framebuffer* framebuffer{nullptr};

    /*
     * statistics and benchmarking.
     */

#ifdef SWR_ENABLE_STATS
    /** fragment processing stage statistics. */
    swr::stats::fragment_data stats_frag;

    /** rasterizer. */
    swr::stats::rasterizer_data stats_rast;
#endif

    /*
     * interface.
     */

    /** default constructor. */
    rasterizer() = default;

    /** initializing constructor. */
    rasterizer(int width, int height, swr::impl::default_framebuffer* in_framebuffer)
    : raster_width{width}
    , raster_height{height}
    , framebuffer{in_framebuffer}
    {
        assert(in_framebuffer);
    }

    /** virtual Destructor. */
    virtual ~rasterizer()
    {
    }

    /** Return a short description of the rasterizer. */
    virtual const std::string describe() const = 0;

    /** Set width and height of the render buffer. */
    virtual void set_dimensions(int in_width, int in_height) = 0;

    /**
     * Add a point which is to be rasterized. The supplied vertices are assumed to
     * be valid pointers when the actual rasterization takes place.
     */
    virtual void add_point(const swr::impl::render_states* s, const geom::vertex* v) = 0;

    /**
     * Add a line which is to be rasterized. The supplied vertices are assumed to
     * be valid pointers when the actual rasterization takes place.
     */
    virtual void add_line(const swr::impl::render_states* s, const geom::vertex* v1, const geom::vertex* v2) = 0;

    /** 
     * Add a triangle which is to be rasterized. The supplied vertices are assumed to
     * be valid pointers when the actual rasterization takes place.
     */
    virtual void add_triangle(const swr::impl::render_states* s, bool is_front_facing, const geom::vertex* v1, const geom::vertex* v2, const geom::vertex* v3) = 0;

    /**
     * Draw all primitives. Operations take place with respect to the internal render context.
     */
    virtual void draw_primitives() = 0;
};

} /* namespace rast */
