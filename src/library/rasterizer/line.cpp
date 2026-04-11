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
  const geom::vertex& v0,
  const geom::vertex& v1)
{
    auto info = rast::line_info::make(v0, v1);
    if(!info.has_value())
    {
        return;
    }

    rast::line_interpolator attr(
      *info->v0,
      *info->v1,
      v0,
      states.shader_info->iqs,
      1.0f / info->max_absolute_delta);

    std::vector<std::byte> shader_storage{states.shader_info->shader->size()};
    swr::program_base* shader =
      states.shader_info->shader->create_fragment_shader_instance(
        shader_storage.data(),
        states.uniforms,
        states.texture_2d_samplers);

    boost::container::static_vector<swr::varying, swr::limits::max::varyings>
      temp_varyings;

    auto emit_fragment = [&](int x, int y, line_emit_kind kind)
    {
        if(x >= 0 && y >= 0
           && x < states.draw_target->properties.width
           && y < states.draw_target->properties.height)
        {
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
        }

        if(kind == line_emit_kind::walked_pixel)
        {
            attr.advance();
        }
    };

    rasterize_line_coverage(*info, emit_fragment);
}

} /* namespace rast */
