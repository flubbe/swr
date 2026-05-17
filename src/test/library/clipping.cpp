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
#define BOOST_TEST_MODULE clipping
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

    swr::impl::program_info info;
    obj.states.shader_info = &info;

    /*
     * test empty input for line clipping.
     */
    swr::impl::clip_line_buffer(obj, swr::impl::clip_output::point_list);
    BOOST_CHECK_EQUAL(obj.clipped_vertices.size(), 0);

    swr::impl::clip_line_buffer(obj, swr::impl::clip_output::line_list);
    BOOST_CHECK_EQUAL(obj.clipped_vertices.size(), 0);

    swr::impl::clip_line_buffer(obj, swr::impl::clip_output::triangle_list);
    BOOST_CHECK_EQUAL(obj.clipped_vertices.size(), 0);

    /*
     * test empty input for triangle clipping.
     */
    swr::impl::clip_triangle_buffer(obj, swr::impl::clip_output::point_list);
    BOOST_CHECK_EQUAL(obj.clipped_vertices.size(), 0);

    swr::impl::clip_triangle_buffer(obj, swr::impl::clip_output::line_list);
    BOOST_CHECK_EQUAL(obj.clipped_vertices.size(), 0);

    swr::impl::clip_triangle_buffer(obj, swr::impl::clip_output::triangle_list);
    BOOST_CHECK_EQUAL(obj.clipped_vertices.size(), 0);
}

/* get bits of float type. note: in C++20, one should use std::bit_cast. */
static_assert(sizeof(std::uint32_t) == sizeof(float), "Types sizes need to match for get_bits to work.");
auto get_bits = [](float f) -> std::uint32_t
{
    std::uint32_t temp;
    std::memcpy(&temp, &f, sizeof(float));
    return temp;
};

