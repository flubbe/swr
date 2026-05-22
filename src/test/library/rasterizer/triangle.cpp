/**
 * swr - a software rasterizer
 *
 * test triangle rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <format>
#include <map>
#include <tuple>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE triangle coverage
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

class alignas(64) over_aligned_program : public swr::program<over_aligned_program>
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
    return geom::vertex{.coords = {x, y, 0, 0}};
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

std::vector<ml::tvec2<int>> collect_covered_triangle_pixels(
  const swr::impl::render_states& states,
  const rast::triangle_info& info,
  const boost::container::static_vector<ml::vec4, 15UL>& base_varyings,
  float polygon_offset = 0.0f,
  bool y_needs_flip = false)
{
    std::vector<ml::tvec2<int>> out;

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
              auto block_attributes = attributes;
              rast::for_each_quad_in_triangle_block(
                block_x,
                block_y,
                block_attributes,
                [&](int x, int y, auto&)
                {
                    if(x < states.x || y < states.y)
                    {
                        return;
                    }
                    if(x >= 0 && y >= 0
                       && (static_cast<unsigned int>(x) >= states.width
                           || static_cast<unsigned int>(y) >= states.height))
                    {
                        return;
                    }

                    out.emplace_back(x, y);
                });
          }
          else
          {
              auto block_attributes = attributes;
              rast::for_each_covered_quad_in_checked_triangle_block(
                block_x,
                block_y,
                lambdas_box,
                block_attributes,
                [&](int x, int y, int mask, auto&)
                {
                    if(x < states.x || y < states.y)
                    {
                        return;
                    }
                    if(x >= 0 && y >= 0
                       && (static_cast<unsigned int>(x) >= states.width
                           || static_cast<unsigned int>(y) >= states.height))
                    {
                        return;
                    }

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

    std::sort(
      out.begin(),
      out.end(),
      [](const ml::tvec2<int>& a, const ml::tvec2<int>& b) -> bool
      {
          if(a.x < b.x)
          {
              return true;
          }
          else if(a.x > b.x)
          {
              return false;
          }

          return a.y < b.y;
      });
    out.erase(
      std::unique(
        out.begin(),
        out.end(),
        [](const ml::tvec2<int>& a, const ml::tvec2<int>& b) -> bool
        {
            return a.x == b.x && a.y == b.y;
        }),
      out.end());

    return out;
}

std::vector<ml::tvec2<int>> make_expected(
  const std::vector<ml::tvec2<int>>& in)
{
    auto out = in;

    std::sort(
      out.begin(),
      out.end(),
      [](const ml::tvec2<int>& a, const ml::tvec2<int>& b) -> bool
      {
          if(a.x < b.x)
          {
              return true;
          }
          else if(a.x > b.x)
          {
              return false;
          }

          return a.y < b.y;
      });

    return out;
}

void check_vec4_close(
  const ml::vec4& actual,
  const ml::vec4& expected,
  float epsilon = 1e-4f)
{
    BOOST_CHECK_SMALL(actual.x - expected.x, epsilon);
    BOOST_CHECK_SMALL(actual.y - expected.y, epsilon);
    BOOST_CHECK_SMALL(actual.z - expected.z, epsilon);
    BOOST_CHECK_SMALL(actual.w - expected.w, epsilon);
}

void check_precomputed_payload_interpolation_matches_regular(
  const swr::impl::render_states& states,
  const rast::triangle_info& info,
  std::span<const ml::vec4> base_varyings,
  int block_x,
  int block_y,
  const rast::small_triangle_interpolator& precomputed_attributes,
  std::span<const rast::small_triangle_quad_payload> quads)
{
    BOOST_REQUIRE(!quads.empty());

    for(const auto& quad: quads)
    {
        BOOST_TEST_CONTEXT(std::format("quad ({}, {}) mask={:#x}", quad.x, quad.y, quad.mask))
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
              base_varyings,
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
}

template<class F>
void for_each_triangle_permutation(
  const geom::vertex& v0,
  const geom::vertex& v1,
  const geom::vertex& v2,
  F&& f)
{
    const std::array<
      std::reference_wrapper<
        const geom::vertex>,
      3>
      vertices{v0, v1, v2};
    std::array<int, 3> perm{0, 1, 2};

    do
    {
        f(vertices[perm[0]].get(),
          vertices[perm[1]].get(),
          vertices[perm[2]].get());
    } while(std::next_permutation(perm.begin(), perm.end()));
}

namespace rast
{

std::ostream& operator<<(
  std::ostream& os,
  const tile_info::rasterization_mode& mode)
{
    switch(mode)
    {
    case tile_info::rasterization_mode::block:
        return os << "block";
    case tile_info::rasterization_mode::checked:
        return os << "checked";
    case tile_info::rasterization_mode::thin_x_major:
        return os << "thin_x_major";
    case tile_info::rasterization_mode::thin_y_major:
        return os << "thin_y_major";
    case tile_info::rasterization_mode::small_checked:
        return os << "small_checked";
    case tile_info::rasterization_mode::sparse_checked:
        return os << "sparse_checked";
    }

    return os << "unknown";
}

}    // namespace rast

namespace ml
{

template<typename T>
bool operator==(const tvec2<T>& a, const tvec2<T>& b)
{
    return a.x == b.x && a.y == b.y;
}

template<typename T>
bool operator!=(const tvec2<T>& a, const tvec2<T>& b)
{
    return !(a == b);
}

std::ostream& operator<<(std::ostream& os, const ml::tvec2<int>& v)
{
    return os << "(" << v.x << ", " << v.y << ")";
}

}    // namespace ml

/*
 * tests.
 */

