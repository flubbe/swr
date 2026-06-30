/**
 * swr - a software rasterizer
 *
 * graphics pipeline tests.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <atomic>
#include <array>
#include <cstring>
#include <cstdint>
#include <set>
#include <vector>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE pipeline tests
#include <boost/test/unit_test.hpp>

/* user headers. */
#include "swr_internal.h"

namespace
{

constexpr std::uint32_t target_size = 32;

ml::mat4x4 opengl_orthographic_projection(
  float left,
  float right,
  float bottom,
  float top,
  float near,
  float far)
{
    const float one_over_width = 1.0f / (right - left);
    const float one_over_height = 1.0f / (top - bottom);
    const float one_over_depth = 1.0f / (far - near);

    return {
      {2.0f * one_over_width, 0.0f, 0.0f, -(right + left) * one_over_width},
      {0.0f, 2.0f * one_over_height, 0.0f, -(top + bottom) * one_over_height},
      {0.0f, 0.0f, -2.0f * one_over_depth, -(far + near) * one_over_depth},
      {0.0f, 0.0f, 0.0f, 1.0f}};
}

class vertex_counting_shader final : public swr::program<vertex_counting_shader>
{
    std::atomic<std::uint64_t>* invocation_count{nullptr};
    ml::vec4 color{1.0f, 0.0f, 0.0f, 1.0f};

public:
    explicit vertex_counting_shader(std::atomic<std::uint64_t>* count)
    : invocation_count{count}
    {
    }

    vertex_counting_shader(std::atomic<std::uint64_t>* count, ml::vec4 in_color)
    : invocation_count{count}
    , color{in_color}
    {
    }

    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    void pre_link(
      boost::container::static_vector<swr::interpolation_qualifier, swr::limits::max::varyings>& iqs) const override
    {
        iqs = {};
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
        if(invocation_count != nullptr)
        {
            invocation_count->fetch_add(1, std::memory_order_relaxed);
        }

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

class texture_sampling_shader final : public swr::program<texture_sampling_shader>
{
public:
    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    void pre_link(
      boost::container::static_vector<swr::interpolation_qualifier, swr::limits::max::varyings>& iqs) const override
    {
        iqs = {swr::interpolation_qualifier::smooth};
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      std::span<const ml::vec4> attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      std::span<ml::vec4> varyings) const override
    {
        gl_Position = attribs[0];
        varyings[0] = attribs[1];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        gl_FragColor = sampler2D(0).sample_at(varyings[0]);
        return swr::fragment_shader_result::accept;
    }
};

class depth_sampling_shader final : public swr::program<depth_sampling_shader>
{
public:
    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    void pre_link(
      boost::container::static_vector<swr::interpolation_qualifier, swr::limits::max::varyings>& iqs) const override
    {
        iqs = {swr::interpolation_qualifier::smooth};
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      std::span<const ml::vec4> attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      std::span<ml::vec4> varyings) const override
    {
        gl_Position = attribs[0];
        varyings[0] = attribs[1];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        const ml::vec4 sampled_depth = sampler2D(0).sample_at(varyings[0]);
        gl_FragColor = {sampled_depth.x,
                        sampled_depth.x,
                        sampled_depth.x,
                        1.0f};
        return swr::fragment_shader_result::accept;
    }
};

class shadow_compare_shader final : public swr::program<shadow_compare_shader>
{
public:
    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    void pre_link(
      boost::container::static_vector<swr::interpolation_qualifier, swr::limits::max::varyings>& iqs) const override
    {
        iqs = {swr::interpolation_qualifier::smooth};
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      std::span<const ml::vec4> attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      std::span<ml::vec4> varyings) const override
    {
        gl_Position = attribs[0];
        varyings[0] = attribs[1];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        const float compare_reference = uniforms[0].f;
        const float lit =
          swr::texture(sampler2DShadow(0), varyings[0], compare_reference);
        gl_FragColor = {lit, lit, lit, 1.0f};
        return swr::fragment_shader_result::accept;
    }
};

struct offscreen_context_fixture
{
    swr::context_handle context{nullptr};

    offscreen_context_fixture()
    {
        context = swr::CreateOffscreenContext(target_size, target_size, 1);
        BOOST_REQUIRE(context != nullptr);
        BOOST_REQUIRE(swr::MakeContextCurrent(context));
    }
};

/**
 * Shader for rendering transformed geometry (for shadow pass testing).
 */
class transformed_solid_color_shader final : public swr::program<transformed_solid_color_shader>
{
    ml::vec4 color;

public:
    transformed_solid_color_shader(ml::vec4 in_color = {1.0f, 0.0f, 0.0f, 1.0f})
    : color{in_color}
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
        // uniforms[0] = proj, uniforms[1] = view
        ml::mat4x4 proj = uniforms[0].m4;
        ml::mat4x4 view = uniforms[1].m4;
        gl_Position = proj * (view * attribs[0]);
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] std::span<const swr::varying> varyings,
      float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        gl_FragColor = color;
        gl_FragDepth = gl_FragCoord.z;
        return swr::fragment_shader_result::accept;
    }
};

/**
 * Shader that writes a constant depth value while also emitting a visible color.
 */
class fixed_depth_shader final : public swr::program<fixed_depth_shader>
{
    float depth_value{0.25f};

public:
    explicit fixed_depth_shader(float in_depth_value)
    : depth_value{in_depth_value}
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
        const ml::mat4x4 proj = uniforms[0].m4;
        const ml::mat4x4 view = uniforms[1].m4;
        gl_Position = proj * (view * attribs[0]);
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] std::span<const swr::varying> varyings,
      float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        gl_FragDepth = depth_value;
        gl_FragColor = ml::vec4{depth_value, depth_value, depth_value, 1.0f};
        return swr::fragment_shader_result::accept;
    }
};

class shadow_demo_depth_shader final : public swr::program<shadow_demo_depth_shader>
{
public:
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
        const ml::mat4x4 light_proj = uniforms[0].m4;
        const ml::mat4x4 light_view = uniforms[1].m4;
        const ml::mat4x4 model = uniforms[2].m4;
        gl_Position = light_proj * (light_view * (model * attribs[0]));
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      [[maybe_unused]] ml::vec4& gl_FragColor) const override
    {
        return swr::fragment_shader_result::accept;
    }
};

