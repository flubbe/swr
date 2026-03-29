/**
 * swr - a software rasterizer
 *
 * line coverage.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "../swr_internal.h"

namespace rast
{

/*
 * Helpers.
 */

/** epsilon to determine whether a point is inside the pixel diamond. */
constexpr float inside_diamond_eps = 1e-6f;

/** epsilon to determine whether a pixel is on an integer coordinate. */
constexpr float integer_pixel_tie_eps = 1e-9f;

/** check if a pixel in coordinates relative to the pixel center is inside the pixel diamond. */
inline bool inside_diamond(ml::vec2 v)
{
    return std::abs(v.x) + std::abs(v.y) < 0.5f - inside_diamond_eps;
}

/** extract the fractional part of a positive float */
inline float fracf(float f)
{
    return f - std::floor(f);
}

/** local pixel information: integer pixel coordinates and offset to pixel center. */
struct pixel_local_info
{
    /** integer pixel coordinates. */
    ml::tvec2<int> coord;

    /** offset with respect to the pixel center. */
    ml::vec2 offset;
};

/** return the integer pixel coordinates and the offsets relative to the pixel center. */
static pixel_local_info pixel_diamond_local(float x, float y)
{
    const int px = static_cast<int>(std::floor(x));
    const int py = static_cast<int>(std::floor(y));

    return {
      {px, py},
      {fracf(x) - 0.5f,
       fracf(y) - 0.5f}};
}

/**
 * tie breaker for integer pixel coordinates.
 * x-major: choose top row
 * y-major: choose left column
 */
static int choose_minor_pixel(float v_real, bool x_major)
{
    const float base_f = std::floor(v_real);
    const int base = static_cast<int>(base_f);
    const float frac = v_real - base_f;

    if(std::abs(frac) < integer_pixel_tie_eps)
    {
        /*
         * exact tie between two pixel centers.
         * x-major: choose top row
         * y-major: choose left column
         */
        return x_major ? base : (base - 1);
    }

    return base;
}

/*
 * line info.
 */

/** Line setup info. */
struct line_info
{
    /** the vertices of the line. after calling setup, v1 contains the left-most vertex (for x-major lines) resp. the upper-most (for y-major lines) vertex. */
    const geom::vertex *v1, *v2;

    /** Line deltas. These are always with respect to v1 as initial vertex and v2 as end vertex. */
    float dx, dy;

    /** maximum of the absolute values of the deltas. */
    const float max_absolute_delta;

    /** If the line is parameterized over the x axis (i.e. if abs(dy)<=abs(dx)), this is true. */
    const bool is_x_major;

    /** whether v1/v2 were swapped. */
    bool swapped;

    /** whether we need to include the originals start pixel. */
    bool include_original_start_pixel;

    /** whether we need to exclude the originals end pixel. */
    bool exclude_original_end_pixel;

    /** original start pixel coordinate. */
    ml::tvec2<int> original_start;

    /** original end pixel coordinate. */
    ml::tvec2<int> original_end;

    /** no default constructor. */
    line_info() = delete;

    /** constructor. */
    line_info(const geom::vertex& in_v1, const geom::vertex& in_v2)
    : v1{&in_v1}
    , v2{&in_v2}
    , dx{in_v2.coords.x - in_v1.coords.x}
    , dy{in_v2.coords.y - in_v1.coords.y}
    , max_absolute_delta{std::max(std::abs(dx), std::abs(dy))}
    , is_x_major{std::abs(dy) <= std::abs(dx)}
    {
    }

    /** implement diamond exit rule and set up line info. */
    void setup();
};

inline void line_info::setup()
{
    dx = v2->coords.x - v1->coords.x;
    dy = v2->coords.y - v1->coords.y;

    if(max_absolute_delta == 0.0f)
    {
        return;
    }

    // set up start/end point inclusion.
    const auto start_local = pixel_diamond_local(v1->coords.x, v1->coords.y);
    const auto end_local = pixel_diamond_local(v2->coords.x, v2->coords.y);

    original_start = start_local.coord;
    original_end = end_local.coord;

    include_original_start_pixel = inside_diamond(start_local.offset);
    exclude_original_end_pixel = inside_diamond(end_local.offset);

    // normalize walking direction.
    swapped = false;
    if(is_x_major)
    {
        if(dx < 0.0f)
        {
            std::swap(v1, v2);
            dx = -dx;
            dy = -dy;
            swapped = true;
        }
    }
    else
    {
        if(dy < 0.0f)
        {
            std::swap(v1, v2);
            dx = -dx;
            dy = -dy;
            swapped = true;
        }
    }
}

}    // namespace rast