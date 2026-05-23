/**
 * swr - a software rasterizer
 *
 * test sparse/thin triangle rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <string_view>
#include <tuple>
#include <vector>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE sparse triangle coverage
#include <boost/test/unit_test.hpp>

/* user headers. */
#include "swr_internal.h"
#include "rasterizer/interpolators.h"
#include "rasterizer/fragment.h"
#include "rasterizer/sweep.h"
#include "rasterizer/triangle.h"
#include "rasterizer/block.h"

/*
 * Helpers.
 */

struct fake_draw_target : swr::impl::framebuffer_draw_target
{
    fake_draw_target(unsigned int width, unsigned int height)
    : swr::impl::framebuffer_draw_target{}
    {
        assert(width < std::numeric_limits<int>::max() && height < std::numeric_limits<int>::max());
        properties.reset(static_cast<int>(width), static_cast<int>(height));
    }

    void clear_color(
      [[maybe_unused]] std::uint32_t attachment,
      [[maybe_unused]] ml::vec4 clear_color) override
    {
    }

    void clear_color(
      [[maybe_unused]] std::uint32_t attachment,
      [[maybe_unused]] ml::vec4 clear_color,
      [[maybe_unused]] const utils::rect& rect) override
    {
    }

    void clear_depth(
      [[maybe_unused]] ml::fixed_32_t clear_depth) override
    {
    }

    void clear_depth(
      [[maybe_unused]] ml::fixed_32_t clear_depth,
      [[maybe_unused]] const utils::rect& rect) override
    {
    }

    void merge_color(
      [[maybe_unused]] std::uint32_t attachment,
      [[maybe_unused]] int x,
      [[maybe_unused]] int y,
      [[maybe_unused]] const swr::impl::fragment_output& frag,
      [[maybe_unused]] bool do_blend,
      [[maybe_unused]] swr::blend_func src,
      [[maybe_unused]] swr::blend_func dst) override
    {
    }

    void merge_color_block(
      [[maybe_unused]] std::uint32_t attachment,
      [[maybe_unused]] int x,
      [[maybe_unused]] int y,
      [[maybe_unused]] const swr::impl::fragment_output_block& frag,
      [[maybe_unused]] bool do_blend,
      [[maybe_unused]] swr::blend_func src,
      [[maybe_unused]] swr::blend_func dst) override
    {
    }

    void depth_compare_write(
      [[maybe_unused]] int x,
      [[maybe_unused]] int y,
      [[maybe_unused]] float depth_value,
      [[maybe_unused]] swr::comparison_func depth_func,
      [[maybe_unused]] bool write_depth,
      [[maybe_unused]] bool& write_mask) override
    {
    }

    void depth_compare_write_block(
      [[maybe_unused]] int x,
      [[maybe_unused]] int y,
      [[maybe_unused]] std::array<float, 4>& depth_value,
      [[maybe_unused]] swr::comparison_func depth_func,
      [[maybe_unused]] bool write_depth,
      [[maybe_unused]] std::uint8_t& write_mask) override
    {
    }
};

class fake_program : public swr::program<fake_program>
{
public:
    void pre_link(
      boost::container::static_vector<
        swr::interpolation_qualifier,
        swr::limits::max::varyings>&
        iqs) const override
    {
        iqs.clear();
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      [[maybe_unused]] std::span<const ml::vec4> attribs,
      [[maybe_unused]] ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      [[maybe_unused]] std::span<ml::vec4> varyings) const override
    {
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      [[maybe_unused]] ml::vec4& gl_FragColor) const override
    {
        gl_FragColor = {1.f, 1.f, 1.f, 1.f};
        return swr::fragment_shader_result::accept;
    }
};

geom::vertex make_vertex(float x, float y)
{
    return geom::vertex{.coords = {x, y, 0.0f, 0.0f}};
}

struct triangle_test_context
{
    fake_draw_target draw_target;
    fake_program shader;
    swr::impl::program_info program_info;
    swr::impl::render_states states;

