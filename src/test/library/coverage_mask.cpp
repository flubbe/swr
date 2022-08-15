/**
 * swr - a software rasterizer
 *
 * test coverage masks.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers */
#include <random>

/* format library */
#include "fmt/format.h"

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE clipping tests
#include <boost/test/unit_test.hpp>

/* user headers. */
#include "swr_internal.h"

/* make sure we include the non-SIMD code. */
#ifdef SWR_USE_SIMD
#    undef SWR_USE_SIMD
#endif

#include "geometry/barycentric_coords.h"

/* include SIMD code. */
namespace simd
{
#define SWR_USE_SIMD
#include "geometry/barycentric_coords.h"
#undef SWR_USE_SIMD
} /* namespace simd */

/*
 * tests.
 */

BOOST_AUTO_TEST_SUITE(coverage_masks)

BOOST_AUTO_TEST_CASE(init)
{
    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{2};
    ml::fixed_24_8_t lambda2{4};

    ml::tvec2<ml::fixed_24_8_t> step0{0, 1};
    ml::tvec2<ml::fixed_24_8_t> step1{0.5, 2};
    ml::tvec2<ml::fixed_24_8_t> step2{1, 2};

    geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    uint32_t c0[4], c1[4], c2[4];

    std::memcpy(c0, &block.corners[0], sizeof(c0));
    std::memcpy(c1, &block.corners[1], sizeof(c1));
    std::memcpy(c2, &block.corners[2], sizeof(c2));

    BOOST_TEST(c0[0] == cnl::unwrap(lambda0));
    BOOST_TEST(c0[1] == cnl::unwrap(lambda0));
    BOOST_TEST(c0[2] == cnl::unwrap(lambda0));
    BOOST_TEST(c0[3] == cnl::unwrap(lambda0));

    BOOST_TEST(c1[0] == cnl::unwrap(lambda1));
    BOOST_TEST(c1[1] == cnl::unwrap(lambda1));
    BOOST_TEST(c1[2] == cnl::unwrap(lambda1));
    BOOST_TEST(c1[3] == cnl::unwrap(lambda1));

    BOOST_TEST(c2[0] == cnl::unwrap(lambda2));
    BOOST_TEST(c2[1] == cnl::unwrap(lambda2));
    BOOST_TEST(c2[2] == cnl::unwrap(lambda2));
    BOOST_TEST(c2[3] == cnl::unwrap(lambda2));

    uint32_t sx0[4], sx1[4], sx2[4];

    std::memcpy(sx0, &block.steps_x[0], sizeof(sx0));
    std::memcpy(sx1, &block.steps_x[1], sizeof(sx1));
    std::memcpy(sx2, &block.steps_x[2], sizeof(sx2));

    BOOST_TEST(sx0[0] == cnl::unwrap(step0.x));
    BOOST_TEST(sx0[1] == cnl::unwrap(step0.x));
    BOOST_TEST(sx0[2] == cnl::unwrap(step0.x));
    BOOST_TEST(sx0[3] == cnl::unwrap(step0.x));

    BOOST_TEST(sx1[0] == cnl::unwrap(step1.x));
    BOOST_TEST(sx1[1] == cnl::unwrap(step1.x));
    BOOST_TEST(sx1[2] == cnl::unwrap(step1.x));
    BOOST_TEST(sx1[3] == cnl::unwrap(step1.x));

    BOOST_TEST(sx2[0] == cnl::unwrap(step2.x));
    BOOST_TEST(sx2[1] == cnl::unwrap(step2.x));
    BOOST_TEST(sx2[2] == cnl::unwrap(step2.x));
    BOOST_TEST(sx2[3] == cnl::unwrap(step2.x));

    uint32_t sy0[4], sy1[4], sy2[4];

    std::memcpy(sy0, &block.steps_y[0], sizeof(sy0));
    std::memcpy(sy1, &block.steps_y[1], sizeof(sy1));
    std::memcpy(sy2, &block.steps_y[2], sizeof(sy2));

    BOOST_TEST(sy0[0] == cnl::unwrap(step0.y));
    BOOST_TEST(sy0[1] == cnl::unwrap(step0.y));
    BOOST_TEST(sy0[2] == cnl::unwrap(step0.y));
    BOOST_TEST(sy0[3] == cnl::unwrap(step0.y));

    BOOST_TEST(sy1[0] == cnl::unwrap(step1.y));
    BOOST_TEST(sy1[1] == cnl::unwrap(step1.y));
    BOOST_TEST(sy1[2] == cnl::unwrap(step1.y));
    BOOST_TEST(sy1[3] == cnl::unwrap(step1.y));

    BOOST_TEST(sy2[0] == cnl::unwrap(step2.y));
    BOOST_TEST(sy2[1] == cnl::unwrap(step2.y));
    BOOST_TEST(sy2[2] == cnl::unwrap(step2.y));
    BOOST_TEST(sy2[3] == cnl::unwrap(step2.y));
}