std::uint32_t read_default_color_pixel(
  swr::context_handle context,
  int x,
  int y)
{
    const auto* render_context =
      static_cast<const swr::impl::render_context*>(context);
    BOOST_REQUIRE(render_context != nullptr);

    const auto& color_buffer = render_context->framebuffer.color_buffer;
    BOOST_REQUIRE(color_buffer.info.data_ptr != nullptr);

    const int row_stride =
      color_buffer.info.pitch
      / static_cast<int>(sizeof(swr::impl::attachment_color_buffer::value_type));

    return color_buffer.info.data_ptr[y * row_stride + x];
}

std::uint32_t to_default_color_pixel(
  swr::context_handle context,
  ml::vec4 color)
{
    const auto* render_context =
      static_cast<const swr::impl::render_context*>(context);
    BOOST_REQUIRE(render_context != nullptr);

    return render_context->framebuffer.color_buffer.converter.to_pixel(
      ml::clamp_to_unit_interval(color));
}

ml::vec4 sample_texture_uv(
  swr::context_handle context,
  std::uint32_t texture_id,
  float u,
  float v)
{
    const auto* render_context =
      static_cast<const swr::impl::render_context*>(context);
    BOOST_REQUIRE(render_context != nullptr);
    BOOST_REQUIRE(texture_id < render_context->texture_2d_storage.size());

    const auto* texture_ptr = render_context->texture_2d_storage[texture_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);
    const auto* color_texture = texture_ptr->as_texture_color_2d();
    BOOST_REQUIRE(color_texture != nullptr);
    BOOST_REQUIRE(!color_texture->data.data_ptrs.empty());

    const swr::varying uv{{u, v, 0.0f, 0.0f}, {}, {}};
    return texture_ptr->sampler->sample_at(uv);
}

