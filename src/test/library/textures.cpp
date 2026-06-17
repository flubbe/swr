/**
 * swr - a software rasterizer
 *
 * test texture management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <cstdint>
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

BOOST_AUTO_TEST_SUITE_END()