BOOST_AUTO_TEST_CASE(setup1)
{
    const int block_size = 2;

    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{2};
    ml::fixed_24_8_t lambda2{4};

    ml::tvec2<ml::fixed_24_8_t> step0{0, 1};
    ml::tvec2<ml::fixed_24_8_t> step1{0.5, 2};
    ml::tvec2<ml::fixed_24_8_t> step2{1, 2};

    geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};
    block.setup(block_size, block_size);

    // calculate the expected values.
    ml::fixed_24_8_t expected_lambda0[4] = {
      lambda0,
      lambda0 + step0.x * block_size,
      lambda0 + step0.y * block_size,
      lambda0 + (step0.x + step0.y) * block_size};
    ml::fixed_24_8_t expected_lambda1[4] = {
      lambda1,
      lambda1 + step1.x * block_size,
      lambda1 + step1.y * block_size,
      lambda1 + (step1.x + step1.y) * block_size};
    ml::fixed_24_8_t expected_lambda2[4] = {
      lambda2,
      lambda2 + step2.x * block_size,
      lambda2 + step2.y * block_size,
      lambda2 + (step2.x + step2.y) * block_size};

    // compare.
    uint32_t q0[4], q1[4], q2[4];

    std::memcpy(q0, &block.corners[0], sizeof(q0));
    std::memcpy(q1, &block.corners[1], sizeof(q1));
    std::memcpy(q2, &block.corners[2], sizeof(q2));

    BOOST_TEST(q0[3] == cnl::unwrap(expected_lambda0[0]));
    BOOST_TEST(q0[2] == cnl::unwrap(expected_lambda0[1]));
    BOOST_TEST(q0[1] == cnl::unwrap(expected_lambda0[2]));
    BOOST_TEST(q0[0] == cnl::unwrap(expected_lambda0[3]));

    BOOST_TEST(q1[3] == cnl::unwrap(expected_lambda1[0]));
    BOOST_TEST(q1[2] == cnl::unwrap(expected_lambda1[1]));
    BOOST_TEST(q1[1] == cnl::unwrap(expected_lambda1[2]));
    BOOST_TEST(q1[0] == cnl::unwrap(expected_lambda1[3]));

    BOOST_TEST(q2[3] == cnl::unwrap(expected_lambda2[0]));
    BOOST_TEST(q2[2] == cnl::unwrap(expected_lambda2[1]));
    BOOST_TEST(q2[1] == cnl::unwrap(expected_lambda2[2]));
    BOOST_TEST(q2[0] == cnl::unwrap(expected_lambda2[3]));
}

BOOST_AUTO_TEST_CASE(setup2)
{
    const int block_size = 2;
    const int block_size_large = 32;

    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{2};
    ml::fixed_24_8_t lambda2{4};

    ml::tvec2<ml::fixed_24_8_t> step0{0, 1};
    ml::tvec2<ml::fixed_24_8_t> step1{0.5, 2};
    ml::tvec2<ml::fixed_24_8_t> step2{1, 2};

    geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};
    block.setup(block_size_large, block_size_large);
    block.step_x(block_size_large);

    block.setup(block_size, block_size);

    // calculate the expected values.
    ml::fixed_24_8_t expected_lambda0[4] = {
      lambda0 + step0.x * block_size_large,
      lambda0 + step0.x * block_size_large + step0.x * block_size,
      lambda0 + step0.x * block_size_large + step0.y * block_size,
      lambda0 + step0.x * block_size_large + (step0.x + step0.y) * block_size};
    ml::fixed_24_8_t expected_lambda1[4] = {
      lambda1 + step1.x * block_size_large,
      lambda1 + step1.x * block_size_large + step1.x * block_size,
      lambda1 + step1.x * block_size_large + step1.y * block_size,
      lambda1 + step1.x * block_size_large + (step1.x + step1.y) * block_size};
    ml::fixed_24_8_t expected_lambda2[4] = {
      lambda2 + step2.x * block_size_large,
      lambda2 + step2.x * block_size_large + step2.x * block_size,
      lambda2 + step2.x * block_size_large + step2.y * block_size,
      lambda2 + step2.x * block_size_large + (step2.x + step2.y) * block_size};

    // compare.
    uint32_t q0[4], q1[4], q2[4];

    std::memcpy(q0, &block.corners[0], sizeof(q0));
    std::memcpy(q1, &block.corners[1], sizeof(q1));
    std::memcpy(q2, &block.corners[2], sizeof(q2));

    BOOST_TEST(q0[3] == cnl::unwrap(expected_lambda0[0]));
    BOOST_TEST(q0[2] == cnl::unwrap(expected_lambda0[1]));
    BOOST_TEST(q0[1] == cnl::unwrap(expected_lambda0[2]));
    BOOST_TEST(q0[0] == cnl::unwrap(expected_lambda0[3]));

    BOOST_TEST(q1[3] == cnl::unwrap(expected_lambda1[0]));
    BOOST_TEST(q1[2] == cnl::unwrap(expected_lambda1[1]));
    BOOST_TEST(q1[1] == cnl::unwrap(expected_lambda1[2]));
    BOOST_TEST(q1[0] == cnl::unwrap(expected_lambda1[3]));

    BOOST_TEST(q2[3] == cnl::unwrap(expected_lambda2[0]));
    BOOST_TEST(q2[2] == cnl::unwrap(expected_lambda2[1]));
    BOOST_TEST(q2[1] == cnl::unwrap(expected_lambda2[2]));
    BOOST_TEST(q2[0] == cnl::unwrap(expected_lambda2[3]));
}

