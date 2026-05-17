/**
 * swr - a software rasterizer
 *
 * test texture management.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <cstdint>
#include <vector>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE texture tests
#include <boost/test/unit_test.hpp>

#include "swr/swr.h"

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

BOOST_AUTO_TEST_SUITE_END()
