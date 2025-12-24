/**
 * swr - a software rasterizer
 *
 * color space conversions.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <cmath>

namespace colors
{

/** Convert from sRGB (range 0-1) space to linear space. */
inline float srgb_to_linear(float c)
{
    c = std::clamp(c, 0.0f, 1.0f);

    if(c <= 0.04045f)
    {
        return c / 12.92f;
    }
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
};

/** Convert a color with ranges 0-1 from sRGB to linear space and keep alpha.  */
inline ml::vec4 srgb_to_linear(ml::vec4 v)
{
    return {
      srgb_to_linear(v.r),
      srgb_to_linear(v.g),
      srgb_to_linear(v.b),
      v.a};
};

/** Convert from linear space (range 0-1) to sRGB. */
inline float linear_to_srgb(float c)
{
    c = std::clamp(c, 0.0f, 1.0f);

    if(c <= 0.0031308f)
    {
        return 12.92f * c;
    }
    return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

/** Convert from linear space (range 0-1) to sRGB and keep alpha. */
inline ml::vec4 linear_to_srgb(ml::vec4 v)
{
    return {
      linear_to_srgb(v.r),
      linear_to_srgb(v.g),
      linear_to_srgb(v.b),
      v.a};
}

}    // namespace colors