BOOST_AUTO_TEST_CASE(trivial_miss)
{
    ml::fixed_24_8_t lambda0{-1};
    ml::fixed_24_8_t lambda1{-1};
    ml::fixed_24_8_t lambda2{-1};

    ml::tvec2<ml::fixed_24_8_t> step0{0, 0};
    ml::tvec2<ml::fixed_24_8_t> step1{0, 0};
    ml::tvec2<ml::fixed_24_8_t> step2{0, 0};

    geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    /*
     * set up 2x2 blocks.
     *
     * the values of lambda are:
     *
     * (-1,-1,-1) (-1,-1,-1)
     * (-1,-1,-1) (-1,-1,-1)
     */
    block.setup(2, 2);

    BOOST_TEST(block.get_coverage_mask() == 0);
}

BOOST_AUTO_TEST_CASE(trivial_hit)
{
    ml::fixed_24_8_t lambda0{1};
    ml::fixed_24_8_t lambda1{1};
    ml::fixed_24_8_t lambda2{1};

    ml::tvec2<ml::fixed_24_8_t> step0{0, 0};
    ml::tvec2<ml::fixed_24_8_t> step1{0, 0};
    ml::tvec2<ml::fixed_24_8_t> step2{0, 0};

    geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    /*
     * set up 2x2 blocks.
     *
     * the values of lambda are:
     *
     * (1,1,1) (1,1,1)
     * (1,1,1) (1,1,1)
     *
     * the reduced coverage mask is thus 0b1111 = 0xf.
     */
    block.setup(2, 2);

    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xf);
}

BOOST_AUTO_TEST_CASE(mask)
{
    geom::barycentric_coordinate_block block = geom::barycentric_coordinate_block{
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{0, 0},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{0, 0},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{0, 0}};
    block.setup(1, 1);

    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x0);

    block = geom::barycentric_coordinate_block{
      ml::fixed_24_8_t{1}, ml::tvec2<ml::fixed_24_8_t>{-1, -1},
      ml::fixed_24_8_t{1}, ml::tvec2<ml::fixed_24_8_t>{-1, -1},
      ml::fixed_24_8_t{1}, ml::tvec2<ml::fixed_24_8_t>{-1, -1}};
    block.setup(1, 1);

    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x8);

    block = geom::barycentric_coordinate_block{
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{1, -1},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{1, -1},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{1, -1}};
    block.setup(1, 1);

    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x4);

    block = geom::barycentric_coordinate_block{
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{-1, 1},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{-1, 1},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{-1, 1}};
    block.setup(1, 1);

    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x2);

    block = geom::barycentric_coordinate_block{
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{-1, 2},
      ml::fixed_24_8_t{1}, ml::tvec2<ml::fixed_24_8_t>{1, -1},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{-1, 2}};
    block.setup(1, 1);

    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x1);
}

BOOST_AUTO_TEST_CASE(step_hit1)
{
    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{-2};
    ml::fixed_24_8_t lambda2{-4};

    ml::tvec2<ml::fixed_24_8_t> step0{0.5, 0};
    ml::tvec2<ml::fixed_24_8_t> step1{1, -0.5};
    ml::tvec2<ml::fixed_24_8_t> step2{2, -1};

    geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    /*
     * set up 2x2 blocks.
     *
     * the values of lambda are:
     *
     * (0,  -2,-4) (0.5,  -1,-2)
     * (0,-2.5,-4) (0.5,-1.5,-2)
     *
     * the coverage mask is thus 0b0000 = 0x0.
     */
    block.setup(1, 1);

    /* coverage mask: 0b0000 */
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (0.5,  -1,-2) (1,   0,0)
     * (0.5,-1.5,-2) (1,-0.5,0)
     *
     * the coverage mask is thus 0b0000 = 0x0.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (1,   0,0) (1.5,  1,2)
     * (1,-0.5,0) (1.5,0.5,2)
     *
     * the coverage mask is thus 0b0101 = 0x5.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x5);

    /*
     * step in y direction.
     *
     * the values of lambda are:
     *
     * (1,-0.5,-1) (1.5,0.5,1)
     * (1,  -1,-1) (1.5,  0,1)
     *
     * the coverage mask is thus 0b0100 = 0x4.
     */
    block.step_y(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x4);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (1.5,0.5,1) (2,1.5,3)
     * (1.5,  0,1) (2,  1,3)
     *
     * the coverage mask is thus 0b1101 = 0xd.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xd);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (2,1.5,3) (2.5,2.5,5)
     * (2,  1,3) (2.5,  2,5)
     *
     * the coverage mask is thus 0b1111 = 0xf.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xf);
}

