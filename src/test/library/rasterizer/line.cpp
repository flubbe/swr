/**
 * swr - a software rasterizer
 *
 * test line rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers */
#include <format>
#include <random>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE clipping tests
#include <boost/test/unit_test.hpp>

/* user headers. */
#include "rasterizer/line.h"

/*
 * Helpers.
 */

struct emitted_pixel
{
    ml::tvec2<int> coord;

    emitted_pixel() = default;
    emitted_pixel(int x, int y)
    : coord{x, y}
    {
    }

    bool operator==(const emitted_pixel& other) const
    {
        return coord.x == other.coord.x
               && coord.y == other.coord.y;
    }
};

inline std::ostream& operator<<(std::ostream& os, const ml::tvec2<int>& v)
{
    return os << "(" << v.x << ", " << v.y << ")";
}

inline std::ostream& operator<<(std::ostream& os, const emitted_pixel& px)
{
    return os << "emitted_pixel{coord=" << px.coord << "}";
}

static std::vector<emitted_pixel> collect_line_pixels(
  const geom::vertex& v1,
  const geom::vertex& v2)
{
    std::vector<emitted_pixel> out;

    rast::rasterize_line_coverage(
      v1, v2,
      [&](int x, int y, [[maybe_unused]] rast::line_emit_kind kind)
      {
          out.emplace_back(x, y);
      });

    return out;
}

static std::pair<geom::vertex, geom::vertex> make_line(
  float sx, float sy, float ex, float ey)
{
    geom::vertex s{};
    geom::vertex e{};

    s.coords = {sx, sy, 0.0f};
    e.coords = {ex, ey, 0.0f};

    return std::make_pair(s, e);
}

/*
 * tests.
 */

BOOST_AUTO_TEST_SUITE(line_coverage)

BOOST_AUTO_TEST_CASE(horizontal)
{
    auto [v1, v2] = make_line(
      1.2f, 3.5f,
      4.8f, 3.5f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {1, 3},
      {2, 3},
      {3, 3}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_1)
{
    auto [v1, v2] = make_line(
      0.5f, 0.5f,
      1.5f, 0.75f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {0, 0}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_2)
{
    auto [v1, v2] = make_line(
      0.7f, -0.1f,
      0.8f, 1.1f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {0, 0}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_3)
{
    auto [v1, v2] = make_line(
      1.0f, 0.5f,
      1.1f, 0.9f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {0, 0}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

#if 0
BOOST_AUTO_TEST_CASE(d3d11_4)
{
    auto [v1, v2] = make_line(
      0.5f, 1.0f,
      0.9f, 0.9f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {0, 0}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_5)
{
    auto [v1, v2] = make_line(
      0.25f, 0.75f,
      0.25f, 1.25f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {0, 0}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_6)
{
    auto [v1, v2] = make_line(
      0.75f, 0.75f,
      0.75f, 1.25f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {0, 0}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}
#endif

BOOST_AUTO_TEST_CASE(d3d11_7)
{
    auto [v1, v2] = make_line(
      0.1f, 0.4f,
      0.4f, 0.1f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_8)
{
    auto [v1, v2] = make_line(
      0.3f, 0.1f,
      0.7f, 0.1f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {0, 0}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_9)
{
    auto [v1, v2] = make_line(
      0.6f, 0.1f,
      0.9f, 0.4f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_10)
{
    auto [v1, v2] = make_line(
      0.2f, 0.4f,
      0.2f, 0.4f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_11)
{
    auto [v1, v2] = make_line(
      0.4f, 0.9f,
      0.1f, 0.6f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_12)
{
    auto [v1, v2] = make_line(
      0.9f, 0.6f,
      0.6f, 0.9f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_13)
{
    auto [v1, v2] = make_line(
      1.5f, 0.75f,
      0.5f, 0.5f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {1, 0}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(d3d11_14)
{
    auto [v1, v2] = make_line(
      1.0f, 0.5f,
      5.5f, 2.0f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {1, 0}, {2, 0}, {3, 1}, {4, 1}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

#if 0
BOOST_AUTO_TEST_CASE(d3d11_15)
{
    auto [v1, v2] = make_line(
      1.5f, 1.0f,
      0.0f, 5.5f);

    const auto pixels = collect_line_pixels(v1, v2);
    const auto expected = std::vector<emitted_pixel>{
      {1, 0}, {1, 1}, {0, 2}, {0, 3}, {0, 4}};

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}
#endif

BOOST_AUTO_TEST_SUITE_END();
