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
#include <format>
#include <random>

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
    swr::impl::render_object obj;

    /*
     * test empty input for line clipping.
     */
    swr::impl::clip_line_buffer(obj, swr::impl::clip_output::point_list);
    BOOST_TEST(obj.clipped_vertices.size() == 0);

    swr::impl::clip_line_buffer(obj, swr::impl::clip_output::line_list);
    BOOST_TEST(obj.clipped_vertices.size() == 0);

    swr::impl::clip_line_buffer(obj, swr::impl::clip_output::triangle_list);
    BOOST_TEST(obj.clipped_vertices.size() == 0);

    /*
     * test empty input for triangle clipping.
     */
    swr::impl::clip_triangle_buffer(obj, swr::impl::clip_output::point_list);
    BOOST_TEST(obj.clipped_vertices.size() == 0);

    swr::impl::clip_triangle_buffer(obj, swr::impl::clip_output::line_list);
    BOOST_TEST(obj.clipped_vertices.size() == 0);

    swr::impl::clip_triangle_buffer(obj, swr::impl::clip_output::triangle_list);
    BOOST_TEST(obj.clipped_vertices.size() == 0);
}

/* get bits of float type. note: in C++20, one should use std::bit_cast. */
static_assert(sizeof(std::uint32_t) == sizeof(float), "Types sizes need to match for get_bits to work.");
auto get_bits = [](float f) -> std::uint32_t
{
    std::uint32_t temp;
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

    // render_object setup.
    swr::impl::render_object obj;

    constexpr std::uint32_t COORD_COUNT = 14;
    constexpr std::uint32_t INDEX_COUNT = 14;

    obj.allocate_coords(COORD_COUNT);
    obj.indices.reserve(INDEX_COUNT);
    for(std::uint32_t i = 0; i < INDEX_COUNT; ++i)
    {
        obj.indices.emplace_back(i);
    }
    obj.vertex_flags.resize(INDEX_COUNT);
    swr::impl::program_info info;
    obj.states.shader_info = &info;

    // input data.
    ml::vec4 coords[COORD_COUNT] = {
      ml::vec4{0, 0, 0, 1}, ml::vec4{1, 0, 0, 1},
      ml::vec4{0, 1, 0, 1}, ml::vec4{0, 0, 1, 1},
      ml::vec4{0.123, -0.456, 0.789, 1.234}, ml::vec4{-0.123, -0.456, -0.789, 12.34},
      ml::vec4{0.1, 0.2, 0.3, 0.4}, ml::vec4{0.5, 0.6, 0.7, 0.8},
      ml::vec4{0.9, -0.1, -0.2, 1.234}, ml::vec4{-0.3, -0.4, -0.5, 0.6},
      ml::vec4{-0.7, -0.8, -0.9, 1.234}, ml::vec4{-10, -20, -30, 40},
      ml::vec4{0.0001, 0.0002, 0.0003, 0.0004}, ml::vec4{-12345.67, -12345.67, -0.789, 23456.98}};

    for(std::uint32_t i = 0; i < COORD_COUNT; ++i)
    {
        obj.coords[i] = coords[i];
    }

    // clip lines.
    BOOST_REQUIRE((INDEX_COUNT & 1) == 0);
    swr::impl::clip_line_buffer(obj, swr::impl::clip_output::line_list);
    BOOST_TEST(obj.clipped_vertices.size() == COORD_COUNT);

    BOOST_REQUIRE(obj.clipped_vertices.size() == COORD_COUNT);
    for(std::size_t i = 0; i < COORD_COUNT; ++i)
    {
        // compare bits.
        BOOST_TEST(get_bits(coords[i].x) == get_bits(obj.clipped_vertices[i].coords.x));
        BOOST_TEST(get_bits(coords[i].y) == get_bits(obj.clipped_vertices[i].coords.y));
        BOOST_TEST(get_bits(coords[i].z) == get_bits(obj.clipped_vertices[i].coords.z));
        BOOST_TEST(get_bits(coords[i].w) == get_bits(obj.clipped_vertices[i].coords.w));
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

    obj.indices = {0, 1};
    obj.vertex_flags.resize(2);

    for(int k = 0; k < 10000; ++k)
    {
        ml::vec4 points[2];
        do
        {
            points[0] = vec_rnd();
            points[1] = vec_rnd();
        } while(!in_frustum(points[0]) || !in_frustum(points[1]));

        obj.allocate_coords(2);
        obj.coords[0] = points[0];
        obj.coords[1] = points[1];

        obj.clipped_vertices.clear();
        swr::impl::clip_line_buffer(obj, swr::impl::clip_output::line_list);
        BOOST_TEST(obj.clipped_vertices.size() == 2);

        BOOST_TEST(get_bits(points[0].x) == get_bits(obj.clipped_vertices[0].coords.x));
        BOOST_TEST(get_bits(points[0].y) == get_bits(obj.clipped_vertices[0].coords.y));
        BOOST_TEST(get_bits(points[0].z) == get_bits(obj.clipped_vertices[0].coords.z));
        BOOST_TEST(get_bits(points[0].w) == get_bits(obj.clipped_vertices[0].coords.w));

        BOOST_TEST(get_bits(points[1].x) == get_bits(obj.clipped_vertices[1].coords.x));
        BOOST_TEST(get_bits(points[1].y) == get_bits(obj.clipped_vertices[1].coords.y));
        BOOST_TEST(get_bits(points[1].z) == get_bits(obj.clipped_vertices[1].coords.z));
        BOOST_TEST(get_bits(points[1].w) == get_bits(obj.clipped_vertices[1].coords.w));
    }
}

BOOST_AUTO_TEST_CASE(line_clip)
{
    // render_object setup.
    const std::uint32_t VERTEX_COUNT = 2;
    swr::impl::render_object obj;
    obj.allocate_coords(VERTEX_COUNT);
    obj.indices.reserve(VERTEX_COUNT);
    for(std::uint32_t i = 0; i < VERTEX_COUNT; ++i)
    {
        obj.indices.emplace_back(i);
    }
    obj.vertex_flags.resize(VERTEX_COUNT);
    swr::impl::program_info info;
    obj.states.shader_info = &info;

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

    std::size_t lines_in_frustum = 0;
    const std::size_t total_lines = 100000;
    for(std::size_t k = 0; k < total_lines; ++k)
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

        obj.coords[0] = v1;
        obj.coords[1] = v2;

        /*
         * clip_line_buffer does not do frustum checks, but relies on the vf_clip_discard flag being set.
         */
        if(!v1_inside)
        {
            obj.vertex_flags[0] |= geom::vf_clip_discard;
        }
        if(!v2_inside)
        {
            obj.vertex_flags[1] |= geom::vf_clip_discard;
        }

        out1.clear();
        swr::impl::clip_line_buffer(obj, swr::impl::clip_output::line_list);
        out1 = obj.clipped_vertices;

        obj.coords[0] = v2;
        obj.coords[1] = v1;

        /*
         * clip_line_buffer does not do frustum checks, but relies on the vf_clip_discard flag being set.
         */
        if(!v1_inside)
        {
            obj.vertex_flags[1] |= geom::vf_clip_discard;
        }
        if(!v2_inside)
        {
            obj.vertex_flags[0] |= geom::vf_clip_discard;
        }

        out2.clear();
        swr::impl::clip_line_buffer(obj, swr::impl::clip_output::line_list);
        out2 = obj.clipped_vertices;

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

    BOOST_TEST_MESSAGE(std::format("{} lines in frustum, {} clipped", lines_in_frustum, total_lines - lines_in_frustum));
}

BOOST_AUTO_TEST_SUITE_END();