BOOST_AUTO_TEST_CASE(step_hit2)
{
    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{-2};
    ml::fixed_24_8_t lambda2{-4};

    ml::tvec2<ml::fixed_24_8_t> step0{0.5, 0};
    ml::tvec2<ml::fixed_24_8_t> step1{1, -0.5};
    ml::tvec2<ml::fixed_24_8_t> step2{2, -1};

    geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    /*
     * set up 2x2 blocks.
     *
     * the values of lambda are:
     *
     * (0,  -2,-4) (0.5,  -1,-2)
     * (0,-2.5,-4) (0.5,-1.5,-2)
     *
     * the coverage mask is thus 0b0000 = 0x0.
     */
    block.setup(1, 1);

    /* coverage mask: 0b0000 */
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (0.5,  -1,-2) (1,   0,0)
     * (0.5,-1.5,-2) (1,-0.5,0)
     *
     * the coverage mask is thus 0b0000 = 0x0.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (1,   0,0) (1.5,  1,2)
     * (1,-0.5,0) (1.5,0.5,2)
     *
     * the coverage mask is thus 0b0101 = 0x5.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x5);

    /*
     * step in y direction.
     *
     * the values of lambda are:
     *
     * (1,-0.5,-1) (1.5,0.5,1)
     * (1,  -1,-1) (1.5,  0,1)
     *
     * the coverage mask is thus 0b0100 = 0x4.
     */
    block.step_y(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x4);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (1.5,0.5,1) (2,1.5,3)
     * (1.5,  0,1) (2,  1,3)
     *
     * the coverage mask is thus 0b1101 = 0xd.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xd);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (2,1.5,3) (2.5,2.5,5)
     * (2,  1,3) (2.5,  2,5)
     *
     * the coverage mask is thus 0b1111 = 0xf.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xf);
}

BOOST_AUTO_TEST_CASE(step_hit3)
{
    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{-2};
    ml::fixed_24_8_t lambda2{-4};

    ml::tvec2<ml::fixed_24_8_t> step0{0.5, 0};
    ml::tvec2<ml::fixed_24_8_t> step1{1, -0.5};
    ml::tvec2<ml::fixed_24_8_t> step2{2, -2};

    geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    /*
     * set up 2x2 blocks.
     *
     * the values of lambda are:
     *
     * (0,  -2,-4) (0.5,  -1,-2)
     * (0,-2.5,-6) (0.5,-1.5,-4)
     *
     * the coverage mask is thus 0b0000 = 0x0.
     */
    block.setup(1, 1);

    /* coverage mask: 0b0000 */
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (1,   0, 0) (1.5,  1,2)
     * (1,-0.5,-2) (1.5,0.5,0)
     *
     * the coverage mask is thus 0b0100 = 0x4.
     */
    block.step_x(2);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x4);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (2,  2,4) (2.5,  3,6)
     * (2,1.5,2) (2.5,2.5,4)
     *
     * the coverage mask is thus 0b1111 = 0xf.
     */
    block.step_x(2);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xf);

    /*
     * step in y direction.
     *
     * the values of lambda are:
     *
     * (2,  1, 0) (2.5,  2,2)
     * (2,0.5,-2) (2.5,1.5,0)
     *
     * the coverage mask is thus 0b0101 = 0x4.
     */
    block.step_y(2);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x4);

    /*
     * step in y direction.
     *
     * the values of lambda are:
     *
     * (2,   0,-4) (2.5,  1,-2)
     * (2,-0.5,-6) (2.5,0.5,-4)
     *
     * the coverage mask is thus 0b0000= 0x0.
     */
    block.step_y(2);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x0);
}

/*
 * test SIMD code.
 */

