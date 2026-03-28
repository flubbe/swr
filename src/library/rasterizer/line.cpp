/**
 * swr - a software rasterizer
 *
 * line setup and drawing.
 *
 * some references:
 *
 *  [1] Direct3D 11 fill rules: https://msdn.microsoft.com/de-de/library/windows/desktop/cc627092(v=vs.85).aspx#Line_1
 *  [2] https://github.com/MIvanchev/diamond-exit-line/blob/master/src/com/podrug/line/LineRenderer.java
 *  [3] http://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
 *  [4] http://rosettacode.org/wiki/Bitmap/Bresenham%27s_line_algorithm#C.2B.2B
 *  [5] Diamond-exit: https://msdn.microsoft.com/de-de/library/windows/desktop/cc627092(v=vs.85).aspx
 *  [6] Mesa 3D: https://github.com/anholt/mesa/blob/master/src/gallium/drivers/llvmpipe/lp_setup_line.c
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "../swr_internal.h"

#include "interpolators.h"
#include "fragment.h"
#include "sweep.h"

namespace rast
{

/** epsilon to determine whether a point is inside the pixel diamond. */
constexpr float inside_diamond_eps = 1e-6f;

/** epsilon to determine whether a pixel is on an integer coordinate. */
constexpr float integer_pixel_tie_eps = 1e-9f;

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

    /** original start pixel x. */
    int original_start_x;

    /** original start pixel y. */
    int original_start_y;

    /** original end pixel x. */
    int original_end_x;

    /** original end pixel y. */
    int original_end_y;

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

/** check if a pixel in coordinates relative to the pixel center is inside the pixel diamond. */
bool inside_diamond(float x, float y)
{
    return std::abs(x) + std::abs(y) < 0.5f - inside_diamond_eps;
}

struct pixel_local_info
{
    /** integer pixel x coordinate. */
    int x;

    /** integer pixel x coordinate. */
    int y;

    /** x offset with respect to the pixel center. */
    float offset_x;

    /** y offset with respect to the pixel center. */
    float offset_y;
};

