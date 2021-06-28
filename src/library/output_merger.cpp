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
static uint32_t approx_srcalpha_oneminussrcalpha(const uint32_t dest, const uint32_t src)
{
    // extract alpha value
    uint32_t a = src >> 24;

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
    uint32_t rb = (((src & 0x00ff00ff) * a) + ((dest & 0x00ff00ff) * (0xff - a))) & 0xff00ff00;
    uint32_t g = (((src & 0x0000ff00) * a) + ((dest & 0x0000ff00) * (0xff - a))) & 0x00ff0000;
    a = ((((src & 0xff000000) >> 8) * a + ((dest & 0xff000000) >> 8) * (0xff - a)) & 0x00ff0000) << 8;

    return a | ((rb | g) >> 8);
}

} /* namespace argb8888 */

namespace xxxx8888
{

/** multiply source and destination in components. */
static uint32_t approx_zero_dstsrccolor(const uint32_t dest, const uint32_t src)
{
    uint32_t c1 = (((src & 0x000000ff) * (dest & 0x000000ff)) >> 8) & 0x000000ff;
    uint32_t c2 = (((src & 0x0000ff00) >> 8) * ((dest & 0x0000ff00) >> 8)) & 0x0000ff00;
    uint32_t c3 = (((src & 0x00ff0000) >> 16) * ((dest & 0x00ff0000) >> 16) << 8) & 0x00ff0000;
    uint32_t c4 = (((src & 0xff000000) >> 24) * ((dest & 0xff000000) >> 24) << 16) & 0xff000000;

    return c1 | c2 | c3 | c4;
}

} /* namespace xxxx8888 */

/*
 * blending.
 */

uint32_t blend(const pixel_format_converter& pf_conv, blend_func blend_src, blend_func blend_dst, const uint32_t dest, const uint32_t src)
{
    // first check for blending modes that do not depend on the pixel format.
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
        return 0;
    }

    // depending on the pixel format, perform the blending operation.
    if(pf_conv.get_name() == pixel_format::argb8888)
    {
        if(blend_src == blend_func::src_alpha
           && blend_dst == blend_func::one_minus_src_alpha)
        {
            return argb8888::approx_srcalpha_oneminussrcalpha(dest, src);
        }
        else if(blend_src == blend_func::zero
                && blend_dst == blend_func::src_color)
        {
            return xxxx8888::approx_zero_dstsrccolor(dest, src);
        }
        else
        {
            //!!todo: unimplemented.
            impl::global_context->last_error = error::unimplemented;
        }
    }
    else
    {
        //!!todo: unimplemented.
        impl::global_context->last_error = error::unimplemented;
    }

    return src;
}

ml::vec4 blend(blend_func blend_src, blend_func blend_dst, const ml::vec4& dest, const ml::vec4& src)
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
        //!!todo: unimplemented.
        impl::global_context->last_error = error::unimplemented;
    }

    return src;
}

} /* namespace output_merger */

} /* namespace swr */
