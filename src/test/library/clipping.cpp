/**
 * swr - a software rasterizer
 * 
 * test clipping functions.
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
#include "clipping.h"

/*
 * tests.
 */

BOOST_AUTO_TEST_SUITE(clipping)

BOOST_AUTO_TEST_CASE(empty_input)
{
    // input data.
    swr::impl::vertex_buffer points;
    swr::impl::index_buffer indices;

    // output data.
    swr::impl::vertex_buffer out;

    /*
     * test empty input for line clipping.
     */
    swr::impl::clip_line_buffer(points, indices, swr::impl::clip_output::point_list, out);
    BOOST_TEST(out.size() == 0);

    swr::impl::clip_line_buffer(points, indices, swr::impl::clip_output::line_list, out);
    BOOST_TEST(out.size() == 0);

    swr::impl::clip_line_buffer(points, indices, swr::impl::clip_output::triangle_list, out);
    BOOST_TEST(out.size() == 0);

    /*
     * test empty input for triangle clipping.
     */
    swr::impl::clip_triangle_buffer(points, indices, swr::impl::clip_output::point_list, out);
    BOOST_TEST(out.size() == 0);

    swr::impl::clip_triangle_buffer(points, indices, swr::impl::clip_output::line_list, out);
    BOOST_TEST(out.size() == 0);

    swr::impl::clip_triangle_buffer(points, indices, swr::impl::clip_output::triangle_list, out);
    BOOST_TEST(out.size() == 0);
}

/* get bits of float type. note: in C++20, one should use std::bit_cast. */
static_assert(sizeof(uint32_t) == sizeof(float), "Types sizes need to match for get_bits to work.");
auto get_bits = [](float f) -> uint32_t
{
    uint32_t temp;
    std::memcpy(&temp, &f, sizeof(float));
    return temp;
};

BOOST_AUTO_TEST_CASE(line_clip_preserve)
{
    /*
     * vertices are inside the view frustum if -w <= x,y,w <= w and w > 0.
     * we first fill some coordinates into the vertex buffer, and then set the clip flags (which are assumed to be set by the clipping functions).
     * 
     * vertices that are inside the view frustum need to have their coordinates preserved (bit-exact).
     * 
     * clip_line_buffer does not check if the supplied indices are valid - we have to ensure that they are.
     */

    // input data.
    swr::impl::vertex_buffer points = {
      ml::vec4{0, 0, 0, 1}, ml::vec4{1, 0, 0, 1},
      ml::vec4{0, 1, 0, 1}, ml::vec4{0, 0, 1, 1},
      ml::vec4{0.123, -0.456, 0.789, 1.234}, ml::vec4{-0.123, -0.456, -0.789, 12.34},
      ml::vec4{0.1, 0.2, 0.3, 0.4}, ml::vec4{0.5, 0.6, 0.7, 0.8},
      ml::vec4{0.9, -0.1, -0.2, 1.234}, ml::vec4{-0.3, -0.4, -0.5, 0.6},
      ml::vec4{-0.7, -0.8, -0.9, 1.234}, ml::vec4{-10, -20, -30, 40},
      ml::vec4{0.0001, 0.0002, 0.0003, 0.0004}, ml::vec4{-12345.67, -12345.67, -0.789, 23456.98}};
    swr::impl::index_buffer indices = {
      0, 1,
      2, 3,
      4, 5,
      6, 7,
      8, 9,
      10, 11,
      12, 13};

    // output data.
    swr::impl::vertex_buffer out;

    // clip lines.
    BOOST_REQUIRE((indices.size() & 1) == 0);
    swr::impl::clip_line_buffer(points, indices, swr::impl::clip_output::line_list, out);
    BOOST_TEST(out.size() == points.size());

    BOOST_REQUIRE(out.size() == points.size());
    for(size_t i = 0; i < points.size(); ++i)
    {
        // compare bits.
        BOOST_TEST(get_bits(points[i].coords.x) == get_bits(out[i].coords.x));
        BOOST_TEST(get_bits(points[i].coords.y) == get_bits(out[i].coords.y));
        BOOST_TEST(get_bits(points[i].coords.z) == get_bits(out[i].coords.z));
        BOOST_TEST(get_bits(points[i].coords.w) == get_bits(out[i].coords.w));
    }

    /*
     * do the same test with randomly generated values.
     */
    std::random_device rnd_device;
    std::mt19937 mersenne_engine{rnd_device()};
    std::uniform_real_distribution<float> dist{-10000, 10000};

    auto gen = [&dist, &mersenne_engine]()
    { return dist(mersenne_engine); };
    auto vec_rnd = [&gen]() -> ml::vec4
    {
        return {gen(), gen(), gen(), gen()};
    };
    auto in_frustum = [](ml::vec4 v) -> bool
    {
        return (v.w > 0) && (-v.w <= v.x) && (v.x <= v.w) && (-v.w <= v.y) && (v.y <= v.w) && (-v.w <= v.z) && (v.z <= v.w);
    };

    indices.clear();
    indices.push_back(0);
    indices.push_back(1);
    for(int k = 0; k < 10000; ++k)
    {
        do
        {
            points.clear();
            points.emplace_back(vec_rnd());
            points.emplace_back(vec_rnd());
        } while(!in_frustum(points[0].coords) || !in_frustum(points[1].coords));

        out.clear();
        swr::impl::clip_line_buffer(points, indices, swr::impl::clip_output::line_list, out);
        BOOST_TEST(out.size() == 2);

        BOOST_TEST(get_bits(points[0].coords.x) == get_bits(out[0].coords.x));
        BOOST_TEST(get_bits(points[0].coords.y) == get_bits(out[0].coords.y));
        BOOST_TEST(get_bits(points[0].coords.z) == get_bits(out[0].coords.z));
        BOOST_TEST(get_bits(points[0].coords.w) == get_bits(out[0].coords.w));

        BOOST_TEST(get_bits(points[1].coords.x) == get_bits(out[1].coords.x));
        BOOST_TEST(get_bits(points[1].coords.y) == get_bits(out[1].coords.y));
        BOOST_TEST(get_bits(points[1].coords.z) == get_bits(out[1].coords.z));
        BOOST_TEST(get_bits(points[1].coords.w) == get_bits(out[1].coords.w));
    }
}

