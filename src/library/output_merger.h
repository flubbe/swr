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

namespace swr
{

namespace output_merger
{

/** apply blending on pixels. */
uint32_t blend(const pixel_format_converter& pf_conv, blend_func blend_src, blend_func blend_dst, const uint32_t dest, const uint32_t src);

/** apply blending on colors. */
ml::vec4 blend(blend_func blend_src, blend_func blend_dst, const ml::vec4& dest, const ml::vec4& src);

} /* namespace output_merger */

} /* namespace swr */
