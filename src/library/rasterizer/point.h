/**
 * swr - a software rasterizer
 *
 * Implements Direct3D point rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "../swr_internal.h"

namespace rast
{

/*
 * Helpers.
 */

using point_fixed_t = ml::fixed_28_4_t;
using point_fixed_vec2 = ml::vec2_fixed<
  ml::static_number_traits<point_fixed_t>::fractional_bits>;

template<typename F>
inline void for_each_covered_point_pixel(
  point_fixed_vec2 point_coords,
  int width,
  int height,
  F&& f)
{
    /*
     * A point is rastered as two triangles in a Z pattern, and triangle fill rules are applied.
     * It is sufficient to get the nearest pixel center and check whether that pixel is selected
     * by the point fill-rule bias.
     */

    const auto bias = cnl::wrap<point_fixed_t>(FILL_RULE_EDGE_BIAS);

    const auto x = static_cast<int>(
      cnl::floor(
        point_coords.x - bias));
    const auto y = static_cast<int>(
      cnl::floor(
        point_coords.y - bias));

    if(x >= 0 && x < width
       && y >= 0 && y < height)
    {
        f(x, y);
    }
}

}    // namespace rast