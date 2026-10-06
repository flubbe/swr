/**
 * swr - a software rasterizer
 *
 * Implements Direct3D point rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <concepts>

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
    requires std::invocable<
      F&,
      std::uint32_t,
      std::uint32_t>
inline void for_each_covered_point_pixel(
  point_fixed_vec2 point_coords,
  std::size_t width,
  std::size_t height,
  F&& f)
{
    /*
     * A point is rastered as two triangles in a Z pattern, and triangle fill rules are applied.
     * It is sufficient to get the nearest pixel center and check whether that pixel is selected
     * by the point fill-rule bias.
     */

    const auto bias = cnl::wrap<point_fixed_t>(FILL_RULE_EDGE_BIAS);

    const auto x = cnl::unwrap(
      cnl::floor(point_coords.x - bias));
    const auto y = cnl::unwrap(
      cnl::floor(point_coords.y - bias));

    if(x >= 0 && y >= 0)
    {
        const auto ux = static_cast<std::uint32_t>(x);
        const auto uy = static_cast<std::uint32_t>(y);

        if(ux < width && uy < height)
        {
            f(ux, uy);
        }
    }
}

}    // namespace rast
