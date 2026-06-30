/**
 * swr - a software rasterizer
 *
 * test texture management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE texture tests
#include <boost/test/unit_test.hpp>

#include "swr_internal.h"

namespace
{

struct offscreen_context_fixture
{
    swr::context_handle context{nullptr};

    offscreen_context_fixture()
    {
        context = swr::CreateOffscreenContext(2, 2, 1);
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

std::vector<std::uint8_t> make_rgba_data(std::size_t texel_count)
{
    std::vector<std::uint8_t> data(texel_count * 4);
    for(std::size_t i = 0; i < texel_count; ++i)
    {
        data[i * 4 + 0] = static_cast<std::uint8_t>(i);
        data[i * 4 + 1] = 0x40;
        data[i * 4 + 2] = 0x80;
        data[i * 4 + 3] = 0xff;
    }

    return data;
}

std::vector<std::uint8_t> make_depth_data(const std::vector<float>& depths)
{
    std::vector<std::uint8_t> data(depths.size() * sizeof(float));
    std::memcpy(
      data.data(),
      depths.data(),
      data.size());
    return data;
}

swr::impl::texture_2d* get_texture_ptr(
  swr::context_handle context,
  std::uint32_t texture_id)
{
    BOOST_REQUIRE(context != nullptr);
    auto* render_context = static_cast<swr::impl::render_context*>(context);
    BOOST_REQUIRE_LT(texture_id, render_context->texture_2d_storage.capacity());
    BOOST_REQUIRE(texture_id < render_context->texture_2d_storage.size());
    BOOST_REQUIRE(render_context->texture_2d_storage[texture_id]);
    return render_context->texture_2d_storage[texture_id].get();
}

} /* namespace */

BOOST_FIXTURE_TEST_SUITE(texture_tests, offscreen_context_fixture)

BOOST_AUTO_TEST_CASE(set_image_accepts_zero_sized_images)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto one_texel = make_rgba_data(1);

    swr::SetImage(texture_id, 0, 0, 4, swr::pixel_format::rgba8888, one_texel);
    BOOST_CHECK(swr::GetLastError() == swr::error::none);

    swr::SetImage(texture_id, 0, 4, 0, swr::pixel_format::rgba8888, one_texel);
    BOOST_CHECK(swr::GetLastError() == swr::error::none);

    swr::SetImage(texture_id, 0, 0, 0, swr::pixel_format::rgba8888, one_texel);
    BOOST_CHECK(swr::GetLastError() == swr::error::none);
}

BOOST_AUTO_TEST_CASE(set_sub_image_accepts_zero_sized_regions_as_noop)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto base_image = make_rgba_data(16);
    swr::SetImage(texture_id, 0, 4, 4, swr::pixel_format::rgba8888, base_image);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto one_texel = make_rgba_data(1);

    swr::SetSubImage(texture_id, 0, 1, 1, 0, 2, swr::pixel_format::rgba8888, one_texel);
    BOOST_CHECK(swr::GetLastError() == swr::error::none);

    swr::SetSubImage(texture_id, 0, 1, 1, 2, 0, swr::pixel_format::rgba8888, {});
    BOOST_CHECK(swr::GetLastError() == swr::error::none);

    swr::SetSubImage(texture_id, 0, 1, 1, 0, 0, swr::pixel_format::rgba8888, {});
    BOOST_CHECK(swr::GetLastError() == swr::error::none);
}

