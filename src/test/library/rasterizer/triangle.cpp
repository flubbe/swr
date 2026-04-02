/**
 * swr - a software rasterizer
 *
 * test triangle rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers */
#include <set>

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
#include "rasterizer/triangle.h"
#include "rasterizer/block.h"

/*
 * Helpers.
 */

struct fake_draw_target : swr::impl::framebuffer_draw_target
{
    fake_draw_target(int width, int height)
    : swr::impl::framebuffer_draw_target{}
    {
        properties.reset(width, height);
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
      [[maybe_unused]] float depth_value[4],
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
        geom::limits::max::varyings>&
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
        geom::limits::max::varyings>&
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
    return geom::vertex{.coords = {x, y, 0, 0}};
}

struct triangle_test_context
{
    fake_draw_target draw_target;
    fake_program shader;
    swr::impl::program_info program_info;
    swr::impl::render_states states;

    triangle_test_context(int width, int height)
    : draw_target{width, height}
    {
        program_info.shader = &shader;
        states.shader_info = &program_info;
        states.draw_target = &draw_target;
    }
};

struct covered_triangle_block
{
    int x;
    int y;
    rast::tile_info::rasterization_mode mode;
};

template<typename EmitFn>
std::vector<covered_triangle_block> collect_covered_triangle_blocks(EmitFn&& emit_fn)
{
    std::vector<covered_triangle_block> out;

    emit_fn(
      [&](int x,
          int y,
          const geom::barycentric_coordinate_block&,
          const rast::triangle_interpolator&,
          rast::tile_info::rasterization_mode mode)
      {
          out.push_back({x, y, mode});
      });

    return out;
}

std::vector<covered_triangle_block> collect_covered_triangle_blocks(
  const swr::impl::render_states& states,
  const rast::triangle_info& info,
  const boost::container::static_vector<ml::vec4, 15UL>& base_varyings,
  float polygon_offset = 0.0f,
  bool y_needs_flip = false)
{
    return collect_covered_triangle_blocks(
      [&](auto&& f)
      {
          rast::for_each_covered_triangle_block(
            states,
            info,
            base_varyings,
            polygon_offset,
            y_needs_flip,
            std::forward<decltype(f)>(f));
      });
}

std::vector<std::pair<int, int>> collect_covered_triangle_pixels(
  const swr::impl::render_states& states,
  const rast::triangle_info& info,
  const boost::container::static_vector<ml::vec4, 15UL>& base_varyings,
  float polygon_offset = 0.0f,
  bool y_needs_flip = false)
{
    std::vector<std::pair<int, int>> out;

    rast::for_each_covered_triangle_block(
      states,
      info,
      base_varyings,
      polygon_offset,
      y_needs_flip,
      [&](int block_x,
          int block_y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes,
          rast::tile_info::rasterization_mode mode)
      {
          if(mode == rast::tile_info::rasterization_mode::block)
          {
              rast::for_each_quad_in_triangle_block(
                block_x,
                block_y,
                attributes,
                [&](unsigned int x, unsigned int y, auto&)
                {
                    out.emplace_back(x, y);
                });
          }
          else
          {
              rast::for_each_covered_quad_in_checked_triangle_block(
                block_x,
                block_y,
                lambdas_box,
                attributes,
                [&](unsigned int x, unsigned int y, int mask, auto&)
                {
                    if(mask & 0x8)
                    {
                        out.emplace_back(x + 0, y + 0);
                    }
                    if(mask & 0x4)
                    {
                        out.emplace_back(x + 1, y + 0);
                    }
                    if(mask & 0x2)
                    {
                        out.emplace_back(x + 0, y + 1);
                    }
                    if(mask & 0x1)
                    {
                        out.emplace_back(x + 1, y + 1);
                    }
                });
          }
      });

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());

    return out;
}

namespace rast
{

inline std::ostream& operator<<(
  std::ostream& os,
  const tile_info::rasterization_mode& mode)
{
    return os << (mode == tile_info::rasterization_mode::checked ? "checked" : "block");
}

}    // namespace rast

/*
 * tests.
 */

BOOST_AUTO_TEST_SUITE(triangle_coverage)

BOOST_AUTO_TEST_CASE(setup_degenerate_triangle)
{
    const geom::vertex v0{};
    const geom::vertex v1{};
    const geom::vertex v2{};

    auto draw_target = fake_draw_target{
      10, 10};
    auto states = swr::impl::render_states{};
    states.draw_target = &draw_target;

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(info.is_degenerate);
}

BOOST_AUTO_TEST_CASE(block_covered)
{
    static_assert(swr::impl::rasterizer_block_size >= 8);

    const auto v0 = make_vertex(1, 1);
    const auto v1 = make_vertex(6, 2);
    const auto v2 = make_vertex(2, 4);

    triangle_test_context ctx{10, 10};

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const auto blocks = collect_covered_triangle_blocks(
      ctx.states,
      info,
      v0.varyings);

    BOOST_REQUIRE(!blocks.empty());
    BOOST_CHECK_EQUAL(blocks.size(), 1u);
    BOOST_CHECK_EQUAL(blocks[0].x, 0);
    BOOST_CHECK_EQUAL(blocks[0].y, 0);
    BOOST_CHECK_EQUAL(blocks[0].mode, rast::tile_info::rasterization_mode::checked);
}

BOOST_AUTO_TEST_CASE(single_pixel_triangle_coverage)
{
    const auto v0 = make_vertex(0.25f, 0.25f);
    const auto v1 = make_vertex(1.25f, 0.25f);
    const auto v2 = make_vertex(0.25f, 1.25f);

    triangle_test_context ctx{10, 10};

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const auto pixels = collect_covered_triangle_pixels(
      ctx.states,
      info,
      v0.varyings);

    BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
    BOOST_CHECK_EQUAL(pixels[0].first, 0);
    BOOST_CHECK_EQUAL(pixels[0].second, 0);
}

BOOST_AUTO_TEST_SUITE_END();
