/**
 * swr - a software rasterizer
 *
 * block processing for triangle rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

namespace rast
{

/** Invoke a callable for each 2x2 quad inside a triangle block. */
template<typename F>
inline void for_each_quad_in_triangle_block(
  unsigned int block_x,
  unsigned int block_y,
  rast::triangle_interpolator& attributes,
  F&& f)
{
    const auto end_x = block_x + swr::impl::rasterizer_block_size;
    const auto end_y = block_y + swr::impl::rasterizer_block_size;

    for(unsigned int y = block_y; y < end_y; y += 2)
    {
        for(unsigned int x = block_x; x < end_x; x += 2)
        {
            f(x, y, attributes);
            attributes.advance_x(2);
        }

        attributes.advance_y(2);
    }
}

/** Bounds for 2x2 quad iteration within one rasterizer block. */
struct quad_bounds
{
    unsigned int start_x{0};
    unsigned int start_y{0};
    unsigned int end_x{0};
    unsigned int end_y{0};

    [[nodiscard]]
    bool empty() const
    {
        return start_x >= end_x
               || start_y >= end_y;
    }
};

inline quad_bounds full_block_quad_bounds(
  unsigned int block_x,
  unsigned int block_y)
{
    return {
      block_x,
      block_y,
      block_x + swr::impl::rasterizer_block_size,
      block_y + swr::impl::rasterizer_block_size};
}

/** Invoke a callable for each covered 2x2 quad inside a checked triangle block. */
template<typename F>
inline void for_each_covered_quad_in_checked_triangle_block(
  unsigned int block_x,
  unsigned int block_y,
  quad_bounds bounds,
  geom::barycentric_coordinate_block lambdas,
  rast::triangle_interpolator& attributes,
  F&& f)
{
    const auto block_end_x = block_x + swr::impl::rasterizer_block_size;
    const auto block_end_y = block_y + swr::impl::rasterizer_block_size;

    bounds.start_x = std::max(bounds.start_x, block_x);
    bounds.start_y = std::max(bounds.start_y, block_y);
    bounds.end_x = std::min(bounds.end_x, block_end_x);
    bounds.end_y = std::min(bounds.end_y, block_end_y);

    if(bounds.empty())
    {
        return;
    }

    // Set up barycentric coordinates for 2x2 quad coverage testing.
    lambdas.setup(1, 1);

    lambdas.step_y(static_cast<int>(bounds.start_y - block_y));
    lambdas.step_x(static_cast<int>(bounds.start_x - block_x));

    attributes.advance_y(static_cast<int>(bounds.start_y - block_y));
    attributes.advance_x(static_cast<int>(bounds.start_x - block_x));
    attributes.setup_block_processing();

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t quad_tests = 0;
    std::uint64_t empty_quads = 0;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    for(unsigned int y = bounds.start_y; y < bounds.end_y; y += 2)
    {
        geom::barycentric_coordinate_block::fixed_24_8_array_4 row_start[3];
        lambdas.store_position(row_start[0], row_start[1], row_start[2]);

        for(unsigned int x = bounds.start_x; x < bounds.end_x; x += 2)
        {
            const int mask = geom::reduce_coverage_mask(lambdas.get_coverage_mask());
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            ++quad_tests;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            if(mask)
            {
                f(x, y, mask, attributes);
            }
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            else
            {
                ++empty_quads;
            }
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

            lambdas.step_x(2);
            attributes.advance_x(2);
        }

        lambdas.load_position(row_start[0], row_start[1], row_start[2]);
        lambdas.step_y(2);
        attributes.advance_y(2);
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_checked_quad_tests.fetch_add(quad_tests, std::memory_order_relaxed);
    swr::impl::profile_checked_empty_quads.fetch_add(empty_quads, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

template<typename F>
inline void for_each_covered_quad_in_checked_triangle_block(
  unsigned int block_x,
  unsigned int block_y,
  geom::barycentric_coordinate_block lambdas,
  rast::triangle_interpolator& attributes,
  F&& f)
{
    for_each_covered_quad_in_checked_triangle_block(
      block_x,
      block_y,
      full_block_quad_bounds(block_x, block_y),
      lambdas,
      attributes,
      std::forward<F>(f));
}

}    // namespace rast
