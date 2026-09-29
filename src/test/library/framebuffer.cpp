/**
 * swr - a software rasterizer
 *
 * framebuffer object completeness tests.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <cstdint>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE framebuffer tests
#include <boost/test/unit_test.hpp>

/* user headers. */
#include "swr_internal.h"

namespace swr
{

inline std::ostream& operator<<(
  std::ostream& os,
  error e)
{
    switch(e)
    {
    case error::none: return os << "error::none";
    case error::invalid_value: return os << "error::invalid_value";
    case error::invalid_operation: return os << "error::invalid_operation";
    case error::unimplemented: return os << "error::unimplemented";
    }

    return os << "error::<" << static_cast<int>(e) << ">";
}

}    // namespace swr

namespace
{

struct offscreen_context_fixture
{
    swr::context_handle context{nullptr};

    offscreen_context_fixture()
    {
        context = swr::CreateOffscreenContext(8, 8, 1);
        BOOST_REQUIRE_NE(context, nullptr);
        BOOST_REQUIRE(swr::MakeContextCurrent(context));
        BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);
    }

    ~offscreen_context_fixture()
    {
        swr::MakeContextCurrent(nullptr);
        swr::DestroyContext(context);
    }
};

}    // namespace

BOOST_FIXTURE_TEST_SUITE(framebuffer_tests, offscreen_context_fixture)

BOOST_AUTO_TEST_CASE(framebuffer_object_without_attachments_is_incomplete)
{
    std::uint32_t fbo = swr::CreateFramebufferObject();
    BOOST_CHECK(!swr::impl::global_context->is_framebuffer_complete(fbo));
}

BOOST_AUTO_TEST_CASE(framebuffer_object_with_valid_color_attachment_is_complete)
{
    const std::uint32_t fbo = swr::CreateFramebufferObject();
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE_NE(texture_id, 0);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::FramebufferTexture(
      fbo,
      swr::framebuffer_attachment::color_attachment_0,
      texture_id,
      0);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    BOOST_CHECK(swr::impl::global_context->is_framebuffer_complete(fbo));
}

BOOST_AUTO_TEST_CASE(framebuffer_object_with_invalid_color_attachment_is_incomplete)
{
    const std::uint32_t fbo = swr::CreateFramebufferObject();
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE_NE(texture_id, 0);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::FramebufferTexture(
      fbo,
      swr::framebuffer_attachment::color_attachment_0,
      texture_id,
      99);    // invalid mip level

    // FIXME Should set error value.
    // BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::invalid_value);

    BOOST_CHECK(!swr::impl::global_context->is_framebuffer_complete(fbo));
}

BOOST_AUTO_TEST_CASE(detaching_last_color_attachment_makes_framebuffer_incomplete)
{
    const std::uint32_t fbo = swr::CreateFramebufferObject();
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE_NE(texture_id, 0);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::FramebufferTexture(
      fbo,
      swr::framebuffer_attachment::color_attachment_0,
      texture_id,
      0);

    BOOST_REQUIRE(swr::impl::global_context->is_framebuffer_complete(fbo));

    swr::FramebufferTexture(
      fbo,
      swr::framebuffer_attachment::color_attachment_0,
      0,
      0);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    BOOST_REQUIRE(!swr::impl::global_context->is_framebuffer_complete(fbo));
}

BOOST_AUTO_TEST_CASE(framebuffer_object_with_valid_color_and_depth_is_complete)
{
    const std::uint32_t fbo = swr::CreateFramebufferObject();
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE_NE(texture_id, 0);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    const std::uint32_t depth_id = swr::CreateDepthRenderbuffer(8, 8);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::FramebufferTexture(
      fbo,
      swr::framebuffer_attachment::color_attachment_0,
      texture_id,
      0);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::FramebufferRenderbuffer(
      fbo,
      swr::framebuffer_attachment::depth_attachment,
      depth_id);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    BOOST_REQUIRE(swr::impl::global_context->is_framebuffer_complete(fbo));

    swr::ReleaseDepthRenderbuffer(depth_id);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);
}

BOOST_AUTO_TEST_CASE(framebuffer_object_with_valid_color_and_invalid_depth_is_incomplete)
{
    const std::uint32_t fbo = swr::CreateFramebufferObject();
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE_NE(texture_id, 0);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    const std::uint32_t depth_id = swr::CreateDepthRenderbuffer(8, 8);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::FramebufferTexture(
      fbo,
      swr::framebuffer_attachment::color_attachment_0,
      texture_id,
      0);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::FramebufferRenderbuffer(
      fbo,
      swr::framebuffer_attachment::depth_attachment,
      std::numeric_limits<std::uint32_t>::max());
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::invalid_value);

    // still complete, since the attachment failed and didn't change the FBO.
    BOOST_CHECK(swr::impl::global_context->is_framebuffer_complete(fbo));

    swr::ReleaseDepthRenderbuffer(depth_id);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);
}

BOOST_AUTO_TEST_CASE(framebuffer_object_with_valid_depth_attachment_is_complete)
{
    const std::uint32_t fbo = swr::CreateFramebufferObject();
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    const std::uint32_t depth_id = swr::CreateDepthRenderbuffer(8, 8);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::FramebufferRenderbuffer(
      fbo,
      swr::framebuffer_attachment::depth_attachment,
      depth_id);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    BOOST_CHECK(swr::impl::global_context->is_framebuffer_complete(fbo));

    swr::ReleaseDepthRenderbuffer(depth_id);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);
}

BOOST_AUTO_TEST_CASE(released_depth_renderbuffer_makes_framebuffer_incomplete)
{
    const std::uint32_t fbo = swr::CreateFramebufferObject();
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    const std::uint32_t depth_id = swr::CreateDepthRenderbuffer(8, 8);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    swr::FramebufferRenderbuffer(
      fbo,
      swr::framebuffer_attachment::depth_attachment,
      depth_id);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    BOOST_CHECK(swr::impl::global_context->is_framebuffer_complete(fbo));

    swr::ReleaseDepthRenderbuffer(depth_id);
    BOOST_REQUIRE_EQUAL(swr::GetLastError(), swr::error::none);

    BOOST_CHECK(!swr::impl::global_context->is_framebuffer_complete(fbo));
}

BOOST_AUTO_TEST_SUITE_END()