BOOST_AUTO_TEST_CASE(init_simd)
{
    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{2};
    ml::fixed_24_8_t lambda2{4};

    ml::tvec2<ml::fixed_24_8_t> step0{0, 1};
    ml::tvec2<ml::fixed_24_8_t> step1{0.5, 2};
    ml::tvec2<ml::fixed_24_8_t> step2{1, 2};

    simd::geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    uint32_t c0[4], c1[4], c2[4];

    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(c0), block.corners[0]);
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(c1), block.corners[1]);
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(c2), block.corners[2]);

    BOOST_TEST(c0[0] == cnl::unwrap(lambda0));
    BOOST_TEST(c0[1] == cnl::unwrap(lambda0));
    BOOST_TEST(c0[2] == cnl::unwrap(lambda0));
    BOOST_TEST(c0[3] == cnl::unwrap(lambda0));

    BOOST_TEST(c1[0] == cnl::unwrap(lambda1));
    BOOST_TEST(c1[1] == cnl::unwrap(lambda1));
    BOOST_TEST(c1[2] == cnl::unwrap(lambda1));
    BOOST_TEST(c1[3] == cnl::unwrap(lambda1));

    BOOST_TEST(c2[0] == cnl::unwrap(lambda2));
    BOOST_TEST(c2[1] == cnl::unwrap(lambda2));
    BOOST_TEST(c2[2] == cnl::unwrap(lambda2));
    BOOST_TEST(c2[3] == cnl::unwrap(lambda2));

    uint32_t sx0[4], sx1[4], sx2[4];

    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(sx0), block.steps_x[0]);
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(sx1), block.steps_x[1]);
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(sx2), block.steps_x[2]);

    BOOST_TEST(sx0[0] == cnl::unwrap(step0.x));
    BOOST_TEST(sx0[1] == cnl::unwrap(step0.x));
    BOOST_TEST(sx0[2] == cnl::unwrap(step0.x));
    BOOST_TEST(sx0[3] == cnl::unwrap(step0.x));

    BOOST_TEST(sx1[0] == cnl::unwrap(step1.x));
    BOOST_TEST(sx1[1] == cnl::unwrap(step1.x));
    BOOST_TEST(sx1[2] == cnl::unwrap(step1.x));
    BOOST_TEST(sx1[3] == cnl::unwrap(step1.x));

    BOOST_TEST(sx2[0] == cnl::unwrap(step2.x));
    BOOST_TEST(sx2[1] == cnl::unwrap(step2.x));
    BOOST_TEST(sx2[2] == cnl::unwrap(step2.x));
    BOOST_TEST(sx2[3] == cnl::unwrap(step2.x));

    uint32_t sy0[4], sy1[4], sy2[4];

    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(sy0), block.steps_y[0]);
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(sy1), block.steps_y[1]);
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(sy2), block.steps_y[2]);

    BOOST_TEST(sy0[0] == cnl::unwrap(step0.y));
    BOOST_TEST(sy0[1] == cnl::unwrap(step0.y));
    BOOST_TEST(sy0[2] == cnl::unwrap(step0.y));
    BOOST_TEST(sy0[3] == cnl::unwrap(step0.y));

    BOOST_TEST(sy1[0] == cnl::unwrap(step1.y));
    BOOST_TEST(sy1[1] == cnl::unwrap(step1.y));
    BOOST_TEST(sy1[2] == cnl::unwrap(step1.y));
    BOOST_TEST(sy1[3] == cnl::unwrap(step1.y));

    BOOST_TEST(sy2[0] == cnl::unwrap(step2.y));
    BOOST_TEST(sy2[1] == cnl::unwrap(step2.y));
    BOOST_TEST(sy2[2] == cnl::unwrap(step2.y));
    BOOST_TEST(sy2[3] == cnl::unwrap(step2.y));
}

BOOST_AUTO_TEST_CASE(setup1_simd)
{
    const int block_size = 2;

    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{2};
    ml::fixed_24_8_t lambda2{4};

    ml::tvec2<ml::fixed_24_8_t> step0{0, 1};
    ml::tvec2<ml::fixed_24_8_t> step1{0.5, 2};
    ml::tvec2<ml::fixed_24_8_t> step2{1, 2};

    simd::geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};
    block.setup(block_size, block_size);

    // calculate the expected values.
    ml::fixed_24_8_t expected_lambda0[4] = {
      lambda0,
      lambda0 + step0.x * block_size,
      lambda0 + step0.y * block_size,
      lambda0 + (step0.x + step0.y) * block_size};
    ml::fixed_24_8_t expected_lambda1[4] = {
      lambda1,
      lambda1 + step1.x * block_size,
      lambda1 + step1.y * block_size,
      lambda1 + (step1.x + step1.y) * block_size};
    ml::fixed_24_8_t expected_lambda2[4] = {
      lambda2,
      lambda2 + step2.x * block_size,
      lambda2 + step2.y * block_size,
      lambda2 + (step2.x + step2.y) * block_size};

    // compare.
    uint32_t q0[4], q1[4], q2[4];

    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(q0), block.corners[0]);
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(q1), block.corners[1]);
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(q2), block.corners[2]);

    BOOST_TEST(q0[3] == cnl::unwrap(expected_lambda0[0]));
    BOOST_TEST(q0[2] == cnl::unwrap(expected_lambda0[1]));
    BOOST_TEST(q0[1] == cnl::unwrap(expected_lambda0[2]));
    BOOST_TEST(q0[0] == cnl::unwrap(expected_lambda0[3]));

    BOOST_TEST(q1[3] == cnl::unwrap(expected_lambda1[0]));
    BOOST_TEST(q1[2] == cnl::unwrap(expected_lambda1[1]));
    BOOST_TEST(q1[1] == cnl::unwrap(expected_lambda1[2]));
    BOOST_TEST(q1[0] == cnl::unwrap(expected_lambda1[3]));

    BOOST_TEST(q2[3] == cnl::unwrap(expected_lambda2[0]));
    BOOST_TEST(q2[2] == cnl::unwrap(expected_lambda2[1]));
    BOOST_TEST(q2[1] == cnl::unwrap(expected_lambda2[2]));
    BOOST_TEST(q2[0] == cnl::unwrap(expected_lambda2[3]));
}

