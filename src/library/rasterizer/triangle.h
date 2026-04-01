/**
 * swr - a software rasterizer
 *
 * Implements Direct3D triangle rasterization.
 *
 * Some reference (on triangle rasterization and software rasterization in general):
 *
 * [1] http://www.scratchapixel.com/lessons/3d-basic-rendering/rasterization-practical-implementation/rasterization-stage
 * [2] http://forum.devmaster.net/t/advanced-rasterization/6145
 * [3] https://fgiesen.wordpress.com/2013/02/08/triangle-rasterization-in-practice/
 * [4] Pineda, “A Parallel Algorithm for Polygon Rasterization”, https://people.csail.mit.edu/ericchan/bib/pdf/p17-pineda.pdf
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "../swr_internal.h"

namespace rast
{

/** triangel setup info. */
struct triangle_info
{
    bool is_degenerate{true};

    // vertices normalized to CW raster order
    const geom::vertex* v0{nullptr};
    const geom::vertex* v1{nullptr};
    const geom::vertex* v2{nullptr};

    // 2D coords in the same normalized order
    ml::vec2 v0_xy;
    ml::vec2 v1_xy;
    ml::vec2 v2_xy;

    float area{0.0f};
    float inv_area{0.0f};

    ml::vec2_fixed<4> v0_xy_fix;
    ml::vec2_fixed<4> v1_xy_fix;
    ml::vec2_fixed<4> v2_xy_fix;

    geom::edge_function_fixed edges_fix[3] = {
      {{}, {}}, {{}, {}}, {{}, {}}};
};

inline triangle_info setup_triangle(
  const geom::vertex& v0,
  const geom::vertex& v1,
  const geom::vertex& v2)
{
    triangle_info info{};

    // calculate the (signed) parallelogram area spanned by the difference vectors.
    auto p0 = v0.coords.xy();
    auto p1 = v1.coords.xy();
    auto p2 = v2.coords.xy();

    auto area = (p1 - p0).area(p2 - p0);

    if(ml::fixed_24_8_t(area) == 0)
    {
        info.is_degenerate = true;
        return info;
    }
    info.is_degenerate = false;

    /*
     * To simplify the rasterization code, we only want to consider CW triangles with respect to the coordinate system
     *
     *     +---->  X
     *     |
     *   Y |
     *     V
     *
     * If a triangle is set up this way, we can check for a sign of the "fixed-point barycentric coordinates" below
     * (instead of checking that all of them have the same sign).
     */

    if(area > 0.0f)
    {
        // keep vertex order
        info.v0 = &v0;
        info.v1 = &v1;
        info.v2 = &v2;

        info.v0_xy = p0;
        info.v1_xy = p1;
        info.v2_xy = p2;

        info.area = area;
    }
    else /* area < 0, since we already checked for area==0 */
    {
        // change vertex order.

        info.v0 = &v1;
        info.v1 = &v0;
        info.v2 = &v2;

        info.v0_xy = p1;
        info.v1_xy = p0;
        info.v2_xy = p2;

        info.area = -area;
    }

    info.inv_area = 1.0f / info.area;

    // convert triangle coordinates into a fixed-point representation with 4-bit subpixel precision.
    info.v0_xy_fix = ml::vec2_fixed<4>(info.v0_xy.x, info.v0_xy.y);
    info.v1_xy_fix = ml::vec2_fixed<4>(info.v1_xy.x, info.v1_xy.y);
    info.v2_xy_fix = ml::vec2_fixed<4>(info.v2_xy.x, info.v2_xy.y);

    // list all edge in fixed point to use them for checking if a particular
    // pixel lies inside the triangle. the order of the edges does not matter,
    // but their orientation does.
    info.edges_fix[0] = geom::edge_function_fixed{info.v0_xy_fix, info.v1_xy_fix};
    info.edges_fix[1] = geom::edge_function_fixed{info.v1_xy_fix, info.v2_xy_fix};
    info.edges_fix[2] = geom::edge_function_fixed{info.v2_xy_fix, info.v0_xy_fix};

    /*
     * Fill Rules.
     *
     * We implement the top-left rule. Here, a pixel is drawn if its center lies entirely
     * inside the triangle or on a top edge or a left edge, where:
     *
     * i)  A top edge is an edge that is above all other edges and exactly horizontal.
     * ii) A left edge, is an edge that is not exactly horizontal and is on the left side of the triangle.
     *
     * References:
     *
     * [1] https://fgiesen.wordpress.com/2013/02/08/triangle-rasterization-in-practice/
     * [2] https://msdn.microsoft.com/en-us/library/windows/desktop/cc627092(v=vs.85).aspx
     */

    /*
     * Check for a top and left edges. For this, we suppose that our coordinate system is given by:
     *
     *     +---->  X
     *     |
     *   Y |
     *     V
     *
     * Thus, the Y coordinates are increasing towards the bottom, and the X coordinates are increasing
     * to the right. The origin is located at the upper-left corner of the viewport.
     *
     * Note that this coordinate system may or may not correspond to the actual rendered output.
     */
    for(int i = 0; i < 3; ++i)
    {
        // Top edge test.
        //
        // 'exactly horizontal' implies that the y coordinate does not change. Since the triangle's vertices are
        // wound CW, the top edge is determined by checking that its x-direction is positive.
        if(info.edges_fix[i].v_diff.y == 0
           && info.edges_fix[i].v_diff.x > 0)
        {
            info.edges_fix[i].c += cnl::wrap<ml::fixed_24_8_t>(FILL_RULE_EDGE_BIAS);
        }
        // Left edge test.
        //
        // In a CW triangle, a left edge goes up, i.e. its endpoint is strictly above its starting point.
        // In terms of the y coordinate, the difference vector has to be strictly negative.
        else if(info.edges_fix[i].v_diff.y < 0)
        {
            info.edges_fix[i].c += cnl::wrap<ml::fixed_24_8_t>(FILL_RULE_EDGE_BIAS);
        }
        else
        {
            // Here we have either a bottom edge or a right edge. Thus, we intentionally do nothing.
        }
    }

    return info;
}

