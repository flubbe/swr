/**
 * swr - a software rasterizer
 *
 * abstract rasterizer that queues up points, lines and triangles and finally draws them.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <string_view>

#include "geometry/vertex.h"
#include "states.h"

namespace rast
{

/** abstract rasterizer interface. */
struct rasterizer
{
    /** default destructor. */
    virtual ~rasterizer() = default;

    /** Return a short description of the rasterizer. */
    [[nodiscard]]
    virtual std::string_view describe() const = 0;

    /**
     * Add a point which is to be rasterized. The supplied vertices are assumed to
     * be valid pointers when the actual rasterization takes place.
     */
    virtual void add_point(
      const swr::impl::render_states* s,
      geom::vertex* v) = 0;

    /**
     * Add a line which is to be rasterized. The supplied vertices are assumed to
     * be valid pointers when the actual rasterization takes place.
     */
    virtual void add_line(
      const swr::impl::render_states* s,
      geom::vertex* v1,
      geom::vertex* v2) = 0;

    /**
     * Add a triangle which is to be rasterized. The supplied vertices are assumed to
     * be valid pointers when the actual rasterization takes place.
     */
    virtual void add_triangle(
      const swr::impl::render_states* s,
      bool is_front_facing,
      geom::vertex* v1,
      geom::vertex* v2,
      geom::vertex* v3) = 0;

    /**
     * Draw all primitives. Operations take place with respect to the internal render context.
     */
    virtual void draw_primitives() = 0;
};

} /* namespace rast */
