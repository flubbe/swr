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

#include "fragment.h"
#include "interpolators.h"
#include "line.h"
#include "sweep.h"

namespace rast
{

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

    const ml::vec2 start = info.v1->coords.xy();
    const ml::vec2 end = info.v2->coords.xy();

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

    std::optional<ml::tvec2<int>> deferred_walk_start_pixel;
    std::optional<ml::tvec2<int>> deferred_walk_end_pixel;
    std::optional<int> reserved_walk_start_major;
    std::optional<int> reserved_walk_end_major;

    if(!info.swapped)
    {
        if(info.include_original_start_pixel)
        {
            deferred_walk_start_pixel = info.original_start;
            reserved_walk_start_major = info.is_x_major ? info.original_start.x : info.original_start.y;
        }

        if(info.exclude_original_end_pixel)
        {
            reserved_walk_end_major = info.is_x_major ? info.original_end.x : info.original_end.y;
        }
    }
    else
    {
        // original start maps to walk end.
        if(info.include_original_start_pixel)
        {
            deferred_walk_end_pixel = info.original_start;
            reserved_walk_end_major = info.is_x_major ? info.original_start.x : info.original_start.y;
        }

        // original end maps to walk start.
        if(info.exclude_original_end_pixel)
        {
            reserved_walk_start_major = info.is_x_major ? info.original_end.x : info.original_end.y;
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

    std::optional<ml::tvec2<int>> last_emitted_pixel;

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
            emit_pixel(deferred_walk_start_pixel->x, deferred_walk_start_pixel->y);
            last_emitted_pixel = deferred_walk_start_pixel.value();
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
            float error = 2.0f * dp * (v_real - (static_cast<float>(v_pix) + 0.5f));

            int p = p_start;
            while(true)
            {
                emit_pixel(p, v_pix);
                last_emitted_pixel = ml::tvec2<int>(p, v_pix);

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
            emit_pixel(deferred_walk_start_pixel->x, deferred_walk_start_pixel->y);
            last_emitted_pixel = deferred_walk_start_pixel.value();
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
            float error = 2.0f * dp * (v_real - (static_cast<float>(v_pix) + 0.5f));

            int p = p_start;
            while(true)
            {
                emit_pixel(v_pix, p);
                last_emitted_pixel = ml::tvec2<int>(v_pix, p);

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
       && (!last_emitted_pixel.has_value()
           || last_emitted_pixel.value().x != deferred_walk_end_pixel.value().x
           || last_emitted_pixel.value().y != deferred_walk_end_pixel.value().y))
    {
        emit_pixel(deferred_walk_end_pixel->x, deferred_walk_end_pixel->y);
    }
}

} /* namespace rast */
