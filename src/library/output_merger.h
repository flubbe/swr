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

/** apply blending. */
uint32_t blend(const pixel_format_converter& pf_conv, const swr::impl::render_states& states, const uint32_t dest, const uint32_t src);

} /* namespace output_merger */

} /* namespace swr */
