/**
 * swr - a software rasterizer
 *
 * triangle rasterization.
 *
 * some reference (on triangle rasterization and software rasterization in general):
 *
 * [1] http://www.scratchapixel.com/lessons/3d-basic-rendering/rasterization-practical-implementation/rasterization-stage
 * [2] http://forum.devmaster.net/t/advanced-rasterization/6145
 * [3] https://fgiesen.wordpress.com/2013/02/08/triangle-rasterization-in-practice/
 * [4] Pineda, “A Parallel Algorithm for Polygon Rasterization”, https://people.csail.mit.edu/ericchan/bib/pdf/p17-pineda.pdf
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "../swr_internal.h"

#include "interpolators.h"
#include "fragment.h"
#include "sweep.h"

namespace rast
{

void sweep_rasterizer::process_block(unsigned int block_x, unsigned int block_y, tile_info& in_data)
{
    boost::container::static_vector<swr::varying, geom::limits::max::varyings> temp_varyings[4];

    const auto varying_count = in_data.attributes.varyings.size();
    temp_varyings[0].resize(varying_count);
    temp_varyings[1].resize(varying_count);
    temp_varyings[2].resize(varying_count);
    temp_varyings[3].resize(varying_count);

    const bool front_facing = in_data.front_facing;

    const auto end_x = block_x + swr::impl::rasterizer_block_size;
    const auto end_y = block_y + swr::impl::rasterizer_block_size;

    // process block.
    for(unsigned int y = block_y; y < end_y; y += 2)
    {
        for(unsigned int x = block_x; x < end_x; x += 2)
        {
            in_data.attributes.get_varyings_block(temp_varyings);

            float frag_depth[4];
            in_data.attributes.get_depth_block(frag_depth);

            float one_over_viewport_z[4];
            in_data.attributes.get_one_over_viewport_z_block(one_over_viewport_z);

            rast::fragment_info frag_info[4] = {
              {frag_depth[0], front_facing, temp_varyings[0]},
              {frag_depth[1], front_facing, temp_varyings[1]},
              {frag_depth[2], front_facing, temp_varyings[2]},
              {frag_depth[3], front_facing, temp_varyings[3]}};
            swr::impl::fragment_output_block out;

            process_fragment_block(x, y, *in_data.states, one_over_viewport_z, frag_info, out);
            in_data.states->draw_target->merge_color_block(0, x, y, out, in_data.states->blending_enabled, in_data.states->blend_src, in_data.states->blend_dst);

            in_data.attributes.advance_x(2);
        }
        in_data.attributes.advance_y(2);
    }
}

void sweep_rasterizer::process_block_checked(unsigned int block_x, unsigned int block_y, tile_info& in_data)
{
    boost::container::static_vector<swr::varying, geom::limits::max::varyings> temp_varyings_block[4];

    const auto varying_count = in_data.attributes.varyings.size();
    temp_varyings_block[0].resize(varying_count);
    temp_varyings_block[1].resize(varying_count);
    temp_varyings_block[2].resize(varying_count);
    temp_varyings_block[3].resize(varying_count);

    const bool front_facing = in_data.front_facing;

    const auto end_x = block_x + swr::impl::rasterizer_block_size;
    const auto end_y = block_y + swr::impl::rasterizer_block_size;

    // set up barycentric coordinates for 2x2 blocks.
    geom::barycentric_coordinate_block lambdas = in_data.lambdas;
    lambdas.setup(1, 1);

    float frag_depth_block[4];
    float one_over_viewport_z_block[4];

    /*
     * process in 2x2 blocks.
     */
    for(unsigned int y = block_y; y < end_y; y += 2)
    {
        geom::barycentric_coordinate_block::fixed_24_8_array_4 row_start[3];
        lambdas.store_position(row_start[0], row_start[1], row_start[2]);

        for(unsigned int x = block_x; x < end_x; x += 2)
        {
            // get reduced coverage mask.
            int mask = geom::reduce_coverage_mask(lambdas.get_coverage_mask());

            if(mask)
            {
                // the block is at least partially covered.
                in_data.attributes.get_varyings_block(temp_varyings_block);
                in_data.attributes.get_depth_block(frag_depth_block);
                in_data.attributes.get_one_over_viewport_z_block(one_over_viewport_z_block);

                rast::fragment_info frag_info[4] = {
                  {frag_depth_block[0], front_facing, temp_varyings_block[0]},
                  {frag_depth_block[1], front_facing, temp_varyings_block[1]},
                  {frag_depth_block[2], front_facing, temp_varyings_block[2]},
                  {frag_depth_block[3], front_facing, temp_varyings_block[3]}};
                swr::impl::fragment_output_block out{(mask & 0x8) != 0, (mask & 0x4) != 0, (mask & 0x2) != 0, (mask & 0x1) != 0};

                process_fragment_block(x, y, *in_data.states, one_over_viewport_z_block, frag_info, out);
                in_data.states->draw_target->merge_color_block(0, x, y, out, in_data.states->blending_enabled, in_data.states->blend_src, in_data.states->blend_dst);
            }

            lambdas.step_x(2);
            in_data.attributes.advance_x(2);
        }

        lambdas.load_position(row_start[0], row_start[1], row_start[2]);
        lambdas.step_y(2);
        in_data.attributes.advance_y(2);
    }
}

