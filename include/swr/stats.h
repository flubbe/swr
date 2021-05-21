/**
 * swr - a software rasterizer
 * 
 * query rasterizer statistics.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

namespace swr
{

namespace stats
{

/** statistics for fragment processing */
struct fragment_data
{
    /** fragments processed. */
    uint64_t count{0};

    /** fragments discarded by the alpha test */
    uint64_t discard_alpha{0};

    /** fragments discarded by the depth test */
    uint64_t discard_depth{0};

    /** fragments discarded by the scissor test */
    uint64_t discard_scissor{0};

    /** fragments discarded by the fragment shader */
    uint64_t discard_shader{0};

    /** fragments with blending. */
    uint64_t blending{0};

    /** CPU cycles it took for all fragments to be processed. */
    uint64_t cycles{0};

    /** default constructor. */
    fragment_data() = default;

    /** reset counters to zero, */
    void reset_counters()
    {
        count = 0;
        discard_alpha = 0;
        discard_depth = 0;
        discard_scissor = 0;
        discard_shader = 0;
        blending = 0;
        cycles = 0;
    }
};

/** read fragment benchmark data. */
void get_fragment_data(fragment_data& data);

/** rasterizer statistics. */
struct rasterizer_data
{
    /** number of available threads in thread pool. */
    uint32_t available_threads{0};

    /** jobs (per frame). */
    uint32_t jobs{0};

    /** default constructor. */
    rasterizer_data() = default;

    /** reset counters. */
    void reset_counters()
    {
        jobs = 0;
    }
};

/** read rasterizer data. */
void get_rasterizer_data(rasterizer_data& data);

} /* namespace stats */

} /* namespace swr */