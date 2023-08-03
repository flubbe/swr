/**
 * swr - a software rasterizer
 *
 * internal header. mostly includes commonly needed headers.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <boost/container/static_vector.hpp>

/*
 * configurable options.
 */

/* use SIMD code by default. */
#define SWR_USE_SIMD

/* use multiple threads by default. */
#define SWR_ENABLE_MULTI_THREADING

/* enable the use of morton codes (for texture access) by default. */
#define SWR_USE_MORTON_CODES

/*
 * headers.
 */

#include "swr/swr.h"
#include "swr/shaders.h"

#include "geometry/all.h"

#include "../common/utils.h"

#include "states.h"
#include "pixelformat.h"
#include "output_merger.h"
#include "textures.h"
#include "renderbuffer.h"
#include "rasterizer/rasterizer.h"

#include "buffers.h"
#include "renderobject.h"
#include "context.h"

/*
 * alignment helpers.
 */

#if defined(__GNUC__)
#    define DECLARE_ALIGNED_ARRAY4(type, name) type name[4] __attribute__((aligned(utils::alignment::sse)))
#    define DECLARE_ALIGNED_FLOAT4(name)       DECLARE_ALIGNED_ARRAY4(float, name)
#elif defined(__MSVC__)
#    define DECLARE_ALIGNED_ARRAY4(type, name) __declspec(align(utils::alignment::sse)) type name[4]
#    define DECLARE_ALIGNED_FLOAT4(name)       DECLARE_ALIGNED_ARRAY4(float, name)
#else
#    error DECLARE_ALIGNED_*4 not defined for this compiler
#endif

/*
 * rasterizer configuration.
 */

namespace swr
{

namespace impl
{

/** block size for triangle rasterization. */
constexpr std::uint32_t rasterizer_block_shift{4};

/** Block size for triangle rasterization. The context buffer sizes have to be aligned on this value. */
constexpr std::uint32_t rasterizer_block_size{1 << rasterizer_block_shift};
static_assert(utils::is_power_of_two(rasterizer_block_size), "rasterizer_block_size has to be a power of 2");

/** round down to block size. */
inline int lower_align_on_block_size(int v)
{
    return v & ~(rasterizer_block_size - 1);
};

/** round up to block size. */
inline int upper_align_on_block_size(int v)
{
    return (v + (rasterizer_block_size - 1)) & ~(rasterizer_block_size - 1);
}

} /* namespace impl */

} /* namespace swr */