    triangle_test_context(unsigned int width, unsigned int height)
    : draw_target{width, height}
    , program_info{&shader}
    {
        states.x = 0;
        states.y = 0;
        states.width = width;
        states.height = height;

        states.shader_info = &program_info;
        states.draw_target = &draw_target;
    }
};

struct checked_quad_sample
{
    int block_x;
    int block_y;
    int x;
    int y;
    int mask;

    bool operator==(const checked_quad_sample&) const = default;
};

std::ostream& operator<<(std::ostream& os, const checked_quad_sample& sample)
{
    return os << "checked_quad_sample{block=("
              << sample.block_x << ", " << sample.block_y << "), quad=("
              << sample.x << ", " << sample.y << "), mask=" << sample.mask << "}";
}

std::vector<checked_quad_sample> collect_checked_quads(
  const swr::impl::render_states& states,
  const rast::triangle_info& info,
  const boost::container::static_vector<ml::vec4, swr::limits::max::varyings>& provoking_vertex_varyings,
  bool use_sparse_bounds)
{
    std::vector<checked_quad_sample> out;

    const rast::bounding_box bounds = rast::compute_triangle_bounds(
      states,
      info,
      false);

    rast::for_each_covered_triangle_block(
      states,
      info,
      provoking_vertex_varyings,
      0.0f,
      false,
      [&](int block_x,
          int block_y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes,
          rast::tile_info::rasterization_mode mode)
      {
          if(mode != rast::tile_info::rasterization_mode::checked)
          {
              return;
          }

          const auto emit_quad =
            [&](int x, int y, int mask, const rast::triangle_interpolator&)
          {
              out.push_back({block_x, block_y, x, y, mask});
          };

          auto block_attributes = attributes;
          if(use_sparse_bounds)
          {
              rast::for_each_covered_quad_in_checked_triangle_block(
                block_x,
                block_y,
                rast::compute_checked_quad_bounds(bounds, block_x, block_y),
                lambdas_box,
                block_attributes,
                emit_quad);
          }
          else
          {
              rast::for_each_covered_quad_in_checked_triangle_block(
                block_x,
                block_y,
                lambdas_box,
                block_attributes,
                emit_quad);
          }
      });

    return out;
}

std::vector<checked_quad_sample> collect_thin_trace_quads(
  const swr::impl::render_states& states,
  const rast::triangle_info& info,
  const boost::container::static_vector<ml::vec4, swr::limits::max::varyings>& provoking_vertex_varyings)
{
    std::vector<checked_quad_sample> out;

    const rast::bounding_box bounds = rast::compute_triangle_bounds(
      states,
      info,
      false);
    const auto mode = rast::classify_triangle_rasterization(bounds, info).mode;

    rast::for_each_thin_triangle_block_with_bounds(
      states,
      bounds,
      info,
      provoking_vertex_varyings,
      0.0f,
      mode,
      [&](int block_x,
          int block_y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes,
          rast::tile_info::rasterization_mode,
          rast::quad_bounds thin_quad_bounds)
      {
          auto block_attributes = attributes;
          rast::for_each_covered_quad_in_checked_triangle_block(
            block_x,
            block_y,
            thin_quad_bounds,
            lambdas_box,
            block_attributes,
            [&](int x, int y, int mask, const rast::triangle_interpolator&)
            {
                out.push_back({block_x, block_y, x, y, mask});
            });
      });

    return out;
}

void sort_checked_quads(std::vector<checked_quad_sample>& quads)
{
    std::sort(
      quads.begin(),
      quads.end(),
      [](const auto& lhs, const auto& rhs)
      {
          return std::tie(lhs.block_y, lhs.block_x, lhs.y, lhs.x, lhs.mask)
                 < std::tie(rhs.block_y, rhs.block_x, rhs.y, rhs.x, rhs.mask);
      });
}

void check_vec4_close(
  const ml::vec4& actual,
  const ml::vec4& expected,
  float epsilon = 2e-4f)
{
    BOOST_CHECK_SMALL(actual.x - expected.x, epsilon);
    BOOST_CHECK_SMALL(actual.y - expected.y, epsilon);
    BOOST_CHECK_SMALL(actual.z - expected.z, epsilon);
    BOOST_CHECK_SMALL(actual.w - expected.w, epsilon);
}