void sweep_rasterizer::draw_filled_triangle(const swr::impl::render_states& states, bool is_front_facing, const geom::vertex& v1, const geom::vertex& v2, const geom::vertex& v3)
{
    // calculate the (signed) parallelogram area spanned by the difference vectors.
    auto v1_xy = v1.coords.xy();
    auto v2_xy = v2.coords.xy();
    auto v3_xy = v3.coords.xy();

    auto area = (v2_xy - v1_xy).area(v3_xy - v1_xy);

    // don't consider degenerate triangles.
    if(ml::fixed_24_8_t(area) == 0)
    {
        return;
    }

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
    const geom::vertex *v1_cw{nullptr}, *v2_cw{nullptr};

    if(area > 0)
    {
        // keep vertex order.
        v1_cw = &v1;
        v2_cw = &v2;
    }
    else /* area < 0, since we already checked for area==0 */
    {
        // change vertex order.
        std::swap(v1_xy, v2_xy);
        area = -area;

        v1_cw = &v2;
        v2_cw = &v1;
    }

    // convert triangle coordinates into a fixed-point representation with 4-bit subpixel precision.
    ml::vec2_fixed<4> v1_xy_fix(v1_xy.x, v1_xy.y);
    ml::vec2_fixed<4> v2_xy_fix(v2_xy.x, v2_xy.y);
    ml::vec2_fixed<4> v3_xy_fix(v3_xy.x, v3_xy.y);

    // list all edge in fixed point to use them for checking if a particular
    // pixel lies inside the triangle. the order of the edges does not matter,
    // but their orientation does.
    geom::edge_function_fixed edges_fix[3] = {
      {v1_xy_fix, v2_xy_fix}, {v2_xy_fix, v3_xy_fix}, {v3_xy_fix, v1_xy_fix}};

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
        if(edges_fix[i].v_diff.y == 0
           && edges_fix[i].v_diff.x > 0)
        {
            edges_fix[i].c += cnl::wrap<ml::fixed_24_8_t>(FILL_RULE_EDGE_BIAS);
        }
        // Left edge test.
        //
        // In a CW triangle, a left edge goes up, i.e. its endpoint is strictly above its starting point.
        // In terms of the y coordinate, the difference vector has to be strictly negative.
        else if(edges_fix[i].v_diff.y < 0)
        {
            edges_fix[i].c += cnl::wrap<ml::fixed_24_8_t>(FILL_RULE_EDGE_BIAS);
        }
        else
        {
            // Here we have either a bottom edge or a right edge. Thus, we intentionally do nothing.
        }
    }

    /*
	 * Loop through blocks of size (rasterizer_block_size,rasterizer_block_size), starting and ending on an aligned value.
	 */

    auto v1x = ml::truncate_unchecked(v1.coords.x);
    auto v1y = ml::truncate_unchecked(v1.coords.y);
    auto v2x = ml::truncate_unchecked(v2.coords.x);
    auto v2y = ml::truncate_unchecked(v2.coords.y);
    auto v3x = ml::truncate_unchecked(v3.coords.x);
    auto v3y = ml::truncate_unchecked(v3.coords.y);

    // take scissor box into account.
    int start_x{0}, start_y{0}, end_x{0}, end_y{0};
    if(states.scissor_test_enabled)
    {
        int x_min = std::max(states.scissor_box.x_min, 0);
        int x_max = std::min(states.scissor_box.x_max, states.draw_target->properties.width);

        int y_min = std::max(states.scissor_box.y_min, 0);
        int y_max = std::min(states.scissor_box.y_max, states.draw_target->properties.height);

        // the default framebuffer needs a flip.
        if(states.draw_target == framebuffer)
        {
            int y_temp = y_min;
            y_min = states.draw_target->properties.height - y_max;
            y_max = states.draw_target->properties.height - y_temp;
        }

        start_x = swr::impl::lower_align_on_block_size(std::max(std::min({v1x, v2x, v3x}), x_min));
        end_x = swr::impl::upper_align_on_block_size(std::min(std::max({v1x + 1, v2x + 1, v3x + 1}), x_max));
        start_y = swr::impl::lower_align_on_block_size(std::max(std::min({v1y, v2y, v3y}), y_min));
        end_y = swr::impl::upper_align_on_block_size(std::min(std::max({v1y + 1, v2y + 1, v3y + 1}), y_max));
    }
    else
    {
        start_x = swr::impl::lower_align_on_block_size(std::max(std::min({v1x, v2x, v3x}), 0));
        end_x = swr::impl::upper_align_on_block_size(std::min(std::max({v1x + 1, v2x + 1, v3x + 1}), states.draw_target->properties.width));
        start_y = swr::impl::lower_align_on_block_size(std::max(std::min({v1y, v2y, v3y}), 0));
        end_y = swr::impl::upper_align_on_block_size(std::min(std::max({v1y + 1, v2y + 1, v3y + 1}), states.draw_target->properties.height));
    }

    // initialize lambdas for point-in-triangle detection.
    const auto start_coord = ml::vec2_fixed<4>{ml::fixed_28_4_t{start_x} + ml::fixed_28_4_t{0.5f}, ml::fixed_28_4_t{start_y} + ml::fixed_28_4_t{0.5f}};
    geom::linear_interpolator_2d<ml::fixed_24_8_t> lambda_row_top_left[3] = {
      {{-edges_fix[0].evaluate(start_coord)},
       {-edges_fix[0].get_change_x(),
        -edges_fix[0].get_change_y()}},
      {{-edges_fix[1].evaluate(start_coord)},
       {-edges_fix[1].get_change_x(),
        -edges_fix[1].get_change_y()}},
      {{-edges_fix[2].evaluate(start_coord)},
       {-edges_fix[2].get_change_x(),
        -edges_fix[2].get_change_y()}}};

    /*
     * Set up an interpolator for the triangle attributes, i.e., depth value, viewport z coordinate and shader varyings.
     */
    const ml::vec2 screen_coords{static_cast<float>(start_x) + 0.5f, static_cast<float>(start_y) + 0.5f};
    rast::triangle_interpolator attributes{screen_coords, *v1_cw, *v2_cw, v3, v1, states.shader_info->iqs, 1.0f / area};

    for(auto y = start_y; y < end_y; y += swr::impl::rasterizer_block_size)
    {
        // initialize lambdas for the corners of the block.
        geom::barycentric_coordinate_block lambdas_box{
          lambda_row_top_left[0].value, lambda_row_top_left[0].step,
          lambda_row_top_left[1].value, lambda_row_top_left[1].step,
          lambda_row_top_left[2].value, lambda_row_top_left[2].step};
        lambdas_box.setup(swr::impl::rasterizer_block_size, swr::impl::rasterizer_block_size);

        rast::triangle_interpolator attributes_row = attributes;
        for(auto x = start_x; x < end_x; x += swr::impl::rasterizer_block_size)
        {
            // check if we have any block coverage. if so, calculate a reduced coverage mask.
            int mask = lambdas_box.get_coverage_mask();
            if(!mask)
            {
                // the block is outside the triangle.
                lambdas_box.step_x(swr::impl::rasterizer_block_size);
                attributes_row.advance_x(swr::impl::rasterizer_block_size);

                continue;
            }

            // reduce mask.
            mask = geom::reduce_coverage_mask(mask);

            if(mask == 0xf)
            {
                // the block is completely covered.

                // try to add the triangle to the tile cache.
                if(!tiles.add_triangle(&states, attributes_row, lambdas_box, x, y, is_front_facing, tile_info::rasterization_mode::block))
                {
                    // the cache is full. process all tiles and add the triangle again.
                    process_tile_cache();

#ifdef NDEBUG
                    tiles.add_triangle(&states, attributes_row, lambdas_box, x, y, is_front_facing, tile_info::rasterization_mode::block);
#else
                    assert(tiles.add_triangle(&states, attributes_row, lambdas_box, x, y, is_front_facing, tile_info::rasterization_mode::block));
#endif
                }
            }
            else
            {
                // the block is partially covered.

                // try to add the triangle to the tile cache.
                if(!tiles.add_triangle(&states, attributes_row, lambdas_box, x, y, is_front_facing, tile_info::rasterization_mode::checked))
                {
                    // the cache is full. process all tiles and add the triangle again.
                    process_tile_cache();

#ifdef NDEBUG
                    tiles.add_triangle(&states, attributes_row, lambdas_box, x, y, is_front_facing, tile_info::rasterization_mode::checked);
#else
                    assert(tiles.add_triangle(&states, attributes_row, lambdas_box, x, y, is_front_facing, tile_info::rasterization_mode::checked));
#endif
                }
            }

            lambdas_box.step_x(swr::impl::rasterizer_block_size);
            attributes_row.advance_x(swr::impl::rasterizer_block_size);
        }

        // advance y
        lambda_row_top_left[0].step_y(swr::impl::rasterizer_block_size);
        lambda_row_top_left[1].step_y(swr::impl::rasterizer_block_size);
        lambda_row_top_left[2].step_y(swr::impl::rasterizer_block_size);

        attributes.advance_y(swr::impl::rasterizer_block_size);
    }
}

} /* namespace rast */