BOOST_AUTO_TEST_SUITE(triangle_coverage)

BOOST_AUTO_TEST_CASE(over_aligned_shader_storage)
{
    over_aligned_program shader;
    swr::impl::program_info program_info{&shader};

    BOOST_TEST(program_info.program_size == sizeof(over_aligned_program));
    BOOST_TEST(program_info.program_alignment == alignof(over_aligned_program));

    boost::container::static_vector<
      swr::uniform,
      swr::limits::max::uniform_locations>
      uniforms;

    swr::impl::shader_storage_buffer vertex_storage{
      program_info.program_size,
      program_info.program_alignment};
    swr::impl::vertex_shader_instance_container vertex_instance{
      vertex_storage.data(),
      &program_info,
      uniforms};
    BOOST_TEST(
      reinterpret_cast<std::uintptr_t>(vertex_instance.get())
        % alignof(over_aligned_program)
      == 0);

    boost::container::static_vector<
      swr::sampler_2d*,
      swr::limits::max::texture_units>
      samplers_2d;
    swr::impl::fragment_shader_instance_container fragment_instance{
      &program_info,
      uniforms,
      samplers_2d};
    BOOST_TEST(
      reinterpret_cast<std::uintptr_t>(fragment_instance.get())
        % alignof(over_aligned_program)
      == 0);
}

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

BOOST_AUTO_TEST_CASE(checked_block_covered)
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

BOOST_AUTO_TEST_CASE(coarse_block_rejects_empty_edge_regions)
{
    const auto v0 = make_vertex(100, 100);
    const auto v1 = make_vertex(200, 100);
    const auto v2 = make_vertex(100, 200);

    triangle_test_context ctx{256, 256};

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const auto blocks = collect_covered_triangle_blocks(
      ctx.states,
      info,
      v0.varyings);

    const auto outside_it = std::find_if(
      blocks.begin(),
      blocks.end(),
      [](const auto& b)
      {
          return b.x == 192
                 && b.y == 192;
      });
    BOOST_CHECK(outside_it == blocks.end());
}

BOOST_AUTO_TEST_CASE(checked_quad_bounds_preserve_coverage)
{
    const auto v0 = make_vertex(17.25f, 2.25f);
    const auto v1 = make_vertex(18.25f, 30.25f);
    const auto v2 = make_vertex(17.25f, 30.25f);

    triangle_test_context ctx{64, 64};

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const rast::bounding_box bounds = rast::compute_triangle_bounds(
      ctx.states,
      info,
      false);
    const int block_x = bounds.start_x;
    const int block_y = bounds.start_y;
    const rast::quad_bounds quad_bounds = rast::compute_checked_quad_bounds(
      bounds,
      block_x,
      block_y);

    BOOST_CHECK_GE(quad_bounds.start_x, static_cast<unsigned int>(block_x));
    BOOST_CHECK_GE(quad_bounds.start_y, static_cast<unsigned int>(block_y));
    BOOST_CHECK_LE(quad_bounds.end_x, static_cast<unsigned int>(block_x + swr::impl::rasterizer_block_size));
    BOOST_CHECK_LE(quad_bounds.end_y, static_cast<unsigned int>(block_y + swr::impl::rasterizer_block_size));
    BOOST_CHECK_EQUAL(quad_bounds.start_x & 1u, 0u);
    BOOST_CHECK_EQUAL(quad_bounds.start_y & 1u, 0u);
    BOOST_CHECK_EQUAL(quad_bounds.end_x & 1u, 0u);
    BOOST_CHECK_EQUAL(quad_bounds.end_y & 1u, 0u);
    BOOST_CHECK_LT(quad_bounds.end_x - quad_bounds.start_x, swr::impl::rasterizer_block_size);

    auto collect_checked_quads = [&](bool use_tight_bounds)
    {
        std::vector<std::tuple<int, int, int>> out;

        rast::for_each_covered_triangle_block(
          ctx.states,
          info,
          v0.varyings,
          0.0f,
          false,
          [&](int block_x,
              int block_y,
              const geom::barycentric_coordinate_block& lambdas_box,
              const rast::triangle_interpolator& attributes,
              rast::tile_info::rasterization_mode mode)
          {
              if(mode != rast::tile_info::rasterization_mode::checked
                 || block_x != bounds.start_x
                 || block_y != bounds.start_y)
              {
                  return;
              }

              auto block_attributes = attributes;
              if(use_tight_bounds)
              {
                  rast::for_each_covered_quad_in_checked_triangle_block(
                    block_x,
                    block_y,
                    quad_bounds,
                    lambdas_box,
                    block_attributes,
                    [&](int x, int y, int mask, auto&)
                    {
                        out.emplace_back(x, y, mask);
                    });
              }
              else
              {
                  rast::for_each_covered_quad_in_checked_triangle_block(
                    block_x,
                    block_y,
                    lambdas_box,
                    block_attributes,
                    [&](int x, int y, int mask, auto&)
                    {
                        out.emplace_back(x, y, mask);
                    });
              }
          });

        return out;
    };

    const auto tight_quads = collect_checked_quads(true);
    const auto full_quads = collect_checked_quads(false);
    BOOST_REQUIRE_EQUAL(tight_quads.size(), full_quads.size());
    BOOST_CHECK(tight_quads == full_quads);
}

