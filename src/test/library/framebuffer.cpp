/**
 * swr - a software rasterizer
 *
 * framebuffer object completeness tests.
 */

#include <cstdint>

/* boost test framework. */
#define BOOST_TEST_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_MODULE framebuffer tests
#include <boost/test/unit_test.hpp>

/* user headers. */
#include "swr_internal.h"

namespace
{

struct offscreen_context_fixture
{
    swr::context_handle context{nullptr};

    offscreen_context_fixture()
    {
        context = swr::CreateOffscreenContext(8, 8, 1);
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

}    // namespace

BOOST_FIXTURE_TEST_SUITE(framebuffer_tests, offscreen_context_fixture)

BOOST_AUTO_TEST_CASE(framebuffer_object_without_attachments_is_incomplete)
{
    swr::impl::framebuffer_object fbo;
    BOOST_CHECK(!fbo.is_complete());
}

BOOST_AUTO_TEST_CASE(framebuffer_object_with_valid_color_attachment_is_complete)
{
    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::impl::framebuffer_object fbo;
    fbo.attach_texture(
      swr::framebuffer_attachment::color_attachment_0,
      get_texture_ptr(context, texture_id),
      0);

    BOOST_CHECK(fbo.is_complete());
}

BOOST_AUTO_TEST_CASE(framebuffer_object_with_invalid_color_attachment_is_incomplete)
{
    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::impl::framebuffer_object fbo;
    fbo.attach_texture(
      swr::framebuffer_attachment::color_attachment_0,
      get_texture_ptr(context, texture_id),
      99);    // invalid mip level

    BOOST_CHECK(!fbo.is_complete());
}

BOOST_AUTO_TEST_CASE(detaching_last_color_attachment_makes_framebuffer_incomplete)
{
    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::impl::framebuffer_object fbo;
    fbo.attach_texture(
      swr::framebuffer_attachment::color_attachment_0,
      get_texture_ptr(context, texture_id),
      0);
    BOOST_REQUIRE(fbo.is_complete());

    fbo.detach_texture(swr::framebuffer_attachment::color_attachment_0);
    BOOST_CHECK(!fbo.is_complete());
}

BOOST_AUTO_TEST_CASE(framebuffer_object_with_valid_color_and_depth_is_complete)
{
    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::impl::attachment_depth depth;
    depth.allocate(8, 8);

    swr::impl::framebuffer_object fbo;
    fbo.attach_texture(
      swr::framebuffer_attachment::color_attachment_0,
      get_texture_ptr(context, texture_id),
      0);
    fbo.attach_depth(&depth);

    BOOST_CHECK(fbo.is_complete());
}

BOOST_AUTO_TEST_CASE(framebuffer_object_with_valid_color_and_invalid_depth_is_incomplete)
{
    const std::uint32_t texture_id = swr::CreateTexture();
    BOOST_REQUIRE(texture_id != 0);
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::SetImage(texture_id, 0, 8, 8, swr::pixel_format::rgba8888, {});
    BOOST_REQUIRE(swr::GetLastError() == swr::error::none);

    swr::impl::attachment_depth invalid_depth;

    swr::impl::framebuffer_object fbo;
    fbo.attach_texture(
      swr::framebuffer_attachment::color_attachment_0,
      get_texture_ptr(context, texture_id),
      0);
    fbo.attach_depth(&invalid_depth);

    BOOST_CHECK(!fbo.is_complete());
}

BOOST_AUTO_TEST_SUITE_END()