BOOST_AUTO_TEST_CASE(line_clip)
{
    swr::impl::vertex_buffer points;
    swr::impl::index_buffer indices = {0, 1};

    swr::impl::vertex_buffer out1, out2;

    /*
     * we generate generate random lines and clip them.
     */
    std::random_device rnd_device;
    std::mt19937 mersenne_engine{rnd_device()};
    std::uniform_real_distribution<float> dist{-10000, 10000};

    auto gen = [&dist, &mersenne_engine]()
    { return dist(mersenne_engine); };
    auto vec_rnd = [&gen]() -> ml::vec4
    {
        return {gen(), gen(), gen(), gen()};
    };
    auto in_frustum = [](ml::vec4 v) -> bool
    {
        return (v.w > 0) && (-v.w <= v.x) && (v.x <= v.w) && (-v.w <= v.y) && (v.y <= v.w) && (-v.w <= v.z) && (v.z <= v.w);
    };

    size_t lines_in_frustum = 0;
    const size_t total_lines = 100000;
    for(size_t k = 0; k < total_lines; ++k)
    {
        ml::vec4 v1 = vec_rnd();
        ml::vec4 v2 = vec_rnd();

        while(v1 == v2)
        {
            v1 = vec_rnd();
            v2 = vec_rnd();
        }

        bool v1_inside = in_frustum(v1);
        bool v2_inside = in_frustum(v2);

        points.clear();
        points.emplace_back(v1);
        points.emplace_back(v2);

        /*
         * clip_line_buffer does not do frustum checks, but relies on the vf_clip_discard flag being set.
         */
        if(!v1_inside)
        {
            points[0].flags |= geom::vf_clip_discard;
        }
        if(!v2_inside)
        {
            points[1].flags |= geom::vf_clip_discard;
        }

        out1.clear();
        swr::impl::clip_line_buffer(points, indices, swr::impl::clip_output::line_list, out1);

        points.clear();
        points.emplace_back(v2);
        points.emplace_back(v1);

        /*
         * clip_line_buffer does not do frustum checks, but relies on the vf_clip_discard flag being set.
         */
        if(!v1_inside)
        {
            points[1].flags |= geom::vf_clip_discard;
        }
        if(!v2_inside)
        {
            points[0].flags |= geom::vf_clip_discard;
        }

        out2.clear();
        swr::impl::clip_line_buffer(points, indices, swr::impl::clip_output::line_list, out2);

        if(v1_inside || v2_inside)
        {
            BOOST_REQUIRE(out1.size() == 2);
            BOOST_REQUIRE(out2.size() == 2);

            BOOST_TEST(get_bits(out1[0].coords.x) == get_bits(out2[1].coords.x));
            BOOST_TEST(get_bits(out1[0].coords.y) == get_bits(out2[1].coords.y));
            BOOST_TEST(get_bits(out1[0].coords.z) == get_bits(out2[1].coords.z));
            BOOST_TEST(get_bits(out1[0].coords.w) == get_bits(out2[1].coords.w));

            BOOST_TEST(get_bits(out1[1].coords.x) == get_bits(out2[0].coords.x));
            BOOST_TEST(get_bits(out1[1].coords.y) == get_bits(out2[0].coords.y));
            BOOST_TEST(get_bits(out1[1].coords.z) == get_bits(out2[0].coords.z));
            BOOST_TEST(get_bits(out1[1].coords.w) == get_bits(out2[0].coords.w));

            ++lines_in_frustum;
        }
        else
        {
            // this may generate a either a line segment (possibly degenerate), or nothing.
            BOOST_TEST((out1.size() == 0 || out1.size() == 2));
            BOOST_TEST((out2.size() == 0 || out2.size() == 2));

            BOOST_REQUIRE(out1.size() == out2.size());
            if(out1.size() == 2)
            {
                // because of floating-point errors, the coordinates might
                // still not satisfy in_frustum.

                BOOST_TEST(get_bits(out1[0].coords.x) == get_bits(out2[1].coords.x));
                BOOST_TEST(get_bits(out1[0].coords.y) == get_bits(out2[1].coords.y));
                BOOST_TEST(get_bits(out1[0].coords.z) == get_bits(out2[1].coords.z));
                BOOST_TEST(get_bits(out1[0].coords.w) == get_bits(out2[1].coords.w));

                BOOST_TEST(get_bits(out1[1].coords.x) == get_bits(out2[0].coords.x));
                BOOST_TEST(get_bits(out1[1].coords.y) == get_bits(out2[0].coords.y));
                BOOST_TEST(get_bits(out1[1].coords.z) == get_bits(out2[0].coords.z));
                BOOST_TEST(get_bits(out1[1].coords.w) == get_bits(out2[0].coords.w));
            }
        }
    }

    BOOST_TEST_MESSAGE(fmt::format("{} lines in frustum, {} clipped", lines_in_frustum, total_lines - lines_in_frustum));
}

BOOST_AUTO_TEST_SUITE_END();
