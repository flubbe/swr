/**
 * swr - a software rasterizer
 *
 * fragment processing.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "../swr_internal.h"

#include "interpolators.h"
#include "fragment.h"
#include "sweep.h"

namespace rast
{

/** pixel center. */
const ml::vec2 pixel_center{0.5f, 0.5f};

/*
 * statically verify assumptions.
 */
static_assert(swr::fragment_shader_result::discard == 0, "swr::fragment_shader_result::discard needs to evaluate to the numerical value 0");
static_assert(swr::fragment_shader_result::accept == 1, "swr::fragment_shader_result::accept needs to evaluate to the numerical value 1");

/**
 * Generate fragment color values, generate color write mask, perform depth testing and depth writing.
 * Everything is performed with respect to the currently active draw target.
 *
 * More precisely, we do the following operations, in order:
 *
 *  1) Scissor test.
 *
 * If it succeeds, we calculate all interpolated values for the varyings.
 *
 *  2) Call the fragment shader.
 *  3) Depth test (note that this cannot be done earlier, since the fragment shader may modify the depth value).
 */
void sweep_rasterizer::process_fragment(
  int x,
  int y,
  const swr::impl::render_states& states,
  const swr::program_base* in_shader,
  float one_over_viewport_z,
  fragment_info& frag_info,
  swr::impl::fragment_output& out)
{
    const bool is_default_framebuffer = (states.draw_target == framebuffer);
    const int framebuffer_height = states.draw_target->properties.height;

    /*
     * Scissor test.
     */
    if(states.scissor_test_enabled)
    {
        const int x_min = states.scissor_box.x_min;
        const int x_max = states.scissor_box.x_max;
        int y_min{states.scissor_box.y_min};
        int y_max{states.scissor_box.y_max};

        // the default framebuffer needs a flip.
        if(is_default_framebuffer)
        {
            int y_temp = y_min;
            y_min = framebuffer_height - y_max;
            y_max = framebuffer_height - y_temp;
        }

        if(x < x_min || x >= x_max
           || y < y_min || y >= y_max)
        {
            out.write_flags = 0;
            return;
        }
    }

    // initialize write flags.
    std::uint32_t write_flags = swr::impl::fragment_output::fof_write_color;

    /* stencil buffering is currently unimplemented and the stencil mask is default-initialized to false. */

    /*
     * Compute z and interpolated values.
     *
     * Recall that one_over_viewport_z comes from the clip coordinates' w component. That is, it is
     * called w_c in eq. (15.1) on p.415 of https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf,
     * and with respect to the notation found there, we compute w_f here.
     */
    float z = 1.0f / one_over_viewport_z;
    for(std::size_t i = 0; i < states.shader_info->iqs.size(); ++i)
    {
        if(states.shader_info->iqs[i] == swr::interpolation_qualifier::smooth)
        {
            frag_info.varyings[i].value *= z;

            // FIXME these need to be calculated.
            frag_info.varyings[i].dFdx *= z;
            frag_info.varyings[i].dFdy *= z;
        }
    }

    /*
     * Execute the fragment shader.
     */
    // FIXME From docs: gl_PointCoord: contains the coordinate of a fragment within a point. currently undefined.

    /*
     * set up the output color attachments for the fragment shader. the default color is explicitly unspecified in OpenGL, and we
     * choose {0,0,0,1} for initialization. see e.g. https://stackoverflow.com/questions/29119097/glsl-default-value-for-output-color
     */
    ml::vec4 color{0, 0, 0, 1};
    float depth_value = frag_info.depth_value;

    /*
     * set up the fragment coordinate. this should match (15.1) on p. 415 in https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     * note that we need to reverse the y-axis for the default framebuffer.
     */
    ml::vec4 frag_coord;
    if(is_default_framebuffer)
    {
        frag_coord = {
          static_cast<float>(x) - pixel_center.x,
          framebuffer_height - (static_cast<float>(y) - pixel_center.y),
          depth_value,
          z};
    }
    else
    {
        frag_coord = {
          static_cast<float>(x) - pixel_center.x,
          static_cast<float>(y) - pixel_center.y,
          depth_value,
          z};
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_fragment_shader = 0;
    utils::clock(stage_fragment_shader);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    auto accept_fragment = in_shader->fragment_shader(
      frag_coord,
      frag_info.front_facing,
      {0, 0},
      frag_info.varyings,
      depth_value,
      color);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_fragment_shader);
    swr::impl::profile_fragment_shader_cycles.fetch_add(stage_fragment_shader, std::memory_order_relaxed);

    swr::impl::profile_fragment_shader_invocations.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if(accept_fragment == swr::discard)
    {
        out.write_flags = 0;
        return;
    }

    /*
     * Depth test.
     */
    bool depth_write_mask = true;
    if(states.depth_test_enabled)
    {
        depth_value = std::clamp(depth_value, 0.f, 1.f);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_depth = 0;
        utils::clock(stage_depth);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        states.draw_target->depth_compare_write(
          x,
          y,
          depth_value,
          states.depth_func,
          states.write_depth,
          depth_write_mask);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_depth);
        swr::impl::profile_depth_cycles.fetch_add(stage_depth, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    }

    auto to_mask = [](bool b) -> std::uint32_t
    { return ~(static_cast<std::uint32_t>(b) - 1); };

    out.color = color;
    out.write_flags = write_flags & to_mask(depth_write_mask);
}