void check_precomputed_payload_interpolation_matches_regular(
  const swr::impl::render_states& states,
  const rast::triangle_info& info,
  std::span<const ml::vec4> provoking_vertex_varyings,
  int block_x,
  int block_y,
  const rast::small_triangle_interpolator& precomputed_attributes,
  std::span<const rast::small_triangle_quad_payload> quads)
{
    BOOST_REQUIRE(!quads.empty());

    for(const auto& quad: quads)
    {
        std::array<
          boost::container::static_vector<
            swr::varying,
            swr::limits::max::varyings>,
          4>
          expected_varyings;
        std::array<
          boost::container::static_vector<
            swr::varying,
            swr::limits::max::varyings>,
          4>
          actual_varyings;
        ml::vec4 expected_depth;
        ml::vec4 actual_depth;
        ml::vec4 expected_one_over_z;
        ml::vec4 actual_one_over_z;

        rast::triangle_interpolator expected_attributes{
          ml::vec2{
            static_cast<float>(quad.x) + 0.5f,
            static_cast<float>(quad.y) + 0.5f},
          info.v0->coords,
          info.v1->coords,
          info.v2->coords,
          info.v0->varyings,
          info.v1->varyings,
          info.v2->varyings,
          provoking_vertex_varyings,
          states.shader_info->iqs,
          info.inv_area,
          0.0f};

        expected_attributes.get_data_block(
          expected_varyings,
          expected_depth,
          expected_one_over_z);
        precomputed_attributes.get_data_block_at(
          static_cast<unsigned int>(static_cast<int>(quad.x) - block_x),
          static_cast<unsigned int>(static_cast<int>(quad.y) - block_y),
          actual_varyings,
          actual_depth,
          actual_one_over_z);

        check_vec4_close(actual_depth, expected_depth);
        check_vec4_close(actual_one_over_z, expected_one_over_z);

        for(std::size_t lane = 0; lane < actual_varyings.size(); ++lane)
        {
            BOOST_REQUIRE_EQUAL(actual_varyings[lane].size(), expected_varyings[lane].size());
            for(std::size_t i = 0; i < actual_varyings[lane].size(); ++i)
            {
                check_vec4_close(actual_varyings[lane][i].value, expected_varyings[lane][i].value);
            }
        }
    }
}

void check_sparse_iteration_preserves_coverage(
  std::string_view name,
  unsigned int width,
  unsigned int height,
  const geom::vertex& v0,
  const geom::vertex& v1,
  const geom::vertex& v2)
{
    BOOST_TEST_CONTEXT(name)
    {
        triangle_test_context ctx{width, height};

        const auto info = rast::setup_triangle(v0, v1, v2);
        BOOST_REQUIRE(!info.is_degenerate);

        const auto full_quads = collect_checked_quads(
          ctx.states,
          info,
          v0.varyings,
          false);
        const auto sparse_quads = collect_checked_quads(
          ctx.states,
          info,
          v0.varyings,
          true);

        BOOST_REQUIRE(!full_quads.empty());
        BOOST_REQUIRE(!sparse_quads.empty());
        BOOST_CHECK_EQUAL_COLLECTIONS(
          sparse_quads.begin(), sparse_quads.end(),
          full_quads.begin(), full_quads.end());
    }
}

void check_thin_trace_preserves_coverage(
  std::string_view name,
  std::optional<rast::tile_info::rasterization_mode> expected_mode,
  unsigned int width,
  unsigned int height,
  const geom::vertex& v0,
  const geom::vertex& v1,
  const geom::vertex& v2)
{
    BOOST_TEST_CONTEXT(name)
    {
        triangle_test_context ctx{width, height};

        const auto info = rast::setup_triangle(v0, v1, v2);
        BOOST_REQUIRE(!info.is_degenerate);

        const rast::bounding_box bounds = rast::compute_triangle_bounds(
          ctx.states,
          info,
          false);
        const auto mode = rast::classify_triangle_rasterization(bounds, info).mode;
        BOOST_REQUIRE(rast::is_thin_rasterization_mode(mode));
        if(expected_mode)
        {
            BOOST_REQUIRE(mode == *expected_mode);
        }

        auto checked_quads = collect_checked_quads(
          ctx.states,
          info,
          v0.varyings,
          true);
        auto thin_quads = collect_thin_trace_quads(
          ctx.states,
          info,
          v0.varyings);

        sort_checked_quads(checked_quads);
        sort_checked_quads(thin_quads);

        BOOST_REQUIRE(!checked_quads.empty());
        BOOST_REQUIRE(!thin_quads.empty());
        BOOST_CHECK_EQUAL_COLLECTIONS(
          thin_quads.begin(), thin_quads.end(),
          checked_quads.begin(), checked_quads.end());
    }
}

