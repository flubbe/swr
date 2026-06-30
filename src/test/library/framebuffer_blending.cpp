/**
 * swr - a software rasterizer
 *
 * test blending into framebuffer object texture attachments.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE framebuffer blending
#include <boost/test/unit_test.hpp>

/* user headers. */
#include "swr_internal.h"

namespace
{

constexpr std::uint32_t target_size = 64;

struct offscreen_context_fixture
{
    swr::context_handle context{nullptr};

    offscreen_context_fixture()
    {
        context = swr::CreateOffscreenContext(target_size, target_size, 1);
        BOOST_REQUIRE(context != nullptr);
        BOOST_REQUIRE(swr::MakeContextCurrent(context));
        BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    }

    ~offscreen_context_fixture()
    {
        swr::MakeContextCurrent(nullptr);
        swr::DestroyContext(context);
    }
};

class constant_color_shader final : public swr::program<constant_color_shader>
{
    ml::vec4 color;

public:
    explicit constant_color_shader(ml::vec4 in_color)
    : color{in_color}
    {
    }

    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

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
      std::span<const ml::vec4> attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      [[maybe_unused]] std::span<ml::vec4> varyings) const override
    {
        gl_Position = attribs[0];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        gl_FragColor = color;
        return swr::fragment_shader_result::accept;
    }
};

void check_vec4_close(
  const ml::vec4& actual,
  const ml::vec4& expected,
  float epsilon = 1e-5f)
{
    BOOST_CHECK_SMALL(actual.x - expected.x, epsilon);
    BOOST_CHECK_SMALL(actual.y - expected.y, epsilon);
    BOOST_CHECK_SMALL(actual.z - expected.z, epsilon);
    BOOST_CHECK_SMALL(actual.w - expected.w, epsilon);
}

