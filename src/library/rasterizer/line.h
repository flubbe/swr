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
inline pixel_local_info pixel_diamond_local(float x, float y)
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
inline int choose_minor_pixel(float v_real, bool x_major)
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

/*
 * fragment emission and rasterization.
 */

/** where the emission took place. */
enum class line_emit_kind
{
    walked_pixel,     /** emission during line walking */
    deferred_endpoint /** emission of deferred endpoint */
};

template<class EmitFragment>
void rasterize_line_coverage(
  const geom::vertex& in_v1,
  const geom::vertex& in_v2,
  EmitFragment&& emit_fragment)
{
    line_info info{in_v1, in_v2};

    if(info.max_absolute_delta == 0.0f)
    {
        return;
    }

    info.setup();

    const ml::vec2 start = info.v1->coords.xy();
    const ml::vec2 end = info.v2->coords.xy();

    std::optional<ml::tvec2<int>> deferred_walk_start_pixel;
    std::optional<ml::tvec2<int>> deferred_walk_end_pixel;
    std::optional<int> reserved_walk_start_major;
    std::optional<int> reserved_walk_end_major;

    if(!info.swapped)
    {
        if(info.include_original_start_pixel)
        {
            deferred_walk_start_pixel = info.original_start;
            reserved_walk_start_major =
              info.is_x_major ? info.original_start.x : info.original_start.y;
        }

        if(info.exclude_original_end_pixel)
        {
            reserved_walk_end_major =
              info.is_x_major ? info.original_end.x : info.original_end.y;
        }
    }
    else
    {
        // original start maps to walk end.
        if(info.include_original_start_pixel)
        {
            deferred_walk_end_pixel = info.original_start;
            reserved_walk_end_major =
              info.is_x_major ? info.original_start.x : info.original_start.y;
        }

        // original end maps to walk start.
        if(info.exclude_original_end_pixel)
        {
            reserved_walk_start_major =
              info.is_x_major ? info.original_end.x : info.original_end.y;
        }
    }

    std::optional<ml::tvec2<int>> last_emitted_pixel;

    auto emit = [&](int x, int y, line_emit_kind kind)
    {
        emit_fragment(x, y, kind);
        last_emitted_pixel = ml::tvec2<int>(x, y);
    };

    if(info.is_x_major)
    {
        const float p0 = start.x;
        const float v0 = start.y;
        const float p1 = end.x;
        const float v1 = end.y;

        const float dp = p1 - p0;
        const float dv = v1 - v0;

        int p_start = static_cast<int>(std::ceil(p0 - 0.5f));
        int p_end = static_cast<int>(std::ceil(p1 - 0.5f)) - 1;

        if(deferred_walk_start_pixel.has_value())
        {
            emit(
              deferred_walk_start_pixel->x,
              deferred_walk_start_pixel->y,
              line_emit_kind::deferred_endpoint);
        }

        if(reserved_walk_start_major.has_value())
        {
            p_start = std::max(p_start, *reserved_walk_start_major + 1);
        }

        if(reserved_walk_end_major.has_value())
        {
            p_end = std::min(p_end, *reserved_walk_end_major - 1);
        }

        if(p_start <= p_end)
        {
            const float p_center = static_cast<float>(p_start) + 0.5f;
            const float v_real = v0 + (p_center - p0) * (dv / dp);
            int v_pix = choose_minor_pixel(v_real, true);
            float error =
              2.0f * dp * (v_real - (static_cast<float>(v_pix) + 0.5f));

            int p = p_start;
            while(true)
            {
                emit(p, v_pix, line_emit_kind::walked_pixel);

                if(p == p_end)
                {
                    break;
                }

                ++p;

                error += 2.0f * dv;
                if(error > dp)
                {
                    ++v_pix;
                    error -= 2.0f * dp;
                }
                else if(error < -dp)
                {
                    --v_pix;
                    error += 2.0f * dp;
                }
            }
        }
    }
    else
    {
        const float p0 = start.y;
        const float v0 = start.x;
        const float p1 = end.y;
        const float v1 = end.x;

        const float dp = p1 - p0;
        const float dv = v1 - v0;

        int p_start = static_cast<int>(std::ceil(p0 - 0.5f));
        int p_end = static_cast<int>(std::ceil(p1 - 0.5f)) - 1;

        if(deferred_walk_start_pixel.has_value())
        {
            emit(
              deferred_walk_start_pixel->x,
              deferred_walk_start_pixel->y,
              line_emit_kind::deferred_endpoint);
        }

        if(reserved_walk_start_major.has_value())
        {
            p_start = std::max(p_start, *reserved_walk_start_major + 1);
        }

        if(reserved_walk_end_major.has_value())
        {
            p_end = std::min(p_end, *reserved_walk_end_major - 1);
        }

        if(p_start <= p_end)
        {
            const float p_center = static_cast<float>(p_start) + 0.5f;
            const float v_real = v0 + (p_center - p0) * (dv / dp);
            int v_pix = choose_minor_pixel(v_real, false);
            float error =
              2.0f * dp * (v_real - (static_cast<float>(v_pix) + 0.5f));

            int p = p_start;
            while(true)
            {
                emit(v_pix, p, line_emit_kind::walked_pixel);

                if(p == p_end)
                {
                    break;
                }

                ++p;

                error += 2.0f * dv;
                if(error > dp)
                {
                    ++v_pix;
                    error -= 2.0f * dp;
                }
                else if(error < -dp)
                {
                    --v_pix;
                    error += 2.0f * dp;
                }
            }
        }
    }

    if(deferred_walk_end_pixel.has_value()
       && (!last_emitted_pixel.has_value()
           || last_emitted_pixel->x != deferred_walk_end_pixel->x
           || last_emitted_pixel->y != deferred_walk_end_pixel->y))
    {
        emit(
          deferred_walk_end_pixel->x,
          deferred_walk_end_pixel->y,
          line_emit_kind::deferred_endpoint);
    }
}

}    // namespace rast