/**
 * swr - a software rasterizer
 *
 * test sparse/thin triangle rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string_view>
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
      [[maybe_unused]] const ml::vec4* attribs,
      [[maybe_unused]] ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] float* gl_ClipDistance,
      [[maybe_unused]] ml::vec4* varyings) const override
    {
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] const boost::container::static_vector<
        swr::varying,
        swr::limits::max::varyings>&
        varyings,
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
    {
        program_info.shader = &shader;

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
  const boost::container::static_vector<ml::vec4, swr::limits::max::varyings>& base_varyings,
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
      base_varyings,
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

          if(use_sparse_bounds)
          {
              rast::for_each_covered_quad_in_checked_triangle_block(
                block_x,
                block_y,
                rast::compute_checked_quad_bounds(bounds, block_x, block_y),
                lambdas_box,
                attributes,
                emit_quad);
          }
          else
          {
              rast::for_each_covered_quad_in_checked_triangle_block(
                block_x,
                block_y,
                lambdas_box,
                attributes,
                emit_quad);
          }
      });

    return out;
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
