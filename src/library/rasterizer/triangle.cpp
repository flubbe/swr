/**
 * swr - a software rasterizer
 *
 * triangle rasterization.
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
#include "triangle.h"
#include "block.h"

namespace rast
{

void sweep_rasterizer::process_block(unsigned int block_x, unsigned int block_y, tile_info& in_data)
{
    std::array<
      boost::container::static_vector<
        swr::varying,
        geom::limits::max::varyings>,
      4>
      temp_varyings;

    const bool front_facing = in_data.front_facing;

    ml::vec4 frag_depth;
    ml::vec4 one_over_viewport_z;
    swr::impl::fragment_output_block out;

    for_each_quad_in_triangle_block(
      block_x,
      block_y,
      in_data.attributes,
      [&](unsigned int x,
          unsigned int y,
          rast::triangle_interpolator& attributes_quad)
      {
          temp_varyings[0].clear();
          temp_varyings[1].clear();
          temp_varyings[2].clear();
          temp_varyings[3].clear();

          attributes_quad.get_data_block(
            temp_varyings,
            frag_depth,
            one_over_viewport_z);

          std::array<rast::fragment_info, 4> frag_info =
            {{{frag_depth[0], front_facing, temp_varyings[0]},
              {frag_depth[1], front_facing, temp_varyings[1]},
              {frag_depth[2], front_facing, temp_varyings[2]},
              {frag_depth[3], front_facing, temp_varyings[3]}}};

          process_fragment_block(
            x, y,
            *in_data.states,
            in_data.shader,
            one_over_viewport_z,
            frag_info,
            out);

          in_data.states->draw_target->merge_color_block(
            0,
            x, y,
            out,
            in_data.states->blending_enabled,
            in_data.states->blend_src,
            in_data.states->blend_dst);
      });
}

void sweep_rasterizer::process_block_checked(
  unsigned int block_x,
  unsigned int block_y,
  tile_info& in_data)
{
    std::array<
      boost::container::static_vector<
        swr::varying,
        geom::limits::max::varyings>,
      4>
      temp_varyings;

    const bool front_facing = in_data.front_facing;

    ml::vec4 frag_depth;
    ml::vec4 one_over_viewport_z;
    swr::impl::fragment_output_block out;

    for_each_covered_quad_in_checked_triangle_block(
      block_x,
      block_y,
      in_data.lambdas,
      in_data.attributes,
      [&](int x,
          int y,
          int mask,
          rast::triangle_interpolator& attributes_quad)
      {
          temp_varyings[0].clear();
          temp_varyings[1].clear();
          temp_varyings[2].clear();
          temp_varyings[3].clear();

          attributes_quad.get_data_block(
            temp_varyings,
            frag_depth,
            one_over_viewport_z);

          std::array<rast::fragment_info, 4> frag_info =
            {{{frag_depth[0], front_facing, temp_varyings[0]},
              {frag_depth[1], front_facing, temp_varyings[1]},
              {frag_depth[2], front_facing, temp_varyings[2]},
              {frag_depth[3], front_facing, temp_varyings[3]}}};

          process_fragment_block(
            x,
            y,
            mask,
            *in_data.states,
            in_data.shader,
            one_over_viewport_z,
            frag_info,
            out);

          in_data.states->draw_target->merge_color_block(
            0,
            x,
            y,
            out,
            in_data.states->blending_enabled,
            in_data.states->blend_src,
            in_data.states->blend_dst);
      });
}

/**
 * Apply depth offset to triangle vertices.
 *
 * FIXME We do the setup for floating-point depth buffers here, but we probably want the fixed-point version.
 *
 * Ref: https://registry.khronos.org/OpenGL/specs/gl/glspec43.core.pdf, Section 14.6.5.
 */
static float setup_polygon_offset(
  const swr::impl::render_states& states,
  const geom::vertex& v1,
  const geom::vertex& v2,
  const geom::vertex& v3,
  float inv_area)
{
    ml::vec3 edges[2] = {
      (v2.coords - v1.coords).xyz(),
      (v3.coords - v1.coords).xyz()};    // edges in window coordinates
    ml::vec2 dz = ml::vec2{
                    edges[1].z * edges[0].y - edges[0].z * edges[1].y,
                    -edges[1].z * edges[0].x + edges[0].z * edges[1].x}
                  * inv_area;

#ifdef __GNUC__
    float m = std::max(fabsf(dz.x), fabsf(dz.y));    // Eq. (14.12)
#else
    float m = std::max(std::fabsf(dz.x), std::fabsf(dz.y));    // Eq. (14.12)
#endif

    /*
     * https://registry.khronos.org/OpenGL/specs/gl/glspec43.core.pdf, Section 14.6.5,
     * on floating-point depth buffers:
     *
     *     "In this case, the minimum resolvable difference for a given polygon is
     *      dependent on the maximum exponent, e, in the range of z values spanned
     *      by the primitive. If n is the number of bits in the floating-point mantissa,
     *      the minimum resolvable difference, r, for the given primitive is defined as
     *      r = 2^(e−n)."
     *
     * A 32-bit float has a 23-bit mantissa.
     */
    union float_integer
    {
        float f;
        std::int32_t i;
        std::uint32_t ui;

        float_integer(float in_f)
        : f{in_f}
        {
        }
    };
    // get the maximum exponent in the range of the z values spanned by the primitive
#ifdef __GNUC__
    float_integer r{
      std::max({fabsf(v1.coords.z), fabsf(v2.coords.z), fabsf(v3.coords.z)})};
#else
    float_integer r{
      std::max({std::fabsf(v1.coords.z), std::fabsf(v2.coords.z), std::fabsf(v3.coords.z)})};
#endif
    r.i &= 0xff << 23;

    // calculate r by subtracting the size of mantissa from exponent
    r.ui -= 23 << 23;

    // clamp to zero (this means no resolvable depth offset for very small numbers)
    r.i = std::max(r.i, 0);

    return m * states.polygon_offset_factor + r.f * states.polygon_offset_units;    // Eq. (14.13)
}

void sweep_rasterizer::draw_filled_triangle(
  const swr::impl::render_states& states,
  bool is_front_facing,
  const geom::vertex& v0,
  const geom::vertex& v1,
  const geom::vertex& v2)
{
    triangle_info info = setup_triangle(v0, v1, v2);
    if(info.is_degenerate)
    {
        return;
    }

    float polygon_offset = 0.f;
    if(states.polygon_offset_fill_enabled)
    {
        polygon_offset = setup_polygon_offset(states, v0, v1, v2, info.inv_area);
    }

    const bool y_needs_flip = states.draw_target == framebuffer;

    for_each_covered_triangle_block(
      states,
      info,
      v0.varyings,
      polygon_offset,
      y_needs_flip,
      [&](int x,
          int y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes_row,
          tile_info::rasterization_mode mode)
      {
          if(tiles.add_triangle(x, y, {&states, lambdas_box, attributes_row, is_front_facing, mode}))
          {
              process_tile_cache();
          }
      });
}

} /* namespace rast */