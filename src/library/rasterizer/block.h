/**
 * swr - a software rasterizer
 *
 * block processing for triangle rasterization.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

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

template<typename F>
inline void for_each_quad_in_triangle_block(
  unsigned int block_x,
  unsigned int block_y,
  const rast::triangle_interpolator& attributes,
  F&& f)
{
    auto attributes_copy = attributes;
    for_each_quad_in_triangle_block(
      block_x,
      block_y,
      attributes_copy,
      std::forward<F>(f));
}

/** Invoke a callable for each covered 2x2 quad inside a checked triangle block. */
template<typename F>
inline void for_each_covered_quad_in_checked_triangle_block(
  unsigned int block_x,
  unsigned int block_y,
  geom::barycentric_coordinate_block lambdas,
  rast::triangle_interpolator& attributes,
  F&& f)
{
    const auto end_x = block_x + swr::impl::rasterizer_block_size;
    const auto end_y = block_y + swr::impl::rasterizer_block_size;

    // Set up barycentric coordinates for 2x2 quad coverage testing.
    lambdas.setup(1, 1);

    for(unsigned int y = block_y; y < end_y; y += 2)
    {
        geom::barycentric_coordinate_block::fixed_24_8_array_4 row_start[3];
        lambdas.store_position(row_start[0], row_start[1], row_start[2]);

        for(unsigned int x = block_x; x < end_x; x += 2)
        {
            const int mask = geom::reduce_coverage_mask(lambdas.get_coverage_mask());
            if(mask)
            {
                f(x, y, mask, attributes);
            }

            lambdas.step_x(2);
            attributes.advance_x(2);
        }

        lambdas.load_position(row_start[0], row_start[1], row_start[2]);
        lambdas.step_y(2);
        attributes.advance_y(2);
    }
}

template<typename F>
inline void for_each_covered_quad_in_checked_triangle_block(
  unsigned int block_x,
  unsigned int block_y,
  geom::barycentric_coordinate_block lambdas,
  const rast::triangle_interpolator& attributes,
  F&& f)
{
    auto attributes_copy = attributes;
    for_each_covered_quad_in_checked_triangle_block(
      block_x,
      block_y,
      lambdas,
      attributes_copy,
      std::forward<F>(f));
}

}    // namespace rast