/*
 * tests.
 */

BOOST_AUTO_TEST_SUITE(sparse_triangle_coverage)

BOOST_AUTO_TEST_CASE(checked_quad_bounds_are_quad_aligned_and_block_clamped)
{
    constexpr int bs = static_cast<int>(swr::impl::rasterizer_block_size);
    constexpr int width = 4 * bs;
    constexpr int height = 4 * bs;

    triangle_test_context ctx{width, height};

    const auto v0 = make_vertex(17.25f, 2.25f);
    const auto v1 = make_vertex(18.25f, static_cast<float>(3 * bs) + 30.25f);
    const auto v2 = make_vertex(17.25f, static_cast<float>(3 * bs) + 30.25f);

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const rast::bounding_box bounds = rast::compute_triangle_bounds(
      ctx.states,
      info,
      false);

    BOOST_CHECK_EQUAL(bounds.tight_start_x, 17);
    BOOST_CHECK_EQUAL(bounds.tight_end_x, 19);

    bool saw_checked_block = false;
    rast::for_each_covered_triangle_block(
      ctx.states,
      info,
      v0.varyings,
      0.0f,
      false,
      [&](int block_x,
          int block_y,
          const geom::barycentric_coordinate_block&,
          const rast::triangle_interpolator&,
          rast::tile_info::rasterization_mode mode)
      {
          if(mode != rast::tile_info::rasterization_mode::checked)
          {
              return;
          }

          saw_checked_block = true;
          const rast::quad_bounds quad_bounds = rast::compute_checked_quad_bounds(
            bounds,
            block_x,
            block_y);

          BOOST_CHECK_EQUAL(quad_bounds.start_x & 1u, 0u);
          BOOST_CHECK_EQUAL(quad_bounds.start_y & 1u, 0u);
          BOOST_CHECK_EQUAL(quad_bounds.end_x & 1u, 0u);
          BOOST_CHECK_EQUAL(quad_bounds.end_y & 1u, 0u);

          BOOST_CHECK_GE(quad_bounds.start_x, static_cast<unsigned int>(block_x));
          BOOST_CHECK_GE(quad_bounds.start_y, static_cast<unsigned int>(block_y));
          BOOST_CHECK_LE(quad_bounds.end_x, static_cast<unsigned int>(block_x + bs));
          BOOST_CHECK_LE(quad_bounds.end_y, static_cast<unsigned int>(block_y + bs));

          BOOST_CHECK_EQUAL(quad_bounds.start_x, 16u);
          BOOST_CHECK_EQUAL(quad_bounds.end_x, 20u);
      });

    BOOST_CHECK(saw_checked_block);
}

