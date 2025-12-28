/**
 * swr - a software rasterizer
 *
 * unnormalized barycentric coordinates.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

namespace geom
{

/** unnormalied, fixed-point barycentric coordinates for triangles, evaluated on a rectangle. */
struct barycentric_coordinate_block
{
    using fixed_24_8_array_4 = __m128i;

    /** values at the corners of the rectangle in question. each __m128i contains the four values [top_left, top_right, bottom_left, bottom_right]. */
    __m128i corners[3];

    /** steps to take in x direction on each advance. */
    __m128i steps_x[3];

    /** steps in y direction. */
    __m128i steps_y[3];

    /** default constructor. */
    barycentric_coordinate_block() = default;

    /** initialize both top coordinates with the same values and also both bottom coordinates with the same values. */
    barycentric_coordinate_block(
      const ml::fixed_24_8_t& lambda0, const ml::tvec2<ml::fixed_24_8_t>& step0,
      const ml::fixed_24_8_t& lambda1, const ml::tvec2<ml::fixed_24_8_t>& step1,
      const ml::fixed_24_8_t& lambda2, const ml::tvec2<ml::fixed_24_8_t>& step2)
    {
        // set every corner to the same value. this does not yet represent a block/ractangle, but one needs
        // to call setup(block_size_x, block_size_y).
        corners[0] = _mm_set1_epi32(cnl::unwrap(lambda0));
        corners[1] = _mm_set1_epi32(cnl::unwrap(lambda1));
        corners[2] = _mm_set1_epi32(cnl::unwrap(lambda2));

        steps_x[0] = _mm_set1_epi32(cnl::unwrap(step0.x));
        steps_x[1] = _mm_set1_epi32(cnl::unwrap(step1.x));
        steps_x[2] = _mm_set1_epi32(cnl::unwrap(step2.x));

        steps_y[0] = _mm_set1_epi32(cnl::unwrap(step0.y));
        steps_y[1] = _mm_set1_epi32(cnl::unwrap(step1.y));
        steps_y[2] = _mm_set1_epi32(cnl::unwrap(step2.y));
    }

    /** set up the block size from given that corners contain the values the top-left corner. */
    void setup(int block_size_x, int block_size_y)
    {
        corners[0] = _mm_and_si128(corners[0], _mm_set_epi32(0xffffffff, 0, 0, 0));
        corners[0] = _mm_or_si128(corners[0], _mm_srli_si128(corners[0], 4));
        corners[0] = _mm_or_si128(corners[0], _mm_srli_si128(corners[0], 8));

        corners[1] = _mm_and_si128(corners[1], _mm_set_epi32(0xffffffff, 0, 0, 0));
        corners[1] = _mm_or_si128(corners[1], _mm_srli_si128(corners[1], 4));
        corners[1] = _mm_or_si128(corners[1], _mm_srli_si128(corners[1], 8));

        corners[2] = _mm_and_si128(corners[2], _mm_set_epi32(0xffffffff, 0, 0, 0));
        corners[2] = _mm_or_si128(corners[2], _mm_srli_si128(corners[2], 4));
        corners[2] = _mm_or_si128(corners[2], _mm_srli_si128(corners[2], 8));

        __m128i block_step_x[3] = {
          _mm_and_si128(steps_x[0], _mm_set_epi32(0, 0xffffffff, 0, 0xffffffff)),
          _mm_and_si128(steps_x[1], _mm_set_epi32(0, 0xffffffff, 0, 0xffffffff)),
          _mm_and_si128(steps_x[2], _mm_set_epi32(0, 0xffffffff, 0, 0xffffffff))};
        __m128i block_step_y[3] = {
          _mm_and_si128(steps_y[0], _mm_set_epi32(0, 0, 0xffffffff, 0xffffffff)),
          _mm_and_si128(steps_y[1], _mm_set_epi32(0, 0, 0xffffffff, 0xffffffff)),
          _mm_and_si128(steps_y[2], _mm_set_epi32(0, 0, 0xffffffff, 0xffffffff))};

        __m128i block_step_xy[3] = {
          _mm_add_epi32(_mm_mullo_epi32(block_step_x[0], _mm_set1_epi32(block_size_x)), _mm_mullo_epi32(block_step_y[0], _mm_set1_epi32(block_size_y))),
          _mm_add_epi32(_mm_mullo_epi32(block_step_x[1], _mm_set1_epi32(block_size_x)), _mm_mullo_epi32(block_step_y[1], _mm_set1_epi32(block_size_y))),
          _mm_add_epi32(_mm_mullo_epi32(block_step_x[2], _mm_set1_epi32(block_size_x)), _mm_mullo_epi32(block_step_y[2], _mm_set1_epi32(block_size_y)))};

        corners[0] = _mm_add_epi32(corners[0], block_step_xy[0]);
        corners[1] = _mm_add_epi32(corners[1], block_step_xy[1]);
        corners[2] = _mm_add_epi32(corners[2], block_step_xy[2]);
    }

    /** step block_size_x steps into x direction. */
    void step_x(int block_size_x)
    {
        corners[0] = _mm_add_epi32(corners[0], _mm_mullo_epi32(steps_x[0], _mm_set1_epi32(block_size_x)));
        corners[1] = _mm_add_epi32(corners[1], _mm_mullo_epi32(steps_x[1], _mm_set1_epi32(block_size_x)));
        corners[2] = _mm_add_epi32(corners[2], _mm_mullo_epi32(steps_x[2], _mm_set1_epi32(block_size_x)));
    }

    /** step block_size_y steps into y direction. */
    void step_y(int block_size_y)
    {
        corners[0] = _mm_add_epi32(corners[0], _mm_mullo_epi32(steps_y[0], _mm_set1_epi32(block_size_y)));
        corners[1] = _mm_add_epi32(corners[1], _mm_mullo_epi32(steps_y[1], _mm_set1_epi32(block_size_y)));
        corners[2] = _mm_add_epi32(corners[2], _mm_mullo_epi32(steps_y[2], _mm_set1_epi32(block_size_y)));
    }

    /** store current position. */
    void store_position(fixed_24_8_array_4& c0, fixed_24_8_array_4& c1, fixed_24_8_array_4& c2) const
    {
        c0 = corners[0];
        c1 = corners[1];
        c2 = corners[2];
    }

    /** load current position. */
    void load_position(const fixed_24_8_array_4& c0, const fixed_24_8_array_4& c1, const fixed_24_8_array_4& c2)
    {
        corners[0] = c0;
        corners[1] = c1;
        corners[2] = c2;
    }

    /**
     * calculate and return the coverage mask.
     *
     * layout:
     *
     *                          lambda2         |       lambda1       |       lambda0
     * bit:             0x800 0x400 0x200 0x100 | 0x80 0x40 0x20 0x10 | 0x8  0x4  0x2  0x1
     * pixel position:     tl    tr    bl    br |   tl   tr   bl   br |  tl   tr   bl   br
     */
    int get_coverage_mask() const
    {
        __m128i l0 = _mm_cmpgt_epi32(corners[0], _mm_setzero_si128());
        __m128i l1 = _mm_cmpgt_epi32(corners[1], _mm_setzero_si128());
        __m128i l2 = _mm_cmpgt_epi32(corners[2], _mm_setzero_si128());

        return _mm_movemask_epi8(_mm_packs_epi16(_mm_packs_epi32(l0, l1), _mm_packs_epi32(l2, _mm_setzero_si128())));
    }
};

} /* namespace geom */