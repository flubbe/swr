/**
 * swr - a software rasterizer
 * 
 * internal header. mostly includes commonly needed headers.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <boost/container/static_vector.hpp>

#include "swr/swr.h"
#include "swr/shaders.h"

#include "geometry/all.h"

#include "../common/utils.h"

#include "states.h"
#include "pixelformat.h"
#include "renderbuffer.h"
#include "rasterizer/rasterizer.h"

#include "buffers.h"
#include "renderobject.h"
#include "textures.h"
#include "context.h"

/* definitions internal to the renderer. */
namespace swr
{

namespace impl
{

/*
 * rasterization.
 */

/** Block size for triangle rasterization. The context buffer sizes have to be aligned on this value. */
constexpr int rasterizer_block_size {1 << 3};
static_assert((rasterizer_block_size & (rasterizer_block_size - 1)) == 0, "rasterizer_block_size has to be a power of 2");

/** round down to block size. */
inline int lower_align_on_block_size(int v)
{
    return v & ~(rasterizer_block_size-1);
};

/** round up to block size. */
inline int upper_align_on_block_size(int v)
{
    return (v + (rasterizer_block_size-1)) & ~(rasterizer_block_size-1);
}
    
} /* namespace impl */

} /* namespace swr */
