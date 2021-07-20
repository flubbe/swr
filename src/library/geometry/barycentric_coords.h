/**
 * swr - a software rasterizer
 * 
 * unnormalized barycentric coordinates.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace geom
{

#ifdef SWR_USE_SIMD

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
    void store_position(fixed_24_8_array_4& c0, fixed_24_8_array_4& c1, fixed_24_8_array_4& c2)
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

    /** check if the current block is (partially) covered. */
    bool check_coverage() const
    {
        __m128i l0 = _mm_cmpgt_epi32(corners[0], _mm_setzero_si128());
        __m128i l1 = _mm_cmpgt_epi32(corners[1], _mm_setzero_si128());
        __m128i l2 = _mm_cmpgt_epi32(corners[2], _mm_setzero_si128());

        return _mm_movemask_epi8(_mm_packs_epi16(_mm_packs_epi32(l0, l1), _mm_packs_epi32(l2, _mm_setzero_si128()))) != 0;
    }

    /** 
     * calculate and return the coverage mask. 
     * 
     * layout:
     * 
     * bit:              8  4  2  1
     * pixsel position: tl tr bl br
     */
    int get_coverage_mask() const
    {
        __m128i l0 = _mm_cmpgt_epi32(corners[0], _mm_setzero_si128());
        __m128i l1 = _mm_cmpgt_epi32(corners[1], _mm_setzero_si128());
        __m128i l2 = _mm_cmpgt_epi32(corners[2], _mm_setzero_si128());

        int mask = _mm_movemask_epi8(_mm_packs_epi16(_mm_packs_epi32(l0, l1), _mm_packs_epi32(l2, _mm_setzero_si128())));
        return mask & (mask >> 4) & (mask >> 8);
    }
};

#else /* SWR_USE_SIMD */

/*
 * note: the struct below is modelled after the SIMD version.
 */

/** unnormalied, fixed-point barycentric coordinates for triangles, evaluated on a rectangle. */
struct barycentric_coordinate_block
{
    struct fixed_24_8_array_4
    {
        ml::fixed_24_8_t f0, f1, f2, f3;

        fixed_24_8_array_4() = default;
        fixed_24_8_array_4(const fixed_24_8_array_4&) = default;

        fixed_24_8_array_4(const ml::fixed_24_8_t& f)
        : f0{f}
        , f1{f}
        , f2{f}
        , f3{f}
        {
        }
        fixed_24_8_array_4(const ml::fixed_24_8_t& in_f3, const ml::fixed_24_8_t& in_f2, const ml::fixed_24_8_t& in_f1, const ml::fixed_24_8_t& in_f0)
        : f0{in_f0}
        , f1{in_f1}
        , f2{in_f2}
        , f3{in_f3}
        {
        }

        const fixed_24_8_array_4 operator*(int i) const
        {
            return {f3 * i, f2 * i, f1 * i, f0 * i};
        }

        const fixed_24_8_array_4 operator+(const fixed_24_8_array_4& fa) const
        {
            return {f3 + fa.f3, f2 + fa.f2, f1 + fa.f1, f0 + fa.f0};
        }
    };

    /** 
     * values at the corners of the block. 
     * 
     * after setup, the assignments of members to corners is given by 
     * (.f3, .f2, .f1, .f0) = (top-left, top-right, bottom-left, bottom-right). 
     */
    fixed_24_8_array_4 corners[3];

    /** 
     * steps to take in x direction on each advance. 
     * 
     * after setup, the assignments of members to corners is given by 
     * (.f3, .f2, .f1, .f0) = (top-left, top-right, bottom-left, bottom-right). 
     * 
     * the members usually all contain the same value.
     */
    fixed_24_8_array_4 steps_x[3];

    /** 
     * steps to take in y direction on each advance.
     *
     * after setup, the assignments of members to corners is given by 
     * (.f3, .f2, .f1, .f0) = (top-left, top-right, bottom-left, bottom-right). 
     * 
     * the members usually all contain the same value.
     */
    fixed_24_8_array_4 steps_y[3];

    /** default constructor. */
    barycentric_coordinate_block() = default;

    /** default copy constructor. */
    barycentric_coordinate_block(const barycentric_coordinate_block&) = default;