BOOST_AUTO_TEST_CASE(mipmapped_texture_storage_is_tightly_packed)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetImage(texture_id, 1, 4, 4, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetImage(texture_id, 2, 2, 2, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetImage(texture_id, 3, 1, 1, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture = get_texture_ptr(context, texture_id);
    auto* color_texture = texture->as_texture_color_2d();
    BOOST_REQUIRE(color_texture != nullptr);

    BOOST_CHECK_EQUAL(color_texture->data.buffer.size(), 64u + 16u + 4u + 1u);
    BOOST_REQUIRE_EQUAL(color_texture->data.data_ptrs.size(), 4u);
    BOOST_REQUIRE_EQUAL(color_texture->data.pitches.size(), 4u);

    BOOST_CHECK_EQUAL(color_texture->data.pitches[0], 8u);
    BOOST_CHECK_EQUAL(color_texture->data.pitches[1], 4u);
    BOOST_CHECK_EQUAL(color_texture->data.pitches[2], 2u);
    BOOST_CHECK_EQUAL(color_texture->data.pitches[3], 1u);

    auto* base_ptr = color_texture->data.buffer.data();
    BOOST_CHECK(color_texture->data.data_ptrs[0] == base_ptr);
    BOOST_CHECK(color_texture->data.data_ptrs[1] == base_ptr + 64);
    BOOST_CHECK(color_texture->data.data_ptrs[2] == base_ptr + 80);
    BOOST_CHECK(color_texture->data.data_ptrs[3] == base_ptr + 84);

    swr::impl::texture_attachment_binding binding;
    binding.attach(texture, 2);
    BOOST_CHECK_EQUAL(binding.info.width, 2);
    BOOST_CHECK_EQUAL(binding.info.height, 2);
    BOOST_CHECK_EQUAL(binding.info.pitch, 2);
    BOOST_CHECK(binding.info.data_ptr == color_texture->data.data_ptrs[2]);
}

BOOST_AUTO_TEST_CASE(texture_v_coordinate_follows_opengl_bottom_to_top_convention)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const std::vector<std::uint8_t> image_data = {
      0xff, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
      0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    swr::SetImage(texture_id, 0, 2, 2, swr::pixel_format::rgba8888, image_data);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture = get_texture_ptr(context, texture_id);
    BOOST_REQUIRE(texture != nullptr);
    texture->set_filter_mag(swr::texture_filter::nearest);
    texture->set_filter_min(swr::texture_filter::nearest);

    const swr::varying uv_bottom_left{
      {0.25f, 0.25f, 0.0f, 0.0f},
      {},
      {}};
    const swr::varying uv_top_left{
      {0.25f, 0.75f, 0.0f, 0.0f},
      {},
      {}};

    const ml::vec4 bottom_left = texture->sampler->sample_at(uv_bottom_left);
    const ml::vec4 top_left = texture->sampler->sample_at(uv_top_left);

    BOOST_CHECK_EQUAL(bottom_left.x, 0.0f);
    BOOST_CHECK_EQUAL(bottom_left.y, 0.0f);
    BOOST_CHECK_EQUAL(bottom_left.z, 1.0f);

    BOOST_CHECK_EQUAL(top_left.x, 1.0f);
    BOOST_CHECK_EQUAL(top_left.y, 0.0f);
    BOOST_CHECK_EQUAL(top_left.z, 0.0f);
}

BOOST_AUTO_TEST_CASE(depth_texture_supports_compare_sampling)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto image_data = make_depth_data({0.75f, 0.50f,
                                             0.25f, 1.00f});
    swr::SetImage(texture_id, 0, 2, 2, swr::pixel_format::depth32f, image_data);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture = get_texture_ptr(context, texture_id);
    BOOST_REQUIRE(texture != nullptr);
    BOOST_REQUIRE(texture->as_texture_depth_2d() != nullptr);
    texture->set_filter_mag(swr::texture_filter::nearest);
    texture->set_filter_min(swr::texture_filter::nearest);

    BOOST_CHECK(swr::GetTextureCompareMode(texture_id) == swr::texture_compare_mode::none);
    BOOST_CHECK(swr::GetTextureCompareFunc(texture_id) == swr::comparison_func::less_equal);

    const swr::varying uv_bottom_left{
      {0.25f, 0.25f, 0.0f, 0.0f},
      {},
      {}};
    const ml::vec4 bottom_left = texture->sampler->sample_at(uv_bottom_left);
    BOOST_CHECK_CLOSE(bottom_left.x, 0.25f, 1e-3f);
    BOOST_CHECK_CLOSE(bottom_left.y, 0.25f, 1e-3f);
    BOOST_CHECK_CLOSE(bottom_left.z, 0.25f, 1e-3f);
    BOOST_CHECK_CLOSE(bottom_left.w, 1.0f, 1e-3f);

    const swr::varying compare_mode_off{
      {0.25f, 0.25f, 0.90f, 0.0f},
      {},
      {}};
    BOOST_CHECK_CLOSE(texture->sampler->sample_compare_at(compare_mode_off), 0.25f, 1e-3f);

    swr::SetTextureCompareMode(texture_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetTextureCompareFunc(texture_id, swr::comparison_func::less_equal);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    BOOST_CHECK(swr::GetTextureCompareMode(texture_id) == swr::texture_compare_mode::ref_to_texture);
    BOOST_CHECK(swr::GetTextureCompareFunc(texture_id) == swr::comparison_func::less_equal);

    const swr::varying compare_pass{
      {0.25f, 0.25f, 0.20f, 0.0f},
      {},
      {}};
    const swr::varying compare_fail{
      {0.25f, 0.25f, 0.30f, 0.0f},
      {},
      {}};

    BOOST_CHECK_EQUAL(texture->sampler->sample_compare_at(compare_pass), 1.0f);
    BOOST_CHECK_EQUAL(texture->sampler->sample_compare_at(compare_fail), 0.0f);
}

BOOST_AUTO_TEST_CASE(format_switch_preserves_sampler_identity_and_updates_sampling_behavior)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto color_data = make_rgba_data(4);
    swr::SetImage(texture_id, 0, 2, 2, swr::pixel_format::rgba8888, color_data);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture = get_texture_ptr(context, texture_id);
    BOOST_REQUIRE(texture != nullptr);

    auto* sampler_before = texture->sampler.get();
    const auto* shadow_sampler_before = sampler_before->as_sampler_shadow_2d();
    BOOST_REQUIRE(shadow_sampler_before != nullptr);

    swr::SetTextureCompareMode(texture_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_CHECK(swr::GetLastError() == swr::error::invalid_operation);

    swr::SetImage(
      texture_id,
      0,
      2,
      2,
      swr::pixel_format::depth32f,
      make_depth_data({0.75f, 0.50f,
                       0.25f, 1.00f}));
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    texture = get_texture_ptr(context, texture_id);
    BOOST_REQUIRE(texture != nullptr);
    BOOST_REQUIRE(texture->as_texture_depth_2d() != nullptr);
    BOOST_CHECK_EQUAL(texture->sampler.get(), sampler_before);

    swr::SetTextureCompareMode(texture_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetTextureCompareFunc(texture_id, swr::comparison_func::less_equal);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const swr::varying compare_pass{
      {0.25f, 0.25f, 0.20f, 0.0f},
      {},
      {}};
    const swr::varying compare_fail{
      {0.25f, 0.25f, 0.30f, 0.0f},
      {},
      {}};

    BOOST_CHECK_EQUAL(shadow_sampler_before->sample_compare_at(compare_pass), 1.0f);
    BOOST_CHECK_EQUAL(shadow_sampler_before->sample_compare_at(compare_fail), 0.0f);
}

BOOST_AUTO_TEST_CASE(depth_texture_linear_compare_sampling_filters_comparison_results)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto image_data = make_depth_data({0.10f, 0.20f,
                                             0.80f, 0.90f});
    swr::SetImage(texture_id, 0, 2, 2, swr::pixel_format::depth32f, image_data);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetTextureCompareMode(texture_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetTextureCompareFunc(texture_id, swr::comparison_func::less_equal);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture = get_texture_ptr(context, texture_id);
    BOOST_REQUIRE(texture != nullptr);
    texture->set_filter_mag(swr::texture_filter::linear);
    texture->set_filter_min(swr::texture_filter::linear);

    const swr::varying uv_center{
      {0.5f, 0.5f, 0.5f, 0.0f},
      {},
      {}};

    BOOST_CHECK_CLOSE(texture->sampler->sample_compare_at(uv_center), 0.5f, 1e-3f);
}

BOOST_AUTO_TEST_CASE(depth_texture_nearest_compare_sampling_stays_binary_per_tap)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto image_data = make_depth_data({0.10f, 0.10f, 0.90f, 0.90f,
                                             0.10f, 0.10f, 0.90f, 0.90f,
                                             0.10f, 0.10f, 0.90f, 0.90f,
                                             0.10f, 0.10f, 0.90f, 0.90f});
    swr::SetImage(texture_id, 0, 4, 4, swr::pixel_format::depth32f, image_data);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetTextureCompareMode(texture_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetTextureCompareFunc(texture_id, swr::comparison_func::less_equal);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture = get_texture_ptr(context, texture_id);
    BOOST_REQUIRE(texture != nullptr);
    texture->set_filter_mag(swr::texture_filter::nearest);
    texture->set_filter_min(swr::texture_filter::nearest);

    const auto* shadow_sampler = texture->sampler->as_sampler_shadow_2d();
    BOOST_REQUIRE(shadow_sampler != nullptr);

    const float reference = 0.50f;
    const float left = swr::textureOffset(
      *shadow_sampler,
      ml::vec3{0.375f, 0.375f, reference},
      {-1, 0});
    const float center = swr::texture(
      *shadow_sampler,
      ml::vec3{0.375f, 0.375f, reference});
    const float right = swr::textureOffset(
      *shadow_sampler,
      ml::vec3{0.375f, 0.375f, reference},
      {1, 0});

    BOOST_CHECK_EQUAL(left, 0.0f);
    BOOST_CHECK_EQUAL(center, 0.0f);
    BOOST_CHECK_EQUAL(right, 1.0f);
}

BOOST_AUTO_TEST_CASE(depth_compare_mipmap_selection_ignores_reference_gradients)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(
      texture_id,
      0,
      4,
      4,
      swr::pixel_format::depth32f,
      make_depth_data(std::vector<float>(16, 0.90f)));
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetImage(
      texture_id,
      1,
      2,
      2,
      swr::pixel_format::depth32f,
      make_depth_data(std::vector<float>(4, 0.10f)));
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetTextureCompareMode(texture_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetTextureCompareFunc(texture_id, swr::comparison_func::less_equal);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture = get_texture_ptr(context, texture_id);
    BOOST_REQUIRE(texture != nullptr);
    texture->set_filter_mag(swr::texture_filter::nearest);
    texture->set_filter_min(swr::texture_filter::nearest_mipmap_nearest);

    const swr::varying compare_sample{
      {0.5f, 0.5f, 0.5f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
      {},
    };

    BOOST_CHECK_EQUAL(texture->sampler->sample_compare_at(compare_sample), 1.0f);
}

BOOST_AUTO_TEST_CASE(shadow_texture_helper_can_preserve_uv_gradients_with_separate_reference)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(
      texture_id,
      0,
      4,
      4,
      swr::pixel_format::depth32f,
      make_depth_data(std::vector<float>(16, 0.90f)));
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetImage(
      texture_id,
      1,
      2,
      2,
      swr::pixel_format::depth32f,
      make_depth_data(std::vector<float>(4, 0.10f)));
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetTextureCompareMode(texture_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetTextureCompareFunc(texture_id, swr::comparison_func::less_equal);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture = get_texture_ptr(context, texture_id);
    BOOST_REQUIRE(texture != nullptr);
    texture->set_filter_mag(swr::texture_filter::nearest);
    texture->set_filter_min(swr::texture_filter::nearest_mipmap_nearest);

    const auto shadow_sampler = texture->sampler->as_sampler_shadow_2d();
    BOOST_REQUIRE(shadow_sampler != nullptr);

    const swr::varying shadow_uv{
      {0.5f, 0.5f, 99.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
      {},
    };

    BOOST_CHECK_EQUAL(swr::texture(*shadow_sampler, shadow_uv, 0.5f), 1.0f);
}

BOOST_AUTO_TEST_CASE(projected_shadow_texture_helper_preserves_projected_gradients)
{
    const auto texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(
      texture_id,
      0,
      4,
      4,
      swr::pixel_format::depth32f,
      make_depth_data(std::vector<float>(16, 0.90f)));
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetImage(
      texture_id,
      1,
      2,
      2,
      swr::pixel_format::depth32f,
      make_depth_data(std::vector<float>(4, 0.10f)));
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetTextureCompareMode(texture_id, swr::texture_compare_mode::ref_to_texture);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetTextureCompareFunc(texture_id, swr::comparison_func::less_equal);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* texture = get_texture_ptr(context, texture_id);
    BOOST_REQUIRE(texture != nullptr);
    texture->set_filter_mag(swr::texture_filter::nearest);
    texture->set_filter_min(swr::texture_filter::nearest_mipmap_nearest);

    const auto* shadow_sampler = texture->sampler->as_sampler_shadow_2d();
    BOOST_REQUIRE(shadow_sampler != nullptr);

    const swr::varying projected_shadow_coords{
      {1.0f, 1.0f, 1.0f, 2.0f},
      {1.0f, 0.0f, 0.0f, 0.0f},
      {},
    };

    BOOST_CHECK_EQUAL(swr::textureProj(*shadow_sampler, projected_shadow_coords), 0.0f);
}

BOOST_AUTO_TEST_CASE(depth_texture_can_be_used_as_framebuffer_depth_attachment)
{
    const auto color_texture_id = swr::CreateTexture();
    const auto depth_texture_id = swr::CreateTexture();
    BOOST_REQUIRE(color_texture_id != 0);
    BOOST_REQUIRE(depth_texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(color_texture_id, 0, 4, 4, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::SetImage(depth_texture_id, 0, 4, 4, swr::pixel_format::depth32f, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    const auto fbo_id = swr::CreateFramebufferObject();
    BOOST_REQUIRE(fbo_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::FramebufferTexture(
      fbo_id,
      swr::framebuffer_attachment::color_attachment_0,
      color_texture_id,
      0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);
    swr::FramebufferTexture(
      fbo_id,
      swr::framebuffer_attachment::depth_attachment,
      depth_texture_id,
      0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::BindFramebufferObject(swr::framebuffer_target::draw, fbo_id);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetClearDepth(0.25f);
    swr::ClearDepthBuffer();
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    auto* depth_texture = get_texture_ptr(context, depth_texture_id);
    BOOST_REQUIRE(depth_texture != nullptr);
    BOOST_REQUIRE(depth_texture->as_texture_depth_2d() != nullptr);

    const swr::varying uv{
      {0.25f, 0.25f, 0.0f, 0.0f},
      {},
      {}};
    BOOST_CHECK_CLOSE(depth_texture->sampler->sample_depth_at(uv), 0.25f, 1e-3f);
}

BOOST_AUTO_TEST_SUITE_END()