static void check_vertex_buffers_equal_bitwise(
  const swr::impl::vertex_buffer& a,
  const swr::impl::vertex_buffer& b,
  std::uint32_t varying_count = 0)
{
    BOOST_REQUIRE_EQUAL(a.size(), b.size());
    for(std::size_t i = 0; i < a.size(); ++i)
    {
        BOOST_CHECK_EQUAL(get_bits(a[i].coords.x), get_bits(b[i].coords.x));
        BOOST_CHECK_EQUAL(get_bits(a[i].coords.y), get_bits(b[i].coords.y));
        BOOST_CHECK_EQUAL(get_bits(a[i].coords.z), get_bits(b[i].coords.z));
        BOOST_CHECK_EQUAL(get_bits(a[i].coords.w), get_bits(b[i].coords.w));
        BOOST_CHECK_EQUAL(a[i].flags, b[i].flags);
        BOOST_CHECK_EQUAL(a[i].flat_varying_ref == nullptr, b[i].flat_varying_ref == nullptr);
        if(a[i].flat_varying_ref != nullptr
           && b[i].flat_varying_ref != nullptr)
        {
            for(std::uint32_t j = 0; j < varying_count; ++j)
            {
                BOOST_CHECK_EQUAL(get_bits(a[i].flat_varying_ref[j].x), get_bits(b[i].flat_varying_ref[j].x));
                BOOST_CHECK_EQUAL(get_bits(a[i].flat_varying_ref[j].y), get_bits(b[i].flat_varying_ref[j].y));
                BOOST_CHECK_EQUAL(get_bits(a[i].flat_varying_ref[j].z), get_bits(b[i].flat_varying_ref[j].z));
                BOOST_CHECK_EQUAL(get_bits(a[i].flat_varying_ref[j].w), get_bits(b[i].flat_varying_ref[j].w));
            }
        }
    }
}

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
    BOOST_CHECK_EQUAL(obj.clipped_vertices.size(), COORD_COUNT);

    BOOST_REQUIRE(obj.clipped_vertices.size() == COORD_COUNT);
    for(std::size_t i = 0; i < COORD_COUNT; ++i)
    {
        // compare bits.
        BOOST_CHECK_EQUAL(get_bits(coords[i].x), get_bits(obj.clipped_vertices[i].coords.x));
        BOOST_CHECK_EQUAL(get_bits(coords[i].y), get_bits(obj.clipped_vertices[i].coords.y));
        BOOST_CHECK_EQUAL(get_bits(coords[i].z), get_bits(obj.clipped_vertices[i].coords.z));
        BOOST_CHECK_EQUAL(get_bits(coords[i].w), get_bits(obj.clipped_vertices[i].coords.w));
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

        if(obj.coords.empty())
        {
            BOOST_FAIL("Failed to allocate coords.");
        }

        if(obj.vertex_flags.size() < 2)
        {
            BOOST_FAIL("vertex_flags size is too small.");
        }

        obj.coords[0] = points[0];
        obj.coords[1] = points[1];

        obj.clipped_vertices.clear();
        swr::impl::clip_line_buffer(obj, swr::impl::clip_output::line_list);
        BOOST_CHECK_EQUAL(obj.clipped_vertices.size(), 2);

        BOOST_CHECK_EQUAL(get_bits(points[0].x), get_bits(obj.clipped_vertices[0].coords.x));
        BOOST_CHECK_EQUAL(get_bits(points[0].y), get_bits(obj.clipped_vertices[0].coords.y));
        BOOST_CHECK_EQUAL(get_bits(points[0].z), get_bits(obj.clipped_vertices[0].coords.z));
        BOOST_CHECK_EQUAL(get_bits(points[0].w), get_bits(obj.clipped_vertices[0].coords.w));

        BOOST_CHECK_EQUAL(get_bits(points[1].x), get_bits(obj.clipped_vertices[1].coords.x));
        BOOST_CHECK_EQUAL(get_bits(points[1].y), get_bits(obj.clipped_vertices[1].coords.y));
        BOOST_CHECK_EQUAL(get_bits(points[1].z), get_bits(obj.clipped_vertices[1].coords.z));
        BOOST_CHECK_EQUAL(get_bits(points[1].w), get_bits(obj.clipped_vertices[1].coords.w));
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

            BOOST_CHECK_EQUAL(get_bits(out1[0].coords.x), get_bits(out2[1].coords.x));
            BOOST_CHECK_EQUAL(get_bits(out1[0].coords.y), get_bits(out2[1].coords.y));
            BOOST_CHECK_EQUAL(get_bits(out1[0].coords.z), get_bits(out2[1].coords.z));
            BOOST_CHECK_EQUAL(get_bits(out1[0].coords.w), get_bits(out2[1].coords.w));

            BOOST_CHECK_EQUAL(get_bits(out1[1].coords.x), get_bits(out2[0].coords.x));
            BOOST_CHECK_EQUAL(get_bits(out1[1].coords.y), get_bits(out2[0].coords.y));
            BOOST_CHECK_EQUAL(get_bits(out1[1].coords.z), get_bits(out2[0].coords.z));
            BOOST_CHECK_EQUAL(get_bits(out1[1].coords.w), get_bits(out2[0].coords.w));

            ++lines_in_frustum;
        }
        else
        {
            // this may generate a either a line segment (possibly degenerate), or nothing.
            BOOST_TEST((out1.empty() || out1.size() == 2));
            BOOST_TEST((out2.empty() || out2.size() == 2));

            BOOST_REQUIRE(out1.size() == out2.size());
            if(out1.size() == 2)
            {
                // because of floating-point errors, the coordinates might
                // still not satisfy in_frustum.

                BOOST_CHECK_EQUAL(get_bits(out1[0].coords.x), get_bits(out2[1].coords.x));
                BOOST_CHECK_EQUAL(get_bits(out1[0].coords.y), get_bits(out2[1].coords.y));
                BOOST_CHECK_EQUAL(get_bits(out1[0].coords.z), get_bits(out2[1].coords.z));
                BOOST_CHECK_EQUAL(get_bits(out1[0].coords.w), get_bits(out2[1].coords.w));

                BOOST_CHECK_EQUAL(get_bits(out1[1].coords.x), get_bits(out2[0].coords.x));
                BOOST_CHECK_EQUAL(get_bits(out1[1].coords.y), get_bits(out2[0].coords.y));
                BOOST_CHECK_EQUAL(get_bits(out1[1].coords.z), get_bits(out2[0].coords.z));
                BOOST_CHECK_EQUAL(get_bits(out1[1].coords.w), get_bits(out2[0].coords.w));
            }
        }
    }

    BOOST_TEST_MESSAGE(std::format("{} lines in frustum, {} clipped", lines_in_frustum, total_lines - lines_in_frustum));
}

