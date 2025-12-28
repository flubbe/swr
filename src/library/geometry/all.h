/**
 * swr - a software rasterizer
 *
 * geometry helpers.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include "ml/all.h"

/* geometry headers. */
#include "coverage_mask.h"

#ifdef SWR_USE_SIMD
#    include "barycentric_coords_sse.h"
#else
#    include "barycentric_coords.h"
#endif

#include "edge_function.h"
#include "interpolators.h"
#include "limits.h"
#include "vertex.h"