std::uint32_t register_and_bind_shader(
  const vertex_counting_shader& shader)
{
    const std::uint32_t shader_id = swr::RegisterShader(&shader);
    BOOST_REQUIRE(shader_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    BOOST_REQUIRE(swr::BindShader(shader_id));
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    return shader_id;
}

void configure_draw_state()
{
    swr::SetState(swr::state::depth_test, false);
    swr::SetState(swr::state::cull_face, false);
    swr::SetState(swr::state::blend, false);
    swr::SetPolygonMode(swr::polygon_mode::fill);
}

void draw_fullscreen_quad_with_uv(
  std::uint32_t position_buffer,
  std::uint32_t uv_buffer)
{
    swr::EnableAttributeBuffer(position_buffer, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::EnableAttributeBuffer(uv_buffer, 1);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::DrawElements(swr::vertex_buffer_mode::triangles, 6);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::DisableAttributeBuffer(uv_buffer);
    swr::DisableAttributeBuffer(position_buffer);
}

float min_depth_texture_value(
  swr::context_handle context,
  std::uint32_t texture_id)
{
    const auto* render_context =
      static_cast<const swr::impl::render_context*>(context);
    BOOST_REQUIRE(render_context != nullptr);
    BOOST_REQUIRE(texture_id < render_context->texture_2d_storage.size());

    const auto* texture_ptr = render_context->texture_2d_storage[texture_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);
    const auto* depth_texture = texture_ptr->as_texture_depth_2d();
    BOOST_REQUIRE(depth_texture != nullptr);
    BOOST_REQUIRE(!depth_texture->data.data_ptrs.empty());

    const auto width = static_cast<std::size_t>(depth_texture->width);
    const auto height = static_cast<std::size_t>(depth_texture->height);
    float min_depth = 1.0f;
    for(std::size_t y = 0; y < height; ++y)
    {
        for(std::size_t x = 0; x < width; ++x)
        {
            const swr::varying uv{
              {((static_cast<float>(x) + 0.5f) / static_cast<float>(width)),
               ((static_cast<float>(y) + 0.5f) / static_cast<float>(height)),
               0.0f,
               0.0f},
              {},
              {}};
            min_depth = std::min(
              min_depth,
              depth_texture->sampler->sample_depth_at(uv));
        }
    }

    return min_depth;
}

}    // namespace

BOOST_FIXTURE_TEST_SUITE(pipeline_tests, offscreen_context_fixture)

BOOST_AUTO_TEST_CASE(nonindexed_draw_invokes_vertex_shader_once_per_submitted_vertex)
{
    configure_draw_state();

    std::atomic<std::uint64_t> vertex_invocation_count{0};
    vertex_counting_shader shader{&vertex_invocation_count};
    const std::uint32_t shader_id =
      register_and_bind_shader(shader);

    const std::vector<ml::vec4> vertices{
      {-0.75f, -0.75f, 0.0f, 1.0f},
      {0.75f, -0.75f, 0.0f, 1.0f},
      {-0.75f, 0.75f, 0.0f, 1.0f},
      {-0.75f, 0.75f, 0.0f, 1.0f},
      {0.75f, -0.75f, 0.0f, 1.0f},
      {0.75f, 0.75f, 0.0f, 1.0f}};
    const std::uint32_t vertex_buffer_id = swr::CreateAttributeBuffer(vertices);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::EnableAttributeBuffer(vertex_buffer_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::DrawElements(swr::vertex_buffer_mode::triangles, vertices.size());
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    BOOST_CHECK_EQUAL(vertex_invocation_count.load(std::memory_order_relaxed), 6ull);

    swr::DisableAttributeBuffer(vertex_buffer_id);
    swr::DeleteAttributeBuffer(vertex_buffer_id);
    swr::UnregisterShader(shader_id);
}

BOOST_AUTO_TEST_CASE(indexed_draw_invokes_vertex_shader_once_per_unique_index_and_renders)
{
    configure_draw_state();
    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::ClearColorBuffer();

    const ml::vec4 draw_color{0.1f, 0.8f, 0.3f, 1.0f};
    std::atomic<std::uint64_t> vertex_invocation_count{0};
    vertex_counting_shader shader{&vertex_invocation_count, draw_color};
    const std::uint32_t shader_id =
      register_and_bind_shader(shader);

    const std::vector<ml::vec4> vertices{
      {-0.75f, -0.75f, 0.0f, 1.0f},
      {0.75f, -0.75f, 0.0f, 1.0f},
      {-0.75f, 0.75f, 0.0f, 1.0f},
      {0.75f, 0.75f, 0.0f, 1.0f}};
    const std::vector<std::uint32_t> indices{0, 1, 2, 2, 1, 3};

    const std::uint32_t vertex_buffer_id = swr::CreateAttributeBuffer(vertices);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::EnableAttributeBuffer(vertex_buffer_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::DrawIndexedElements(
      swr::vertex_buffer_mode::triangles,
      indices.size(),
      indices);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    BOOST_CHECK_EQUAL(vertex_invocation_count.load(std::memory_order_relaxed), 4ull);
    BOOST_CHECK_EQUAL(
      read_default_color_pixel(context, target_size / 2, target_size / 2),
      to_default_color_pixel(context, draw_color));

    swr::DisableAttributeBuffer(vertex_buffer_id);
    swr::DeleteAttributeBuffer(vertex_buffer_id);
    swr::UnregisterShader(shader_id);
}

BOOST_AUTO_TEST_CASE(scissor_box_uses_opengl_lower_left_coordinates_for_framebuffer_objects)
{
    configure_draw_state();
    swr::SetState(swr::state::scissor_test, true);
    swr::SetScissorBox(0, 0, target_size, target_size / 2);

    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    swr::SetImage(texture_id, 0, target_size, target_size, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture_ptr = static_cast<swr::impl::render_context*>(context)->texture_2d_storage[texture_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);
    texture_ptr->set_filter_mag(swr::texture_filter::nearest);
    texture_ptr->set_filter_min(swr::texture_filter::nearest);

    const std::uint32_t depth_id = swr::CreateDepthRenderbuffer(target_size, target_size);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t framebuffer_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(framebuffer_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::FramebufferTexture(framebuffer_id, swr::framebuffer_attachment::color_attachment_0, texture_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::FramebufferRenderbuffer(framebuffer_id, swr::framebuffer_attachment::depth_attachment, depth_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::BindFramebufferObject(swr::framebuffer_target::draw, framebuffer_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::ClearColorBuffer();

    const ml::vec4 draw_color{0.1f, 0.8f, 0.3f, 1.0f};
    std::atomic<std::uint64_t> vertex_invocation_count{0};
    vertex_counting_shader shader{&vertex_invocation_count, draw_color};
    const std::uint32_t shader_id = register_and_bind_shader(shader);

    const std::vector<ml::vec4> vertices{
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f}};
    const std::uint32_t vertex_buffer_id = swr::CreateAttributeBuffer(vertices);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::EnableAttributeBuffer(vertex_buffer_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::DrawElements(swr::vertex_buffer_mode::triangles, vertices.size());
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const ml::vec4 top_sample = sample_texture_uv(context, texture_id, 0.5f, 0.75f);
    const ml::vec4 bottom_sample = sample_texture_uv(context, texture_id, 0.5f, 0.25f);

    BOOST_CHECK_SMALL(top_sample.x, 1.0e-6f);
    BOOST_CHECK_SMALL(top_sample.y, 1.0e-6f);
    BOOST_CHECK_SMALL(top_sample.z, 1.0e-6f);

    BOOST_CHECK_CLOSE(bottom_sample.x, draw_color.x, 1.0e-3f);
    BOOST_CHECK_CLOSE(bottom_sample.y, draw_color.y, 1.0e-3f);
    BOOST_CHECK_CLOSE(bottom_sample.z, draw_color.z, 1.0e-3f);

    swr::DisableAttributeBuffer(vertex_buffer_id);
    swr::DeleteAttributeBuffer(vertex_buffer_id);
    swr::UnregisterShader(shader_id);
    swr::BindFramebufferObject(swr::framebuffer_target::draw, 0);
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::scissor_test, false);
    swr::ReleaseFramebufferObject(framebuffer_id);
    swr::ReleaseDepthRenderbuffer(depth_id);
    swr::ReleaseTexture(texture_id);
}

BOOST_AUTO_TEST_CASE(viewport_uses_opengl_lower_left_coordinates_for_framebuffer_objects)
{
    configure_draw_state();

    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    swr::SetImage(texture_id, 0, target_size, target_size, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture_ptr = static_cast<swr::impl::render_context*>(context)->texture_2d_storage[texture_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);
    texture_ptr->set_filter_mag(swr::texture_filter::nearest);
    texture_ptr->set_filter_min(swr::texture_filter::nearest);

    const std::uint32_t depth_id = swr::CreateDepthRenderbuffer(target_size, target_size);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t framebuffer_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(framebuffer_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::FramebufferTexture(framebuffer_id, swr::framebuffer_attachment::color_attachment_0, texture_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::FramebufferRenderbuffer(framebuffer_id, swr::framebuffer_attachment::depth_attachment, depth_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::BindFramebufferObject(swr::framebuffer_target::draw, framebuffer_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, 0, target_size, target_size / 2);
    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::ClearColorBuffer();

    const ml::vec4 draw_color{0.1f, 0.8f, 0.3f, 1.0f};
    std::atomic<std::uint64_t> vertex_invocation_count{0};
    vertex_counting_shader shader{&vertex_invocation_count, draw_color};
    const std::uint32_t shader_id = register_and_bind_shader(shader);

    const std::vector<ml::vec4> vertices{
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f}};
    const std::uint32_t vertex_buffer_id = swr::CreateAttributeBuffer(vertices);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::EnableAttributeBuffer(vertex_buffer_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::DrawElements(swr::vertex_buffer_mode::triangles, vertices.size());
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const ml::vec4 top_sample = sample_texture_uv(context, texture_id, 0.5f, 0.75f);
    const ml::vec4 bottom_sample = sample_texture_uv(context, texture_id, 0.5f, 0.25f);

    BOOST_CHECK_SMALL(top_sample.x, 1.0e-6f);
    BOOST_CHECK_SMALL(top_sample.y, 1.0e-6f);
    BOOST_CHECK_SMALL(top_sample.z, 1.0e-6f);

    BOOST_CHECK_CLOSE(bottom_sample.x, draw_color.x, 1.0e-3f);
    BOOST_CHECK_CLOSE(bottom_sample.y, draw_color.y, 1.0e-3f);
    BOOST_CHECK_CLOSE(bottom_sample.z, draw_color.z, 1.0e-3f);

    swr::DisableAttributeBuffer(vertex_buffer_id);
    swr::DeleteAttributeBuffer(vertex_buffer_id);
    swr::UnregisterShader(shader_id);
    swr::BindFramebufferObject(swr::framebuffer_target::draw, 0);
    swr::SetViewport(0, 0, target_size, target_size);
    swr::ReleaseFramebufferObject(framebuffer_id);
    swr::ReleaseDepthRenderbuffer(depth_id);
    swr::ReleaseTexture(texture_id);
}

BOOST_AUTO_TEST_CASE(viewport_y_offset_uses_opengl_lower_left_coordinates_for_framebuffer_objects)
{
    configure_draw_state();

    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    swr::SetImage(texture_id, 0, target_size, target_size, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture_ptr = static_cast<swr::impl::render_context*>(context)->texture_2d_storage[texture_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);
    texture_ptr->set_filter_mag(swr::texture_filter::nearest);
    texture_ptr->set_filter_min(swr::texture_filter::nearest);

    const std::uint32_t depth_id = swr::CreateDepthRenderbuffer(target_size, target_size);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t framebuffer_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(framebuffer_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::FramebufferTexture(framebuffer_id, swr::framebuffer_attachment::color_attachment_0, texture_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::FramebufferRenderbuffer(framebuffer_id, swr::framebuffer_attachment::depth_attachment, depth_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::BindFramebufferObject(swr::framebuffer_target::draw, framebuffer_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, target_size / 2, target_size, target_size / 2);
    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::ClearColorBuffer();

    const ml::vec4 draw_color{0.1f, 0.8f, 0.3f, 1.0f};
    std::atomic<std::uint64_t> vertex_invocation_count{0};
    vertex_counting_shader shader{&vertex_invocation_count, draw_color};
    const std::uint32_t shader_id = register_and_bind_shader(shader);

    const std::vector<ml::vec4> vertices{
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f}};
    const std::uint32_t vertex_buffer_id = swr::CreateAttributeBuffer(vertices);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::EnableAttributeBuffer(vertex_buffer_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::DrawElements(swr::vertex_buffer_mode::triangles, vertices.size());
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const ml::vec4 top_sample = sample_texture_uv(context, texture_id, 0.5f, 0.75f);
    const ml::vec4 bottom_sample = sample_texture_uv(context, texture_id, 0.5f, 0.25f);

    BOOST_CHECK_CLOSE(top_sample.x, draw_color.x, 1.0e-3f);
    BOOST_CHECK_CLOSE(top_sample.y, draw_color.y, 1.0e-3f);
    BOOST_CHECK_CLOSE(top_sample.z, draw_color.z, 1.0e-3f);

    BOOST_CHECK_SMALL(bottom_sample.x, 1.0e-6f);
    BOOST_CHECK_SMALL(bottom_sample.y, 1.0e-6f);
    BOOST_CHECK_SMALL(bottom_sample.z, 1.0e-6f);

    swr::DisableAttributeBuffer(vertex_buffer_id);
    swr::DeleteAttributeBuffer(vertex_buffer_id);
    swr::UnregisterShader(shader_id);
    swr::BindFramebufferObject(swr::framebuffer_target::draw, 0);
    swr::SetViewport(0, 0, target_size, target_size);
    swr::ReleaseFramebufferObject(framebuffer_id);
    swr::ReleaseDepthRenderbuffer(depth_id);
    swr::ReleaseTexture(texture_id);
}

BOOST_AUTO_TEST_CASE(fragment_shader_receives_bound_color_sampler)
{
    configure_draw_state();
    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::ClearColorBuffer();

    texture_sampling_shader shader;
    const std::uint32_t shader_id = swr::RegisterShader(&shader);
    BOOST_REQUIRE(shader_id != 0);
    BOOST_REQUIRE(swr::BindShader(shader_id));

    const std::vector<ml::vec4> positions{
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f}};
    const std::vector<ml::vec4> uvs(6, ml::vec4{0.25f, 0.25f, 0.0f, 0.0f});

    const std::uint32_t pos_id = swr::CreateAttributeBuffer(positions);
    const std::uint32_t uv_id = swr::CreateAttributeBuffer(uvs);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    const std::vector<std::uint8_t> tex_data = {
      0xff, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
      0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    swr::SetImage(texture_id, 0, 2, 2, swr::pixel_format::rgba8888, tex_data);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture_ptr = static_cast<swr::impl::render_context*>(context)->texture_2d_storage[texture_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);
    texture_ptr->set_filter_mag(swr::texture_filter::nearest);
    texture_ptr->set_filter_min(swr::texture_filter::nearest);

    swr::BindTexture(swr::texture_target::texture_2d, texture_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    BOOST_CHECK_EQUAL(
      read_default_color_pixel(context, target_size / 2, target_size / 2),
      to_default_color_pixel(context, ml::vec4{0.0f, 0.0f, 1.0f, 1.0f}));

    swr::DeleteAttributeBuffer(uv_id);
    swr::DeleteAttributeBuffer(pos_id);
    swr::ReleaseTexture(texture_id);
    swr::UnregisterShader(shader_id);
}

BOOST_AUTO_TEST_CASE(fragment_shader_can_read_depth_and_shadow_compare_values)
{
    configure_draw_state();
    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::ClearColorBuffer();

    const std::vector<ml::vec4> positions{
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f}};
    const std::vector<ml::vec4> uvs(6, ml::vec4{0.25f, 0.25f, 0.0f, 0.0f});

    const std::uint32_t pos_id = swr::CreateAttributeBuffer(positions);
    const std::uint32_t uv_id = swr::CreateAttributeBuffer(uvs);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t depth_tex_id = swr::CreateTexture();
    BOOST_REQUIRE(depth_tex_id != 0);
    const std::vector<float> depth_values = {
      0.75f, 0.50f,
      0.25f, 1.00f};
    std::vector<std::uint8_t> depth_data(depth_values.size() * sizeof(float));
    std::memcpy(depth_data.data(), depth_values.data(), depth_data.size());
    swr::SetImage(depth_tex_id, 0, 2, 2, swr::pixel_format::depth32f, depth_data);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture_ptr = static_cast<swr::impl::render_context*>(context)->texture_2d_storage[depth_tex_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);
    texture_ptr->set_filter_mag(swr::texture_filter::nearest);
    texture_ptr->set_filter_min(swr::texture_filter::nearest);

    swr::BindTexture(swr::texture_target::texture_2d, depth_tex_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    depth_sampling_shader depth_shader;
    const std::uint32_t depth_shader_id = swr::RegisterShader(&depth_shader);
    BOOST_REQUIRE(depth_shader_id != 0);
    BOOST_REQUIRE(swr::BindShader(depth_shader_id));

    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    BOOST_CHECK_EQUAL(
      read_default_color_pixel(context, target_size / 2, target_size / 2),
      to_default_color_pixel(context, ml::vec4{0.25f, 0.25f, 0.25f, 1.0f}));

    swr::SetTextureCompareMode(depth_tex_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetTextureCompareFunc(depth_tex_id, swr::comparison_func::less_equal);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    shadow_compare_shader shadow_shader;
    const std::uint32_t shadow_shader_id = swr::RegisterShader(&shadow_shader);
    BOOST_REQUIRE(shadow_shader_id != 0);
    BOOST_REQUIRE(swr::BindShader(shadow_shader_id));

    swr::BindUniform(0, 0.20f);
    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    BOOST_CHECK_EQUAL(
      read_default_color_pixel(context, target_size / 2, target_size / 2),
      to_default_color_pixel(context, ml::vec4{1.0f, 1.0f, 1.0f, 1.0f}));

    swr::BindUniform(0, 0.30f);
    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    BOOST_CHECK_EQUAL(
      read_default_color_pixel(context, target_size / 2, target_size / 2),
      to_default_color_pixel(context, ml::vec4{0.0f, 0.0f, 0.0f, 1.0f}));

    swr::DeleteAttributeBuffer(uv_id);
    swr::DeleteAttributeBuffer(pos_id);
    swr::UnregisterShader(shadow_shader_id);
    swr::UnregisterShader(depth_shader_id);
    swr::ReleaseTexture(depth_tex_id);
}

BOOST_AUTO_TEST_CASE(shadow_compare_can_consume_depth_generated_in_previous_pass)
{
    swr::SetState(swr::state::cull_face, false);
    swr::SetState(swr::state::blend, false);

    const std::vector<ml::vec4> positions{
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f}};
    const std::vector<ml::vec4> uvs(6, ml::vec4{0.5f, 0.5f, 0.0f, 0.0f});

    const std::uint32_t pos_id = swr::CreateAttributeBuffer(positions);
    const std::uint32_t uv_id = swr::CreateAttributeBuffer(uvs);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t color_tex_id = swr::CreateTexture();
    const std::uint32_t depth_tex_id = swr::CreateTexture();
    BOOST_REQUIRE(color_tex_id != 0);
    BOOST_REQUIRE(depth_tex_id != 0);

    swr::SetImage(color_tex_id, 0, target_size, target_size, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetImage(depth_tex_id, 0, target_size, target_size, swr::pixel_format::depth32f, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* depth_tex_ptr = static_cast<swr::impl::render_context*>(context)->texture_2d_storage[depth_tex_id].get();
    BOOST_REQUIRE(depth_tex_ptr != nullptr);
    depth_tex_ptr->set_filter_mag(swr::texture_filter::nearest);
    depth_tex_ptr->set_filter_min(swr::texture_filter::nearest);

    const std::uint32_t fbo_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(fbo_id != 0);
    swr::FramebufferTexture(fbo_id, swr::framebuffer_attachment::color_attachment_0, color_tex_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::FramebufferTexture(fbo_id, swr::framebuffer_attachment::depth_attachment, depth_tex_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    // Pass 1: generate depth map via rasterization.
    swr::BindFramebufferObject(swr::framebuffer_target::draw, fbo_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, true);
    swr::SetClearDepth(1.0f);
    swr::ClearDepthBuffer();

    std::atomic<std::uint64_t> vertex_invocation_count{0};
    vertex_counting_shader depth_writer_shader{&vertex_invocation_count, ml::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    const std::uint32_t depth_writer_shader_id = register_and_bind_shader(depth_writer_shader);

    swr::EnableAttributeBuffer(pos_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::DrawElements(swr::vertex_buffer_mode::triangles, 6);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::DisableAttributeBuffer(pos_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    // Pass 2: read depth map through shadow compare sampling.
    swr::BindFramebufferObject(swr::framebuffer_target::draw, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, false);
    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::ClearColorBuffer();

    swr::SetTextureCompareMode(depth_tex_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetTextureCompareFunc(depth_tex_id, swr::comparison_func::less_equal);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::BindTexture(swr::texture_target::texture_2d, depth_tex_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    shadow_compare_shader shadow_shader;
    const std::uint32_t shadow_shader_id = swr::RegisterShader(&shadow_shader);
    BOOST_REQUIRE(shadow_shader_id != 0);
    BOOST_REQUIRE(swr::BindShader(shadow_shader_id));

    swr::BindUniform(0, 0.25f);
    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    BOOST_CHECK_EQUAL(
      read_default_color_pixel(context, target_size / 2, target_size / 2),
      to_default_color_pixel(context, ml::vec4{1.0f, 1.0f, 1.0f, 1.0f}));

    swr::BindUniform(0, 0.75f);
    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    BOOST_CHECK_EQUAL(
      read_default_color_pixel(context, target_size / 2, target_size / 2),
      to_default_color_pixel(context, ml::vec4{0.0f, 0.0f, 0.0f, 1.0f}));

    swr::DeleteAttributeBuffer(uv_id);
    swr::DeleteAttributeBuffer(pos_id);
    swr::UnregisterShader(shadow_shader_id);
    swr::UnregisterShader(depth_writer_shader_id);
    swr::ReleaseFramebufferObject(fbo_id);
    swr::ReleaseTexture(depth_tex_id);
    swr::ReleaseTexture(color_tex_id);
}

BOOST_AUTO_TEST_CASE(shadow_map_depth_written_in_one_pass_is_visible_to_shadow_compare_shader)
{
    // This is the end-to-end shadow-map regression test.
    // It checks two things:
    // 1. the first pass actually writes non-clear depth into the shadow map
    // 2. the second pass can read that depth from a shader via shadow compare

    swr::SetState(swr::state::cull_face, false);
    swr::SetState(swr::state::blend, false);
    swr::SetState(swr::state::depth_test, true);

    const std::vector<ml::vec4> positions{
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f}};
    const std::vector<ml::vec4> uvs(6, ml::vec4{0.5f, 0.5f, 0.0f, 0.0f});

    const std::uint32_t pos_id = swr::CreateAttributeBuffer(positions);
    const std::uint32_t uv_id = swr::CreateAttributeBuffer(uvs);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t color_tex_id = swr::CreateTexture();
    const std::uint32_t depth_tex_id = swr::CreateTexture();
    BOOST_REQUIRE(color_tex_id != 0);
    BOOST_REQUIRE(depth_tex_id != 0);

    swr::SetImage(color_tex_id, 0, target_size, target_size, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetImage(depth_tex_id, 0, target_size, target_size, swr::pixel_format::depth32f, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* depth_tex_ptr = static_cast<swr::impl::render_context*>(context)->texture_2d_storage[depth_tex_id].get();
    BOOST_REQUIRE(depth_tex_ptr != nullptr);
    depth_tex_ptr->set_filter_mag(swr::texture_filter::nearest);
    depth_tex_ptr->set_filter_min(swr::texture_filter::nearest);

    const std::uint32_t fbo_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(fbo_id != 0);
    swr::FramebufferTexture(fbo_id, swr::framebuffer_attachment::color_attachment_0, color_tex_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::FramebufferTexture(fbo_id, swr::framebuffer_attachment::depth_attachment, depth_tex_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    // Pass 1: write depth into the shadow map.
    swr::BindFramebufferObject(swr::framebuffer_target::draw, fbo_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();

    fixed_depth_shader writer{0.25f};
    const std::uint32_t writer_shader_id = swr::RegisterShader(&writer);
    BOOST_REQUIRE(writer_shader_id != 0);
    BOOST_REQUIRE(swr::BindShader(writer_shader_id));

    swr::BindUniform(0, ml::mat4x4::identity());
    swr::BindUniform(1, ml::mat4x4::identity());

    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto* render_context = static_cast<const swr::impl::render_context*>(context);
    BOOST_REQUIRE(render_context != nullptr);
    const auto* texture_ptr = render_context->texture_2d_storage[depth_tex_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);
    const auto* depth_texture = texture_ptr->as_texture_depth_2d();
    BOOST_REQUIRE(depth_texture != nullptr);
    BOOST_REQUIRE(!depth_texture->data.data_ptrs.empty());

    const std::size_t center_index = static_cast<std::size_t>(target_size / 2) * target_size + static_cast<std::size_t>(target_size / 2);
    const float stored_depth = ml::to_float(depth_texture->data.data_ptrs[0][center_index]);
    BOOST_CHECK_CLOSE(stored_depth, 0.25f, 0.5f);

    // Pass 2: read the written depth via shadow compare.
    swr::BindFramebufferObject(swr::framebuffer_target::draw, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetState(swr::state::depth_test, false);
    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::ClearColorBuffer();

    swr::SetTextureCompareMode(depth_tex_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetTextureCompareFunc(depth_tex_id, swr::comparison_func::less_equal);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::BindTexture(swr::texture_target::texture_2d, depth_tex_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    shadow_compare_shader shadow_shader;
    const std::uint32_t shadow_shader_id = swr::RegisterShader(&shadow_shader);
    BOOST_REQUIRE(shadow_shader_id != 0);
    BOOST_REQUIRE(swr::BindShader(shadow_shader_id));

    swr::BindUniform(0, 0.25f);
    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    BOOST_CHECK_EQUAL(
      read_default_color_pixel(context, target_size / 2, target_size / 2),
      to_default_color_pixel(context, ml::vec4{1.0f, 1.0f, 1.0f, 1.0f}));

    swr::BindUniform(0, 0.75f);
    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    BOOST_CHECK_EQUAL(
      read_default_color_pixel(context, target_size / 2, target_size / 2),
      to_default_color_pixel(context, ml::vec4{0.0f, 0.0f, 0.0f, 1.0f}));

    swr::DeleteAttributeBuffer(uv_id);
    swr::DeleteAttributeBuffer(pos_id);
    swr::UnregisterShader(shadow_shader_id);
    swr::UnregisterShader(writer_shader_id);
    swr::ReleaseFramebufferObject(fbo_id);
    swr::ReleaseTexture(depth_tex_id);
    swr::ReleaseTexture(color_tex_id);
}

BOOST_AUTO_TEST_CASE(depth_texture_can_be_written_and_read_back)
{
    // Directly verify the two things we care about:
    // 1. a fragment shader can write to a depth texture attachment
    // 2. the same depth texture can be sampled back as raw depth

    swr::SetState(swr::state::cull_face, false);
    swr::SetState(swr::state::blend, false);
    swr::SetState(swr::state::depth_test, true);

    const std::vector<ml::vec4> positions{
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f}};
    const std::vector<ml::vec4> uvs(6, ml::vec4{0.5f, 0.5f, 0.0f, 0.0f});

    const std::uint32_t pos_id = swr::CreateAttributeBuffer(positions);
    const std::uint32_t uv_id = swr::CreateAttributeBuffer(uvs);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t color_tex_id = swr::CreateTexture();
    const std::uint32_t depth_tex_id = swr::CreateTexture();
    BOOST_REQUIRE(color_tex_id != 0);
    BOOST_REQUIRE(depth_tex_id != 0);

    swr::SetImage(color_tex_id, 0, target_size, target_size, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetImage(depth_tex_id, 0, target_size, target_size, swr::pixel_format::depth32f, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* depth_tex_ptr = static_cast<swr::impl::render_context*>(context)->texture_2d_storage[depth_tex_id].get();
    BOOST_REQUIRE(depth_tex_ptr != nullptr);
    depth_tex_ptr->set_filter_mag(swr::texture_filter::nearest);
    depth_tex_ptr->set_filter_min(swr::texture_filter::nearest);

    const std::uint32_t fbo_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(fbo_id != 0);
    swr::FramebufferTexture(fbo_id, swr::framebuffer_attachment::color_attachment_0, color_tex_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::FramebufferTexture(fbo_id, swr::framebuffer_attachment::depth_attachment, depth_tex_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::BindFramebufferObject(swr::framebuffer_target::draw, fbo_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, 0, target_size, target_size);
    swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    swr::ClearColorBuffer();
    swr::ClearDepthBuffer();

    fixed_depth_shader writer{0.25f};
    const std::uint32_t writer_shader_id = swr::RegisterShader(&writer);
    BOOST_REQUIRE(writer_shader_id != 0);
    BOOST_REQUIRE(swr::BindShader(writer_shader_id));

    swr::BindUniform(0, ml::mat4x4::identity());
    swr::BindUniform(1, ml::mat4x4::identity());

    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto* render_context = static_cast<const swr::impl::render_context*>(context);
    BOOST_REQUIRE(render_context != nullptr);
    const auto* texture_ptr = render_context->texture_2d_storage[depth_tex_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);
    const auto* depth_texture = texture_ptr->as_texture_depth_2d();
    BOOST_REQUIRE(depth_texture != nullptr);
    BOOST_REQUIRE(!depth_texture->data.data_ptrs.empty());

    const std::size_t center_index = static_cast<std::size_t>(target_size / 2) * target_size + static_cast<std::size_t>(target_size / 2);
    const float stored_depth = ml::to_float(depth_texture->data.data_ptrs[0][center_index]);
    BOOST_CHECK_CLOSE(stored_depth, 0.25f, 0.5f);

    const swr::varying center_uv{{0.5f, 0.5f, 0.0f, 0.0f}, {}, {}};
    const float sampled_depth = texture_ptr->sampler->sample_at(center_uv).x;
    BOOST_CHECK_CLOSE(sampled_depth, 0.25f, 0.5f);

    swr::DeleteAttributeBuffer(uv_id);
    swr::DeleteAttributeBuffer(pos_id);
    swr::UnregisterShader(writer_shader_id);
    swr::ReleaseFramebufferObject(fbo_id);
    swr::ReleaseTexture(depth_tex_id);
    swr::ReleaseTexture(color_tex_id);
}

BOOST_AUTO_TEST_CASE(depth_only_framebuffer_can_capture_depth_for_shadow_mapping)
{
    swr::SetState(swr::state::cull_face, false);
    swr::SetState(swr::state::blend, false);
    swr::SetState(swr::state::depth_test, true);

    const std::vector<ml::vec4> positions{
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f}};
    const std::vector<ml::vec4> uvs(6, ml::vec4{0.5f, 0.5f, 0.0f, 0.0f});

    const std::uint32_t pos_id = swr::CreateAttributeBuffer(positions);
    const std::uint32_t uv_id = swr::CreateAttributeBuffer(uvs);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t depth_tex_id = swr::CreateTexture();
    BOOST_REQUIRE(depth_tex_id != 0);

    swr::SetImage(depth_tex_id, 0, target_size, target_size, swr::pixel_format::depth32f, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* depth_tex_ptr = static_cast<swr::impl::render_context*>(context)->texture_2d_storage[depth_tex_id].get();
    BOOST_REQUIRE(depth_tex_ptr != nullptr);
    depth_tex_ptr->set_filter_mag(swr::texture_filter::nearest);
    depth_tex_ptr->set_filter_min(swr::texture_filter::nearest);

    const std::uint32_t fbo_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(fbo_id != 0);
    swr::FramebufferTexture(fbo_id, swr::framebuffer_attachment::depth_attachment, depth_tex_id, 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::BindFramebufferObject(swr::framebuffer_target::draw, fbo_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, 0, target_size, target_size);
    swr::ClearDepthBuffer();

    fixed_depth_shader writer{0.25f};
    const std::uint32_t writer_shader_id = swr::RegisterShader(&writer);
    BOOST_REQUIRE(writer_shader_id != 0);
    BOOST_REQUIRE(swr::BindShader(writer_shader_id));

    swr::BindUniform(0, ml::mat4x4::identity());
    swr::BindUniform(1, ml::mat4x4::identity());

    draw_fullscreen_quad_with_uv(pos_id, uv_id);
    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto* render_context = static_cast<const swr::impl::render_context*>(context);
    BOOST_REQUIRE(render_context != nullptr);
    const auto* texture_ptr = render_context->texture_2d_storage[depth_tex_id].get();
    BOOST_REQUIRE(texture_ptr != nullptr);
    const auto* depth_texture = texture_ptr->as_texture_depth_2d();
    BOOST_REQUIRE(depth_texture != nullptr);
    BOOST_REQUIRE(!depth_texture->data.data_ptrs.empty());

    const std::size_t center_index =
      static_cast<std::size_t>(target_size / 2) * target_size
      + static_cast<std::size_t>(target_size / 2);
    const float stored_depth =
      ml::to_float(depth_texture->data.data_ptrs[0][center_index]);
    BOOST_CHECK_CLOSE(stored_depth, 0.25f, 0.5f);

    const swr::varying center_uv{{0.5f, 0.5f, 0.0f, 0.0f}, {}, {}};
    const float sampled_depth = texture_ptr->sampler->sample_at(center_uv).x;
    BOOST_CHECK_CLOSE(sampled_depth, 0.25f, 0.5f);

    swr::DeleteAttributeBuffer(uv_id);
    swr::DeleteAttributeBuffer(pos_id);
    swr::UnregisterShader(writer_shader_id);
    swr::ReleaseFramebufferObject(fbo_id);
    swr::ReleaseTexture(depth_tex_id);
}

BOOST_AUTO_TEST_CASE(shadow_demo_light_pass_writes_non_clear_depth_when_culling_is_disabled)
{
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::cull_face, false);
    swr::SetState(swr::state::blend, false);

    std::vector<std::uint32_t> cube_indices = {
#define FACE_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef FACE_LIST
    };
    std::vector<ml::vec4> cube_vertices = {
#define VERTEX_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef VERTEX_LIST
    };
    for(auto& v: cube_vertices)
    {
        v.w = 1.0f;
    }

    std::vector<std::uint32_t> plane_indices = {
#define FACE_LIST(...) __VA_ARGS__
#include "common/plane.geom"
#undef FACE_LIST
    };
    std::vector<ml::vec4> plane_vertices = {
#define VERTEX_LIST(...) __VA_ARGS__
#include "common/plane.geom"
#undef VERTEX_LIST
    };
    for(auto& v: plane_vertices)
    {
        v.w = 1.0f;
    }

    const std::uint32_t cube_verts_id = swr::CreateAttributeBuffer(cube_vertices);
    const std::uint32_t plane_verts_id = swr::CreateAttributeBuffer(plane_vertices);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t depth_tex_id = swr::CreateTexture();
    BOOST_REQUIRE(depth_tex_id != 0);
    swr::SetImage(depth_tex_id, 0, 64, 64, swr::pixel_format::depth32f, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t fbo_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(fbo_id != 0);
    swr::FramebufferTexture(
      fbo_id,
      swr::framebuffer_attachment::depth_attachment,
      depth_tex_id,
      0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    shadow_demo_depth_shader shader;
    const std::uint32_t shader_id = swr::RegisterShader(&shader);
    BOOST_REQUIRE(shader_id != 0);
    BOOST_REQUIRE(swr::BindShader(shader_id));

    const ml::vec3 light_direction{0.45f, -1.0f, -0.35f};
    const ml::vec3 light_eye =
      ml::vec3{-light_direction.x, -light_direction.y, -light_direction.z} * 6.0f;
    const ml::mat4x4 light_proj = opengl_orthographic_projection(
      -7.0f, 7.0f, -7.0f, 7.0f, 1.0f, 18.0f);
    const ml::mat4x4 light_view = ml::matrices::look_at(
      light_eye,
      ml::vec3{0.0f, -0.5f, 0.0f},
      ml::vec3{0.0f, 1.0f, 0.0f});

    ml::mat4x4 cube_model = ml::mat4x4::identity();
    cube_model *= ml::matrices::translation(0.0f, 0.4f, 0.0f);
    cube_model *= ml::matrices::rotation_y(0.7f);
    cube_model *= ml::matrices::rotation_x(0.3f);
    cube_model *= ml::matrices::scaling(0.85f);

    ml::mat4x4 plane_model = ml::mat4x4::identity();
    plane_model *= ml::matrices::translation(0.0f, -1.5f, 0.0f);

    swr::BindFramebufferObject(swr::framebuffer_target::draw, fbo_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, 0, 64, 64);
    swr::SetClearDepth(1.0f);
    swr::ClearDepthBuffer();

    swr::EnableAttributeBuffer(cube_verts_id, 0);
    swr::BindUniform(0, light_proj);
    swr::BindUniform(1, light_view);
    swr::BindUniform(2, cube_model);
    swr::DrawIndexedElements(
      swr::vertex_buffer_mode::triangles,
      cube_indices.size(),
      cube_indices);
    swr::DisableAttributeBuffer(cube_verts_id);

    swr::EnableAttributeBuffer(plane_verts_id, 0);
    swr::BindUniform(2, plane_model);
    swr::DrawIndexedElements(
      swr::vertex_buffer_mode::triangles,
      plane_indices.size(),
      plane_indices);
    swr::DisableAttributeBuffer(plane_verts_id);

    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const float min_depth = min_depth_texture_value(context, depth_tex_id);
    BOOST_CHECK_LT(min_depth, 0.999f);

    swr::UnregisterShader(shader_id);
    swr::ReleaseFramebufferObject(fbo_id);
    swr::ReleaseTexture(depth_tex_id);
    swr::DeleteAttributeBuffer(plane_verts_id);
    swr::DeleteAttributeBuffer(cube_verts_id);
}

BOOST_AUTO_TEST_CASE(shadow_demo_light_pass_writes_non_clear_depth_when_culling_is_enabled)
{
    swr::SetState(swr::state::depth_test, true);
    swr::SetState(swr::state::cull_face, true);
    swr::SetState(swr::state::blend, false);

    std::vector<std::uint32_t> cube_indices = {
#define FACE_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef FACE_LIST
    };
    std::vector<ml::vec4> cube_vertices = {
#define VERTEX_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef VERTEX_LIST
    };
    for(auto& v: cube_vertices)
    {
        v.w = 1.0f;
    }

    std::vector<std::uint32_t> plane_indices = {
#define FACE_LIST(...) __VA_ARGS__
#include "common/plane.geom"
#undef FACE_LIST
    };
    std::vector<ml::vec4> plane_vertices = {
#define VERTEX_LIST(...) __VA_ARGS__
#include "common/plane.geom"
#undef VERTEX_LIST
    };
    for(auto& v: plane_vertices)
    {
        v.w = 1.0f;
    }

    const std::uint32_t cube_verts_id = swr::CreateAttributeBuffer(cube_vertices);
    const std::uint32_t plane_verts_id = swr::CreateAttributeBuffer(plane_vertices);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t depth_tex_id = swr::CreateTexture();
    BOOST_REQUIRE(depth_tex_id != 0);
    swr::SetImage(depth_tex_id, 0, 64, 64, swr::pixel_format::depth32f, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::uint32_t fbo_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(fbo_id != 0);
    swr::FramebufferTexture(
      fbo_id,
      swr::framebuffer_attachment::depth_attachment,
      depth_tex_id,
      0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    shadow_demo_depth_shader shader;
    const std::uint32_t shader_id = swr::RegisterShader(&shader);
    BOOST_REQUIRE(shader_id != 0);
    BOOST_REQUIRE(swr::BindShader(shader_id));

    const ml::vec3 light_direction{0.45f, -1.0f, -0.35f};
    const ml::vec3 light_eye =
      ml::vec3{-light_direction.x, -light_direction.y, -light_direction.z} * 6.0f;
    const ml::mat4x4 light_proj = opengl_orthographic_projection(
      -7.0f, 7.0f, -7.0f, 7.0f, 1.0f, 18.0f);
    const ml::mat4x4 light_view = ml::matrices::look_at(
      light_eye,
      ml::vec3{0.0f, -0.5f, 0.0f},
      ml::vec3{0.0f, 1.0f, 0.0f});

    ml::mat4x4 cube_model = ml::mat4x4::identity();
    cube_model *= ml::matrices::translation(0.0f, 0.4f, 0.0f);
    cube_model *= ml::matrices::rotation_y(0.7f);
    cube_model *= ml::matrices::rotation_x(0.3f);
    cube_model *= ml::matrices::scaling(0.85f);

    ml::mat4x4 plane_model = ml::mat4x4::identity();
    plane_model *= ml::matrices::translation(0.0f, -1.5f, 0.0f);

    swr::BindFramebufferObject(swr::framebuffer_target::draw, fbo_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetViewport(0, 0, 64, 64);
    swr::SetClearDepth(1.0f);
    swr::ClearDepthBuffer();

    swr::EnableAttributeBuffer(cube_verts_id, 0);
    swr::BindUniform(0, light_proj);
    swr::BindUniform(1, light_view);
    swr::BindUniform(2, cube_model);
    swr::DrawIndexedElements(
      swr::vertex_buffer_mode::triangles,
      cube_indices.size(),
      cube_indices);
    swr::DisableAttributeBuffer(cube_verts_id);

    swr::EnableAttributeBuffer(plane_verts_id, 0);
    swr::BindUniform(2, plane_model);
    swr::DrawIndexedElements(
      swr::vertex_buffer_mode::triangles,
      plane_indices.size(),
      plane_indices);
    swr::DisableAttributeBuffer(plane_verts_id);

    swr::Present();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const float min_depth = min_depth_texture_value(context, depth_tex_id);
    BOOST_CHECK_LT(min_depth, 0.999f);

    swr::UnregisterShader(shader_id);
    swr::ReleaseFramebufferObject(fbo_id);
    swr::ReleaseTexture(depth_tex_id);
    swr::DeleteAttributeBuffer(plane_verts_id);
    swr::DeleteAttributeBuffer(cube_verts_id);
}

BOOST_AUTO_TEST_SUITE_END()