BOOST_AUTO_TEST_CASE(line_clip_range_parity)
{
    swr::impl::render_object obj;
    swr::impl::program_info info;
    obj.states.shader_info = &info;

    constexpr std::size_t line_count = 512;
    constexpr std::size_t vertex_count = line_count * 2;
    obj.allocate_coords(vertex_count);
    obj.vertex_flags.assign(vertex_count, 0);
    obj.indices.resize(vertex_count);

    std::mt19937 rng{1337u};
    std::uniform_real_distribution<float> coord_dist(-2.0f, 2.0f);
    std::bernoulli_distribution discard_dist(0.25);

    for(std::size_t i = 0; i < vertex_count; ++i)
    {
        obj.indices[i] = static_cast<std::uint32_t>(i);
        obj.coords[i] = {coord_dist(rng), coord_dist(rng), coord_dist(rng), coord_dist(rng)};
        if(discard_dist(rng))
        {
            obj.vertex_flags[i] |= geom::vf_clip_discard;
        }
    }

    swr::impl::vertex_buffer full;
    swr::impl::clip_line_buffer_range(
      obj,
      swr::impl::clip_output::line_list,
      0,
      obj.indices.size(),
      full);

    // Split into deterministic chunks and stitch output in chunk order.
    swr::impl::vertex_buffer stitched;
    constexpr std::size_t chunk_count = 7;
    for(std::size_t c = 0; c < chunk_count; ++c)
    {
        const std::size_t begin_line = (c * line_count) / chunk_count;
        const std::size_t end_line = ((c + 1) * line_count) / chunk_count;
        const std::size_t begin_index = begin_line * 2;
        const std::size_t end_index = end_line * 2;

        swr::impl::vertex_buffer chunk;
        swr::impl::clip_line_buffer_range(
          obj,
          swr::impl::clip_output::line_list,
          begin_index,
          end_index,
          chunk);
        stitched.insert(
          std::end(stitched),
          std::begin(chunk),
          std::end(chunk));
    }

    check_vertex_buffers_equal_bitwise(full, stitched, info.varying_count);
}

BOOST_AUTO_TEST_CASE(triangle_clip_range_parity)
{
    swr::impl::render_object obj;
    swr::impl::program_info info;
    obj.states.shader_info = &info;

    constexpr std::size_t triangle_count = 384;
    constexpr std::size_t vertex_count = triangle_count * 3;
    obj.allocate_coords(vertex_count);
    obj.vertex_flags.assign(vertex_count, 0);
    obj.indices.resize(vertex_count);

    std::mt19937 rng{4242u};
    std::uniform_real_distribution<float> coord_dist(-2.0f, 2.0f);
    std::bernoulli_distribution discard_dist(0.3);

    for(std::size_t i = 0; i < vertex_count; ++i)
    {
        obj.indices[i] = static_cast<std::uint32_t>(i);
        obj.coords[i] = {coord_dist(rng), coord_dist(rng), coord_dist(rng), coord_dist(rng)};
        if(discard_dist(rng))
        {
            obj.vertex_flags[i] |= geom::vf_clip_discard;
        }
    }

    swr::impl::vertex_buffer full;
    swr::impl::clip_triangle_buffer_range(
      obj,
      swr::impl::clip_output::triangle_list,
      0,
      obj.indices.size(),
      full);

    swr::impl::vertex_buffer stitched;
    constexpr std::size_t chunk_count = 9;
    for(std::size_t c = 0; c < chunk_count; ++c)
    {
        const std::size_t begin_tri = (c * triangle_count) / chunk_count;
        const std::size_t end_tri = ((c + 1) * triangle_count) / chunk_count;
        const std::size_t begin_index = begin_tri * 3;
        const std::size_t end_index = end_tri * 3;

        swr::impl::vertex_buffer chunk;
        swr::impl::clip_triangle_buffer_range(
          obj,
          swr::impl::clip_output::triangle_list,
          begin_index,
          end_index,
          chunk);
        stitched.insert(
          std::end(stitched),
          std::begin(chunk),
          std::end(chunk));
    }

    check_vertex_buffers_equal_bitwise(full, stitched, info.varying_count);
}