/*
 * helpers.
 */

/** the same as above, but operates on 2x2 tiles. does not return any value. */
void sweep_rasterizer::process_fragment_block(
  int x,
  int y,
  const swr::impl::render_states& states,
  const swr::program_base* in_shader,
  const ml::vec4& one_over_viewport_z,
  std::array<fragment_info, 4>& frag_info,
  swr::impl::fragment_output_block& out)
{
    const bool is_default_framebuffer = (states.draw_target == framebuffer);
    const int framebuffer_height = states.draw_target->properties.height;

    // initialize masks.
    std::uint8_t depth_mask = 0b1111;
    depth_mask &= states.write_depth ? 0b1111 : 0;

    std::uint8_t write_color = 0b1111;
    std::uint8_t write_stencil = 0x0; /* unimplemented */

    /* stencil buffering is currently unimplemented and the stencil mask is default-initialized to false. */

    const int x0 = x;
    const int x1 = x + 1;
    const int y0 = y;
    const int y1 = y + 1;

    /*
     * Scissor test.
     */
    if(states.scissor_test_enabled)
    {
        const int x_min = states.scissor_box.x_min;
        const int x_max = states.scissor_box.x_max;
        int y_min{states.scissor_box.y_min};
        int y_max{states.scissor_box.y_max};

        // the default framebuffer needs a flip.
        if(is_default_framebuffer)
        {
            int y_temp = y_min;
            y_min = framebuffer_height - y_max;
            y_max = framebuffer_height - y_temp;
        }

        const std::uint8_t scissor_mask =
          (((x0 >= x_min && x0 < x_max && y0 >= y_min && y0 < y_max) ? 1 : 0) << 3)
          | (((x1 >= x_min && x1 < x_max && y0 >= y_min && y0 < y_max) ? 1 : 0) << 2)
          | (((x0 >= x_min && x0 < x_max && y1 >= y_min && y1 < y_max) ? 1 : 0) << 1)
          | ((x1 >= x_min && x1 < x_max && y1 >= y_min && y1 < y_max) ? 1 : 0);

        if(scissor_mask == 0)
        {
            // the mask only contains 'false'.
            out.write_color = 0;
            out.write_stencil = 0;

            return;
        }

        depth_mask &= scissor_mask;
        write_color &= scissor_mask;
        write_stencil &= scissor_mask;
    }

    /*
     * Compute z and interpolated values.
     */
#ifdef SWR_USE_SIMD
    alignas(utils::alignment::sse) std::array<float, 4> z;
    _mm_store_ps(
      z,
      _mm_div_ps(
        _mm_set_ps1(1.0f),
        _mm_set_ps(
          one_over_viewport_z[3],
          one_over_viewport_z[2],
          one_over_viewport_z[1],
          one_over_viewport_z[0])));
#else  /* SWR_USE_SIMD */
    const ml::vec4 z = ml::vec4::one() / ml::vec4(one_over_viewport_z);
#endif /* SWR_USE_SIMD */

    for(std::size_t i = 0; i < states.shader_info->iqs.size(); ++i)
    {
        if(states.shader_info->iqs[i] == swr::interpolation_qualifier::smooth)
        {
            frag_info[0].varyings[i].value *= z[0];
            frag_info[1].varyings[i].value *= z[1];
            frag_info[2].varyings[i].value *= z[2];
            frag_info[3].varyings[i].value *= z[3];

            /*
             * calculate the approximate derivatives for this quad.
             */

            frag_info[0].varyings[i].dFdx = frag_info[1].varyings[i].value - frag_info[0].varyings[i].value;
            frag_info[1].varyings[i].dFdx = frag_info[0].varyings[i].dFdx;
            frag_info[2].varyings[i].dFdx = frag_info[3].varyings[i].value - frag_info[2].varyings[i].value;
            frag_info[3].varyings[i].dFdx = frag_info[2].varyings[i].dFdx;

            frag_info[0].varyings[i].dFdy = frag_info[2].varyings[i].value - frag_info[0].varyings[i].value;
            frag_info[1].varyings[i].dFdy = frag_info[3].varyings[i].value - frag_info[1].varyings[i].value;
            frag_info[2].varyings[i].dFdy = frag_info[0].varyings[i].dFdy;
            frag_info[3].varyings[i].dFdy = frag_info[1].varyings[i].dFdy;
        }
    }

    /*
     * Execute the fragment shader.
     */
    // FIXME From docs: gl_PointCoord: contains the coordinate of a fragment within a point. currently undefined.

    /*
     * set up the output color attachments for the fragment shader. the default color is explicitly unspecified in OpenGL, and we
     * choose {0,0,0,1} for initialization. see e.g. https://stackoverflow.com/questions/29119097/glsl-default-value-for-output-color
     */

    std::array<ml::vec4, 4> color =
      {{{0, 0, 0, 1},
        {0, 0, 0, 1},
        {0, 0, 0, 1},
        {0, 0, 0, 1}}};
    alignas(utils::alignment::sse) std::array<float, 4> depth_value = {
      frag_info[0].depth_value,
      frag_info[1].depth_value,
      frag_info[2].depth_value,
      frag_info[3].depth_value};

    /*
     * set up the fragment coordinate. this should match (15.1) on p. 415 in https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     * note that we need to reverse the y-axis.
     */
    const float fx0 = static_cast<float>(x0) - pixel_center.x;
    const float fx1 = static_cast<float>(x1) - pixel_center.x;
    const float fy0 = static_cast<float>(y0) - pixel_center.y;
    const float fy1 = static_cast<float>(y1) - pixel_center.y;
    std::array<ml::vec4, 4> frag_coord = {
      ml::vec4{fx0, fy0, depth_value[0], z[0]},
      ml::vec4{fx1, fy0, depth_value[1], z[1]},
      ml::vec4{fx0, fy1, depth_value[2], z[2]},
      ml::vec4{fx1, fy1, depth_value[3], z[3]}};

    if(is_default_framebuffer)
    {
        frag_coord[0].y = framebuffer_height - frag_coord[0].y;
        frag_coord[1].y = framebuffer_height - frag_coord[1].y;
        frag_coord[2].y = framebuffer_height - frag_coord[2].y;
        frag_coord[3].y = framebuffer_height - frag_coord[3].y;
    }

    std::uint8_t accept_mask = 0;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_fragment_shader = 0;
    utils::clock(stage_fragment_shader);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    accept_mask |= in_shader->fragment_shader(frag_coord[0], frag_info[0].front_facing, {0, 0}, frag_info[0].varyings, depth_value[0], color[0]) << 3;
    accept_mask |= in_shader->fragment_shader(frag_coord[1], frag_info[1].front_facing, {0, 0}, frag_info[1].varyings, depth_value[1], color[1]) << 2;
    accept_mask |= in_shader->fragment_shader(frag_coord[2], frag_info[2].front_facing, {0, 0}, frag_info[2].varyings, depth_value[2], color[2]) << 1;
    accept_mask |= in_shader->fragment_shader(frag_coord[3], frag_info[3].front_facing, {0, 0}, frag_info[3].varyings, depth_value[3], color[3]);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_fragment_shader);
    swr::impl::profile_fragment_shader_cycles.fetch_add(stage_fragment_shader, std::memory_order_relaxed);

    swr::impl::profile_fragment_shader_invocations.fetch_add(4, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if(accept_mask == 0)
    {
        out.write_color = 0;
        out.write_stencil = 0;

        return;
    }

    depth_mask &= accept_mask;
    write_color &= accept_mask;
    write_stencil &= accept_mask;

    /*
     * Depth test.
     */
    if(states.depth_test_enabled)
    {
#ifdef SWR_USE_SIMD
        _mm_store_ps(depth_value, _mm_min_ps(_mm_max_ps(_mm_load_ps(depth_value), _mm_set_ps1(0.0f)), _mm_set_ps1(1.0f)));
#else  /* SWR_USE_SIMD */
        depth_value[0] = std::clamp(depth_value[0], 0.f, 1.f);
        depth_value[1] = std::clamp(depth_value[1], 0.f, 1.f);
        depth_value[2] = std::clamp(depth_value[2], 0.f, 1.f);
        depth_value[3] = std::clamp(depth_value[3], 0.f, 1.f);
#endif /* SWR_USE_SIMD */

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_depth = 0;
        utils::clock(stage_depth);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        states.draw_target->depth_compare_write_block(
          x, y,
          depth_value,
          states.depth_func,
          states.write_depth,
          depth_mask);
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_depth);
        swr::impl::profile_depth_cycles.fetch_add(stage_depth, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    }

    write_color &= depth_mask;
    write_stencil &= depth_mask;

    // copy color and masks into output
    out.color[0] = color[0];
    out.color[1] = color[1];
    out.color[2] = color[2];
    out.color[3] = color[3];

    out.write_color = write_color;
    out.write_stencil = write_stencil;
}