/** return the integer pixel coordinates and the offsets relative to the pixel center. */
inline pixel_local_info pixel_diamond_local(float x, float y)
{
    const float fx = std::floor(x);
    const float fy = std::floor(y);

    return {
      static_cast<int>(fx),
      static_cast<int>(fy),
      x - (fx + 0.5f),
      y - (fy + 0.5f)};
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

void line_info::setup()
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

    original_start_x = start_local.x;
    original_start_y = start_local.y;
    original_end_x = end_local.x;
    original_end_y = end_local.y;

    include_original_start_pixel = inside_diamond(start_local.offset_x, start_local.offset_y);
    exclude_original_end_pixel = inside_diamond(end_local.offset_x, end_local.offset_y);

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
 * note: in_draw_endpoint should be used for correctly drawing line-strips, but is currently unused.
 */
void sweep_rasterizer::draw_line(
  const swr::impl::render_states& states,
  [[maybe_unused]] bool in_draw_endpoint,
  const geom::vertex& v1,
  const geom::vertex& v2)
{
    line_info info{v1, v2};

    if(info.max_absolute_delta == 0)
    {
        return;
    }

    info.setup();

    const float start_x = info.v1->coords.x;
    const float start_y = info.v1->coords.y;
    const float end_x = info.v2->coords.x;
    const float end_y = info.v2->coords.y;

    rast::line_interpolator attr(
      *info.v1,
      *info.v2,
      v1,
      states.shader_info->iqs,
      1.0f / info.max_absolute_delta);

    std::vector<std::byte> shader_storage{states.shader_info->shader->size()};
    swr::program_base* shader =
      states.shader_info->shader->create_fragment_shader_instance(
        shader_storage.data(),
        states.uniforms,
        states.texture_2d_samplers);

    boost::container::static_vector<swr::varying, geom::limits::max::varyings> temp_varyings;

    std::optional<std::pair<int, int>> deferred_walk_start_pixel;
    std::optional<std::pair<int, int>> deferred_walk_end_pixel;
    std::optional<int> reserved_walk_start_major;
    std::optional<int> reserved_walk_end_major;

    if(!info.swapped)
    {
        if(info.include_original_start_pixel)
        {
            deferred_walk_start_pixel = std::make_pair(info.original_start_x, info.original_start_y);
            reserved_walk_start_major = info.is_x_major ? info.original_start_x : info.original_start_y;
        }

        if(info.exclude_original_end_pixel)
        {
            reserved_walk_end_major = info.is_x_major ? info.original_end_x : info.original_end_y;
        }
    }
    else
    {
        // original start maps to walk end.
        if(info.include_original_start_pixel)
        {
            deferred_walk_end_pixel = std::make_pair(info.original_start_x, info.original_start_y);
            reserved_walk_end_major = info.is_x_major ? info.original_start_x : info.original_start_y;
        }

        // original end maps to walk start.
        if(info.exclude_original_end_pixel)
        {
            reserved_walk_start_major = info.is_x_major ? info.original_end_x : info.original_end_y;
        }
    }

    // NOTE coverage may be emitted outside the normal attr.advance() sequence;
    //      attribute interpolation is therefore approximate at deferred endpoint pixels.
    auto emit_pixel = [&](int x, int y)
    {
        if(x < 0 || y < 0
           || x >= states.draw_target->properties.width
           || y >= states.draw_target->properties.height)
        {
            return;
        }

        attr.get_varyings(temp_varyings);

        rast::fragment_info frag_info{attr.depth_value.value, true, temp_varyings};
        swr::impl::fragment_output out;

        process_fragment(
          x,
          y,
          states,
          shader,
          attr.one_over_viewport_z.value,
          frag_info,
          out);

        states.draw_target->merge_color(
          0,
          x,
          y,
          out,
          states.blending_enabled,
          states.blend_src,
          states.blend_dst);
    };

    std::optional<std::pair<int, int>> last_walk_pixel;

    if(info.is_x_major)
    {
        const float p0 = start_x;
        const float v0 = start_y;
        const float p1 = end_x;
        const float v1 = end_y;

        const float dp = p1 - p0;
        const float dv = v1 - v0;

        int p_start = static_cast<int>(std::ceil(p0 - 0.5f));
        int p_end = static_cast<int>(std::ceil(p1 - 0.5f)) - 1;

        if(deferred_walk_start_pixel.has_value())
        {
            emit_pixel(deferred_walk_start_pixel->first, deferred_walk_start_pixel->second);
            last_walk_pixel = deferred_walk_start_pixel.value();
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
            float v_real = v0 + ((static_cast<float>(p_start) + 0.5f) - p0) * dv / dp;
            int v_pix = choose_minor_pixel(v_real, true);
            float error = 2.0f * dp * (v_real - (static_cast<float>(v_pix) + 0.5f));

            int p = p_start;
            while(true)
            {
                emit_pixel(p, v_pix);
                last_walk_pixel = std::make_pair(p, v_pix);

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

                attr.advance();
            }
        }
    }
    else
    {
        const float p0 = start_y;
        const float v0 = start_x;
        const float p1 = end_y;
        const float v1 = end_x;

        const float dp = p1 - p0;
        const float dv = v1 - v0;

        int p_start = static_cast<int>(std::ceil(p0 - 0.5f));
        int p_end = static_cast<int>(std::ceil(p1 - 0.5f)) - 1;

        if(deferred_walk_start_pixel.has_value())
        {
            emit_pixel(deferred_walk_start_pixel->first, deferred_walk_start_pixel->second);
            last_walk_pixel = deferred_walk_start_pixel.value();
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
            float v_real = v0 + ((static_cast<float>(p_start) + 0.5f) - p0) * dv / dp;
            int v_pix = choose_minor_pixel(v_real, false);
            float error = 2.0f * dp * (v_real - (static_cast<float>(v_pix) + 0.5f));

            int p = p_start;
            while(true)
            {
                emit_pixel(v_pix, p);
                last_walk_pixel = std::make_pair(v_pix, p);

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

                attr.advance();
            }
        }
    }

    if(deferred_walk_end_pixel.has_value()
       && (!last_walk_pixel.has_value()
           || last_walk_pixel.value() != deferred_walk_end_pixel.value()))
    {
        emit_pixel(deferred_walk_end_pixel->first, deferred_walk_end_pixel->second);
    }
}

} /* namespace rast */