BOOST_AUTO_TEST_CASE(checked_quad_bounds_respect_scissor)
{
    constexpr int bs = static_cast<int>(swr::impl::rasterizer_block_size);

    triangle_test_context ctx{2 * bs, 2 * bs};
    ctx.states.scissor_test_enabled = true;
    ctx.states.set_scissor_box(16, 20, 20, 30);

    const auto v0 = make_vertex(17.25f, 0.25f);
    const auto v1 = make_vertex(18.25f, 120.25f);
    const auto v2 = make_vertex(17.25f, 120.25f);

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const rast::bounding_box bounds = rast::compute_triangle_bounds(
      ctx.states,
      info,
      false);
    const rast::quad_bounds quad_bounds = rast::compute_checked_quad_bounds(
      bounds,
      bounds.start_x,
      bounds.start_y);

    BOOST_CHECK_EQUAL(bounds.tight_start_x, 17);
    BOOST_CHECK_EQUAL(bounds.tight_end_x, 19);
    BOOST_CHECK_EQUAL(bounds.tight_start_y, 20);
    BOOST_CHECK_EQUAL(bounds.tight_end_y, 30);

    BOOST_CHECK_EQUAL(quad_bounds.start_x, 16u);
    BOOST_CHECK_EQUAL(quad_bounds.end_x, 20u);
    BOOST_CHECK_EQUAL(quad_bounds.start_y, 20u);
    BOOST_CHECK_EQUAL(quad_bounds.end_y, 30u);
}

BOOST_AUTO_TEST_CASE(bounded_checked_iteration_preserves_emitted_quads)
{
    constexpr int bs = static_cast<int>(swr::impl::rasterizer_block_size);
    constexpr unsigned int viewport_size = 4 * bs;

    check_sparse_iteration_preserves_coverage(
      "vertical sliver",
      viewport_size,
      viewport_size,
      make_vertex(17.25f, 2.25f),
      make_vertex(18.25f, static_cast<float>(3 * bs) + 30.25f),
      make_vertex(17.25f, static_cast<float>(3 * bs) + 30.25f));

    check_sparse_iteration_preserves_coverage(
      "horizontal sliver",
      viewport_size,
      viewport_size,
      make_vertex(2.25f, 17.25f),
      make_vertex(static_cast<float>(3 * bs) + 30.25f, 18.25f),
      make_vertex(static_cast<float>(3 * bs) + 30.25f, 17.25f));

    check_sparse_iteration_preserves_coverage(
      "long diagonal sliver",
      viewport_size,
      viewport_size,
      make_vertex(3.25f, 2.25f),
      make_vertex(static_cast<float>(3 * bs) + 26.25f, static_cast<float>(3 * bs) + 28.25f),
      make_vertex(static_cast<float>(3 * bs) + 27.25f, static_cast<float>(3 * bs) + 26.25f));
}

BOOST_AUTO_TEST_CASE(thin_trace_preserves_emitted_quads)
{
    constexpr int bs = static_cast<int>(swr::impl::rasterizer_block_size);
    constexpr unsigned int viewport_size = 4 * bs;

    check_thin_trace_preserves_coverage(
      "vertical sliver",
      rast::tile_info::rasterization_mode::thin_y_major,
      viewport_size,
      viewport_size,
      make_vertex(17.25f, 2.25f),
      make_vertex(18.25f, static_cast<float>(3 * bs) + 30.25f),
      make_vertex(17.25f, static_cast<float>(3 * bs) + 30.25f));

    check_thin_trace_preserves_coverage(
      "single-block vertical sliver",
      rast::tile_info::rasterization_mode::thin_y_major,
      viewport_size,
      viewport_size,
      make_vertex(17.25f, 2.25f),
      make_vertex(18.25f, 20.25f),
      make_vertex(17.25f, 20.25f));

    check_thin_trace_preserves_coverage(
      "horizontal sliver",
      rast::tile_info::rasterization_mode::thin_x_major,
      viewport_size,
      viewport_size,
      make_vertex(2.25f, 17.25f),
      make_vertex(static_cast<float>(3 * bs) + 30.25f, 18.25f),
      make_vertex(static_cast<float>(3 * bs) + 30.25f, 17.25f));

    check_thin_trace_preserves_coverage(
      "single-block horizontal sliver",
      rast::tile_info::rasterization_mode::thin_x_major,
      viewport_size,
      viewport_size,
      make_vertex(2.25f, 17.25f),
      make_vertex(20.25f, 18.25f),
      make_vertex(20.25f, 17.25f));

    check_thin_trace_preserves_coverage(
      "long diagonal sliver",
      std::nullopt,
      viewport_size,
      viewport_size,
      make_vertex(3.25f, 2.25f),
      make_vertex(static_cast<float>(3 * bs) + 26.25f, static_cast<float>(3 * bs) + 28.25f),
      make_vertex(static_cast<float>(3 * bs) + 27.25f, static_cast<float>(3 * bs) + 26.25f));
}