void sweep_rasterizer::process_fragment_block(
  int x,
  int y,
  std::uint8_t mask,
  const swr::impl::render_states& states,
  const swr::program_base* in_shader,
  const ml::vec4& one_over_viewport_z,
  std::array<fragment_info, 4>& frag_info,
  swr::impl::fragment_output_block& out)
{
    const bool is_default_framebuffer = (states.draw_target == framebuffer);
    const int framebuffer_height = states.draw_target->properties.height;

    std::uint8_t active_mask = mask;

    // initialize masks.
    std::uint8_t depth_mask = active_mask;
    depth_mask &= states.write_depth ? 0b1111 : 0;

    std::uint8_t write_color = active_mask;
    std::uint8_t write_stencil = 0x0; /* unimplemented */

    /* stencil buffering is currently unimplemented and the stencil mask is default-initialized to 0. */

    const int x0 = x;
    const int x1 = x + 1;
    const int y0 = y;
    const int y1 = y + 1;

    /*
     * Scissor test.
     */
    if(states.scissor_test_enabled)
    {
        const int x_min = states.scissor_box.x_min;
        const int x_max = states.scissor_box.x_max;
        int y_min{states.scissor_box.y_min};
        int y_max{states.scissor_box.y_max};

        // the default framebuffer needs a flip.
        if(is_default_framebuffer)
        {
            int y_temp = y_min;
            y_min = framebuffer_height - y_max;
            y_max = framebuffer_height - y_temp;
        }

        const std::uint8_t scissor_mask =
          (((x0 >= x_min && x0 < x_max && y0 >= y_min && y0 < y_max) ? 1 : 0) << 3)
          | (((x1 >= x_min && x1 < x_max && y0 >= y_min && y0 < y_max) ? 1 : 0) << 2)
          | (((x0 >= x_min && x0 < x_max && y1 >= y_min && y1 < y_max) ? 1 : 0) << 1)
          | ((x1 >= x_min && x1 < x_max && y1 >= y_min && y1 < y_max) ? 1 : 0);

        if(scissor_mask == 0)
        {
            // the mask only contains 'false'.
            out.write_color = 0;
            out.write_stencil = 0;

            return;
        }

        active_mask &= scissor_mask;
        if(active_mask == 0)
        {
            out.write_color = 0;
            out.write_stencil = 0;
            return;
        }

        depth_mask &= active_mask;
        write_color &= active_mask;
        write_stencil &= active_mask;
    }

    /*
     * Compute z and interpolated values.
     */
#ifdef SWR_USE_SIMD
    alignas(utils::alignment::sse) std::array<float, 4> z;
    _mm_store_ps(
      z,
      _mm_div_ps(
        _mm_set_ps1(1.0f),
        _mm_set_ps(
          one_over_viewport_z[3],
          one_over_viewport_z[2],
          one_over_viewport_z[1],
          one_over_viewport_z[0])));
#else  /* SWR_USE_SIMD */
    const ml::vec4 z = ml::vec4::one() / ml::vec4(one_over_viewport_z);
#endif /* SWR_USE_SIMD */

    for(std::size_t i = 0; i < states.shader_info->iqs.size(); ++i)
    {
        if(states.shader_info->iqs[i] == swr::interpolation_qualifier::smooth)
        {
            frag_info[0].varyings[i].value *= z[0];
            frag_info[1].varyings[i].value *= z[1];
            frag_info[2].varyings[i].value *= z[2];
            frag_info[3].varyings[i].value *= z[3];

            /*
             * calculate the approximate derivatives for this quad.
             */

            frag_info[0].varyings[i].dFdx = frag_info[1].varyings[i].value - frag_info[0].varyings[i].value;
            frag_info[1].varyings[i].dFdx = frag_info[0].varyings[i].dFdx;
            frag_info[2].varyings[i].dFdx = frag_info[3].varyings[i].value - frag_info[2].varyings[i].value;
            frag_info[3].varyings[i].dFdx = frag_info[2].varyings[i].dFdx;

            frag_info[0].varyings[i].dFdy = frag_info[2].varyings[i].value - frag_info[0].varyings[i].value;
            frag_info[1].varyings[i].dFdy = frag_info[3].varyings[i].value - frag_info[1].varyings[i].value;
            frag_info[2].varyings[i].dFdy = frag_info[0].varyings[i].dFdy;
            frag_info[3].varyings[i].dFdy = frag_info[1].varyings[i].dFdy;
        }
    }

    /*
     * Execute the fragment shader.
     */
    // FIXME From docs: gl_PointCoord: contains the coordinate of a fragment within a point. currently undefined.

    /*
     * set up the output color attachments for the fragment shader. the default color is explicitly unspecified in OpenGL, and we
     * choose {0,0,0,1} for initialization. see e.g. https://stackoverflow.com/questions/29119097/glsl-default-value-for-output-color
     */

    std::array<ml::vec4, 4> color =
      {{{0, 0, 0, 1},
        {0, 0, 0, 1},
        {0, 0, 0, 1},
        {0, 0, 0, 1}}};
    alignas(utils::alignment::sse) std::array<float, 4> depth_value = {
      frag_info[0].depth_value,
      frag_info[1].depth_value,
      frag_info[2].depth_value,
      frag_info[3].depth_value};

    /*
     * set up the fragment coordinate. this should match (15.1) on p. 415 in https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf.
     * note that we need to reverse the y-axis.
     */
    const float fx0 = static_cast<float>(x0) - pixel_center.x;
    const float fx1 = static_cast<float>(x1) - pixel_center.x;
    const float fy0 = static_cast<float>(y0) - pixel_center.y;
    const float fy1 = static_cast<float>(y1) - pixel_center.y;
    std::uint8_t accept_mask = 0;

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_fragment_shader = 0;
    utils::clock(stage_fragment_shader);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    auto make_frag_coord = [&](int lane) -> ml::vec4
    {
        float fx = fx0;
        float fy = fy0;
        switch(lane)
        {
        case 1:
            fx = fx1;
            fy = fy0;
            break;
        case 2:
            fx = fx0;
            fy = fy1;
            break;
        case 3:
            fx = fx1;
            fy = fy1;
            break;
        default:
            break;
        }

        if(is_default_framebuffer)
        {
            fy = framebuffer_height - fy;
        }

        return {fx, fy, depth_value[lane], z[lane]};
    };

    if(active_mask == 0b1111)
    {
        ml::vec4 frag_coord0 = make_frag_coord(0);
        ml::vec4 frag_coord1 = make_frag_coord(1);
        ml::vec4 frag_coord2 = make_frag_coord(2);
        ml::vec4 frag_coord3 = make_frag_coord(3);

        accept_mask |= in_shader->fragment_shader(frag_coord0, frag_info[0].front_facing, {0, 0}, frag_info[0].varyings, depth_value[0], color[0]) << 3;
        accept_mask |= in_shader->fragment_shader(frag_coord1, frag_info[1].front_facing, {0, 0}, frag_info[1].varyings, depth_value[1], color[1]) << 2;
        accept_mask |= in_shader->fragment_shader(frag_coord2, frag_info[2].front_facing, {0, 0}, frag_info[2].varyings, depth_value[2], color[2]) << 1;
        accept_mask |= in_shader->fragment_shader(frag_coord3, frag_info[3].front_facing, {0, 0}, frag_info[3].varyings, depth_value[3], color[3]);
    }
    else
    {
        if(active_mask & 8)
        {
            ml::vec4 frag_coord0 = make_frag_coord(0);
            accept_mask |= in_shader->fragment_shader(frag_coord0, frag_info[0].front_facing, {0, 0}, frag_info[0].varyings, depth_value[0], color[0]) << 3;
        }
        if(active_mask & 4)
        {
            ml::vec4 frag_coord1 = make_frag_coord(1);
            accept_mask |= in_shader->fragment_shader(frag_coord1, frag_info[1].front_facing, {0, 0}, frag_info[1].varyings, depth_value[1], color[1]) << 2;
        }
        if(active_mask & 2)
        {
            ml::vec4 frag_coord2 = make_frag_coord(2);
            accept_mask |= in_shader->fragment_shader(frag_coord2, frag_info[2].front_facing, {0, 0}, frag_info[2].varyings, depth_value[2], color[2]) << 1;
        }
        if(active_mask & 1)
        {
            ml::vec4 frag_coord3 = make_frag_coord(3);
            accept_mask |= in_shader->fragment_shader(frag_coord3, frag_info[3].front_facing, {0, 0}, frag_info[3].varyings, depth_value[3], color[3]);
        }
    }
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_fragment_shader);
    swr::impl::profile_fragment_shader_cycles.fetch_add(stage_fragment_shader, std::memory_order_relaxed);

    const std::uint64_t fragment_invocations =
      ((active_mask & 8) ? 1u : 0u)
      + ((active_mask & 4) ? 1u : 0u)
      + ((active_mask & 2) ? 1u : 0u)
      + ((active_mask & 1) ? 1u : 0u);
    swr::impl::profile_fragment_shader_invocations.fetch_add(fragment_invocations, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if(accept_mask == 0)
    {
        out.write_color = 0;
        out.write_stencil = 0;

        return;
    }

    depth_mask &= accept_mask;
    write_color &= accept_mask;
    write_stencil &= accept_mask;

    /*
     * Depth test.
     */
    if(states.depth_test_enabled)
    {
#ifdef SWR_USE_SIMD
        _mm_store_ps(
          depth_value,
          _mm_min_ps(
            _mm_max_ps(
              _mm_load_ps(depth_value),
              _mm_set_ps1(0.0f)),
            _mm_set_ps1(1.0f)));
#else  /* SWR_USE_SIMD */
        depth_value[0] = std::clamp(depth_value[0], 0.f, 1.f);
        depth_value[1] = std::clamp(depth_value[1], 0.f, 1.f);
        depth_value[2] = std::clamp(depth_value[2], 0.f, 1.f);
        depth_value[3] = std::clamp(depth_value[3], 0.f, 1.f);
#endif /* SWR_USE_SIMD */

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t stage_depth = 0;
        utils::clock(stage_depth);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        states.draw_target->depth_compare_write_block(x, y, depth_value, states.depth_func, states.write_depth, depth_mask);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_depth);
        swr::impl::profile_depth_cycles.fetch_add(stage_depth, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    }

    write_color &= depth_mask;
    write_stencil &= depth_mask;

    // copy color and masks into output
    out.color[0] = color[0];
    out.color[1] = color[1];
    out.color[2] = color[2];
    out.color[3] = color[3];

    out.write_color = write_color;
    out.write_stencil = write_stencil;
}

} /* namespace rast */
