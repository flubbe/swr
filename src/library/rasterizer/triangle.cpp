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

void sweep_rasterizer::process_block(unsigned int tile_index)
{
    auto tile = tile_cache[tile_index];
    boost::container::static_vector<swr::varying, geom::limits::max::varyings> temp_varyings(tile.attributes.varyings.size());

    const auto end_x = tile.x + swr::impl::rasterizer_block_size;
    const auto end_y = tile.y + swr::impl::rasterizer_block_size;

    // process block.
    for(; tile.y < end_y; ++tile.y)
    {
        for(unsigned int x = tile.x; x < end_x; ++x)
        {
            tile.attributes.get_varyings(temp_varyings);

            rast::fragment_info frag_info(tile.attributes.depth_value.value, tile.front_facing, temp_varyings);
            process_fragment(x, tile.y, *tile.states, tile.attributes.one_over_viewport_z.value, frag_info);

            tile.attributes.advance_x();
        }
        tile.attributes.advance_y();
    }
}

void sweep_rasterizer::process_block_checked(unsigned int tile_index, const geom::linear_interpolator_2d<ml::fixed_24_8_t> lambdas_top_left[3])
{
    auto tile = tile_cache[tile_index];
    boost::container::static_vector<swr::varying, geom::limits::max::varyings> temp_varyings(tile.attributes.varyings.size());

    const auto end_x = tile.x + swr::impl::rasterizer_block_size;
    const auto end_y = tile.y + swr::impl::rasterizer_block_size;
    geom::linear_interpolator_2d<ml::fixed_24_8_t> lambdas[3] = {lambdas_top_left[0], lambdas_top_left[1], lambdas_top_left[2]};

    // process block.
    for(; tile.y < end_y; ++tile.y)
    {
        for(unsigned int x = tile.x; x < end_x; ++x)
        {
            if(lambdas[0].value > 0 && lambdas[1].value > 0 && lambdas[2].value > 0)
            {
                tile.attributes.get_varyings(temp_varyings);

                rast::fragment_info frag_info(tile.attributes.depth_value.value, tile.front_facing, temp_varyings);
                process_fragment(x, tile.y, *tile.states, tile.attributes.one_over_viewport_z.value, frag_info);
            }

            lambdas[0].advance_x();
            lambdas[1].advance_x();
            lambdas[2].advance_x();
            tile.attributes.advance_x();
        }

        lambdas[0].advance_y();
        lambdas[1].advance_y();
        lambdas[2].advance_y();
        tile.attributes.advance_y();
    }
}

#ifdef SWR_ENABLE_MULTI_THREADING

/** static block drawing function. callable by threads. */
void sweep_rasterizer::thread_process_block(sweep_rasterizer* rasterizer, unsigned int tile_index)
{
    rasterizer->process_block(tile_index);
}

/** static block drawing function. callable by threads. */
void sweep_rasterizer::thread_process_block_checked(sweep_rasterizer* rasterizer, unsigned int tile_index, const geom::linear_interpolator_2d<ml::fixed_24_8_t> lambdas[3])
{
    rasterizer->process_block_checked(tile_index, lambdas);
}

#endif /* SWR_ENABLE_MULTI_THREADING */

/** 
 * step through 12 fixed-point (unnormalized) barycentric coordinates. these
 * are interpretted as barycentric coordinates evaluated at 4 rectangle corners.
 */
struct barycentric_coordinate_stepper
{
    /** top-left coordinates. */
    geom::linear_interpolator_2d<ml::fixed_24_8_t> top_left[3];

    /** top-right coordinates. */
    geom::linear_interpolator_2d<ml::fixed_24_8_t> top_right[3];

    /** bottom-left coordinates. */
    geom::linear_interpolator_2d<ml::fixed_24_8_t> bottom_left[3];

    /** bottom-right coordinates. */
    geom::linear_interpolator_2d<ml::fixed_24_8_t> bottom_right[3];

    /** default constructor. */
    barycentric_coordinate_stepper() = default;

    /** initialize both top coordinates with the same values and also both bottom coordinates with the same values. */
    barycentric_coordinate_stepper(const geom::linear_interpolator_2d<ml::fixed_24_8_t> top[3], const geom::linear_interpolator_2d<ml::fixed_24_8_t> bottom[3])
    : top_left{top[0], top[1], top[2]}
    , top_right{top[0], top[1], top[2]}
    , bottom_left{bottom[0], bottom[1], bottom[2]}
    , bottom_right{bottom[0], bottom[1], bottom[2]}
    {
    }

