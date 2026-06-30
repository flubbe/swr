/**
 * swr - a software rasterizer
 *
 * fragment processing helpers.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <cstdint>
#include <span>

namespace rast
{

/**
 * Information on a fragment which is passed on to the fragment shader.
 */
struct fragment_info
{
    /**
     * Fragment z coordinate (within [0,1]), which may be written or compared to the depth buffer.
     *
     * In eq. (15.1), p.415 in https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf,
     * this is called z_f.
     */
    float depth_value;

    /** whether this fragment comes from a front-facing triangle. */
    bool front_facing;

    /** varyings. */
    std::span<swr::varying> varyings;

    /** no default constructor. */
    fragment_info() = delete;

    /** constructor. */
    fragment_info(
      float depth,
      bool in_front_facing,
      std::span<swr::varying> in_varyings)
    : depth_value{depth}
    , front_facing{in_front_facing}
    , varyings{in_varyings}
    {
    }
};

/** Early depth test telemetry used by adaptive rasterizer decisions. */
struct early_depth_sample
{
    /** Number of fragments tested by the early depth path. */
    std::uint64_t tested_fragments{0};

    /** Number of fragments rejected by the early depth path. */
    std::uint64_t rejected_fragments{0};

    /** Clear the sample. */
    void reset()
    {
        tested_fragments = 0;
        rejected_fragments = 0;
    }

    /** Store one sample. */
    void set(
      std::uint64_t tested,
      std::uint64_t rejected)
    {
        tested_fragments = tested;
        rejected_fragments = rejected;
    }

    /** Accumulate another sample. */
    void add(const early_depth_sample& sample)
    {
        tested_fragments += sample.tested_fragments;
        rejected_fragments += sample.rejected_fragments;
    }
};

} /* namespace rast */
