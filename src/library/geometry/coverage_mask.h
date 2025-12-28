/**
 * swr - a software rasterizer
 *
 * coverage mask helpers.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

namespace geom
{

/** reduce coverage mask to 4 bits. */
inline int reduce_coverage_mask(int x)
{
    return x & (x >> 4) & (x >> 8);
}

}    // namespace geom