    /** advance the top-right and bottom-right coordinates in x direction. */
    void advance_right_x(int i)
    {
        top_right[0].advance_x(i);
        top_right[1].advance_x(i);
        top_right[2].advance_x(i);
        bottom_right[0].advance_x(i);
        bottom_right[1].advance_x(i);
        bottom_right[2].advance_x(i);
    }

    /** advance the top-left and bottom-left coordinates in x direction. */
    void advance_left_x(int i)
    {
        top_left[0].advance_x(i);
        top_left[1].advance_x(i);
        top_left[2].advance_x(i);
        bottom_left[0].advance_x(i);
        bottom_left[1].advance_x(i);
        bottom_left[2].advance_x(i);
    }

    /** prepare the next advance_right_x-step by seting top_left to top_right and bottom_left to bottom_right. */
    void setup_next_advance_right_x()
    {
        top_left[0] = top_right[0];
        top_left[1] = top_right[1];
        top_left[2] = top_right[2];
        bottom_left[0] = bottom_right[0];
        bottom_left[1] = bottom_right[1];
        bottom_left[2] = bottom_right[2];
    }

    /** prepare the next advance_left_x-step by seting top_right to top_left and bottom_right to bottom_left. */
    void setup_next_advance_left_x()
    {
        top_right[0] = top_left[0];
        top_right[1] = top_left[1];
        top_right[2] = top_left[2];
        bottom_right[0] = bottom_left[0];
        bottom_right[1] = bottom_left[1];
        bottom_right[2] = bottom_left[2];
    }

