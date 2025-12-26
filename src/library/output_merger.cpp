/**
 * swr - a software rasterizer
 *
 * rasterizer output merging (currently only blending for the default framebuffer).
 * the functions here operate in the pixel format of the output buffer.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{

namespace output_merger
{

namespace argb8888
{

/*
 * Fast alpha blending functions.
 *
 * Note: This is a approximation to accurate alpha blending, since the fast blending functions divide by 256 instead of 255
 */

/** A fast approximation to alpha blending. */
static std::uint32_t approx_srcalpha_oneminussrcalpha(const std::uint32_t src, const std::uint32_t dest)
{
    // extract alpha value
    std::uint32_t a = src >> 24;

    // if source pixel is transparent, just return the destination pixel.
    if(a == 0)
    {
        return dest;
    }
    // if source pixel is opaque, return source.
    if(a == 0xff)
    {
        return src;
    }

    // alpha blend the source and the background colors
    std::uint32_t rb = (((src & 0x00ff00ff) * a) + ((dest & 0x00ff00ff) * (0xff - a))) & 0xff00ff00;
    std::uint32_t g = (((src & 0x0000ff00) * a) + ((dest & 0x0000ff00) * (0xff - a))) & 0x00ff0000;
    a = ((((src & 0xff000000) >> 8) * a + ((dest & 0xff000000) >> 8) * (0xff - a)) & 0x00ff0000) << 8;

    return a | ((rb | g) >> 8);
}

} /* namespace argb8888 */

namespace xxxx8888
{

/** multiply source and destination in components. */
static std::uint32_t approx_zero_dstsrccolor(const std::uint32_t src, const std::uint32_t dest)
{
    std::uint32_t c1 = (((src & 0x000000ff) * (dest & 0x000000ff)) >> 8) & 0x000000ff;
    std::uint32_t c2 = (((src & 0x0000ff00) >> 8) * ((dest & 0x0000ff00) >> 8)) & 0x0000ff00;
    std::uint32_t c3 = (((src & 0x00ff0000) >> 16) * ((dest & 0x00ff0000) >> 16) << 8) & 0x00ff0000;
    std::uint32_t c4 = (((src & 0xff000000) >> 24) * ((dest & 0xff000000) >> 24) << 16) & 0xff000000;

    return c1 | c2 | c3 | c4;
}

} /* namespace xxxx8888 */

/*
 * blending.
 */

std::uint32_t blend(const pixel_format_converter& pf_conv, blend_func blend_src, blend_func blend_dst, const std::uint32_t src, const std::uint32_t dest)
{
    // first check for blending modes that do not depend on the pixel format.
    if(blend_src == blend_func::one
       && blend_dst == blend_func::zero)
    {
        return src;
    }
    else if(blend_src == blend_func::zero
            && blend_dst == blend_func::one)
    {
        return dest;
    }
    else if(blend_src == blend_func::zero
            && blend_dst == blend_func::zero)
    {
        return 0;
    }

    // depending on the pixel format, perform the blending operation.
    if(pf_conv.get_name() == pixel_format::argb8888)
    {
        if(blend_src == blend_func::src_alpha
           && blend_dst == blend_func::one_minus_src_alpha)
        {
            return argb8888::approx_srcalpha_oneminussrcalpha(src, dest);
        }
        else if(blend_src == blend_func::zero
                && blend_dst == blend_func::src_color)
        {
            return xxxx8888::approx_zero_dstsrccolor(src, dest);
        }
        else
        {
            // TODO unimplemented.
            impl::global_context->last_error = error::unimplemented;
        }
    }
    else
    {
        // TODO unimplemented.
        impl::global_context->last_error = error::unimplemented;
    }

    return src;
}

