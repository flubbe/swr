/**
 * swr - a software rasterizer
 *
 * test output-merger blending helpers.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <array>
#include <cstdint>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE output merger
#include <boost/test/unit_test.hpp>

/* user headers. */
#include "swr_internal.h"

namespace
{

void check_vec4_close(
  const ml::vec4& actual,
  const ml::vec4& expected,
  float epsilon = 1e-6f)
{
    BOOST_CHECK_SMALL(actual.x - expected.x, epsilon);
    BOOST_CHECK_SMALL(actual.y - expected.y, epsilon);
    BOOST_CHECK_SMALL(actual.z - expected.z, epsilon);
    BOOST_CHECK_SMALL(actual.w - expected.w, epsilon);
}

template<typename T>
void check_array_equal(
  const std::array<T, 4>& actual,
  const std::array<T, 4>& expected)
{
    BOOST_CHECK_EQUAL_COLLECTIONS(
      actual.begin(),
      actual.end(),
      expected.begin(),
      expected.end());
}

void check_vec4_array_close(
  const std::array<ml::vec4, 4>& actual,
  const std::array<ml::vec4, 4>& expected)
{
    for(std::size_t i = 0; i < actual.size(); ++i)
    {
        BOOST_TEST_CONTEXT("lane " << i)
        {
            check_vec4_close(actual[i], expected[i]);
        }
    }
}

}    // namespace

BOOST_AUTO_TEST_SUITE(output_merger_tests)

BOOST_AUTO_TEST_CASE(packed_argb8888_blend_factors_handle_source_and_destination)
{
    const swr::pixel_format_converter converter{
      swr::pixel_format_descriptor::named_format(swr::pixel_format::argb8888)};

    constexpr std::uint32_t src = 0x80402010;
    constexpr std::uint32_t dest = 0x10203040;

    BOOST_CHECK_EQUAL(
      swr::output_merger::blend(
        converter,
        swr::blend_func::one,
        swr::blend_func::zero,
        src,
        dest),
      src);

    BOOST_CHECK_EQUAL(
      swr::output_merger::blend(
        converter,
        swr::blend_func::zero,
        swr::blend_func::one,
        src,
        dest),
      dest);

    BOOST_CHECK_EQUAL(
      swr::output_merger::blend(
        converter,
        swr::blend_func::zero,
        swr::blend_func::zero,
        src,
        dest),
      0u);

    BOOST_CHECK_EQUAL(
      swr::output_merger::blend(
        converter,
        swr::blend_func::src_alpha,
        swr::blend_func::one_minus_src_alpha,
        0x00112233,
        dest),
      dest);

    BOOST_CHECK_EQUAL(
      swr::output_merger::blend(
        converter,
        swr::blend_func::src_alpha,
        swr::blend_func::one_minus_src_alpha,
        0xff112233,
        dest),
      0xff112233u);
}

BOOST_AUTO_TEST_CASE(packed_argb8888_blend_block_matches_scalar_factor_selection)
{
    const swr::pixel_format_converter converter{
      swr::pixel_format_descriptor::named_format(swr::pixel_format::argb8888)};

    const std::array<std::uint32_t, 4> src{
      0x10203040,
      0x50607080,
      0x90a0b0c0,
      0xd0e0f001};
    const std::array<std::uint32_t, 4> dest{
      0x01020304,
      0x05060708,
      0x090a0b0c,
      0x0d0e0f10};
    std::array<std::uint32_t, 4> out{};

    swr::output_merger::blend_block(
      converter,
      swr::blend_func::one,
      swr::blend_func::zero,
      src,
      dest,
      out);
    check_array_equal(out, src);

    swr::output_merger::blend_block(
      converter,
      swr::blend_func::zero,
      swr::blend_func::one,
      src,
      dest,
      out);
    check_array_equal(out, dest);

    swr::output_merger::blend_block(
      converter,
      swr::blend_func::zero,
      swr::blend_func::zero,
      src,
      dest,
      out);
    check_array_equal(out, {0u, 0u, 0u, 0u});
}

BOOST_AUTO_TEST_CASE(float_color_blend_factors_handle_source_and_destination)
{
    const ml::vec4 src{0.2f, 0.4f, 0.6f, 0.8f};
    const ml::vec4 dest{0.9f, 0.7f, 0.5f, 0.3f};

    check_vec4_close(
      swr::output_merger::blend(
        swr::blend_func::one,
        swr::blend_func::zero,
        src,
        dest),
      src);

    check_vec4_close(
      swr::output_merger::blend(
        swr::blend_func::zero,
        swr::blend_func::one,
        src,
        dest),
      dest);

    check_vec4_close(
      swr::output_merger::blend(
        swr::blend_func::zero,
        swr::blend_func::zero,
        src,
        dest),
      ml::vec4::zero());

    check_vec4_close(
      swr::output_merger::blend(
        swr::blend_func::src_alpha,
        swr::blend_func::one_minus_src_alpha,
        src,
        dest),
      ml::lerp(src.a, dest, src));

    check_vec4_close(
      swr::output_merger::blend(
        swr::blend_func::zero,
        swr::blend_func::src_color,
        src,
        dest),
      src * dest);
}

BOOST_AUTO_TEST_CASE(float_color_blend_block_matches_scalar_factor_selection)
{
    const std::array<ml::vec4, 4> src{
      ml::vec4{0.2f, 0.4f, 0.6f, 0.8f},
      ml::vec4{0.1f, 0.3f, 0.5f, 0.7f},
      ml::vec4{0.9f, 0.7f, 0.5f, 0.3f},
      ml::vec4{0.8f, 0.6f, 0.4f, 0.2f}};
    const std::array<ml::vec4, 4> dest{
      ml::vec4{0.9f, 0.7f, 0.5f, 0.3f},
      ml::vec4{0.8f, 0.6f, 0.4f, 0.2f},
      ml::vec4{0.1f, 0.3f, 0.5f, 0.7f},
      ml::vec4{0.2f, 0.4f, 0.6f, 0.8f}};
    std::array<ml::vec4, 4> out{};

    swr::output_merger::blend_block(
      swr::blend_func::one,
      swr::blend_func::zero,
      src,
      dest,
      out);
    check_vec4_array_close(out, src);

    swr::output_merger::blend_block(
      swr::blend_func::zero,
      swr::blend_func::one,
      src,
      dest,
      out);
    check_vec4_array_close(out, dest);

    swr::output_merger::blend_block(
      swr::blend_func::zero,
      swr::blend_func::zero,
      src,
      dest,
      out);
    check_vec4_array_close(
      out,
      {ml::vec4::zero(),
       ml::vec4::zero(),
       ml::vec4::zero(),
       ml::vec4::zero()});

    swr::output_merger::blend_block(
      swr::blend_func::src_alpha,
      swr::blend_func::one_minus_src_alpha,
      src,
      dest,
      out);
    check_vec4_array_close(
      out,
      {ml::lerp(src[0].a, dest[0], src[0]),
       ml::lerp(src[1].a, dest[1], src[1]),
       ml::lerp(src[2].a, dest[2], src[2]),
       ml::lerp(src[3].a, dest[3], src[3])});
}

BOOST_AUTO_TEST_SUITE_END()
