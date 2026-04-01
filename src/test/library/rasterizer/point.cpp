/**
 * swr - a software rasterizer
 *
 * test point rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers */
#include <set>
#include <print>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE clipping tests
#include <boost/test/unit_test.hpp>

/* user headers. */
#include "swr_internal.h"
#include "rasterizer/interpolators.h"
#include "rasterizer/fragment.h"
#include "rasterizer/sweep.h"
#include "rasterizer/point.h"

/*
 * Helpers.
 */

std::vector<std::pair<int, int>> collect_covered_pixels(
  rast::point_fixed_vec2 point,
  int width,
  int height)
{
    std::vector<std::pair<int, int>> out;

    rast::for_each_covered_point_pixel(
      point,
      width,
      height,
      [&](int x, int y)
      { out.emplace_back(x, y); });

    return out;
}

/*
 * tests.
 */

BOOST_AUTO_TEST_SUITE(point_coverage)

BOOST_AUTO_TEST_CASE(d3d11_1)
{
    const rast::point_fixed_vec2 point{
      0, 0};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE(pixels.empty());
}

BOOST_AUTO_TEST_CASE(d3d11_2)
{
    const rast::point_fixed_vec2 point{
      16, 0};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE(pixels.empty());
}

BOOST_AUTO_TEST_CASE(d3d11_3)
{
    const rast::point_fixed_vec2 point{
      0.5, 0.5};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 0);
    BOOST_CHECK_EQUAL(pixels[0].second, 0);
}

BOOST_AUTO_TEST_CASE(d3d11_4)
{
    const rast::point_fixed_vec2 point{
      0.5, 1.0};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 0);
    BOOST_CHECK_EQUAL(pixels[0].second, 0);
}

BOOST_AUTO_TEST_CASE(d3d11_5)
{
    const rast::point_fixed_vec2 point{
      1.0, 0.5};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 0);
    BOOST_CHECK_EQUAL(pixels[0].second, 0);
}

BOOST_AUTO_TEST_CASE(d3d11_6)
{
    const rast::point_fixed_vec2 point{
      1.0, 1.0};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 0);
    BOOST_CHECK_EQUAL(pixels[0].second, 0);
}

BOOST_AUTO_TEST_CASE(d3d11_7)
{
    const rast::point_fixed_vec2 point{
      0.2, 0.2};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 0);
    BOOST_CHECK_EQUAL(pixels[0].second, 0);
}

BOOST_AUTO_TEST_CASE(d3d11_8)
{
    const rast::point_fixed_vec2 point{
      0.5, 0.75};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 0);
    BOOST_CHECK_EQUAL(pixels[0].second, 0);
}

BOOST_AUTO_TEST_CASE(d3d11_9)
{
    const rast::point_fixed_vec2 point{
      0.75, 0.5};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 0);
    BOOST_CHECK_EQUAL(pixels[0].second, 0);
}

BOOST_AUTO_TEST_CASE(d3d11_10)
{
    const rast::point_fixed_vec2 point{
      0.75, 0.75};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 0);
    BOOST_CHECK_EQUAL(pixels[0].second, 0);
}

BOOST_AUTO_TEST_CASE(d3d11_11)
{
    const rast::point_fixed_vec2 point{
      0, 16};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_CHECK(pixels.empty());
}

BOOST_AUTO_TEST_CASE(d3d11_12)
{
    const rast::point_fixed_vec2 point{
      5, 16};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 4);
    BOOST_CHECK_EQUAL(pixels[0].second, 15);
}

BOOST_AUTO_TEST_CASE(d3d11_13)
{
    const rast::point_fixed_vec2 point{
      16, 16};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 15);
    BOOST_CHECK_EQUAL(pixels[0].second, 15);
}

BOOST_AUTO_TEST_CASE(point_coverage_emits_one_pixel_for_interior_point)
{
    const rast::point_fixed_vec2 point{
      3.25, 5.75};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
}

BOOST_AUTO_TEST_CASE(point_coverage_selects_expected_pixel)
{
    const rast::point_fixed_vec2 point{
      3.25, 5.75};

    const auto pixels = collect_covered_pixels(point, 16, 16);
    const auto bias = cnl::wrap<rast::point_fixed_t>(rast::FILL_RULE_EDGE_BIAS);

    const int expected_x = static_cast<int>(
      ml::integral_part(
        point.x - bias));
    const int expected_y = static_cast<int>(
      ml::integral_part(
        point.y - bias));

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, expected_x);
    BOOST_CHECK_EQUAL(pixels[0].second, expected_y);
}

BOOST_AUTO_TEST_CASE(point_coverage_clips_when_pixel_is_left_of_viewport)
{
    const rast::point_fixed_vec2 point{
      -1.0, 4.0};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_CHECK(pixels.empty());
}

BOOST_AUTO_TEST_CASE(point_coverage_clips_when_pixel_is_outside_y_range)
{
    const rast::point_fixed_vec2 point{
      4.0, 20.0};

    const auto pixels = collect_covered_pixels(point, 16, 16);

    BOOST_CHECK(pixels.empty());
}

BOOST_AUTO_TEST_SUITE_END();