void blend_block(const pixel_format_converter& pf_conv, blend_func blend_src, blend_func blend_dst, const std::uint32_t src[4], const std::uint32_t dest[4], std::uint32_t out[4])
{
    // first check for blending modes that do not depend on the pixel format.
    if(blend_src == blend_func::one
       && blend_dst == blend_func::zero)
    {
        out[0] = src[0];
        out[1] = src[1];
        out[2] = src[2];
        out[3] = src[3];
        return;
    }
    else if(blend_src == blend_func::zero
            && blend_dst == blend_func::one)
    {
        out[0] = dest[0];
        out[1] = dest[1];
        out[2] = dest[2];
        out[3] = dest[3];
        return;
    }
    else if(blend_src == blend_func::zero
            && blend_dst == blend_func::zero)
    {
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
        out[3] = 0;
        return;
    }

    // depending on the pixel format, perform the blending operation.
    if(pf_conv.get_name() == pixel_format::argb8888)
    {
        if(blend_src == blend_func::src_alpha
           && blend_dst == blend_func::one_minus_src_alpha)
        {
            out[0] = argb8888::approx_srcalpha_oneminussrcalpha(src[0], dest[0]);
            out[1] = argb8888::approx_srcalpha_oneminussrcalpha(src[1], dest[1]);
            out[2] = argb8888::approx_srcalpha_oneminussrcalpha(src[2], dest[2]);
            out[3] = argb8888::approx_srcalpha_oneminussrcalpha(src[3], dest[3]);
            return;
        }
        else if(blend_src == blend_func::zero
                && blend_dst == blend_func::src_color)
        {
            out[0] = xxxx8888::approx_zero_dstsrccolor(src[0], dest[0]);
            out[1] = xxxx8888::approx_zero_dstsrccolor(src[1], dest[1]);
            out[2] = xxxx8888::approx_zero_dstsrccolor(src[2], dest[2]);
            out[3] = xxxx8888::approx_zero_dstsrccolor(src[3], dest[3]);
            return;
        }
        else
        {
            // TODO unimplemented.
            impl::global_context->last_error = error::unimplemented;
        }
    }
    else
    {
        // TODO unimplemented.
        impl::global_context->last_error = error::unimplemented;
    }

    // return src as default.
    out[0] = src[0];
    out[1] = src[1];
    out[2] = src[2];
    out[3] = src[3];
}

ml::vec4 blend(blend_func blend_src, blend_func blend_dst, const ml::vec4& src, const ml::vec4& dest)
{
    if(blend_src == blend_func::one
       && blend_dst == blend_func::zero)
    {
        return dest;
    }
    else if(blend_src == blend_func::zero
            && blend_dst == blend_func::one)
    {
        return src;
    }
    else if(blend_src == blend_func::zero
            && blend_dst == blend_func::zero)
    {
        return ml::vec4::zero();
    }
    else if(blend_src == blend_func::src_alpha
            && blend_dst == blend_func::one_minus_src_alpha)
    {
        return ml::lerp(src.a, dest, src);
    }
    else if(blend_src == blend_func::zero
            && blend_dst == blend_func::src_color)
    {
        return src * dest;
    }
    else
    {
        // TODO unimplemented.
        impl::global_context->last_error = error::unimplemented;
    }

    return src;
}

void blend_block(blend_func blend_src, blend_func blend_dst, const ml::vec4 src[4], const ml::vec4 dest[4], ml::vec4 out[4])
{
    auto copy_to_out = [&out](const ml::vec4 from[4])
    {
        out[0] = from[0];
        out[1] = from[1];
        out[2] = from[2];
        out[3] = from[3];
    };

    if(blend_src == blend_func::one
       && blend_dst == blend_func::zero)
    {
        copy_to_out(dest);
    }
    else if(blend_src == blend_func::zero
            && blend_dst == blend_func::one)
    {
        copy_to_out(src);
    }
    else if(blend_src == blend_func::zero
            && blend_dst == blend_func::zero)
    {
        out[0] = ml::vec4::zero();
        out[1] = ml::vec4::zero();
        out[2] = ml::vec4::zero();
        out[3] = ml::vec4::zero();
    }
    else if(blend_src == blend_func::src_alpha
            && blend_dst == blend_func::one_minus_src_alpha)
    {
        out[0] = ml::lerp(src[0].a, dest[0], src[0]);
        out[1] = ml::lerp(src[1].a, dest[1], src[1]);
        out[2] = ml::lerp(src[2].a, dest[2], src[2]);
        out[3] = ml::lerp(src[3].a, dest[3], src[3]);
    }
    else if(blend_src == blend_func::zero
            && blend_dst == blend_func::src_color)
    {
        out[0] = src[0] * dest[0];
        out[1] = src[1] * dest[1];
        out[2] = src[2] * dest[2];
        out[3] = src[3] * dest[3];
    }
    else
    {
        // TODO unimplemented.
        impl::global_context->last_error = error::unimplemented;

        copy_to_out(src);
    }
}

} /* namespace output_merger */

} /* namespace swr */
