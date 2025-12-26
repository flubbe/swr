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
std::uint32_t blend(const pixel_format_converter& pf_conv, blend_func blend_src, blend_func blend_dst, const std::uint32_t src, const std::uint32_t dest);

/** apply blending on a 2x2 block of pixels. when compiling with SIMD/SSE enabled, assumes that src, dest and out are aligned on 16-byte boundaries. */
void blend_block(const pixel_format_converter& pf_conv, blend_func blend_src, blend_func blend_dst, const std::uint32_t src[4], const std::uint32_t dest[4], std::uint32_t out[4]);

/** apply blending on colors. */
ml::vec4 blend(blend_func blend_src, blend_func blend_dst, const ml::vec4& src, const ml::vec4& dest);

/** apply blending on a 2x2 block of colors. */
void blend_block(blend_func blend_src, blend_func blend_dst, const ml::vec4 src[4], const ml::vec4 dest[4], ml::vec4 out[4]);

} /* namespace output_merger */

} /* namespace swr */