BOOST_AUTO_TEST_CASE(setup2_simd)
{
    const int block_size = 2;
    const int block_size_large = 32;

    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{2};
    ml::fixed_24_8_t lambda2{4};

    ml::tvec2<ml::fixed_24_8_t> step0{0, 1};
    ml::tvec2<ml::fixed_24_8_t> step1{0.5, 2};
    ml::tvec2<ml::fixed_24_8_t> step2{1, 2};

    simd::geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};
    block.setup(block_size_large, block_size_large);
    block.step_x(block_size_large);

    block.setup(block_size, block_size);

    // calculate the expected values.
    ml::fixed_24_8_t expected_lambda0[4] = {
      lambda0 + step0.x * block_size_large,
      lambda0 + step0.x * block_size_large + step0.x * block_size,
      lambda0 + step0.x * block_size_large + step0.y * block_size,
      lambda0 + step0.x * block_size_large + (step0.x + step0.y) * block_size};
    ml::fixed_24_8_t expected_lambda1[4] = {
      lambda1 + step1.x * block_size_large,
      lambda1 + step1.x * block_size_large + step1.x * block_size,
      lambda1 + step1.x * block_size_large + step1.y * block_size,
      lambda1 + step1.x * block_size_large + (step1.x + step1.y) * block_size};
    ml::fixed_24_8_t expected_lambda2[4] = {
      lambda2 + step2.x * block_size_large,
      lambda2 + step2.x * block_size_large + step2.x * block_size,
      lambda2 + step2.x * block_size_large + step2.y * block_size,
      lambda2 + step2.x * block_size_large + (step2.x + step2.y) * block_size};

    // compare.
    uint32_t q0[4], q1[4], q2[4];

    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(q0), block.corners[0]);
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(q1), block.corners[1]);
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(q2), block.corners[2]);

    BOOST_TEST(q0[3] == cnl::unwrap(expected_lambda0[0]));
    BOOST_TEST(q0[2] == cnl::unwrap(expected_lambda0[1]));
    BOOST_TEST(q0[1] == cnl::unwrap(expected_lambda0[2]));
    BOOST_TEST(q0[0] == cnl::unwrap(expected_lambda0[3]));

    BOOST_TEST(q1[3] == cnl::unwrap(expected_lambda1[0]));
    BOOST_TEST(q1[2] == cnl::unwrap(expected_lambda1[1]));
    BOOST_TEST(q1[1] == cnl::unwrap(expected_lambda1[2]));
    BOOST_TEST(q1[0] == cnl::unwrap(expected_lambda1[3]));

    BOOST_TEST(q2[3] == cnl::unwrap(expected_lambda2[0]));
    BOOST_TEST(q2[2] == cnl::unwrap(expected_lambda2[1]));
    BOOST_TEST(q2[1] == cnl::unwrap(expected_lambda2[2]));
    BOOST_TEST(q2[0] == cnl::unwrap(expected_lambda2[3]));
}

BOOST_AUTO_TEST_CASE(trivial_miss_simd)
{
    ml::fixed_24_8_t lambda0{-1};
    ml::fixed_24_8_t lambda1{-1};
    ml::fixed_24_8_t lambda2{-1};

    ml::tvec2<ml::fixed_24_8_t> step0{0, 0};
    ml::tvec2<ml::fixed_24_8_t> step1{0, 0};
    ml::tvec2<ml::fixed_24_8_t> step2{0, 0};

    simd::geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    /*
     * set up 2x2 blocks.
     *
     * the values of lambda are:
     *
     * (-1,-1,-1) (-1,-1,-1)
     * (-1,-1,-1) (-1,-1,-1)
     */
    block.setup(2, 2);

    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0);
}

BOOST_AUTO_TEST_CASE(trivial_hit_simd)
{
    ml::fixed_24_8_t lambda0{1};
    ml::fixed_24_8_t lambda1{1};
    ml::fixed_24_8_t lambda2{1};

    ml::tvec2<ml::fixed_24_8_t> step0{0, 0};
    ml::tvec2<ml::fixed_24_8_t> step1{0, 0};
    ml::tvec2<ml::fixed_24_8_t> step2{0, 0};

    simd::geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    /*
     * set up 2x2 blocks.
     *
     * the values of lambda are:
     *
     * (1,1,1) (1,1,1)
     * (1,1,1) (1,1,1)
     *
     * the coverage mask is thus 0b1111 = 0xf.
     */
    block.setup(2, 2);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xf);
}

BOOST_AUTO_TEST_CASE(mask_simd)
{
    simd::geom::barycentric_coordinate_block block = simd::geom::barycentric_coordinate_block{
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{0, 0},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{0, 0},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{0, 0}};
    block.setup(1, 1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x0);

    block = simd::geom::barycentric_coordinate_block{
      ml::fixed_24_8_t{1}, ml::tvec2<ml::fixed_24_8_t>{-1, -1},
      ml::fixed_24_8_t{1}, ml::tvec2<ml::fixed_24_8_t>{-1, -1},
      ml::fixed_24_8_t{1}, ml::tvec2<ml::fixed_24_8_t>{-1, -1}};
    block.setup(1, 1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x8);

    block = simd::geom::barycentric_coordinate_block{
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{1, -1},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{1, -1},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{1, -1}};
    block.setup(1, 1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x4);

    block = simd::geom::barycentric_coordinate_block{
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{-1, 1},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{-1, 1},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{-1, 1}};
    block.setup(1, 1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x2);

    block = simd::geom::barycentric_coordinate_block{
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{-1, 2},
      ml::fixed_24_8_t{1}, ml::tvec2<ml::fixed_24_8_t>{1, -1},
      ml::fixed_24_8_t{0}, ml::tvec2<ml::fixed_24_8_t>{-1, 2}};
    block.setup(1, 1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x1);
}