BOOST_AUTO_TEST_CASE(thin_trace_provides_precomputed_sparse_payloads)
{
    constexpr int bs = static_cast<int>(swr::impl::rasterizer_block_size);
    constexpr unsigned int viewport_size = 4 * bs;

    const auto v0 = make_vertex(17.25f, 2.25f);
    const auto v1 = make_vertex(18.25f, 20.25f);
    const auto v2 = make_vertex(17.25f, 20.25f);

    triangle_test_context ctx{viewport_size, viewport_size};

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const rast::bounding_box bounds = rast::compute_triangle_bounds(
      ctx.states,
      info,
      false);
    const auto mode = rast::classify_triangle_rasterization(bounds, info).mode;
    BOOST_REQUIRE(mode == rast::tile_info::rasterization_mode::thin_y_major);

    std::vector<checked_quad_sample> payload_quads;
    rast::tile_cache cache;
    cache.reset(viewport_size / bs, viewport_size / bs);
    bool saw_payload = false;
    bool saw_fallback = false;

    rast::for_each_thin_triangle_block_with_bounds(
      ctx.states,
      bounds,
      info,
      v0.varyings,
      0.0f,
      mode,
      [&](int block_x,
          int block_y,
          const geom::barycentric_coordinate_block&,
          const rast::triangle_interpolator&,
          rast::tile_info::rasterization_mode,
          rast::quad_bounds thin_quad_bounds,
          const rast::sparse_triangle_payload* precomputed_payload)
      {
          if(!precomputed_payload)
          {
              saw_fallback = true;
              return;
          }

          saw_payload = true;
          for(const auto& quad: precomputed_payload->quads)
          {
              payload_quads.push_back({
                block_x,
                block_y,
                static_cast<int>(quad.x),
                static_cast<int>(quad.y),
                quad.mask});
          }

          bool emitted = false;
          const bool needs_flush = cache.add_sparse_triangle_checked_payload(
            static_cast<unsigned int>(block_x),
            static_cast<unsigned int>(block_y),
            &ctx.states,
            thin_quad_bounds,
            *precomputed_payload,
            true,
            emitted);
          BOOST_CHECK(!needs_flush);
          BOOST_CHECK(emitted);
      });

    auto thin_quads = collect_thin_trace_quads(
      ctx.states,
      info,
      v0.varyings);
    std::vector<checked_quad_sample> cached_quads;
    for(const auto tile_index: cache.active_tile_indices)
    {
        const auto& tile = cache.entries[tile_index];
        for(const auto& primitive: tile.primitives)
        {
            BOOST_CHECK(primitive.mode == rast::tile_info::rasterization_mode::sparse_checked);
            BOOST_REQUIRE(primitive.precomputed_payload_index < tile.primitive_sparse_payloads.size());
            const auto& cached_payload = tile.primitive_sparse_payloads[primitive.precomputed_payload_index];
            BOOST_REQUIRE(cached_payload.quad_offset + cached_payload.quad_count <= tile.primitive_sparse_quad_payloads.size());
            for(std::uint16_t i = 0; i < cached_payload.quad_count; ++i)
            {
                const auto& quad = tile.primitive_sparse_quad_payloads[cached_payload.quad_offset + i];
                cached_quads.push_back({
                  static_cast<int>(tile.x),
                  static_cast<int>(tile.y),
                  static_cast<int>(quad.x),
                  static_cast<int>(quad.y),
                  quad.mask});
            }
        }
    }

    sort_checked_quads(payload_quads);
    sort_checked_quads(cached_quads);
    sort_checked_quads(thin_quads);

    BOOST_REQUIRE(saw_payload);
    BOOST_CHECK(!saw_fallback);
    BOOST_CHECK_EQUAL_COLLECTIONS(
      payload_quads.begin(), payload_quads.end(),
      thin_quads.begin(), thin_quads.end());
    BOOST_CHECK_EQUAL_COLLECTIONS(
      cached_quads.begin(), cached_quads.end(),
      thin_quads.begin(), thin_quads.end());
}