BOOST_AUTO_TEST_CASE(full_block_covered)
{
    constexpr float eps = ml::to_float(cnl::wrap<ml::fixed_28_4_t>(1));
    const auto v0 = make_vertex(0.5f, 0.5f);
    const auto v1 = make_vertex(0.5f + 2 * swr::impl::rasterizer_block_size + eps, 0.5f);
    const auto v2 = make_vertex(0.5f, 0.5f + 2 * swr::impl::rasterizer_block_size + eps);

    triangle_test_context ctx{2 * swr::impl::rasterizer_block_size, 2 * swr::impl::rasterizer_block_size};

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const auto blocks = collect_covered_triangle_blocks(
      ctx.states,
      info,
      v0.varyings);

    BOOST_REQUIRE(!blocks.empty());

    const auto it = std::find_if(
      blocks.begin(),
      blocks.end(),
      [](const auto& b)
      {
          return b.x == 0
                 && b.y == 0
                 && b.mode == rast::tile_info::rasterization_mode::block;
      });

    BOOST_CHECK(it != blocks.end());
}

BOOST_AUTO_TEST_CASE(single_pixel_triangle_coverage)
{
    const auto v0 = make_vertex(0.25f, 0.25f);
    const auto v1 = make_vertex(1.25f, 0.25f);
    const auto v2 = make_vertex(0.25f, 1.25f);

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_REQUIRE_EQUAL(pixels.size(), 1u);
          BOOST_CHECK_EQUAL(pixels[0].x, 0);
          BOOST_CHECK_EQUAL(pixels[0].y, 0);
      });
}

BOOST_AUTO_TEST_CASE(d3d11_1)
{
    const auto v0 = make_vertex(1, 1);
    const auto v1 = make_vertex(6, 2);
    const auto v2 = make_vertex(2, 4);

    const auto expected = make_expected({{1, 1}, {2, 1}, {1, 2}, {2, 2}, {3, 2}, {4, 2}, {2, 3}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(d3d11_2)
{
    const auto v0 = make_vertex(0.5, 0.5);
    const auto v1 = make_vertex(0.5, 0.5);
    const auto v2 = make_vertex(0.5, 0.5);

    triangle_test_context ctx{10, 10};

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(info.is_degenerate);
}

BOOST_AUTO_TEST_CASE(d3d11_3)
{
    const auto v0 = make_vertex(1.25, 0.25);
    const auto v1 = make_vertex(1.25, 1.25);
    const auto v2 = make_vertex(0.25, 1.25);

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_REQUIRE_EQUAL(pixels.size(), 0u);
      });
}

BOOST_AUTO_TEST_CASE(d3d11_4)
{
    const auto v0 = make_vertex(1.5, 0.5);
    const auto v1 = make_vertex(1.5, 1.5);
    const auto v2 = make_vertex(0.5, 1.5);

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_REQUIRE_EQUAL(pixels.size(), 0u);
      });
}