    /** initializes all block corners with the same values. */
    barycentric_coordinate_block(
      const ml::fixed_24_8_t& lambda0, const ml::tvec2<ml::fixed_24_8_t>& step0,
      const ml::fixed_24_8_t& lambda1, const ml::tvec2<ml::fixed_24_8_t>& step1,
      const ml::fixed_24_8_t& lambda2, const ml::tvec2<ml::fixed_24_8_t>& step2)
    {
        corners[0] = {lambda0};
        corners[1] = {lambda1};
        corners[2] = {lambda2};

        steps_x[0] = {step0.x};
        steps_x[1] = {step1.x};
        steps_x[2] = {step2.x};

        steps_y[0] = {step0.y};
        steps_y[1] = {step1.y};
        steps_y[2] = {step2.y};
    }

    /** set up the block size from given that corners contain the values the top-left corner. */
    void setup(int block_size_x, int block_size_y)
    {
        // .f3 contains the values for the top-left corner. we reset all values to the top-left corner.
        corners[0] = {corners[0].f3};
        corners[1] = {corners[1].f3};
        corners[2] = {corners[2].f3};

        // .f2 is the top-right and .f0 the bottom-right corner.
        fixed_24_8_array_4 block_step_x[3] = {
          {0, steps_x[0].f2, 0, steps_x[0].f0},
          {0, steps_x[1].f2, 0, steps_x[1].f0},
          {0, steps_x[2].f2, 0, steps_x[2].f0}};

        // .f1 is the bottom-left and .f0 the bottom-right corner.
        fixed_24_8_array_4 block_step_y[3] = {
          {0, 0, steps_y[0].f1, steps_y[0].f0},
          {0, 0, steps_y[1].f1, steps_y[1].f0},
          {0, 0, steps_y[2].f1, steps_y[2].f0}};

        fixed_24_8_array_4 block_step_xy[3] = {
          block_step_x[0] * block_size_x + block_step_y[0] * block_size_y,
          block_step_x[1] * block_size_x + block_step_y[1] * block_size_y,
          block_step_x[2] * block_size_x + block_step_y[2] * block_size_y};

        corners[0] = corners[0] + block_step_xy[0];
        corners[1] = corners[1] + block_step_xy[1];
        corners[2] = corners[2] + block_step_xy[2];
    }

    /** step block_size_x steps into x direction. */
    void step_x(int block_size_x)
    {
        corners[0] = corners[0] + steps_x[0] * block_size_x;
        corners[1] = corners[1] + steps_x[1] * block_size_x;
        corners[2] = corners[2] + steps_x[2] * block_size_x;
    }

    /** step block_size_y steps into y direction. */
    void step_y(int block_size_y)
    {
        corners[0] = corners[0] + steps_y[0] * block_size_y;
        corners[1] = corners[1] + steps_y[1] * block_size_y;
        corners[2] = corners[2] + steps_y[2] * block_size_y;
    }

    /** store current position. */
    void store_position(fixed_24_8_array_4& c0, fixed_24_8_array_4& c1, fixed_24_8_array_4& c2)
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

    /** check if the current block is (partially) covered. */
    bool check_coverage() const
    {
        auto is_positive = [](const fixed_24_8_array_4& f) -> bool
        { return (f.f3 > 0) && (f.f2 > 0) && (f.f1 > 0) && (f.f0 > 0); };

        if(is_positive(corners[0]) && is_positive(corners[1]) && is_positive(corners[2]))
        {
            // the block is completely inside the triangle.
            return true;
        }

        auto is_negative = [](const fixed_24_8_array_4& f) -> bool
        { return (f.f3 < 0) && (f.f2 < 0) && (f.f1 < 0) && (f.f0 < 0); };

        if(is_negative(corners[0]) || is_negative(corners[1]) || is_negative(corners[2]))
        {
            // the block is completely outside the triangle.
            return false;
        }

        // the block partly covers the triangle.
        return true;
    }

    /** 
     * calculate and return the coverage mask. 
     * 
     * layout:
     * 
     * bit:              8  4  2  1
     * pixsel position: tl tr bl br
     */
    int get_coverage_mask() const
    {
        auto gen_mask = [](const fixed_24_8_array_4& f) -> int
        { return ((f.f3 > 0) << 3) | ((f.f2 > 0) << 2) | ((f.f1 > 0) << 1) | (f.f0 > 0); };
        return gen_mask(corners[0]) & gen_mask(corners[1]) & gen_mask(corners[2]);
    }
};

#endif /* SWR_USE_SIMD */

} /* namespace geom */