BOOST_AUTO_TEST_CASE(sparse_triangle_payload_interpolation_matches_regular_interpolator)
{
    constexpr int bs = static_cast<int>(swr::impl::rasterizer_block_size);
    constexpr unsigned int viewport_size = 4 * bs;

    triangle_test_context ctx{viewport_size, viewport_size};
    ctx.program_info.varying_count = 3;
    ctx.program_info.iqs.clear();
    ctx.program_info.iqs.emplace_back(swr::interpolation_qualifier::smooth);
    ctx.program_info.iqs.emplace_back(swr::interpolation_qualifier::no_perspective);
    ctx.program_info.iqs.emplace_back(swr::interpolation_qualifier::flat);
    ctx.program_info.flags |= swr::impl::program_flags::has_flat_varyings;

    auto make_v = [](float x,
                     float y,
                     float z,
                     float w,
                     float smooth_value,
                     float no_perspective_value,
                     float flat_value) -> geom::vertex
    {
        geom::vertex v{};
        v.coords = {x, y, z, w};
        v.varyings.emplace_back(ml::vec4{smooth_value, smooth_value + 1.0f, 0.0f, 0.0f});
        v.varyings.emplace_back(ml::vec4{no_perspective_value, no_perspective_value + 1.0f, 0.0f, 0.0f});
        v.varyings.emplace_back(ml::vec4{flat_value, flat_value + 1.0f, 0.0f, 0.0f});
        return v;
    };

    const auto v0 = make_v(17.25f, 2.25f, 0.10f, 0.50f, 10.0f, 100.0f, 1000.0f);
    const auto v1 = make_v(18.25f, 20.25f, 0.40f, 2.00f, 20.0f, 200.0f, 2000.0f);
    const auto v2 = make_v(17.25f, 20.25f, 0.70f, 1.25f, 30.0f, 300.0f, 3000.0f);

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const rast::bounding_box bounds = rast::compute_triangle_bounds(
      ctx.states,
      info,
      false);
    const auto mode = rast::classify_triangle_rasterization(bounds, info).mode;
    BOOST_REQUIRE(mode == rast::tile_info::rasterization_mode::thin_y_major);

    bool saw_payload = false;
    rast::for_each_thin_triangle_block_with_bounds(
      ctx.states,
      bounds,
      info,
      v0.varyings,
      0.0f,
      mode,
      [&](int block_x,
          int block_y,
          const geom::barycentric_coordinate_block&,
          const rast::triangle_interpolator&,
          rast::tile_info::rasterization_mode,
          rast::quad_bounds,
          const rast::sparse_triangle_payload* precomputed_payload)
      {
          BOOST_REQUIRE(precomputed_payload);
          saw_payload = true;
          check_precomputed_payload_interpolation_matches_regular(
            ctx.states,
            info,
            v0.varyings,
            block_x,
            block_y,
            precomputed_payload->attributes,
            precomputed_payload->quads);
      });

    BOOST_CHECK(saw_payload);
}

BOOST_AUTO_TEST_CASE(coarse_block_rejects_edge_empty_regions)
{
    const auto v0 = make_vertex(100.0f, 100.0f);
    const auto v1 = make_vertex(200.0f, 100.0f);
    const auto v2 = make_vertex(100.0f, 200.0f);

    triangle_test_context ctx{256, 256};

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    bool saw_touched_region = false;
    bool saw_edge_empty_region = false;

    rast::for_each_covered_triangle_block(
      ctx.states,
      info,
      v0.varyings,
      0.0f,
      false,
      [&](int block_x,
          int block_y,
          const geom::barycentric_coordinate_block&,
          const rast::triangle_interpolator&,
          rast::tile_info::rasterization_mode)
      {
          saw_touched_region = true;
          if(block_x == 192
             && block_y == 192)
          {
              saw_edge_empty_region = true;
          }
      });

    BOOST_CHECK(saw_touched_region);
    BOOST_CHECK(!saw_edge_empty_region);
}

BOOST_AUTO_TEST_SUITE_END();