BOOST_AUTO_TEST_CASE(d3d11_5)
{
    const auto v0 = make_vertex(0.75, 2.5);
    const auto v1 = make_vertex(2.75, 0.75);
    const auto v2 = make_vertex(4.75, 2.5);

    const auto expected = make_expected({{2, 1}, {3, 1}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(d3d11_6)
{
    const auto v0 = make_vertex(0.75, 2.5);
    const auto v1 = make_vertex(4.75, 2.5);
    const auto v2 = make_vertex(2.5, 5.25);

    const auto expected = make_expected({{1, 2}, {2, 2}, {3, 2}, {4, 2}, {1, 3}, {2, 3}, {3, 3}, {2, 4}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(d3d11_7)
{
    const auto v0 = make_vertex(2, 0);
    const auto v1 = make_vertex(1.5, 2.5);
    const auto v2 = make_vertex(0.5, 1.5);

    const auto expected = make_expected({{1, 0}, {0, 1}, {1, 1}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(d3d11_8)
{
    const auto v0 = make_vertex(1.5, 2.5);
    const auto v1 = make_vertex(0.5, 1.5);
    const auto v2 = make_vertex(1.5, 4.5);

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_REQUIRE_EQUAL(pixels.size(), 0u);
      });
}

BOOST_AUTO_TEST_CASE(d3d11_9)
{
    const auto v0 = make_vertex(0, 2);
    const auto v1 = make_vertex(6, 0);
    const auto v2 = make_vertex(4, 2);

    const auto expected = make_expected({{4, 0}, {1, 1}, {2, 1}, {3, 1}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(d3d11_10)
{
    const auto v0 = make_vertex(6, 0);
    const auto v1 = make_vertex(4, 2);
    const auto v2 = make_vertex(7, 3);

    const auto expected = make_expected({{5, 0}, {4, 1}, {5, 1}, {5, 2}, {6, 2}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(d3d11_11)
{
    const auto v0 = make_vertex(6, 0);
    const auto v1 = make_vertex(7, 3);
    const auto v2 = make_vertex(8.5, 1.5);

    const auto expected = make_expected({{6, 0}, {6, 1}, {7, 1}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(d3d11_12)
{
    const auto v0 = make_vertex(0.5, 0.5);
    const auto v1 = make_vertex(1.5, 1.5);
    const auto v2 = make_vertex(0.5, 2.5);

    const auto expected = make_expected({{0, 1}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(d3d11_13)
{
    const auto v0 = make_vertex(0.5, 0.5);
    const auto v1 = make_vertex(2.5, 0.5);
    const auto v2 = make_vertex(0.5, 2.5);

    const auto expected = make_expected({{0, 0}, {1, 0}, {0, 1}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(d3d11_14)
{
    const auto v0 = make_vertex(2.5, 2.5);
    const auto v1 = make_vertex(2.5, 0.5);
    const auto v2 = make_vertex(0.5, 2.5);

    const auto expected = make_expected({{1, 1}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(d3d11_15)
{
    const auto v0 = make_vertex(0.5, 9.5);
    const auto v1 = make_vertex(0.5, 11.5);
    const auto v2 = make_vertex(1.5, 9.5);

    const auto expected = make_expected({{0, 9}});

    for_each_triangle_permutation(
      v0, v1, v2,
      [&](const auto& a, const auto& b, const auto& c)
      {
          triangle_test_context ctx{10, 10};

          const auto info = rast::setup_triangle(a, b, c);
          BOOST_REQUIRE(!info.is_degenerate);

          const auto pixels = collect_covered_triangle_pixels(
            ctx.states,
            info,
            a.varyings);

          BOOST_CHECK_EQUAL_COLLECTIONS(
            pixels.begin(), pixels.end(),
            expected.begin(), expected.end());
      });
}

BOOST_AUTO_TEST_CASE(varying_block_iteration_preserves_row_state)
{
    rast::triangle_interpolator attributes{};
    attributes.varyings.emplace_back(
      swr::varying{ml::vec4{10.0f, 0.0f, 0.0f, 0.0f}, ml::vec4::zero(), ml::vec4::zero()},
      ml::tvec2<ml::vec4>{
        ml::vec4{1.0f, 0.0f, 0.0f, 0.0f},
        ml::vec4{10.0f, 0.0f, 0.0f, 0.0f}});
    attributes.setup_block_processing();

    std::vector<std::tuple<int, int, float>> samples;
    rast::for_each_quad_in_triangle_block(
      0,
      0,
      attributes,
      [&](int x, int y, const rast::triangle_interpolator& attrs)
      {
          samples.emplace_back(x, y, attrs.varyings[0].value.x);
      });

    for(const auto& [x, y, value]: samples)
    {
        const float expected = 10.0f + static_cast<float>(x) + static_cast<float>(10 * y);
        BOOST_CHECK_SMALL(value - expected, 1e-5f);
    }
}

BOOST_AUTO_TEST_CASE(mixed_flat_and_smooth_varyings_use_per_varying_qualifiers)
{
    triangle_test_context ctx{16, 16};

    ctx.program_info.varying_count = 2;
    ctx.program_info.iqs.clear();
    ctx.program_info.iqs.emplace_back(swr::interpolation_qualifier::smooth);
    ctx.program_info.iqs.emplace_back(swr::interpolation_qualifier::flat);
    ctx.program_info.flags |= swr::impl::program_flags::has_flat_varyings;

    auto make_v = [](float x, float y, float flat_value) -> geom::vertex
    {
        geom::vertex v{};
        v.coords = {x, y, 0.0f, 1.0f};
        v.varyings.emplace_back(ml::vec4{x + 10.0f * y, 0.0f, 0.0f, 0.0f});
        v.varyings.emplace_back(ml::vec4{flat_value, 0.0f, 0.0f, 0.0f});
        return v;
    };

    const auto v0 = make_v(0.5f, 0.5f, 100.0f);
    const auto v1 = make_v(0.5f, 8.5f, 200.0f);
    const auto v2 = make_v(8.5f, 0.5f, 300.0f);

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);
    BOOST_REQUIRE(info.v0 == &v1);

    std::size_t sample_count = 0;
    bool saw_smooth_value_from_non_provoking_vertex = false;

    auto check_sample = [&](int px,
                            int py,
                            const swr::varying& smooth_varying,
                            const swr::varying& flat_varying)
    {
        const float expected_smooth =
          static_cast<float>(px) + 0.5f
          + 10.0f * (static_cast<float>(py) + 0.5f);

        BOOST_CHECK_SMALL(smooth_varying.value.x - expected_smooth, 1e-4f);
        BOOST_CHECK_SMALL(flat_varying.value.x - v0.varyings[1].x, 1e-5f);

        if(std::abs(smooth_varying.value.x - v0.varyings[0].x) > 1e-4f)
        {
            saw_smooth_value_from_non_provoking_vertex = true;
        }
        ++sample_count;
    };

    auto check_quad = [&](int x,
                          int y,
                          int mask,
                          const rast::triangle_interpolator& attrs)
    {
        std::array<
          boost::container::static_vector<
            swr::varying,
            swr::limits::max::varyings>,
          4>
          temp_varyings;
        ml::vec4 depth;
        ml::vec4 one_over_z;
        attrs.get_data_block(temp_varyings, depth, one_over_z);

        if(mask & 0x8)
        {
            check_sample(x + 0, y + 0, temp_varyings[0][0], temp_varyings[0][1]);
        }
        if(mask & 0x4)
        {
            check_sample(x + 1, y + 0, temp_varyings[1][0], temp_varyings[1][1]);
        }
        if(mask & 0x2)
        {
            check_sample(x + 0, y + 1, temp_varyings[2][0], temp_varyings[2][1]);
        }
        if(mask & 0x1)
        {
            check_sample(x + 1, y + 1, temp_varyings[3][0], temp_varyings[3][1]);
        }
    };

    rast::for_each_covered_triangle_block(
      ctx.states,
      info,
      v0.varyings,
      0.0f,
      false,
      [&](int block_x,
          int block_y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes,
          rast::tile_info::rasterization_mode mode)
      {
          auto block_attributes = attributes;
          block_attributes.setup_block_processing();

          if(mode == rast::tile_info::rasterization_mode::block)
          {
              rast::for_each_quad_in_triangle_block(
                block_x,
                block_y,
                block_attributes,
                [&](int x, int y, const rast::triangle_interpolator& attrs)
                {
                    check_quad(x, y, 0xf, attrs);
                });
          }
          else
          {
              rast::for_each_covered_quad_in_checked_triangle_block(
                block_x,
                block_y,
                lambdas_box,
                block_attributes,
                check_quad);
          }
      });

    BOOST_CHECK_GT(sample_count, 0u);
    BOOST_CHECK(saw_smooth_value_from_non_provoking_vertex);
}

BOOST_AUTO_TEST_CASE(varying_interpolation_no_perspective_and_invalid_qualifier)
{
    boost::container::static_vector<swr::interpolation_qualifier, swr::limits::max::varyings> iqs;
    iqs.emplace_back(swr::interpolation_qualifier::no_perspective);

    geom::vertex a{};
    geom::vertex b{};
    geom::vertex c{};

    a.coords = {0.0f, 0.0f, 0.0f, 0.5f};
    b.coords = {0.0f, 4.0f, 0.0f, 2.0f};
    c.coords = {4.0f, 0.0f, 0.0f, 1.0f};

    a.varyings.emplace_back(ml::vec4{0.0f, 0.0f, 0.0f, 0.0f});
    b.varyings.emplace_back(ml::vec4{40.0f, 0.0f, 0.0f, 0.0f});
    c.varyings.emplace_back(ml::vec4{4.0f, 0.0f, 0.0f, 0.0f});

    const auto info = rast::setup_triangle(a, b, c);
    BOOST_REQUIRE(!info.is_degenerate);

    rast::triangle_interpolator no_persp_attr{
      ml::vec2{0.5f, 0.5f},
      info.v0->coords,
      info.v1->coords,
      info.v2->coords,
      info.v0->varyings,
      info.v1->varyings,
      info.v2->varyings,
      info.v0->varyings,
      iqs,
      info.inv_area,
      0.0f};

    BOOST_CHECK_SMALL(no_persp_attr.varyings[0].value.x - 5.5f, 1e-5f);

    iqs[0] = static_cast<swr::interpolation_qualifier>(999);

    BOOST_CHECK_THROW((rast::triangle_interpolator{
      ml::vec2{0.5f, 0.5f},
      info.v0->coords,
      info.v1->coords,
      info.v2->coords,
      info.v0->varyings,
      info.v1->varyings,
      info.v2->varyings,
      info.v0->varyings,
      iqs,
      info.inv_area,
      0.0f}), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(varying_continuity_across_block_boundaries)
{
    const int bs = static_cast<int>(swr::impl::rasterizer_block_size);
    triangle_test_context ctx{static_cast<unsigned int>(3 * bs), static_cast<unsigned int>(3 * bs)};

    ctx.program_info.varying_count = 1;
    ctx.program_info.iqs.clear();
    ctx.program_info.iqs.emplace_back(swr::interpolation_qualifier::smooth);

    auto make_v = [](float x, float y) -> geom::vertex
    {
        geom::vertex v{};
        v.coords = {x, y, 0.0f, 1.0f};
        v.varyings.emplace_back(ml::vec4{x + y, 0.0f, 0.0f, 0.0f});
        return v;
    };

    const auto v0 = make_v(0.5f, 0.5f);
    const auto v1 = make_v(0.5f + 2.0f * bs, 0.5f);
    const auto v2 = make_v(0.5f, 0.5f + 2.0f * bs);

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    std::map<std::pair<int, int>, float> values_all;
    std::map<std::pair<int, int>, float> values_block;
    std::map<std::pair<int, int>, float> values_checked;
    bool ok = true;
    std::string fail_msg;

    auto expected_value_at = [&](int px, int py) -> float
    {
        rast::triangle_interpolator expected{
          ml::vec2{static_cast<float>(px) + 0.5f, static_cast<float>(py) + 0.5f},
          info.v0->coords, info.v1->coords, info.v2->coords,
          info.v0->varyings, info.v1->varyings, info.v2->varyings, v0.varyings,
          ctx.program_info.iqs, info.inv_area, 0.0f};
        return expected.varyings[0].value.x;
    };

    rast::for_each_covered_triangle_block(
      ctx.states,
      info,
      v0.varyings,
      0.0f,
      false,
      [&](int block_x,
          int block_y,
          const geom::barycentric_coordinate_block& lambdas_box,
          const rast::triangle_interpolator& attributes,
          rast::tile_info::rasterization_mode mode)
      {
          auto emit_px = [&](int px, int py, float value)
          {
              if(!ok)
              {
                  return;
              }

              const float expected = expected_value_at(px, py);
              if(std::abs(value - expected) > 1e-4f)
              {
                  ok = false;
                  fail_msg = std::format(
                    "analytic mismatch at pixel ({}, {}) in block ({}, {}), mode={}, got={}, expected={}",
                    px, py,
                    block_x, block_y,
                    (mode == rast::tile_info::rasterization_mode::block ? "block" : "checked"),
                    value, expected);
                  return;
              }

              auto& values_mode =
                (mode == rast::tile_info::rasterization_mode::block) ? values_block : values_checked;

              const auto key = std::make_pair(px, py);
              auto it = values_mode.find(key);
              if(it == values_mode.end())
              {
                  values_mode.emplace(key, value);
              }
              else if(std::abs(it->second - value) > 1e-5f)
              {
                  ok = false;
                  fail_msg = std::format(
                    "discontinuity at pixel ({}, {}) in block ({}, {}), mode={}, previous={}, current={}",
                    px, py,
                    block_x, block_y,
                    (mode == rast::tile_info::rasterization_mode::block ? "block" : "checked"),
                    it->second, value);
              }

              auto all_it = values_all.find(key);
              if(all_it == values_all.end())
              {
                  values_all.emplace(key, value);
              }
              else if(std::abs(all_it->second - value) > 1e-5f)
              {
                  ok = false;
                  fail_msg = std::format(
                    "cross-mode discontinuity at pixel ({}, {}), previous={}, current={}",
                    px, py,
                    all_it->second, value);
              }
          };

          if(mode == rast::tile_info::rasterization_mode::block)
          {
              auto block_attributes = attributes;
              block_attributes.setup_block_processing();
              rast::for_each_quad_in_triangle_block(
                block_x,
                block_y,
                block_attributes,
                [&](int x, int y, const rast::triangle_interpolator& attrs)
                {
                    std::array<
                      boost::container::static_vector<
                        swr::varying,
                        swr::limits::max::varyings>,
                      4>
                      temp_varyings;
                    ml::vec4 depth;
                    ml::vec4 one_over_z;
                    attrs.get_data_block(temp_varyings, depth, one_over_z);

                    emit_px(x + 0, y + 0, temp_varyings[0][0].value.x);
                    emit_px(x + 1, y + 0, temp_varyings[1][0].value.x);
                    emit_px(x + 0, y + 1, temp_varyings[2][0].value.x);
                    emit_px(x + 1, y + 1, temp_varyings[3][0].value.x);
                });
          }
          else
          {
              auto block_attributes = attributes;
              block_attributes.setup_block_processing();
              rast::for_each_covered_quad_in_checked_triangle_block(
                block_x,
                block_y,
                lambdas_box,
                block_attributes,
                [&](int x, int y, int mask, const rast::triangle_interpolator& attrs)
                {
                    std::array<
                      boost::container::static_vector<
                        swr::varying,
                        swr::limits::max::varyings>,
                      4>
                      temp_varyings;
                    ml::vec4 depth;
                    ml::vec4 one_over_z;
                    attrs.get_data_block(temp_varyings, depth, one_over_z);

                    if(mask & 0x8)
                    {
                        emit_px(x + 0, y + 0, temp_varyings[0][0].value.x);
                    }
                    if(mask & 0x4)
                    {
                        emit_px(x + 1, y + 0, temp_varyings[1][0].value.x);
                    }
                    if(mask & 0x2)
                    {
                        emit_px(x + 0, y + 1, temp_varyings[2][0].value.x);
                    }
                    if(mask & 0x1)
                    {
                        emit_px(x + 1, y + 1, temp_varyings[3][0].value.x);
                    }
                });
          }
      });

    BOOST_CHECK(!values_all.empty());
    // Depending on rasterizer block size and triangle alignment, all touched tiles may route
    // through checked-mode, so block-mode coverage is not guaranteed.
    BOOST_CHECK(!values_checked.empty());

    BOOST_CHECK_MESSAGE(ok, fail_msg);
}

BOOST_AUTO_TEST_CASE(small_quad_triangle_preserves_exact_coverage_inside_block)
{
    const auto v0 = make_vertex(8.5f, 8.5f);
    const auto v1 = make_vertex(11.5f, 8.5f);
    const auto v2 = make_vertex(8.5f, 11.5f);
    const auto expected = make_expected({{8, 8}, {8, 9}, {8, 10}, {9, 8}, {9, 9}, {10, 8}});

    triangle_test_context ctx{16, 16};

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const auto bounds = rast::compute_triangle_bounds(ctx.states, info, false);
    BOOST_REQUIRE(rast::is_small_quad_triangle(bounds));
    BOOST_CHECK_EQUAL(bounds.end_x - bounds.start_x, static_cast<int>(swr::impl::rasterizer_block_size));
    BOOST_CHECK_EQUAL(bounds.end_y - bounds.start_y, static_cast<int>(swr::impl::rasterizer_block_size));

    const auto pixels = collect_covered_triangle_pixels(
      ctx.states,
      info,
      v0.varyings);

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(small_quad_triangle_payload_stores_covered_quads)
{
    fake_draw_target draw_target{16, 16};
    fake_program shader;
    swr::impl::program_info program_info{&shader};
    swr::impl::render_states states;
    states.x = 0;
    states.y = 0;
    states.width = 16;
    states.height = 16;
    states.shader_info = &program_info;
    states.draw_target = &draw_target;

    const auto v0 = make_vertex(8.5f, 8.5f);
    const auto v1 = make_vertex(11.5f, 8.5f);
    const auto v2 = make_vertex(8.5f, 11.5f);

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const auto bounds = rast::compute_triangle_bounds(states, info, false);
    BOOST_REQUIRE(rast::is_small_quad_triangle(bounds));

    const int block_x = swr::impl::lower_align_on_block_size(bounds.tight_start_x);
    const int block_y = swr::impl::lower_align_on_block_size(bounds.tight_start_y);
    const auto lambdas = rast::compute_triangle_lambdas_at_block(info, block_x, block_y);
    const auto quad_bounds = rast::compute_checked_quad_bounds(bounds, block_x, block_y);
    const auto attributes = rast::compute_triangle_attributes_at_block(
      states,
      info,
      v0.varyings,
      0.0f,
      block_x,
      block_y);

    rast::tile_cache cache;
    cache.reset(1, 1);

    bool emitted = false;
    const bool needs_flush = cache.add_small_triangle_checked(
      block_x,
      block_y,
      &states,
      lambdas,
      quad_bounds,
      attributes,
      true,
      emitted);

    BOOST_CHECK(!needs_flush);
    BOOST_REQUIRE(emitted);
    BOOST_REQUIRE_EQUAL(cache.active_tile_indices.size(), 1u);

    auto& tile = cache.entries[cache.active_tile_indices[0]];
    BOOST_REQUIRE_EQUAL(tile.primitives.size(), 1u);
    BOOST_REQUIRE_EQUAL(tile.primitive_small_payloads.size(), 1u);
    BOOST_CHECK_EQUAL(tile.primitives[0].mode, rast::tile_info::rasterization_mode::small_checked);

    const auto& payload = tile.primitive_small_payloads[tile.primitives[0].precomputed_payload_index];
    BOOST_REQUIRE_EQUAL(payload.quad_count, 3u);

    BOOST_CHECK_EQUAL(payload.quads[0].x, 8u);
    BOOST_CHECK_EQUAL(payload.quads[0].y, 8u);
    BOOST_CHECK_EQUAL(payload.quads[0].mask, 0xfu);

    BOOST_CHECK_EQUAL(payload.quads[1].x, 10u);
    BOOST_CHECK_EQUAL(payload.quads[1].y, 8u);
    BOOST_CHECK_EQUAL(payload.quads[1].mask, 0x8u);

    BOOST_CHECK_EQUAL(payload.quads[2].x, 8u);
    BOOST_CHECK_EQUAL(payload.quads[2].y, 10u);
    BOOST_CHECK_EQUAL(payload.quads[2].mask, 0x8u);
}

BOOST_AUTO_TEST_CASE(small_quad_triangle_iterator_provides_precomputed_payload)
{
    triangle_test_context ctx{16, 16};

    const auto v0 = make_vertex(8.5f, 8.5f);
    const auto v1 = make_vertex(11.5f, 8.5f);
    const auto v2 = make_vertex(8.5f, 11.5f);

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const auto bounds = rast::compute_triangle_bounds(ctx.states, info, false);
    BOOST_REQUIRE(rast::is_small_quad_triangle(bounds));

    bool saw_block = false;
    rast::small_triangle_payload payload{};
    rast::quad_bounds payload_bounds{};

    rast::for_each_small_quad_triangle(
      ctx.states,
      bounds,
      info,
      v0.varyings,
      0.0f,
      [&](int,
          int,
          const geom::barycentric_coordinate_block&,
          const rast::triangle_interpolator&,
          rast::tile_info::rasterization_mode mode,
          const rast::quad_bounds* quad_bounds,
          const rast::small_triangle_payload* precomputed_payload)
      {
          saw_block = true;
          BOOST_CHECK_EQUAL(mode, rast::tile_info::rasterization_mode::checked);
          BOOST_REQUIRE(quad_bounds);
          BOOST_REQUIRE(precomputed_payload);

          payload_bounds = *quad_bounds;
          payload = *precomputed_payload;
      });

    BOOST_REQUIRE(saw_block);
    BOOST_CHECK_EQUAL(payload_bounds.start_x, 8u);
    BOOST_CHECK_EQUAL(payload_bounds.start_y, 8u);
    BOOST_CHECK_EQUAL(payload_bounds.end_x, 12u);
    BOOST_CHECK_EQUAL(payload_bounds.end_y, 12u);

    BOOST_REQUIRE_EQUAL(payload.quad_count, 3u);
    BOOST_CHECK_EQUAL(payload.quads[0].x, 8u);
    BOOST_CHECK_EQUAL(payload.quads[0].y, 8u);
    BOOST_CHECK_EQUAL(payload.quads[0].mask, 0xfu);
    BOOST_CHECK_EQUAL(payload.quads[1].x, 10u);
    BOOST_CHECK_EQUAL(payload.quads[1].y, 8u);
    BOOST_CHECK_EQUAL(payload.quads[1].mask, 0x8u);
    BOOST_CHECK_EQUAL(payload.quads[2].x, 8u);
    BOOST_CHECK_EQUAL(payload.quads[2].y, 10u);
    BOOST_CHECK_EQUAL(payload.quads[2].mask, 0x8u);
}

BOOST_AUTO_TEST_CASE(small_quad_triangle_payload_interpolation_matches_regular_interpolator)
{
    triangle_test_context ctx{16, 16};
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

    const auto v0 = make_v(8.5f, 8.5f, 0.10f, 0.50f, 10.0f, 100.0f, 1000.0f);
    const auto v1 = make_v(11.5f, 8.5f, 0.40f, 2.00f, 20.0f, 200.0f, 2000.0f);
    const auto v2 = make_v(8.5f, 11.5f, 0.70f, 1.25f, 30.0f, 300.0f, 3000.0f);

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const auto bounds = rast::compute_triangle_bounds(ctx.states, info, false);
    BOOST_REQUIRE(rast::is_small_quad_triangle(bounds));

    bool saw_payload = false;
    rast::for_each_small_quad_triangle(
      ctx.states,
      bounds,
      info,
      v0.varyings,
      0.0f,
      [&](int block_x,
          int block_y,
          const geom::barycentric_coordinate_block&,
          const rast::triangle_interpolator&,
          rast::tile_info::rasterization_mode,
          const rast::quad_bounds*,
          const rast::small_triangle_payload* precomputed_payload)
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
            std::span{
              precomputed_payload->quads.data(),
              precomputed_payload->quad_count});
      });

    BOOST_CHECK(saw_payload);
}

BOOST_AUTO_TEST_CASE(small_quad_triangle_threshold_is_two_by_two_quads)
{
    static_assert(rast::rasterizer_quad_size == 2);
    static_assert(rast::small_triangle_quad_span == 2);
    static_assert(rast::small_triangle_footprint_size == 4);

    const auto v0 = make_vertex(2.5f, 0.5f);
    const auto v1 = make_vertex(4.5f, 0.5f);
    const auto v2 = make_vertex(2.5f, 2.5f);
    const auto expected = make_expected({{2, 0}, {2, 1}, {3, 0}});

    triangle_test_context ctx{10, 10};

    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);

    const auto bounds = rast::compute_triangle_bounds(ctx.states, info, false);
    const int quad_start_x = rast::lower_align_on_quad_size(bounds.tight_start_x);
    const int quad_start_y = rast::lower_align_on_quad_size(bounds.tight_start_y);
    const int quad_end_x = rast::upper_align_on_quad_size(bounds.tight_end_x);
    const int quad_end_y = rast::upper_align_on_quad_size(bounds.tight_end_y);

    BOOST_REQUIRE(rast::is_small_quad_triangle(bounds));
    BOOST_CHECK_EQUAL(quad_end_x - quad_start_x, rast::small_triangle_footprint_size);
    BOOST_CHECK_EQUAL(quad_end_y - quad_start_y, rast::small_triangle_footprint_size);

    const auto pixels = collect_covered_triangle_pixels(
      ctx.states,
      info,
      v0.varyings);

    BOOST_CHECK_EQUAL_COLLECTIONS(
      pixels.begin(), pixels.end(),
      expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(small_quad_triangle_crossing_block_boundary_uses_general_path)
{
    constexpr int block_size = static_cast<int>(swr::impl::rasterizer_block_size);
    const auto v0 = make_vertex(block_size - 2.5f, 1.5f);
    const auto v1 = make_vertex(block_size + 1.5f, 1.5f);
    const auto v2 = make_vertex(block_size - 2.5f, 3.5f);

    triangle_test_context ctx{block_size * 2, block_size};
    const auto info = rast::setup_triangle(v0, v1, v2);
    BOOST_REQUIRE(!info.is_degenerate);
    const auto bounds = rast::compute_triangle_bounds(ctx.states, info, false);

    BOOST_CHECK(!rast::is_small_quad_triangle(bounds));

    const auto pixels = collect_covered_triangle_pixels(ctx.states, info, v0.varyings);
    BOOST_REQUIRE(!pixels.empty());

    bool has_left = false, has_right = false;
    for(const auto& px: pixels)
    {
        if(px.x < block_size)
            has_left = true;
        if(px.x >= block_size)
            has_right = true;
    }
    BOOST_CHECK_MESSAGE(has_left && has_right, "Triangle covers both sides of the block boundary");
}

BOOST_AUTO_TEST_SUITE_END();
