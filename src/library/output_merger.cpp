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
#include "output_merger.h"

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

/*
 * A fast approximation to alpha blending.
 */
uint32_t approx_srcalpha_oneminussrcalpha(const uint32_t Dest, const uint32_t Src)
{
    // extract alpha value
    uint32_t a = Src >> 24;

    // if source pixel is transparent, just return the destination pixel.
    if(a == 0)
    {
        return Dest;
    }
    // if source pixel is opaque, return source.
    if(a == 0xff)
    {
        return Src;
    }

    // alpha blending the source and the background colors
    uint32_t RedBlue = (((Src & 0x00ff00ff) * a) + ((Dest & 0x00ff00ff) * (0xff - a))) & 0xff00ff00;
    uint32_t Green = (((Src & 0x0000ff00) * a) + ((Dest & 0x0000ff00) * (0xff - a))) & 0x00ff0000;
    uint32_t Alpha = ((((Src & 0xff000000) >> 8) * a + ((Dest & 0xff000000) >> 8) * (0xff - a)) & 0x00ff0000) << 8;

    return Alpha | ((RedBlue | Green) >> 8);
}

uint32_t approx_zero_dstsrccolor(const uint32_t Dest, const uint32_t Src)
{
    // multiply source and destination in components.
    uint32_t C1 = (((Src & 0x000000ff) * (Dest & 0x000000ff)) >> 8) & 0x000000ff;
    uint32_t C2 = (((Src & 0x0000ff00) >> 8) * ((Dest & 0x0000ff00) >> 8)) & 0x0000ff00;
    uint32_t C3 = (((Src & 0x00ff0000) >> 16) * ((Dest & 0x00ff0000) >> 16) << 8) & 0x00ff0000;
    uint32_t C4 = (((Src & 0xff000000) >> 24) * ((Dest & 0xff000000) >> 24) << 16) & 0xff000000;

    return C1 | C2 | C3 | C4;
}

} /* namespace argb8888 */

/*
 * blending.
 */

uint32_t blend(const pixel_format_converter& pf_conv, const impl::render_states& states, const uint32_t dest, const uint32_t src)
{
    // first check for blending modes that do not depend on the pixel format.
    if(states.blend_src == blend_func::one
       && states.blend_dst == blend_func::zero)
    {
        return dest;
    }
    else if(states.blend_src == blend_func::zero
            && states.blend_dst == blend_func::one)
    {
        return src;
    }
    else if(states.blend_src == blend_func::zero
            && states.blend_dst == blend_func::zero)
    {
        return 0;
    }

    // depending on the pixel format, perform the blending operation.
    if(pf_conv.get_name() == pixel_format::argb8888)
    {
        if(states.blend_src == blend_func::src_alpha
           && states.blend_dst == blend_func::one_minus_src_alpha)
        {
            return argb8888::approx_srcalpha_oneminussrcalpha(dest, src);
        }
        else if(states.blend_src == blend_func::zero
                && states.blend_dst == blend_func::src_color)
        {
            return argb8888::approx_zero_dstsrccolor(dest, src);
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

} /* namespace output_merger */

} /* namespace swr */