BOOST_AUTO_TEST_CASE(triangle_clip_preserves_flat_reference)
{
    swr::impl::render_object obj;
    swr::impl::program_info info;
    info.iqs.emplace_back(swr::interpolation_qualifier::flat);
    info.varying_count = 1;
    obj.states.shader_info = &info;

    obj.allocate_coords(3);
    obj.allocate_varyings(1);
    obj.indices = {0, 1, 2};
    obj.vertex_flags.assign(3, 0);

    obj.coords[0] = {-2.0f, 0.0f, 0.0f, 1.0f};
    obj.coords[1] = {0.0f, 0.8f, 0.0f, 1.0f};
    obj.coords[2] = {0.0f, -0.8f, 0.0f, 1.0f};
    obj.vertex_flags[0] |= geom::vf_clip_discard;

    obj.varyings[0] = {11.0f, 1.0f, 2.0f, 3.0f};
    obj.varyings[1] = {22.0f, 4.0f, 5.0f, 6.0f};
    obj.varyings[2] = {33.0f, 7.0f, 8.0f, 9.0f};

    swr::impl::clip_triangle_buffer(obj, swr::impl::clip_output::triangle_list);

    BOOST_REQUIRE_GE(obj.clipped_vertices.size(), 3);
    BOOST_REQUIRE_EQUAL(obj.clipped_vertices.size() % 3, 0);

    for(std::size_t i = 0; i < obj.clipped_vertices.size(); i += 3)
    {
        const geom::vertex& emitted_first = obj.clipped_vertices[i];
        BOOST_REQUIRE(emitted_first.flat_varying_ref != nullptr);

        BOOST_CHECK_EQUAL(get_bits(emitted_first.flat_varying_ref[0].x), get_bits(obj.varyings[0].x));
        BOOST_CHECK_EQUAL(get_bits(emitted_first.flat_varying_ref[0].y), get_bits(obj.varyings[0].y));
        BOOST_CHECK_EQUAL(get_bits(emitted_first.flat_varying_ref[0].z), get_bits(obj.varyings[0].z));
        BOOST_CHECK_EQUAL(get_bits(emitted_first.flat_varying_ref[0].w), get_bits(obj.varyings[0].w));
    }
}

BOOST_AUTO_TEST_CASE(triangle_clip_preserves_flat_reference_per_input_triangle)
{
    swr::impl::render_object obj;
    swr::impl::program_info info;
    info.iqs.emplace_back(swr::interpolation_qualifier::flat);
    info.varying_count = 1;
    obj.states.shader_info = &info;

    obj.allocate_coords(6);
    obj.allocate_varyings(1);
    obj.indices = {0, 1, 2, 3, 4, 5};
    obj.vertex_flags.assign(6, 0);

    obj.coords[0] = {-2.0f, 0.0f, 0.0f, 1.0f};
    obj.coords[1] = {0.0f, 0.8f, 0.0f, 1.0f};
    obj.coords[2] = {0.0f, -0.8f, 0.0f, 1.0f};
    obj.vertex_flags[0] |= geom::vf_clip_discard;

    obj.coords[3] = {2.0f, 0.0f, 0.0f, 1.0f};
    obj.coords[4] = {0.0f, -0.9f, 0.0f, 1.0f};
    obj.coords[5] = {0.0f, 0.9f, 0.0f, 1.0f};
    obj.vertex_flags[3] |= geom::vf_clip_discard;

    obj.varyings[0] = {11.0f, 1.0f, 2.0f, 3.0f};
    obj.varyings[1] = {12.0f, 1.0f, 2.0f, 3.0f};
    obj.varyings[2] = {13.0f, 1.0f, 2.0f, 3.0f};
    obj.varyings[3] = {44.0f, 4.0f, 5.0f, 6.0f};
    obj.varyings[4] = {45.0f, 4.0f, 5.0f, 6.0f};
    obj.varyings[5] = {46.0f, 4.0f, 5.0f, 6.0f};

    swr::impl::clip_triangle_buffer(obj, swr::impl::clip_output::triangle_list);

    BOOST_REQUIRE_EQUAL(obj.clipped_vertices.size(), 12);

    auto check_flat_reference = [&](std::size_t emitted_vertex, std::size_t source_vertex)
    {
        const geom::vertex& emitted = obj.clipped_vertices[emitted_vertex];
        BOOST_REQUIRE(emitted.flat_varying_ref != nullptr);

        BOOST_CHECK_EQUAL(get_bits(emitted.flat_varying_ref[0].x), get_bits(obj.varyings[source_vertex].x));
        BOOST_CHECK_EQUAL(get_bits(emitted.flat_varying_ref[0].y), get_bits(obj.varyings[source_vertex].y));
        BOOST_CHECK_EQUAL(get_bits(emitted.flat_varying_ref[0].z), get_bits(obj.varyings[source_vertex].z));
        BOOST_CHECK_EQUAL(get_bits(emitted.flat_varying_ref[0].w), get_bits(obj.varyings[source_vertex].w));
    };

    for(std::size_t i = 0; i < 6; i += 3)
    {
        check_flat_reference(i, 0);
    }

    for(std::size_t i = 6; i < obj.clipped_vertices.size(); i += 3)
    {
        check_flat_reference(i, 3);
    }
}

BOOST_AUTO_TEST_SUITE_END();