BOOST_AUTO_TEST_CASE(step_hit1_simd)
{
    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{-2};
    ml::fixed_24_8_t lambda2{-4};

    ml::tvec2<ml::fixed_24_8_t> step0{0.5, 0};
    ml::tvec2<ml::fixed_24_8_t> step1{1, -0.5};
    ml::tvec2<ml::fixed_24_8_t> step2{2, -1};

    simd::geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    /*
     * set up 2x2 blocks.
     *
     * the values of lambda are:
     *
     * (0,  -2,-4) (0.5,  -1,-2)
     * (0,-2.5,-4) (0.5,-1.5,-2)
     *
     * the coverage mask is thus 0b0000 = 0x0.
     */
    block.setup(1, 1);

    /* coverage mask: 0b0000 */
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (0.5,  -1,-2) (1,   0,0)
     * (0.5,-1.5,-2) (1,-0.5,0)
     *
     * the coverage mask is thus 0b0000 = 0x0.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (1,   0,0) (1.5,  1,2)
     * (1,-0.5,0) (1.5,0.5,2)
     *
     * the coverage mask is thus 0b0101 = 0x5.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x5);

    /*
     * step in y direction.
     *
     * the values of lambda are:
     *
     * (1,-0.5,-1) (1.5,0.5,1)
     * (1,  -1,-1) (1.5,  0,1)
     *
     * the coverage mask is thus 0b0100 = 0x4.
     */
    block.step_y(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x4);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (1.5,0.5,1) (2,1.5,3)
     * (1.5,  0,1) (2,  1,3)
     *
     * the coverage mask is thus 0b1101 = 0xd.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xd);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (2,1.5,3) (2.5,2.5,5)
     * (2,  1,3) (2.5,  2,5)
     *
     * the coverage mask is thus 0b1111 = 0xf.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xf);
}

BOOST_AUTO_TEST_CASE(step_hit2_simd)
{
    ml::fixed_24_8_t lambda0{0};
    ml::fixed_24_8_t lambda1{-2};
    ml::fixed_24_8_t lambda2{-4};

    ml::tvec2<ml::fixed_24_8_t> step0{0.5, 0};
    ml::tvec2<ml::fixed_24_8_t> step1{1, -0.5};
    ml::tvec2<ml::fixed_24_8_t> step2{2, -1};

    geom::barycentric_coordinate_block block{
      lambda0, step0,
      lambda1, step1,
      lambda2, step2};

    /*
     * set up 2x2 blocks.
     *
     * the values of lambda are:
     *
     * (0,  -2,-4) (0.5,  -1,-2)
     * (0,-2.5,-4) (0.5,-1.5,-2)
     *
     * the coverage mask is thus 0b0000 = 0x0.
     */
    block.setup(1, 1);

    /* coverage mask: 0b0000 */
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (0.5,  -1,-2) (1,   0,0)
     * (0.5,-1.5,-2) (1,-0.5,0)
     *
     * the coverage mask is thus 0b0000 = 0x0.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (1,   0,0) (1.5,  1,2)
     * (1,-0.5,0) (1.5,0.5,2)
     *
     * the coverage mask is thus 0b0101 = 0x5.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x5);

    /*
     * step in y direction.
     *
     * the values of lambda are:
     *
     * (1,-0.5,-1) (1.5,0.5,1)
     * (1,  -1,-1) (1.5,  0,1)
     *
     * the coverage mask is thus 0b0100 = 0x4.
     */
    block.step_y(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0x4);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (1.5,0.5,1) (2,1.5,3)
     * (1.5,  0,1) (2,  1,3)
     *
     * the coverage mask is thus 0b1101 = 0xd.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xd);

    /*
     * step in x direction.
     *
     * the values of lambda are:
     *
     * (2,1.5,3) (2.5,2.5,5)
     * (2,  1,3) (2.5,  2,5)
     *
     * the coverage mask is thus 0b1111 = 0xf.
     */
    block.step_x(1);
    BOOST_TEST(geom::reduce_coverage_mask(block.get_coverage_mask()) == 0xf);
}

template<typename T, size_t N>
size_t countof([[maybe_unused]] T const (&array)[N])
{
    return N;
}

/*
 * the block size in the rasterizer will possibly get adjusted, so we define our own here to not accidentally break the test.
 */
namespace test
{

constexpr std::uint32_t rasterizer_block_shift{5};
constexpr std::uint32_t rasterizer_block_size{1 << rasterizer_block_shift};
static_assert(utils::is_power_of_two(rasterizer_block_size), "rasterizer_block_size has to be a power of 2");

inline int lower_align_on_block_size(int v)
{
    return v & ~(rasterizer_block_size - 1);
};

inline int upper_align_on_block_size(int v)
{
    return (v + (rasterizer_block_size - 1)) & ~(rasterizer_block_size - 1);
}

} /* namespace test */

