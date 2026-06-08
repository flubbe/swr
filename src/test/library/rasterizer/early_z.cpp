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
#include "rasterizer/early_depth_policy.h"

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

class counting_color_shader final : public swr::program<counting_color_shader>
{
    ml::vec4 color;
    std::uint64_t* invocation_count{nullptr};
    bool allow_early_depth{false};

public:
    counting_color_shader(
      ml::vec4 in_color,
      std::uint64_t* in_invocation_count,
      bool in_allow_early_depth)
    : color{in_color}
    , invocation_count{in_invocation_count}
    , allow_early_depth{in_allow_early_depth}
    {
    }

    swr::program_metadata get_metadata() const override
    {
        if(!allow_early_depth)
        {
            return {};
        }

        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
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
        ++(*invocation_count);
        gl_FragColor = color;
        return swr::fragment_shader_result::accept;
    }
};

class depth_writing_color_shader final : public swr::program<depth_writing_color_shader>
{
    ml::vec4 color;
    float depth{0.0f};

public:
    depth_writing_color_shader(
      ml::vec4 in_color,
      float in_depth)
    : color{in_color}
    , depth{in_depth}
    {
    }

    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = true};
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
      float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        gl_FragDepth = depth;
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

void draw_depth_writing_fullscreen_triangle(
  const ml::vec4& color,
  float vertex_z,
  float fragment_z)
{
    depth_writing_color_shader shader{color, fragment_z};
    const std::uint32_t shader_id = swr::RegisterShader(&shader);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::vector<ml::vec4> vertices{
      {-1.0f, -1.0f, vertex_z, 1.0f},
      {3.0f, -1.0f, vertex_z, 1.0f},
      {-1.0f, 3.0f, vertex_z, 1.0f}};
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

void draw_counted_fullscreen_triangle(
  std::uint64_t& invocation_count,
  bool allow_early_depth,
  float z)
{
    counting_color_shader shader{
      {0.25f, 0.50f, 0.75f, 1.0f},
      &invocation_count,
      allow_early_depth};
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

void fill_default_depth_checkerboard(
  swr::context_handle context,
  float near_depth,
  float far_depth)
{
    auto* render_context =
      static_cast<swr::impl::render_context*>(context);
    BOOST_REQUIRE(render_context != nullptr);

    auto& depth_buffer = render_context->framebuffer.depth_buffer;
    BOOST_REQUIRE(depth_buffer.info.data_ptr != nullptr);

    const int row_stride =
      depth_buffer.info.pitch / static_cast<int>(sizeof(swr::impl::attachment_depth::value_type));
    for(int y = 0; y < depth_buffer.info.height; ++y)
    {
        for(int x = 0; x < depth_buffer.info.width; ++x)
        {
            depth_buffer.info.data_ptr[y * row_stride + x] =
              ((x + y) & 1) ? ml::fixed_32_t{far_depth} : ml::fixed_32_t{near_depth};
        }
    }
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

}    // namespace

BOOST_AUTO_TEST_SUITE(rasterizer_early_z_policy_tests)

BOOST_AUTO_TEST_CASE(early_depth_sample_accumulates_counts)
{
    rast::early_depth_sample sample;
    sample.set(4, 1);

    rast::early_depth_sample next;
    next.set(3, 2);
    sample.add(next);

    BOOST_CHECK_EQUAL(sample.tested_fragments, 7);
    BOOST_CHECK_EQUAL(sample.rejected_fragments, 3);
}

BOOST_AUTO_TEST_CASE(early_fragment_depth_auto_state_uses_samples_for_decisions)
{
    constexpr std::uint64_t min_fragments =
      rast::early_fragment_depth_test_auto_min_fragments;
    constexpr std::uint64_t reject_threshold =
      (min_fragments * rast::early_fragment_depth_test_auto_threshold_num
       + rast::early_fragment_depth_test_auto_threshold_den - 1)
      / rast::early_fragment_depth_test_auto_threshold_den;

    rast::early_fragment_depth_test_auto_state state;
    rast::early_depth_sample sample;
    sample.set(min_fragments - 1, 0);
    state.record_test_result(sample);
    BOOST_CHECK(
      state.choose_action()
      == rast::early_fragment_depth_test_auto_action::enabled_collect);

    state = {};
    sample.set(min_fragments, 0);
    state.record_test_result(sample);
    for(std::uint64_t i = 1;
        i < rast::early_fragment_depth_test_auto_probe_period;
        ++i)
    {
        BOOST_CHECK(
          state.choose_action()
          == rast::early_fragment_depth_test_auto_action::disabled);
    }
    BOOST_CHECK(
      state.choose_action()
      == rast::early_fragment_depth_test_auto_action::enabled_collect);

    state = {};
    sample.set(min_fragments, reject_threshold);
    state.record_test_result(sample);
    for(std::uint64_t i = 1;
        i < rast::early_fragment_depth_test_auto_sample_period;
        ++i)
    {
        BOOST_CHECK(
          state.choose_action()
          == rast::early_fragment_depth_test_auto_action::enabled_fast);
    }
    BOOST_CHECK(
      state.choose_action()
      == rast::early_fragment_depth_test_auto_action::enabled_collect);
}

BOOST_AUTO_TEST_CASE(early_fragment_depth_auto_state_decays_old_samples)
{
    rast::early_fragment_depth_test_auto_state state;
    state.record_test_result(
      rast::early_fragment_depth_test_auto_window_fragments + 1,
      rast::early_fragment_depth_test_auto_window_fragments + 1);

    BOOST_CHECK_LE(
      state.tested_fragments,
      rast::early_fragment_depth_test_auto_window_fragments);
    BOOST_CHECK_EQUAL(
      state.tested_fragments,
      state.rejected_fragments);
}

BOOST_AUTO_TEST_CASE(block_early_depth_auto_state_uses_reject_ratio_and_probes)
{
    constexpr std::uint64_t min_samples =
      rast::block_early_depth_reject_auto_min_samples;
    constexpr std::uint64_t reject_threshold =
      (min_samples * rast::block_early_depth_reject_auto_threshold_num
       + rast::block_early_depth_reject_auto_threshold_den - 1)
      / rast::block_early_depth_reject_auto_threshold_den;

    rast::block_early_depth_reject_auto_state state;
    for(std::uint64_t i = 0; i < min_samples; ++i)
    {
        state.record_test_result(false);
    }

    for(std::uint64_t i = 1;
        i < rast::block_early_depth_reject_auto_probe_period;
        ++i)
    {
        BOOST_CHECK(!state.should_test());
    }
    BOOST_CHECK(state.should_test());

    state = {};
    for(std::uint64_t i = 0; i < min_samples; ++i)
    {
        state.record_test_result(i < reject_threshold);
    }
    BOOST_CHECK(state.should_test());
}

BOOST_AUTO_TEST_CASE(block_early_depth_auto_state_decays_old_samples)
{
    rast::block_early_depth_reject_auto_state state;
    for(std::uint64_t i = 0;
        i <= rast::block_early_depth_reject_auto_window_samples;
        ++i)
    {
        state.record_test_result(true);
    }

    BOOST_CHECK_LE(
      state.sample_count,
      rast::block_early_depth_reject_auto_window_samples);
    BOOST_CHECK_EQUAL(
      state.sample_count,
      state.rejects);
}

BOOST_AUTO_TEST_SUITE_END()

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

BOOST_AUTO_TEST_CASE(depth_write_disabled_still_allows_passing_fragments_to_write_color)
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

    draw_fullscreen_triangle({1.0f, 0.0f, 0.0f, 1.0f}, 0.5f);
    const auto color_after_first = snapshot_default_color(context);
    const auto depth_after_first = snapshot_default_depth(context);

    swr::SetState(swr::state::depth_write, false);
    draw_fullscreen_triangle({0.0f, 0.0f, 1.0f, 1.0f}, 0.25f);
    const auto color_after_second = snapshot_default_color(context);
    const auto depth_after_second = snapshot_default_depth(context);

    BOOST_CHECK(color_after_second != color_after_first);
    BOOST_CHECK(depth_after_second == depth_after_first);
}

BOOST_AUTO_TEST_CASE(block_early_depth_reject_respects_fragment_depth_writes)
{
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::depth_write, true);
    swr::SetDepthTest(swr::comparison_func::less);
    swr::SetRasterizerFeature(
      swr::rasterizer_feature::block_early_depth_reject,
      swr::rasterizer_feature_mode::on);
    swr::SetRasterizerFeature(
      swr::rasterizer_feature::early_fragment_depth_test,
      swr::rasterizer_feature_mode::on);

    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::SetClearDepth(1.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    draw_fullscreen_triangle({1.0f, 0.0f, 0.0f, 1.0f}, 0.2f);
    const auto color_after_occluder = snapshot_default_color(context);
    const auto depth_after_occluder = snapshot_default_depth(context);

    draw_depth_writing_fullscreen_triangle(
      {0.0f, 0.0f, 1.0f, 1.0f},
      0.8f,
      0.1f);
    const auto color_after_depth_write = snapshot_default_color(context);
    const auto depth_after_depth_write = snapshot_default_depth(context);

    BOOST_CHECK(color_after_depth_write != color_after_occluder);
    BOOST_CHECK(depth_after_depth_write != depth_after_occluder);
}

BOOST_AUTO_TEST_CASE(shader_metadata_enables_early_fragment_depth_rejection)
{
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::depth_write, true);
    swr::SetDepthTest(swr::comparison_func::less);
    swr::SetRasterizerFeature(
      swr::rasterizer_feature::early_fragment_depth_test,
      swr::rasterizer_feature_mode::on);

    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::SetClearDepth(1.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    fill_default_depth_checkerboard(context, 0.2f, 1.0f);

    std::uint64_t conservative_invocations = 0;
    draw_counted_fullscreen_triangle(
      conservative_invocations,
      false,
      0.8f);
    BOOST_CHECK_EQUAL(conservative_invocations, target_size * target_size);

    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    fill_default_depth_checkerboard(context, 0.2f, 1.0f);

    std::uint64_t early_depth_invocations = 0;
    draw_counted_fullscreen_triangle(
      early_depth_invocations,
      true,
      0.8f);

    BOOST_CHECK_EQUAL(early_depth_invocations, target_size * target_size / 2);
}

BOOST_AUTO_TEST_CASE(rasterizer_early_depth_features_are_enabled_by_default)
{
    BOOST_CHECK(swr::GetRasterizerFeature(
                  swr::rasterizer_feature::block_early_depth_reject)
                == swr::rasterizer_feature_mode::automatic);
    BOOST_CHECK(swr::GetRasterizerFeature(
                  swr::rasterizer_feature::early_fragment_depth_test)
                == swr::rasterizer_feature_mode::automatic);
}

BOOST_AUTO_TEST_CASE(rasterizer_early_depth_feature_modes_round_trip)
{
    swr::SetRasterizerFeature(
      swr::rasterizer_feature::block_early_depth_reject,
      swr::rasterizer_feature_mode::on);
    BOOST_CHECK(swr::GetRasterizerFeature(
                  swr::rasterizer_feature::block_early_depth_reject)
                == swr::rasterizer_feature_mode::on);

    swr::SetRasterizerFeature(
      swr::rasterizer_feature::early_fragment_depth_test,
      swr::rasterizer_feature_mode::off);
    BOOST_CHECK(swr::GetRasterizerFeature(
                  swr::rasterizer_feature::early_fragment_depth_test)
                == swr::rasterizer_feature_mode::off);

    swr::SetRasterizerFeature(
      swr::rasterizer_feature::block_early_depth_reject,
      swr::rasterizer_feature_mode::automatic);
    swr::SetRasterizerFeature(
      swr::rasterizer_feature::early_fragment_depth_test,
      swr::rasterizer_feature_mode::automatic);
    BOOST_CHECK(swr::GetRasterizerFeature(
                  swr::rasterizer_feature::block_early_depth_reject)
                == swr::rasterizer_feature_mode::automatic);
    BOOST_CHECK(swr::GetRasterizerFeature(
                  swr::rasterizer_feature::early_fragment_depth_test)
                == swr::rasterizer_feature_mode::automatic);
}

BOOST_AUTO_TEST_CASE(early_fragment_depth_feature_can_be_disabled)
{
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::depth_write, true);
    swr::SetDepthTest(swr::comparison_func::less);
    swr::SetRasterizerFeature(
      swr::rasterizer_feature::block_early_depth_reject,
      swr::rasterizer_feature_mode::off);

    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::SetClearDepth(1.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    fill_default_depth_checkerboard(context, 0.2f, 1.0f);

    swr::SetRasterizerFeature(
      swr::rasterizer_feature::early_fragment_depth_test,
      swr::rasterizer_feature_mode::off);
    std::uint64_t disabled_invocations = 0;
    draw_counted_fullscreen_triangle(
      disabled_invocations,
      true,
      0.8f);
    BOOST_CHECK_EQUAL(disabled_invocations, target_size * target_size);

    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    fill_default_depth_checkerboard(context, 0.2f, 1.0f);

    swr::SetRasterizerFeature(
      swr::rasterizer_feature::early_fragment_depth_test,
      swr::rasterizer_feature_mode::on);
    std::uint64_t enabled_invocations = 0;
    draw_counted_fullscreen_triangle(
      enabled_invocations,
      true,
      0.8f);
    BOOST_CHECK_EQUAL(enabled_invocations, target_size * target_size / 2);
}

BOOST_AUTO_TEST_CASE(block_early_depth_feature_can_be_disabled)
{
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::depth_write, true);
    swr::SetDepthTest(swr::comparison_func::fail);
    swr::SetRasterizerFeature(
      swr::rasterizer_feature::early_fragment_depth_test,
      swr::rasterizer_feature_mode::off);

    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::SetClearDepth(1.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetRasterizerFeature(
      swr::rasterizer_feature::block_early_depth_reject,
      swr::rasterizer_feature_mode::on);
    std::uint64_t enabled_invocations = 0;
    draw_counted_fullscreen_triangle(
      enabled_invocations,
      true,
      0.5f);
    BOOST_CHECK_EQUAL(enabled_invocations, 0);

    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetRasterizerFeature(
      swr::rasterizer_feature::block_early_depth_reject,
      swr::rasterizer_feature_mode::off);
    std::uint64_t disabled_invocations = 0;
    draw_counted_fullscreen_triangle(
      disabled_invocations,
      true,
      0.5f);
    BOOST_CHECK_EQUAL(disabled_invocations, target_size * target_size);
}

BOOST_AUTO_TEST_SUITE_END()
