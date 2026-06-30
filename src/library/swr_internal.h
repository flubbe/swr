/**
 * swr - a software rasterizer
 *
 * internal header. mostly includes commonly needed headers.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <atomic>
#include <type_traits>

#include <boost/container/static_vector.hpp>

/*
 * headers.
 */

#include "swr/swr.h"
#include "swr/shaders.h"

#include "geometry/all.h"

#include "common/utils.h"

#include "states.h"
#include "pixelformat.h"
#include "output_merger.h"
#include "textures.h"
#include "renderbuffer.h"
#include "rasterizer/rasterizer.h"

#include "buffers.h"
#include "renderobject.h"
#include "context.h"

#ifdef SWR_ENABLE_PIPELINE_PROFILING
#    include "profiling.h"
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

/*
 * rasterizer configuration.
 */

namespace swr::impl
{

/** block size for triangle rasterization. */
#ifndef SWR_RASTERIZER_BLOCK_SHIFT
constexpr std::uint32_t rasterizer_block_shift{4};
#else
constexpr std::uint32_t rasterizer_block_shift{SWR_RASTERIZER_BLOCK_SHIFT};
#endif

/** Block size for triangle rasterization. The context buffer sizes have to be aligned on this value. */
constexpr std::uint32_t rasterizer_block_size{1 << rasterizer_block_shift};
static_assert(utils::is_power_of_two(rasterizer_block_size), "rasterizer_block_size has to be a power of 2");

/** round down to block size. */
template<typename T>
    requires(std::is_integral_v<T>)
inline T lower_align_on_block_size(const T& v)
{
    return v & ~(rasterizer_block_size - 1);
};

/** round up to block size. */
template<typename T>
    requires(std::is_integral_v<T>)
inline T upper_align_on_block_size(const T& v)
{
    return (v + (rasterizer_block_size - 1)) & ~(rasterizer_block_size - 1);
}

} /* namespace swr::impl */