    /** 
     * check the signs of the barycentric coordinate tuples.
     * 
     * \return -1 if all rectangle coordinates are outside the triangle, 1 if all tuples are positive and 0 in all other cases.
     */
    int sign() const
    {
        if(top_left[0].value > 0 && top_left[1].value > 0 && top_left[2].value > 0
           && top_right[0].value > 0 && top_right[1].value > 0 && top_right[2].value > 0
           && bottom_left[0].value > 0 && bottom_left[1].value > 0 && bottom_left[2].value > 0
           && bottom_right[0].value > 0 && bottom_right[1].value > 0 && bottom_right[2].value > 0)
        {
            // the rectangle is completely inside the triangle.
            return 1;
        }

        // here we know that we the rectangle is either partially or completely outside the triangle.
        if((top_left[0].value < 0 && top_right[0].value < 0 && bottom_left[0].value < 0 && bottom_right[0].value < 0)
           || (top_left[1].value < 0 && top_right[1].value < 0 && bottom_left[1].value < 0 && bottom_right[1].value < 0)
           || (top_left[2].value < 0 && top_right[2].value < 0 && bottom_left[2].value < 0 && bottom_right[2].value < 0))
        {
            // the rectangle is completely outside the triangle.
            return -1;
        }

        // the rectangle partially covers the triangle.
        return 0;
    }
};

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
            //  This is equivalent to EdgeList[i]->c += ml::fixed_28_4_t(0.0625f * FILL_RULE_EDGE_BIAS);
            edges_fix[i].c += cnl::wrap<ml::fixed_24_8_t>(FILL_RULE_EDGE_BIAS);
        }
        // Left edge test.
        //
        // In a CW triangle, a left edge goes up, i.e. its endpoint is strictly above its starting point.
        // In terms of the y coordinate, the difference vector has to be strictly negative.
        else if(edges_fix[i].v_diff.y < 0)
        {
            //  This is equivalent to EdgeList[i]->c += ml::fixed_28_4_t(0.0625f * FILL_RULE_EDGE_BIAS);
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
        int x_max = std::min(states.scissor_box.x_max, raster_width);
        int y_min = std::max(raster_height - states.scissor_box.y_max, 0);
        int y_max = std::min(raster_height - states.scissor_box.y_min, raster_height);

        start_x = std::max(swr::impl::lower_align_on_block_size(std::min({v1x, v2x, v3x})), x_min);
        end_x = std::min(swr::impl::upper_align_on_block_size(std::max({v1x + 1, v2x + 1, v3x + 1})), x_max);
        start_y = std::max(swr::impl::lower_align_on_block_size(std::min({v1y, v2y, v3y})), y_min);
        end_y = std::min(swr::impl::upper_align_on_block_size(std::max({v1y + 1, v2y + 1, v3y + 1})), y_max);
    }
    else
    {
        start_x = std::max(swr::impl::lower_align_on_block_size(std::min({v1x, v2x, v3x})), 0);
        end_x = std::min(swr::impl::upper_align_on_block_size(std::max({v1x + 1, v2x + 1, v3x + 1})), raster_width);
        start_y = std::max(swr::impl::lower_align_on_block_size(std::min({v1y, v2y, v3y})), 0);
        end_y = std::min(swr::impl::upper_align_on_block_size(std::max({v1y + 1, v2y + 1, v3y + 1})), raster_height);
    }

    // initialize lambdas for point-in-triangle detection.
    const auto start_coord = ml::vec2_fixed<4>{ml::fixed_28_4_t{start_x} + ml::fixed_28_4_t{0.5f}, ml::fixed_28_4_t{start_y} + ml::fixed_28_4_t{0.5f}};
    geom::linear_interpolator_2d<ml::fixed_24_8_t> lambda_row_top_left_next[3] = {
      {{-edges_fix[0].evaluate(start_coord)},
       {-edges_fix[0].get_change_x(),
        -edges_fix[0].get_change_y()},
       {}},
      {{-edges_fix[1].evaluate(start_coord)},
       {-edges_fix[1].get_change_x(),
        -edges_fix[1].get_change_y()},
       {}},
      {{-edges_fix[2].evaluate(start_coord)},
       {-edges_fix[2].get_change_x(),
        -edges_fix[2].get_change_y()},
       {}}};
    geom::linear_interpolator_2d<ml::fixed_24_8_t> lambda_row_top_left[3];

    /*
     * Set up an interpolator for the triangle attributes, i.e., depth value, viewport z coordinate and shader varyings.
     */
    rast::triangle_interpolator attributes{*v1_cw, *v2_cw, v3, states.shader_info->iqs, 1.0f / area};
    attributes.setup_from_screen_coords({static_cast<float>(start_x) + 0.5f, static_cast<float>(start_y) + 0.5f});

    for(auto y = start_y; y < end_y; y += swr::impl::rasterizer_block_size, attributes.advance_y(swr::impl::rasterizer_block_size))
    {
        // set up next lambdas for the "bottom part" of the block.
        lambda_row_top_left[0] = lambda_row_top_left_next[0];
        lambda_row_top_left[1] = lambda_row_top_left_next[1];
        lambda_row_top_left[2] = lambda_row_top_left_next[2];
        lambda_row_top_left_next[0].step_y(swr::impl::rasterizer_block_size);
        lambda_row_top_left_next[1].step_y(swr::impl::rasterizer_block_size);
        lambda_row_top_left_next[2].step_y(swr::impl::rasterizer_block_size);

        // initialize lambdas for top-left and bottom-left corner of the block.
        barycentric_coordinate_stepper lambdas_box_next{lambda_row_top_left, lambda_row_top_left_next};
        barycentric_coordinate_stepper lambdas_box;

        rast::triangle_interpolator attributes_row = attributes;
        for(auto x = start_x; x < end_x; x += swr::impl::rasterizer_block_size, attributes_row.advance_x(swr::impl::rasterizer_block_size))
        {
            // set up lambdas for the current row.
            lambdas_box_next.advance_right_x(swr::impl::rasterizer_block_size);
            lambdas_box = lambdas_box_next;
            lambdas_box_next.setup_next_advance_right_x();

            int sign = lambdas_box.sign();
            if(sign < 0)
            {
                // the block is outside the triangle.
                continue;
            }

            rast::triangle_interpolator attributes_temp = attributes_row;
            attributes_temp.setup_block_processing();

            // add this block to the tile cache.
            std::size_t tile_index = tile_cache.size();
            tile_cache.emplace_back(&states, attributes_temp, x, y, is_front_facing);

            if(sign > 0)
            {
                // the block is completely covered.
#ifdef SWR_ENABLE_MULTI_THREADING
                rasterizer_threads.push_task(thread_process_block, this, tile_index);
#else
                process_block(tile_index);
#endif
            }
            else
            {
                // the block is partially covered.

                // until here, we didn't use or set the .row_start variables, but process_block_checked
                // expects that the interpolators are correctly set up, which we now do.
                lambdas_box.top_left[0].setup_block_processing();
                lambdas_box.top_left[1].setup_block_processing();
                lambdas_box.top_left[2].setup_block_processing();

#ifdef SWR_ENABLE_MULTI_THREADING
                rasterizer_threads.push_task(thread_process_block_checked, this, tile_index, lambdas_box.top_left);
#else
                process_block_checked(tile_index, lambdas_box.top_left);
#endif
            }

            SWR_STATS_INCREMENT(stats_rast.jobs);
        }
    }
}

} /* namespace rast */