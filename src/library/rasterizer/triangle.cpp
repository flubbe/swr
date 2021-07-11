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

#include "fmt/format.h"

namespace rast
{

void sweep_rasterizer::process_block(unsigned int tile_index)
{
    auto tile = tile_cache[tile_index];

    boost::container::static_vector<swr::varying, geom::limits::max::varyings> temp_varyings[4];
    temp_varyings[0].resize(tile.attributes.varyings.size());
    temp_varyings[1].resize(tile.attributes.varyings.size());
    temp_varyings[2].resize(tile.attributes.varyings.size());
    temp_varyings[3].resize(tile.attributes.varyings.size());

    const auto end_x = tile.x + swr::impl::rasterizer_block_size;
    const auto end_y = tile.y + swr::impl::rasterizer_block_size;

    const bool front_facing = tile.front_facing;

    // process block.
    for(; tile.y < end_y; tile.y += 2)
    {
        for(unsigned int x = tile.x; x < end_x; x += 2)
        {
            tile.attributes.get_varyings_block(temp_varyings);

            float frag_depth[4];
            tile.attributes.get_depth_block(frag_depth);

            float one_over_viewport_z[4];
            tile.attributes.get_one_over_viewport_z_block(one_over_viewport_z);

            rast::fragment_info frag_info[4] = {
              {frag_depth[0], front_facing, temp_varyings[0]},
              {frag_depth[1], front_facing, temp_varyings[1]},
              {frag_depth[2], front_facing, temp_varyings[2]},
              {frag_depth[3], front_facing, temp_varyings[3]}};
            swr::impl::fragment_output_block out;

            process_fragment_block(x, tile.y, *tile.states, one_over_viewport_z, frag_info, out);
            tile.states->draw_target->merge_color_block(0, x, tile.y, out, tile.states->blending_enabled, tile.states->blend_src, tile.states->blend_dst);

            tile.attributes.advance_x(2);
        }
        tile.attributes.advance_y(2);
    }
}

#ifdef SWR_USE_SIMD

void sweep_rasterizer::process_block_checked(unsigned int tile_index, const geom::linear_interpolator_2d<ml::fixed_24_8_t> lambdas_top_left[3])
{
    auto tile = tile_cache[tile_index];

    boost::container::static_vector<swr::varying, geom::limits::max::varyings> temp_varyings_block[4];
    temp_varyings_block[0].resize(tile.attributes.varyings.size());
    temp_varyings_block[1].resize(tile.attributes.varyings.size());
    temp_varyings_block[2].resize(tile.attributes.varyings.size());
    temp_varyings_block[3].resize(tile.attributes.varyings.size());

    float frag_depth_block[4];
    float one_over_viewport_z_block[4];

    const auto end_x = tile.x + swr::impl::rasterizer_block_size;
    const auto end_y = tile.y + swr::impl::rasterizer_block_size;

    __m128i lambda0 = _mm_set1_epi32(cnl::unwrap(lambdas_top_left[0].value));
    __m128i lambda1 = _mm_set1_epi32(cnl::unwrap(lambdas_top_left[1].value));
    __m128i lambda2 = _mm_set1_epi32(cnl::unwrap(lambdas_top_left[2].value));

    __m128i step0 = _mm_set_epi32(0, cnl::unwrap(lambdas_top_left[0].step.x), cnl::unwrap(lambdas_top_left[0].step.y), cnl::unwrap(lambdas_top_left[0].step.x + lambdas_top_left[0].step.y));
    __m128i step1 = _mm_set_epi32(0, cnl::unwrap(lambdas_top_left[1].step.x), cnl::unwrap(lambdas_top_left[1].step.y), cnl::unwrap(lambdas_top_left[1].step.x + lambdas_top_left[1].step.y));
    __m128i step2 = _mm_set_epi32(0, cnl::unwrap(lambdas_top_left[2].step.x), cnl::unwrap(lambdas_top_left[2].step.y), cnl::unwrap(lambdas_top_left[2].step.x + lambdas_top_left[2].step.y));

    __m128i row_start0 = _mm_set1_epi32(cnl::unwrap(lambdas_top_left[0].row_start));
    __m128i row_start1 = _mm_set1_epi32(cnl::unwrap(lambdas_top_left[1].row_start));
    __m128i row_start2 = _mm_set1_epi32(cnl::unwrap(lambdas_top_left[2].row_start));

    __m128i loop_stepx0 = _mm_add_epi32(_mm_set1_epi32(cnl::unwrap(lambdas_top_left[0].step.x)), _mm_set1_epi32(cnl::unwrap(lambdas_top_left[0].step.x)));
    __m128i loop_stepx1 = _mm_add_epi32(_mm_set1_epi32(cnl::unwrap(lambdas_top_left[1].step.x)), _mm_set1_epi32(cnl::unwrap(lambdas_top_left[1].step.x)));
    __m128i loop_stepx2 = _mm_add_epi32(_mm_set1_epi32(cnl::unwrap(lambdas_top_left[2].step.x)), _mm_set1_epi32(cnl::unwrap(lambdas_top_left[2].step.x)));

    __m128i loop_stepy0 = _mm_add_epi32(_mm_set1_epi32(cnl::unwrap(lambdas_top_left[0].step.y)), _mm_set1_epi32(cnl::unwrap(lambdas_top_left[0].step.y)));
    __m128i loop_stepy1 = _mm_add_epi32(_mm_set1_epi32(cnl::unwrap(lambdas_top_left[1].step.y)), _mm_set1_epi32(cnl::unwrap(lambdas_top_left[1].step.y)));
    __m128i loop_stepy2 = _mm_add_epi32(_mm_set1_epi32(cnl::unwrap(lambdas_top_left[2].step.y)), _mm_set1_epi32(cnl::unwrap(lambdas_top_left[2].step.y)));

    const bool front_facing = tile.front_facing;

    // process block.
    for(; tile.y < end_y; tile.y += 2)
    {
        for(unsigned int x = tile.x; x < end_x; x += 2)
        {
            __m128i lambda0_block = _mm_add_epi32(lambda0, step0);
            __m128i lambda1_block = _mm_add_epi32(lambda1, step1);
            __m128i lambda2_block = _mm_add_epi32(lambda2, step2);

            __m128i l0 = _mm_cmpgt_epi32(lambda0_block, _mm_setzero_si128());
            __m128i l1 = _mm_cmpgt_epi32(lambda1_block, _mm_setzero_si128());
            __m128i l2 = _mm_cmpgt_epi32(lambda2_block, _mm_setzero_si128());
            int cmp_mask = _mm_movemask_epi8(_mm_packs_epi16(_mm_packs_epi32(l0, l1), _mm_packs_epi32(l2, _mm_setzero_si128())));

            if((cmp_mask & 0xf) == 0 || (cmp_mask & 0xf0) == 0 || (cmp_mask & 0xf00) == 0)
            {
                // the block was completely missed.
            }
            else if(cmp_mask == 0xfff)
            {
                // the block is completely covered.
                tile.attributes.get_varyings_block(temp_varyings_block);
                tile.attributes.get_depth_block(frag_depth_block);
                tile.attributes.get_one_over_viewport_z_block(one_over_viewport_z_block);

                rast::fragment_info frag_info[4] = {
                  {frag_depth_block[0], front_facing, temp_varyings_block[0]},
                  {frag_depth_block[1], front_facing, temp_varyings_block[1]},
                  {frag_depth_block[2], front_facing, temp_varyings_block[2]},
                  {frag_depth_block[3], front_facing, temp_varyings_block[3]}};
                swr::impl::fragment_output_block out;

                process_fragment_block(x, tile.y, *tile.states, one_over_viewport_z_block, frag_info, out);
                tile.states->draw_target->merge_color_block(0, x, tile.y, out, tile.states->blending_enabled, tile.states->blend_src, tile.states->blend_dst);
            }
            else
            {
                // the block is partially covered.

                // alias names.
                float& frag_depth = frag_depth_block[0];
                float& one_over_viewport_z = one_over_viewport_z_block[0];
                auto& temp_varyings = temp_varyings_block[0];

#    define CMPMASK(k) ((1 << k) | (16 << k) | (256 << k))
#    define PROCESS_FRAGMENT_CHECKED(k)                                                                                                                                \
        if((cmp_mask & CMPMASK(k)) == CMPMASK(k))                                                                                                                      \
        {                                                                                                                                                              \
            const auto offs_x = (~k) & 1;                                                                                                                              \
            const auto offs_y = ((~k) & 2) >> 1;                                                                                                                       \
                                                                                                                                                                       \
            tile.attributes.get_varyings<offs_x, offs_y>(temp_varyings);                                                                                               \
            tile.attributes.get_depth<offs_x, offs_y>(frag_depth);                                                                                                     \
            tile.attributes.get_one_over_viewport_z<offs_x, offs_y>(one_over_viewport_z);                                                                              \
                                                                                                                                                                       \
            rast::fragment_info frag_info{frag_depth, front_facing, temp_varyings};                                                                                    \
            swr::impl::fragment_output out;                                                                                                                            \
            process_fragment(x + offs_x, tile.y + offs_y, *tile.states, one_over_viewport_z, frag_info, out);                                                          \
            tile.states->draw_target->merge_color(0, x + offs_x, tile.y + offs_y, out, tile.states->blending_enabled, tile.states->blend_src, tile.states->blend_dst); \
        }

                PROCESS_FRAGMENT_CHECKED(0); /* (x,   tile.y  ) */
                PROCESS_FRAGMENT_CHECKED(1); /* (x+1, tile.y  ) */
                PROCESS_FRAGMENT_CHECKED(2); /* (x,   tile.y+1) */
                PROCESS_FRAGMENT_CHECKED(3); /* (x+1, tile.y+1) */

#    undef PROCESS_FRAGMENT_CHECKED
#    undef CMPMASK
            }

            lambda0 = _mm_add_epi32(lambda0, loop_stepx0);
            lambda1 = _mm_add_epi32(lambda1, loop_stepx1);
            lambda2 = _mm_add_epi32(lambda2, loop_stepx2);

            tile.attributes.advance_x(2);
        }

        row_start0 = _mm_add_epi32(row_start0, loop_stepy0);
        row_start1 = _mm_add_epi32(row_start1, loop_stepy1);
        row_start2 = _mm_add_epi32(row_start2, loop_stepy2);

        lambda0 = row_start0;
        lambda1 = row_start1;
        lambda2 = row_start2;

        tile.attributes.advance_y(2);
    }
}

#else /* SWR_USE_SIMD */

void sweep_rasterizer::process_block_checked(unsigned int tile_index, const geom::linear_interpolator_2d<ml::fixed_24_8_t> lambdas_top_left[3])
{
    auto tile = tile_cache[tile_index];

    boost::container::static_vector<swr::varying, geom::limits::max::varyings> temp_varyings_block[4];
    temp_varyings_block[0].resize(tile.attributes.varyings.size());
    temp_varyings_block[1].resize(tile.attributes.varyings.size());
    temp_varyings_block[2].resize(tile.attributes.varyings.size());
    temp_varyings_block[3].resize(tile.attributes.varyings.size());

    float frag_depth_block[4];
    float one_over_viewport_z_block[4];

    const auto end_x = tile.x + swr::impl::rasterizer_block_size;
    const auto end_y = tile.y + swr::impl::rasterizer_block_size;

    geom::linear_interpolator_2d<ml::fixed_24_8_t> lambdas[3] = {lambdas_top_left[0], lambdas_top_left[1], lambdas_top_left[2]};

    const bool front_facing = tile.front_facing;

    // process block.
    for(; tile.y < end_y; tile.y += 2)
    {
        for(unsigned int x = tile.x; x < end_x; x += 2)
        {
            const bool mask[4] = {
              lambdas[0].value > 0 && lambdas[1].value > 0 && lambdas[2].value > 0,
              (lambdas[0].value + lambdas[0].step.x > 0) && (lambdas[1].value + lambdas[1].step.x) > 0 && (lambdas[2].value + lambdas[2].step.x) > 0,
              (lambdas[0].value + lambdas[0].step.y > 0) && (lambdas[1].value + lambdas[1].step.y) > 0 && (lambdas[2].value + lambdas[2].step.y) > 0,
              (lambdas[0].value + lambdas[0].step.x + lambdas[0].step.y > 0) && (lambdas[1].value + lambdas[1].step.x + lambdas[1].step.y) > 0 && (lambdas[2].value + lambdas[2].step.x + lambdas[2].step.y) > 0};

            if(mask[0] && mask[1] && mask[2] && mask[3])
            {
                tile.attributes.get_varyings_block(temp_varyings_block);
                tile.attributes.get_depth_block(frag_depth_block);
                tile.attributes.get_one_over_viewport_z_block(one_over_viewport_z_block);

                rast::fragment_info frag_info[4] = {
                  {frag_depth_block[0], front_facing, temp_varyings_block[0]},
                  {frag_depth_block[1], front_facing, temp_varyings_block[1]},
                  {frag_depth_block[2], front_facing, temp_varyings_block[2]},
                  {frag_depth_block[3], front_facing, temp_varyings_block[3]}};
                swr::impl::fragment_output_block out;

                process_fragment_block(x, tile.y, *tile.states, one_over_viewport_z_block, frag_info, out);
                tile.states->draw_target->merge_color_block(0, x, tile.y, out, tile.states->blending_enabled, tile.states->blend_src, tile.states->blend_dst);
            }
            else
            {
                // alias names.
                float& frag_depth = frag_depth_block[0];
                float& one_over_viewport_z = one_over_viewport_z_block[0];
                auto& temp_varyings = temp_varyings_block[0];

#    define PROCESS_FRAGMENT_CHECKED(k)                                                                                                                                \
        if(mask[k])                                                                                                                                                    \
        {                                                                                                                                                              \
            const auto offs_x = k & 1;                                                                                                                                 \
            const auto offs_y = (k & 2) >> 1;                                                                                                                          \
                                                                                                                                                                       \
            tile.attributes.get_varyings<offs_x, offs_y>(temp_varyings);                                                                                               \
            tile.attributes.get_depth<offs_x, offs_y>(frag_depth);                                                                                                     \
            tile.attributes.get_one_over_viewport_z<offs_x, offs_y>(one_over_viewport_z);                                                                              \
                                                                                                                                                                       \
            rast::fragment_info frag_info{frag_depth, front_facing, temp_varyings};                                                                                    \
            swr::impl::fragment_output out;                                                                                                                            \
            process_fragment(x + offs_x, tile.y + offs_y, *tile.states, one_over_viewport_z, frag_info, out);                                                          \
            tile.states->draw_target->merge_color(0, x + offs_x, tile.y + offs_y, out, tile.states->blending_enabled, tile.states->blend_src, tile.states->blend_dst); \
        }

                PROCESS_FRAGMENT_CHECKED(0); /* (x,   tile.y  ) */
                PROCESS_FRAGMENT_CHECKED(1); /* (x+1, tile.y  ) */
                PROCESS_FRAGMENT_CHECKED(2); /* (x,   tile.y+1) */
                PROCESS_FRAGMENT_CHECKED(3); /* (x+1, tile.y+1) */

#    undef PROCESS_FRAGMENT_CHECKED
            }

            lambdas[0].advance_x(2);
            lambdas[1].advance_x(2);
            lambdas[2].advance_x(2);

            tile.attributes.advance_x(2);
        }

        lambdas[0].advance_y(2);
        lambdas[1].advance_y(2);
        lambdas[2].advance_y(2);

        tile.attributes.advance_y(2);
    }
}

#endif /* SWR_USE_SIMD */

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