/** triangle bounding box. */
struct bounding_box
{
    int start_x, start_y;
    int end_x, end_y;
};

/** compute block-aligned triangle bounds in viewport coordinates. */
bounding_box compute_triangle_bounds(
  const swr::impl::render_states& states,
  const triangle_info& info,
  bool y_needs_flip)
{
    // take scissor box into account.
    int x_min = 0;
    int x_max = states.draw_target->properties.width;
    int y_min = 0;
    int y_max = states.draw_target->properties.height;

    if(states.scissor_test_enabled)
    {
        x_min = std::max(states.scissor_box.x_min, 0);
        x_max = std::min(states.scissor_box.x_max, states.draw_target->properties.width);

        y_min = std::max(states.scissor_box.y_min, 0);
        y_max = std::min(states.scissor_box.y_max, states.draw_target->properties.height);

        if(y_needs_flip)
        {
            const int y_temp = y_min;
            y_min = states.draw_target->properties.height - y_max;
            y_max = states.draw_target->properties.height - y_temp;
        }
    }

    auto v0x = ml::truncate_unchecked(info.v0_xy.x);
    auto v0y = ml::truncate_unchecked(info.v0_xy.y);
    auto v1x = ml::truncate_unchecked(info.v1_xy.x);
    auto v1y = ml::truncate_unchecked(info.v1_xy.y);
    auto v2x = ml::truncate_unchecked(info.v2_xy.x);
    auto v2y = ml::truncate_unchecked(info.v2_xy.y);

    return {
      .start_x = swr::impl::lower_align_on_block_size(std::max(std::min({v0x, v1x, v2x}), x_min)),
      .start_y = swr::impl::lower_align_on_block_size(std::max(std::min({v0y, v1y, v2y}), y_min)),
      .end_x = swr::impl::upper_align_on_block_size(std::min(std::max({v0x + 1, v1x + 1, v2x + 1}), x_max)),
      .end_y = swr::impl::upper_align_on_block_size(std::min(std::max({v0y + 1, v1y + 1, v2y + 1}), y_max))};
}

template<class F>
inline void for_each_covered_triangle_block(
  const swr::impl::render_states& states,
  const triangle_info& info,
  float polygon_offset,
  bool y_needs_flip,
  F&& f)
{
    const bounding_box bounds = compute_triangle_bounds(
      states,
      info,
      y_needs_flip);

    const auto start_coord = ml::vec2_fixed<4>{
      ml::fixed_28_4_t{bounds.start_x} + ml::fixed_28_4_t{0.5f},
      ml::fixed_28_4_t{bounds.start_y} + ml::fixed_28_4_t{0.5f}};

    geom::linear_interpolator_2d<ml::fixed_24_8_t> lambda_row_top_left[3] = {
      {{-info.edges_fix[0].evaluate(start_coord)},
       {-info.edges_fix[0].get_change_x(),
        -info.edges_fix[0].get_change_y()}},
      {{-info.edges_fix[1].evaluate(start_coord)},
       {-info.edges_fix[1].get_change_x(),
        -info.edges_fix[1].get_change_y()}},
      {{-info.edges_fix[2].evaluate(start_coord)},
       {-info.edges_fix[2].get_change_x(),
        -info.edges_fix[2].get_change_y()}}};

    const ml::vec2 screen_coords{
      static_cast<float>(bounds.start_x) + 0.5f,
      static_cast<float>(bounds.start_y) + 0.5f};

    rast::triangle_interpolator attributes{
      screen_coords,
      info.v0->coords, info.v1->coords, info.v2->coords,
      info.v0->varyings, info.v1->varyings, info.v2->varyings, info.v0->varyings,
      states.shader_info->iqs, info.inv_area, polygon_offset};

    for(auto y = bounds.start_y; y < bounds.end_y; y += swr::impl::rasterizer_block_size)
    {
        geom::barycentric_coordinate_block lambdas_box{
          lambda_row_top_left[0].value, lambda_row_top_left[0].step,
          lambda_row_top_left[1].value, lambda_row_top_left[1].step,
          lambda_row_top_left[2].value, lambda_row_top_left[2].step};
        lambdas_box.setup(
          swr::impl::rasterizer_block_size,
          swr::impl::rasterizer_block_size);

        rast::triangle_interpolator attributes_row = attributes;

        for(auto x = bounds.start_x; x < bounds.end_x; x += swr::impl::rasterizer_block_size)
        {
            int mask = lambdas_box.get_coverage_mask();
            if(mask)
            {
                mask = geom::reduce_coverage_mask(mask);

                static_assert(static_cast<int>(tile_info::rasterization_mode::block) == 0);
                static_assert(static_cast<int>(tile_info::rasterization_mode::checked) == 1);

                const auto mode =
                  static_cast<tile_info::rasterization_mode>(static_cast<int>(mask != 0xf));

                f(x, y, lambdas_box, attributes_row, mode);
            }

            lambdas_box.step_x(swr::impl::rasterizer_block_size);
            attributes_row.advance_x(swr::impl::rasterizer_block_size);
        }

        lambda_row_top_left[0].step_y(swr::impl::rasterizer_block_size);
        lambda_row_top_left[1].step_y(swr::impl::rasterizer_block_size);
        lambda_row_top_left[2].step_y(swr::impl::rasterizer_block_size);

        attributes.advance_y(swr::impl::rasterizer_block_size);
    }
}

}    // namespace rast
