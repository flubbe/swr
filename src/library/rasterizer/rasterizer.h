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

    /** width and height of rasterized area. should equal the viewport's width and height. */
    int raster_width{0}, raster_height{0};

    /** the color buffer to write to. */
    swr::impl::color_buffer* color_buffer{nullptr};

    /** the depth buffer to write to. */
    swr::impl::depth_buffer* depth_buffer{nullptr};

    /*
     * statistics and benchmarking.
     */

    /** fragment processing stage statistics. */
    swr::stats::fragment_data stats_frag;

    /** rasterizer. */
    swr::stats::rasterizer_data stats_rast;

    /*
     * interface.
     */

    /** default constructor. */
    rasterizer() = default;

    /** initializing constructor. */
    rasterizer(swr::impl::color_buffer* in_color_buffer, swr::impl::depth_buffer* in_depth_buffer)
    : color_buffer(in_color_buffer)
    , depth_buffer(in_depth_buffer)
    {
        assert(in_color_buffer);
        assert(in_depth_buffer);
        assert(in_color_buffer->width == in_depth_buffer->width);
        assert(in_color_buffer->height == in_depth_buffer->height);

        raster_width = in_color_buffer->width;
        raster_height = in_color_buffer->height;
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