        start_x = std::max(swr::impl::lower_align_on_block_size(std::min({v1x, v2x, v3x})), x_min);
        end_x = std::min(swr::impl::upper_align_on_block_size(std::max({v1x + 1, v2x + 1, v3x + 1})), x_max);
        start_y = std::max(swr::impl::lower_align_on_block_size(std::min({v1y, v2y, v3y})), y_min);
        end_y = std::min(swr::impl::upper_align_on_block_size(std::max({v1y + 1, v2y + 1, v3y + 1})), y_max);
    }
    else
    {
        start_x = std::max(swr::impl::lower_align_on_block_size(std::min({v1x, v2x, v3x})), 0);
        end_x = std::min(swr::impl::upper_align_on_block_size(std::max({v1x + 1, v2x + 1, v3x + 1})), states.draw_target->properties.width);
        start_y = std::max(swr::impl::lower_align_on_block_size(std::min({v1y, v2y, v3y})), 0);
        end_y = std::min(swr::impl::upper_align_on_block_size(std::max({v1y + 1, v2y + 1, v3y + 1})), states.draw_target->properties.height);
    }

    // initialize lambdas for point-in-triangle detection.
    const auto start_coord = ml::vec2_fixed<4>{ml::fixed_28_4_t{start_x} + ml::fixed_28_4_t{0.5f}, ml::fixed_28_4_t{start_y} + ml::fixed_28_4_t{0.5f}};
    geom::linear_interpolator_2d<ml::fixed_24_8_t> lambda_row_top_left_next[3] = {
      {{-edges_fix[0].evaluate(start_coord)},
       {-edges_fix[0].get_change_x(),
        -edges_fix[0].get_change_y()}},
      {{-edges_fix[1].evaluate(start_coord)},
       {-edges_fix[1].get_change_x(),
        -edges_fix[1].get_change_y()}},
      {{-edges_fix[2].evaluate(start_coord)},
       {-edges_fix[2].get_change_x(),
        -edges_fix[2].get_change_y()}}};
    geom::linear_interpolator_2d<ml::fixed_24_8_t> lambda_row_top_left[3];

    /*
     * Set up an interpolator for the triangle attributes, i.e., depth value, viewport z coordinate and shader varyings.
     */
    const ml::vec2 screen_coords{static_cast<float>(start_x) + 0.5f, static_cast<float>(start_y) + 0.5f};
    rast::triangle_interpolator attributes{screen_coords, *v1_cw, *v2_cw, v3, v1, states.shader_info->iqs, 1.0f / area};

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
            std::size_t tile_index = allocate_tile(&states, attributes_temp, x, y, is_front_facing);

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
        }
    }
}

} /* namespace rast */