BOOST_AUTO_TEST_CASE(triangle_coarse)
{
    ml::vec2 v1_xy{100, 100};
    ml::vec2 v2_xy{200, 100};
    ml::vec2 v3_xy{100, 200};

    auto area = (v2_xy - v1_xy).area(v3_xy - v1_xy);
    BOOST_TEST(area > 0);

    ml::vec2_fixed<4> v1_xy_fix(v1_xy.x, v1_xy.y);
    ml::vec2_fixed<4> v2_xy_fix(v2_xy.x, v2_xy.y);
    ml::vec2_fixed<4> v3_xy_fix(v3_xy.x, v3_xy.y);

    geom::edge_function_fixed edges_fix[3] = {
      {v1_xy_fix, v2_xy_fix}, {v2_xy_fix, v3_xy_fix}, {v3_xy_fix, v1_xy_fix}};

    auto v1x = ml::truncate_unchecked(v1_xy.x);
    auto v1y = ml::truncate_unchecked(v1_xy.y);
    auto v2x = ml::truncate_unchecked(v2_xy.x);
    auto v2y = ml::truncate_unchecked(v2_xy.y);
    auto v3x = ml::truncate_unchecked(v3_xy.x);
    auto v3y = ml::truncate_unchecked(v3_xy.y);

    const int width = 640;
    const int height = 480;

    std::uint32_t start_x{0}, start_y{0}, end_x{0}, end_y{0};
    start_x = test::lower_align_on_block_size(std::max(std::min({v1x, v2x, v3x}), 0));
    end_x = test::upper_align_on_block_size(std::min(std::max({v1x + 1, v2x + 1, v3x + 1}), width));
    start_y = test::lower_align_on_block_size(std::max(std::min({v1y, v2y, v3y}), 0));
    end_y = test::upper_align_on_block_size(std::min(std::max({v1y + 1, v2y + 1, v3y + 1}), height));

    /*
     * bounding box (32-aligned): 3*32 = 96 < 100; 200 < 224 = 7*32.
     */
    BOOST_TEST(start_x == 96);
    BOOST_TEST(start_y == 96);
    BOOST_TEST(end_x == 224);
    BOOST_TEST(end_y == 224);

    /*
     * triangle:
     *
     *         96    128    160    192    224
     *    96
     *          *--------------------*
     *   128    |                 *
     *          |             *
     *   160    |         *
     *          |     *
     *   192    | *
     *          *
     *   224
     *
     * coverage:
     *
     *       96    128    160    192    224
     *    96   0      0      0      0      0
     *          *--------------------*
     *   128   0|     1      1    * 0      0
     *          |             *
     *   160   0|     1   *  0      0      0
     *          |     *
     *   192   0| *   0      0      0      0
     *          *
     *   224   0      0      0      0      0
     *
     * corresponding 2x2 4-bit masks (order: top-left top-right bottom-left bottom-right):
     *
     *  0001 0011 0010 0000   =   1320
     *  0101 1110 1000 0000   =   5e80
     *  0100 1000 0000 0000   =   4800
     *  0000 0000 0000 0000   =   0000
     */

    const int reference_masks_32[16] = {
      0x1, 0x3, 0x2, 0x0,
      0x5, 0xe, 0x8, 0x0,
      0x4, 0x8, 0x0, 0x0,
      0x0, 0x0, 0x0, 0x0};
    const int* ref_mask_ptr = reference_masks_32;

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

    const ml::vec2 screen_coords{static_cast<float>(start_x) + 0.5f, static_cast<float>(start_y) + 0.5f};

    for(auto y = start_y; y < end_y; y += test::rasterizer_block_size)
    {
        // initialize lambdas for the corners of the block.
        geom::barycentric_coordinate_block lambdas_box{
          lambda_row_top_left[0].value, lambda_row_top_left[0].step,
          lambda_row_top_left[1].value, lambda_row_top_left[1].step,
          lambda_row_top_left[2].value, lambda_row_top_left[2].step};
        lambdas_box.setup(test::rasterizer_block_size, test::rasterizer_block_size);

        for(auto x = start_x; x < end_x; x += test::rasterizer_block_size)
        {
            BOOST_REQUIRE(ref_mask_ptr < reference_masks_32 + countof(reference_masks_32));

            BOOST_TEST(geom::reduce_coverage_mask(lambdas_box.get_coverage_mask()) == *ref_mask_ptr);
            ++ref_mask_ptr;

            lambdas_box.step_x(test::rasterizer_block_size);
        }

        // advance y
        lambda_row_top_left[0].step_y(test::rasterizer_block_size);
        lambda_row_top_left[1].step_y(test::rasterizer_block_size);
        lambda_row_top_left[2].step_y(test::rasterizer_block_size);
    }
}

BOOST_AUTO_TEST_SUITE_END();