std::uint32_t create_fbo_with_texture_attachment(
  std::uint32_t width = target_size,
  std::uint32_t height = target_size)
{
    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(
      texture_id,
      0,
      width,
      height,
      swr::pixel_format::rgba8888,
      {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t fbo_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(fbo_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::FramebufferTexture(
      fbo_id,
      swr::framebuffer_attachment::color_attachment_0,
      texture_id,
      0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::BindFramebufferObject(swr::framebuffer_target::draw, fbo_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetViewport(0, 0, width, height);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    return texture_id;
}

void draw_fullscreen_triangle(
  ml::vec4 color,
  swr::blend_func src_factor,
  swr::blend_func dst_factor)
{
    constant_color_shader shader{color};
    const std::uint32_t shader_id = swr::RegisterShader(&shader);
    BOOST_REQUIRE(shader_id != 0);

    const std::vector<ml::vec4> vertices{
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {3.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 3.0f, 0.0f, 1.0f}};
    const std::uint32_t vertex_buffer_id = swr::CreateAttributeBuffer(vertices);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    BOOST_REQUIRE(swr::BindShader(shader_id));
    swr::SetState(swr::state::depth_test, false);
    swr::SetState(swr::state::blend, true);
    swr::SetBlendFunc(src_factor, dst_factor);
    swr::EnableAttributeBuffer(vertex_buffer_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::DrawElements(swr::vertex_buffer_mode::triangles, vertices.size());
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
}

const ml::vec4& read_texture_pixel(
  swr::context_handle context,
  std::uint32_t texture_id,
  std::uint32_t x,
  std::uint32_t y)
{
    BOOST_REQUIRE(context != nullptr);

    const auto* render_context =
      static_cast<const swr::impl::render_context*>(context);
    BOOST_REQUIRE_LT(texture_id, render_context->texture_2d_storage.capacity());

    const auto* texture_ptr = render_context->texture_2d_storage[texture_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);

    const auto* texture = texture_ptr->as_texture_color_2d();
    BOOST_REQUIRE(texture != nullptr);
    BOOST_REQUIRE(!texture->data.data_ptrs.empty());
    BOOST_REQUIRE(texture->data.data_ptrs[0] != nullptr);

#ifdef SWR_USE_MORTON_CODES
    return texture->data.data_ptrs[0][libmorton::morton2D_32_encode(x, y)];
#else
    const std::uint32_t pitch =
      texture->data.data_ptrs.size() > 1
        ? texture->width + (texture->width >> 1)
        : texture->width;
    return texture->data.data_ptrs[0][y * pitch + x];
#endif
}

void check_representative_pixels(
  swr::context_handle context,
  std::uint32_t texture_id,
  std::uint32_t width,
  std::uint32_t height,
  ml::vec4 expected)
{
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> samples{
      std::pair{1u, 1u},
      std::pair{width / 2, height / 2},
      std::pair{width - 2, 1u},
      std::pair{1u, height - 2}};

    for(const auto& [x, y]: samples)
    {
        BOOST_TEST_CONTEXT("pixel (" << x << ", " << y << ")")
        {
            check_vec4_close(read_texture_pixel(context, texture_id, x, y), expected);
        }
    }
}

}    // namespace

BOOST_FIXTURE_TEST_SUITE(framebuffer_blending_tests, offscreen_context_fixture)

BOOST_AUTO_TEST_CASE(fbo_blend_one_zero_overwrites_texture_attachment)
{
    const std::uint32_t texture_id = create_fbo_with_texture_attachment();

    const ml::vec4 dest{0.75f, 0.50f, 0.25f, 1.0f};
    const ml::vec4 src{0.10f, 0.30f, 0.70f, 0.90f};

    swr::SetClearColor(dest.r, dest.g, dest.b, dest.a);
    swr::ClearColorBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    check_representative_pixels(context, texture_id, target_size, target_size, dest);

    draw_fullscreen_triangle(
      src,
      swr::blend_func::one,
      swr::blend_func::zero);

    check_representative_pixels(context, texture_id, target_size, target_size, src);
}

BOOST_AUTO_TEST_CASE(fbo_blend_zero_one_preserves_texture_attachment)
{
    const std::uint32_t texture_id = create_fbo_with_texture_attachment();

    const ml::vec4 dest{0.60f, 0.20f, 0.40f, 1.0f};
    const ml::vec4 src{0.90f, 0.10f, 0.30f, 0.50f};

    swr::SetClearColor(dest.r, dest.g, dest.b, dest.a);
    swr::ClearColorBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    check_representative_pixels(context, texture_id, target_size, target_size, dest);

    draw_fullscreen_triangle(
      src,
      swr::blend_func::zero,
      swr::blend_func::one);

    check_representative_pixels(context, texture_id, target_size, target_size, dest);
}

BOOST_AUTO_TEST_CASE(fbo_blend_source_alpha_combines_source_and_destination)
{
    const std::uint32_t texture_id = create_fbo_with_texture_attachment();

    const ml::vec4 dest{0.20f, 0.40f, 0.60f, 0.80f};
    const ml::vec4 src{0.90f, 0.70f, 0.50f, 0.25f};

    swr::SetClearColor(dest.r, dest.g, dest.b, dest.a);
    swr::ClearColorBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    check_representative_pixels(context, texture_id, target_size, target_size, dest);

    draw_fullscreen_triangle(
      src,
      swr::blend_func::src_alpha,
      swr::blend_func::one_minus_src_alpha);

    check_representative_pixels(
      context,
      texture_id,
      target_size,
      target_size,
      ml::lerp(src.a, dest, src));
}

BOOST_AUTO_TEST_CASE(fbo_larger_than_default_draw_target_rasterizes_without_missing_tiles)
{
    constexpr std::uint32_t fbo_width = 128;
    constexpr std::uint32_t fbo_height = 128;

    const std::uint32_t texture_id =
      create_fbo_with_texture_attachment(fbo_width, fbo_height);

    const ml::vec4 clear{0.20f, 0.15f, 0.10f, 1.0f};
    const ml::vec4 src{0.85f, 0.35f, 0.25f, 1.0f};

    swr::SetClearColor(clear.r, clear.g, clear.b, clear.a);
    swr::ClearColorBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    check_representative_pixels(
      context,
      texture_id,
      fbo_width,
      fbo_height,
      clear);

    draw_fullscreen_triangle(
      src,
      swr::blend_func::one,
      swr::blend_func::zero);

    check_representative_pixels(
      context,
      texture_id,
      fbo_width,
      fbo_height,
      src);
}

BOOST_AUTO_TEST_SUITE_END()
