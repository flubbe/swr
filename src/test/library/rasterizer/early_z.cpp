/**
 * swr - a software rasterizer
 *
 * test conservative early depth rejection behavior via fully occluded draws.
 */

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE rasterizer early z
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

class constant_color_shader : public swr::program<constant_color_shader>
{
    ml::vec4 color;

public:
    explicit constant_color_shader(ml::vec4 in_color)
    : color{in_color}
    {
    }

    void pre_link(
      boost::container::static_vector<
        swr::interpolation_qualifier,
        swr::limits::max::varyings>& iqs) const override
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

void draw_fullscreen_triangle(
  const ml::vec4& color,
  float z)
{
    constant_color_shader shader{color};
    const std::uint32_t shader_id = swr::RegisterShader(&shader);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::vector<ml::vec4> vertices{
      {-1.0f, -1.0f, z, 1.0f},
      {3.0f, -1.0f, z, 1.0f},
      {-1.0f, 3.0f, z, 1.0f}};
    const std::uint32_t vertex_buffer_id = swr::CreateAttributeBuffer(vertices);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    BOOST_REQUIRE(swr::BindShader(shader_id));
    swr::EnableAttributeBuffer(vertex_buffer_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::DrawElements(swr::vertex_buffer_mode::triangles, vertices.size());
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::DisableAttributeBuffer(vertex_buffer_id);
    swr::DeleteAttributeBuffer(vertex_buffer_id);
    swr::UnregisterShader(shader_id);
}

std::vector<std::uint32_t> snapshot_default_color(swr::context_handle context)
{
    const auto* render_context =
      static_cast<const swr::impl::render_context*>(context);
    BOOST_REQUIRE(render_context != nullptr);

    const auto& color_buffer = render_context->framebuffer.color_buffer;
    BOOST_REQUIRE(color_buffer.info.data_ptr != nullptr);
    BOOST_REQUIRE(color_buffer.info.width > 0);
    BOOST_REQUIRE(color_buffer.info.height > 0);

    const int pixel_count = color_buffer.info.width * color_buffer.info.height;
    return {
      color_buffer.info.data_ptr,
      color_buffer.info.data_ptr + pixel_count};
}

std::vector<std::uint32_t> snapshot_default_depth(swr::context_handle context)
{
    const auto* render_context =
      static_cast<const swr::impl::render_context*>(context);
    BOOST_REQUIRE(render_context != nullptr);

    const auto& depth_buffer = render_context->framebuffer.depth_buffer;
    BOOST_REQUIRE(depth_buffer.info.data_ptr != nullptr);
    BOOST_REQUIRE(depth_buffer.info.width > 0);
    BOOST_REQUIRE(depth_buffer.info.height > 0);

    const int pixel_count = depth_buffer.info.width * depth_buffer.info.height;
    std::vector<std::uint32_t> out;
    out.reserve(pixel_count);
    for(int i = 0; i < pixel_count; ++i)
    {
        out.push_back(ml::unwrap(depth_buffer.info.data_ptr[i]));
    }
    return out;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(rasterizer_early_z_tests, offscreen_context_fixture)

BOOST_AUTO_TEST_CASE(fully_occluded_triangle_does_not_change_color_or_depth_buffers)
{
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::depth_write, true);
    swr::SetDepthTest(swr::comparison_func::less);

    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::SetClearDepth(1.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    // Occluder: near and covering the full viewport.
    draw_fullscreen_triangle({1.0f, 0.0f, 0.0f, 1.0f}, 0.2f);
    const auto color_after_occluder = snapshot_default_color(context);
    const auto depth_after_occluder = snapshot_default_depth(context);

    // Occluded: farther and also covering the full viewport.
    draw_fullscreen_triangle({0.0f, 0.0f, 1.0f, 1.0f}, 0.8f);
    const auto color_after_occluded = snapshot_default_color(context);
    const auto depth_after_occluded = snapshot_default_depth(context);

    BOOST_CHECK(color_after_occluded == color_after_occluder);
    BOOST_CHECK(depth_after_occluded == depth_after_occluder);
}

BOOST_AUTO_TEST_CASE(fully_occluded_triangle_with_less_equal_does_not_change_color_or_depth_buffers)
{
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::depth_write, true);
    swr::SetDepthTest(swr::comparison_func::less_equal);

    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::SetClearDepth(1.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    draw_fullscreen_triangle({0.0f, 1.0f, 0.0f, 1.0f}, 0.25f);
    const auto color_after_occluder = snapshot_default_color(context);
    const auto depth_after_occluder = snapshot_default_depth(context);

    draw_fullscreen_triangle({1.0f, 1.0f, 0.0f, 1.0f}, 0.75f);
    const auto color_after_occluded = snapshot_default_color(context);
    const auto depth_after_occluded = snapshot_default_depth(context);

    BOOST_CHECK(color_after_occluded == color_after_occluder);
    BOOST_CHECK(depth_after_occluded == depth_after_occluder);
}

BOOST_AUTO_TEST_CASE(nearer_second_triangle_overwrites_color_and_depth)
{
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::depth_write, true);
    swr::SetDepthTest(swr::comparison_func::less);

    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::SetClearDepth(1.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    draw_fullscreen_triangle({1.0f, 0.0f, 0.0f, 1.0f}, 0.7f);
    const auto color_after_far = snapshot_default_color(context);
    const auto depth_after_far = snapshot_default_depth(context);

    draw_fullscreen_triangle({0.0f, 0.0f, 1.0f, 1.0f}, 0.3f);
    const auto color_after_near = snapshot_default_color(context);
    const auto depth_after_near = snapshot_default_depth(context);

    BOOST_CHECK(color_after_near != color_after_far);
    BOOST_CHECK(depth_after_near != depth_after_far);
}

BOOST_AUTO_TEST_CASE(greater_depth_test_allows_farther_second_triangle)
{
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::depth_write, true);
    swr::SetDepthTest(swr::comparison_func::greater);

    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::SetClearDepth(0.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    draw_fullscreen_triangle({0.0f, 1.0f, 1.0f, 1.0f}, 0.2f);
    const auto color_after_near = snapshot_default_color(context);
    const auto depth_after_near = snapshot_default_depth(context);

    draw_fullscreen_triangle({1.0f, 0.0f, 1.0f, 1.0f}, 0.8f);
    const auto color_after_far = snapshot_default_color(context);
    const auto depth_after_far = snapshot_default_depth(context);

    BOOST_CHECK(color_after_far != color_after_near);
    BOOST_CHECK(depth_after_far != depth_after_near);
}

BOOST_AUTO_TEST_CASE(equal_depth_test_rejects_when_depth_ranges_do_not_overlap)
{
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::depth_write, true);
    swr::SetDepthTest(swr::comparison_func::equal);

    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::SetClearDepth(0.2f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    draw_fullscreen_triangle({1.0f, 0.0f, 0.0f, 1.0f}, 0.2f);
    const auto color_after_first = snapshot_default_color(context);
    const auto depth_after_first = snapshot_default_depth(context);

    draw_fullscreen_triangle({0.0f, 1.0f, 0.0f, 1.0f}, 0.8f);
    const auto color_after_second = snapshot_default_color(context);
    const auto depth_after_second = snapshot_default_depth(context);

    BOOST_CHECK(color_after_second == color_after_first);
    BOOST_CHECK(depth_after_second == depth_after_first);
}

BOOST_AUTO_TEST_CASE(not_equal_depth_test_rejects_when_old_and_new_depth_are_identical)
{
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::depth_write, true);
    swr::SetDepthTest(swr::comparison_func::not_equal);

    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::SetClearDepth(1.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    draw_fullscreen_triangle({1.0f, 0.0f, 1.0f, 1.0f}, 0.5f);
    const auto color_after_first = snapshot_default_color(context);
    const auto depth_after_first = snapshot_default_depth(context);

    draw_fullscreen_triangle({0.0f, 1.0f, 1.0f, 1.0f}, 0.5f);
    const auto color_after_second = snapshot_default_color(context);
    const auto depth_after_second = snapshot_default_depth(context);

    BOOST_CHECK(color_after_second == color_after_first);
    BOOST_CHECK(depth_after_second == depth_after_first);
}

BOOST_AUTO_TEST_CASE(all_depth_functions_match_expected_second_draw_visibility)
{
    struct depth_case
    {
        swr::comparison_func func;
        std::string_view name;
        float clear_depth;
        bool second_draw_should_write;
    };

    // Scenario:
    // - First draw writes z=0.2 into the full viewport.
    // - Second draw uses z=0.8.
    // This table encodes expected visibility/write behavior for the second draw.
    constexpr std::array<depth_case, 8> cases{{
      {swr::comparison_func::pass, "pass", 1.0f, true},
      {swr::comparison_func::fail, "fail", 1.0f, false},
      {swr::comparison_func::equal, "equal", 0.2f, false},
      {swr::comparison_func::not_equal, "not_equal", 1.0f, true},
      {swr::comparison_func::less, "less", 1.0f, false},
      {swr::comparison_func::less_equal, "less_equal", 1.0f, false},
      {swr::comparison_func::greater, "greater", 0.0f, true},
      {swr::comparison_func::greater_equal, "greater_equal", 0.0f, true},
    }};

    for(const auto& c: cases)
    {
        BOOST_TEST_CONTEXT("depth_func=" << c.name)
        {
            swr::SetViewport(0, 0, target_size, target_size);
            swr::SetState(swr::state::depth_test, true);
            swr::SetState(swr::state::depth_write, true);
            swr::SetDepthTest(c.func);

            swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            swr::SetClearDepth(c.clear_depth);
            swr::ClearColorBuffer();
            swr::ClearDepthBuffer();
            BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

            draw_fullscreen_triangle({1.0f, 0.0f, 0.0f, 1.0f}, 0.2f);
            const auto color_after_first = snapshot_default_color(context);
            const auto depth_after_first = snapshot_default_depth(context);

            draw_fullscreen_triangle({0.0f, 1.0f, 0.0f, 1.0f}, 0.8f);
            const auto color_after_second = snapshot_default_color(context);
            const auto depth_after_second = snapshot_default_depth(context);

            if(c.second_draw_should_write)
            {
                BOOST_CHECK(color_after_second != color_after_first);
                BOOST_CHECK(depth_after_second != depth_after_first);
            }
            else
            {
                BOOST_CHECK(color_after_second == color_after_first);
                BOOST_CHECK(depth_after_second == depth_after_first);
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
