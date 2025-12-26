/**
 * swr - a software rasterizer
 *
 * abstract rasterizer that queues up points, lines and triangles and finally draws them.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

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

    /** pointer to the default framebuffer. */
    swr::impl::default_framebuffer* framebuffer{nullptr};

    /*
     * interface.
     */

    /** default constructor. */
    rasterizer() = default;

    /** initializing constructor. */
    rasterizer(swr::impl::default_framebuffer* in_framebuffer)
    : framebuffer{in_framebuffer}
    {
        assert(in_framebuffer);
    }

    /** virtual Destructor. */
    virtual ~rasterizer()
    {
    }

    /** Return a short description of the rasterizer. */
    virtual const std::string describe() const = 0;

    /**
     * Add a point which is to be rasterized. The supplied vertices are assumed to
     * be valid pointers when the actual rasterization takes place.
     */
    virtual void add_point(const swr::impl::render_states* s, geom::vertex* v) = 0;

    /**
     * Add a line which is to be rasterized. The supplied vertices are assumed to
     * be valid pointers when the actual rasterization takes place.
     */
    virtual void add_line(const swr::impl::render_states* s, geom::vertex* v1, geom::vertex* v2) = 0;

    /**
     * Add a triangle which is to be rasterized. The supplied vertices are assumed to
     * be valid pointers when the actual rasterization takes place.
     */
    virtual void add_triangle(const swr::impl::render_states* s, bool is_front_facing, geom::vertex* v1, geom::vertex* v2, geom::vertex* v3) = 0;

    /**
     * Draw all primitives. Operations take place with respect to the internal render context.
     */
    virtual void draw_primitives() = 0;
};

} /* namespace rast */
