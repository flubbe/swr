/**
 * swr - a software rasterizer
 *
 * Implements Direct3D triangle rasterization.
 *
 * Some reference (on triangle rasterization and software rasterization in general):
 *
 * [1] http://www.scratchapixel.com/lessons/3d-basic-rendering/rasterization-practical-implementation/rasterization-stage
 * [2] http://forum.devmaster.net/t/advanced-rasterization/6145
 * [3] https://fgiesen.wordpress.com/2013/02/08/triangle-rasterization-in-practice/
 * [4] Pineda, “A Parallel Algorithm for Polygon Rasterization”, https://people.csail.mit.edu/ericchan/bib/pdf/p17-pineda.pdf
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "../swr_internal.h"

#include <array>
#include <span>

namespace rast
{

/** triangel setup info. */
struct triangle_info
{
    bool is_degenerate{true};

    // vertices normalized to CW raster order
    const geom::vertex* v0{nullptr};
    const geom::vertex* v1{nullptr};
    const geom::vertex* v2{nullptr};

    // 2D coords in the same normalized order
    ml::vec2 v0_xy;
    ml::vec2 v1_xy;
    ml::vec2 v2_xy;

    float area{0.0f};
    float inv_area{0.0f};

    ml::vec2_fixed<4> v0_xy_fix;
    ml::vec2_fixed<4> v1_xy_fix;
    ml::vec2_fixed<4> v2_xy_fix;

    // FIXME add appropriate constructor to edge_function_fixed
    std::array<geom::edge_function_fixed, 3> edges_fix = {
      geom::edge_function_fixed{{}, {}},
      geom::edge_function_fixed{{}, {}},
      geom::edge_function_fixed{{}, {}}};
};

inline triangle_info setup_triangle(
  const geom::vertex& v0,
  const geom::vertex& v1,
  const geom::vertex& v2)
{
    triangle_info info{};

    // calculate the (signed) parallelogram area spanned by the difference vectors.
    auto p0 = v0.coords.xy();
    auto p1 = v1.coords.xy();
    auto p2 = v2.coords.xy();

    auto area = (p1 - p0).area(p2 - p0);

    if(ml::fixed_24_8_t(area) == 0)
    {
        info.is_degenerate = true;
        return info;
    }
    info.is_degenerate = false;

    /*
     * To simplify the rasterization code, we only want to consider CW triangles with respect to the coordinate system
     *
     *     +---->  X
     *     |
     *   Y |
     *     V
     *
     * If a triangle is set up this way, we can check for a sign of the "fixed-point barycentric coordinates" below
     * (instead of checking that all of them have the same sign).
     */

    if(area > 0.0f)
    {
        // keep vertex order
        info.v0 = &v0;
        info.v1 = &v1;
        info.v2 = &v2;

        info.v0_xy = p0;
        info.v1_xy = p1;
        info.v2_xy = p2;

        info.area = area;
    }
    else /* area < 0, since we already checked for area==0 */
    {
        // change vertex order.

        info.v0 = &v1;
        info.v1 = &v0;
        info.v2 = &v2;

        info.v0_xy = p1;
        info.v1_xy = p0;
        info.v2_xy = p2;

        info.area = -area;
    }

    info.inv_area = 1.0f / info.area;

    // convert triangle coordinates into a fixed-point representation with 4-bit subpixel precision.
    info.v0_xy_fix = ml::vec2_fixed<4>(info.v0_xy.x, info.v0_xy.y);
    info.v1_xy_fix = ml::vec2_fixed<4>(info.v1_xy.x, info.v1_xy.y);
    info.v2_xy_fix = ml::vec2_fixed<4>(info.v2_xy.x, info.v2_xy.y);

    // list all edge in fixed point to use them for checking if a particular
    // pixel lies inside the triangle. the order of the edges does not matter,
    // but their orientation does.
    info.edges_fix[0] = geom::edge_function_fixed{info.v0_xy_fix, info.v1_xy_fix};
    info.edges_fix[1] = geom::edge_function_fixed{info.v1_xy_fix, info.v2_xy_fix};
    info.edges_fix[2] = geom::edge_function_fixed{info.v2_xy_fix, info.v0_xy_fix};

    /*
     * Fill Rules.
     *
     * We implement the top-left rule. Here, a pixel is drawn if its center lies entirely
     * inside the triangle or on a top edge or a left edge, where:
     *
     * i)  A top edge is an edge that is above all other edges and exactly horizontal.
     * ii) A left edge, is an edge that is not exactly horizontal and is on the left side of the triangle.
     *
     * References:
     *
     * [1] https://fgiesen.wordpress.com/2013/02/08/triangle-rasterization-in-practice/
     * [2] https://msdn.microsoft.com/en-us/library/windows/desktop/cc627092(v=vs.85).aspx
     */

    /*
     * Check for a top and left edges. For this, we suppose that our coordinate system is given by:
     *
     *     +---->  X
     *     |
     *   Y |
     *     V
     *
     * Thus, the Y coordinates are increasing towards the bottom, and the X coordinates are increasing
     * to the right. The origin is located at the upper-left corner of the viewport.
     *
     * Note that this coordinate system may or may not correspond to the actual rendered output.
     */
    for(auto& edge: info.edges_fix)
    {
        // Top edge test.
        //
        // 'exactly horizontal' implies that the y coordinate does not change. Since the triangle's vertices are
        // wound CW, the top edge is determined by checking that its x-direction is positive.
        if(edge.v_diff.y == 0
           && edge.v_diff.x > 0)
        {
            edge.c += cnl::wrap<ml::fixed_24_8_t>(FILL_RULE_EDGE_BIAS);
        }
        // Left edge test.
        //
        // In a CW triangle, a left edge goes up, i.e. its endpoint is strictly above its starting point.
        // In terms of the y coordinate, the difference vector has to be strictly negative.
        else if(edge.v_diff.y < 0)
        {
            edge.c += cnl::wrap<ml::fixed_24_8_t>(FILL_RULE_EDGE_BIAS);
        }
        else
        {
            // Here we have either a bottom edge or a right edge. Thus, we intentionally do nothing.
        }
    }

    return info;
}

/** triangle bounding box. */
struct bounding_box
{
    int start_x, start_y;
    int end_x, end_y;
    int tight_start_x, tight_start_y;
    int tight_end_x, tight_end_y;
};

inline constexpr int rasterizer_quad_size = 2;
inline constexpr int small_triangle_quad_span = 2;
inline constexpr int small_triangle_footprint_size = rasterizer_quad_size * small_triangle_quad_span;

inline int lower_align_on_quad_size(int v)
{
    return v & ~(rasterizer_quad_size - 1);
}

inline int upper_align_on_quad_size(int v)
{
    return (v + rasterizer_quad_size - 1) & ~(rasterizer_quad_size - 1);
}

inline bool tight_bounds_are_within_single_block(const bounding_box& bounds)
{
    if(bounds.tight_start_x >= bounds.tight_end_x
       || bounds.tight_start_y >= bounds.tight_end_y)
    {
        return false;
    }

    const int block_start_x = swr::impl::lower_align_on_block_size(bounds.tight_start_x);
    const int block_end_x = swr::impl::lower_align_on_block_size(bounds.tight_end_x - 1);
    const int block_start_y = swr::impl::lower_align_on_block_size(bounds.tight_start_y);
    const int block_end_y = swr::impl::lower_align_on_block_size(bounds.tight_end_y - 1);
    return block_start_x == block_end_x && block_start_y == block_end_y;
}

/** compute block-aligned triangle bounds in viewport coordinates. */
bounding_box compute_triangle_bounds(
  const swr::impl::render_states& states,
  const triangle_info& info,
  bool y_needs_flip)
{
    // take scissor box into account.
    int x_min = 0;
    int x_max = states.draw_target->properties.width;
    int y_min = 0;
    int y_max = states.draw_target->properties.height;

    if(states.scissor_test_enabled)
    {
        x_min = std::max(states.scissor_box.x_min, 0);
        x_max = std::min(states.scissor_box.x_max, states.draw_target->properties.width);

        y_min = std::max(states.scissor_box.y_min, 0);
        y_max = std::min(states.scissor_box.y_max, states.draw_target->properties.height);

        if(y_needs_flip)
        {
            const int y_temp = y_min;
            y_min = states.draw_target->properties.height - y_max;
            y_max = states.draw_target->properties.height - y_temp;
        }
    }

    auto v0x = ml::truncate_unchecked(info.v0_xy.x);
    auto v0y = ml::truncate_unchecked(info.v0_xy.y);
    auto v1x = ml::truncate_unchecked(info.v1_xy.x);
    auto v1y = ml::truncate_unchecked(info.v1_xy.y);
    auto v2x = ml::truncate_unchecked(info.v2_xy.x);
    auto v2y = ml::truncate_unchecked(info.v2_xy.y);

    const int tight_start_x = std::max(std::min({v0x, v1x, v2x}), x_min);
    const int tight_start_y = std::max(std::min({v0y, v1y, v2y}), y_min);
    const int tight_end_x = std::min(std::max({v0x + 1, v1x + 1, v2x + 1}), x_max);
    const int tight_end_y = std::min(std::max({v0y + 1, v1y + 1, v2y + 1}), y_max);

    return {
      .start_x = swr::impl::lower_align_on_block_size(tight_start_x),
      .start_y = swr::impl::lower_align_on_block_size(tight_start_y),
      .end_x = swr::impl::upper_align_on_block_size(tight_end_x),
      .end_y = swr::impl::upper_align_on_block_size(tight_end_y),
      .tight_start_x = tight_start_x,
      .tight_start_y = tight_start_y,
      .tight_end_x = tight_end_x,
      .tight_end_y = tight_end_y};
}

inline quad_bounds compute_checked_quad_bounds(
  const bounding_box& bounds,
  int block_x,
  int block_y)
{
    const int block_end_x = block_x + static_cast<int>(swr::impl::rasterizer_block_size);
    const int block_end_y = block_y + static_cast<int>(swr::impl::rasterizer_block_size);

    const int start_x = std::max(
      block_x,
      lower_align_on_quad_size(bounds.tight_start_x));
    const int start_y = std::max(
      block_y,
      lower_align_on_quad_size(bounds.tight_start_y));
    const int end_x = std::min(
      block_end_x,
      upper_align_on_quad_size(bounds.tight_end_x));
    const int end_y = std::min(
      block_end_y,
      upper_align_on_quad_size(bounds.tight_end_y));

    if(start_x >= end_x
       || start_y >= end_y)
    {
        return {
          static_cast<unsigned int>(block_x),
          static_cast<unsigned int>(block_y),
          static_cast<unsigned int>(block_x),
          static_cast<unsigned int>(block_y)};
    }

    return {
      static_cast<unsigned int>(start_x),
      static_cast<unsigned int>(start_y),
      static_cast<unsigned int>(end_x),
      static_cast<unsigned int>(end_y)};
}

inline geom::barycentric_coordinate_block compute_triangle_lambdas_at_block(
  const triangle_info& info,
  int block_x,
  int block_y)
{
    const auto coord = ml::vec2_fixed<4>{
      ml::fixed_28_4_t{block_x} + ml::fixed_28_4_t{0.5f},
      ml::fixed_28_4_t{block_y} + ml::fixed_28_4_t{0.5f}};

    geom::barycentric_coordinate_block lambdas{
      -info.edges_fix[0].evaluate(coord),
      {-info.edges_fix[0].get_change_x(), -info.edges_fix[0].get_change_y()},
      -info.edges_fix[1].evaluate(coord),
      {-info.edges_fix[1].get_change_x(), -info.edges_fix[1].get_change_y()},
      -info.edges_fix[2].evaluate(coord),
      {-info.edges_fix[2].get_change_x(), -info.edges_fix[2].get_change_y()}};

    lambdas.setup(
      swr::impl::rasterizer_block_size,
      swr::impl::rasterizer_block_size);

    return lambdas;
}

inline rast::triangle_interpolator compute_triangle_attributes_at_block(
  const swr::impl::render_states& states,
  const triangle_info& info,
  std::span<const ml::vec4> base_varyings,
  float polygon_offset,
  int block_x,
  int block_y)
{
    const ml::vec2 screen_coords{
      static_cast<float>(block_x) + 0.5f,
      static_cast<float>(block_y) + 0.5f};

    return {
      screen_coords,
      info.v0->coords, info.v1->coords, info.v2->coords,
      std::span<const ml::vec4>{info.v0->varyings.data(), info.v0->varyings.size()},
      std::span<const ml::vec4>{info.v1->varyings.data(), info.v1->varyings.size()},
      std::span<const ml::vec4>{info.v2->varyings.data(), info.v2->varyings.size()},
      base_varyings,
      states.shader_info->iqs, info.inv_area, polygon_offset};
}

struct thin_span
{
    int start{0};
    int end{0};
    int tight_start{0};
    int tight_end{0};

    [[nodiscard]]
    bool empty() const
    {
        return start >= end;
    }
};

inline void include_thin_span_value(
  bool& has_value,
  float& min_value,
  float& max_value,
  float value)
{
    if(!has_value)
    {
        min_value = value;
        max_value = value;
        has_value = true;
        return;
    }

    min_value = std::min(min_value, value);
    max_value = std::max(max_value, value);
}

inline void include_thin_span_edge_crossing(
  bool& has_value,
  float& min_value,
  float& max_value,
  const ml::vec2& p0,
  const ml::vec2& p1,
  float axis_value,
  bool x_major)
{
    const float a0 = x_major ? p0.x : p0.y;
    const float a1 = x_major ? p1.x : p1.y;
    const float axis_min = std::min(a0, a1);
    const float axis_max = std::max(a0, a1);

    if(a0 == a1
       || axis_value < axis_min
       || axis_value > axis_max)
    {
        return;
    }

    const float t = (axis_value - a0) / (a1 - a0);
    const float value =
      x_major
        ? p0.y + (p1.y - p0.y) * t
        : p0.x + (p1.x - p0.x) * t;
    include_thin_span_value(has_value, min_value, max_value, value);
}

inline thin_span compute_thin_triangle_minor_span_in_major_range(
  const bounding_box& bounds,
  const triangle_info& info,
  int major_start,
  int major_end,
  bool x_major)
{
    const float clipped_major_start = static_cast<float>(
      std::max(major_start, x_major ? bounds.tight_start_x : bounds.tight_start_y));
    const float clipped_major_end = static_cast<float>(
      std::min(major_end, x_major ? bounds.tight_end_x : bounds.tight_end_y));

    if(clipped_major_start >= clipped_major_end)
    {
        return {};
    }

    const std::array<ml::vec2, 3> vertices{
      info.v0_xy,
      info.v1_xy,
      info.v2_xy};

    bool has_value = false;
    float min_value = 0.0f;
    float max_value = 0.0f;

    for(const auto& vertex: vertices)
    {
        const float major = x_major ? vertex.x : vertex.y;
        const float minor = x_major ? vertex.y : vertex.x;
        if(major >= clipped_major_start
           && major <= clipped_major_end)
        {
            include_thin_span_value(has_value, min_value, max_value, minor);
        }
    }

    for(std::size_t i = 0; i < vertices.size(); ++i)
    {
        const auto& p0 = vertices[i];
        const auto& p1 = vertices[(i + 1) % vertices.size()];
        include_thin_span_edge_crossing(
          has_value,
          min_value,
          max_value,
          p0,
          p1,
          clipped_major_start,
          x_major);
        include_thin_span_edge_crossing(
          has_value,
          min_value,
          max_value,
          p0,
          p1,
          clipped_major_end,
          x_major);
    }

    if(!has_value)
    {
        return {};
    }

    const int tight_min = x_major ? bounds.tight_start_y : bounds.tight_start_x;
    const int tight_max = x_major ? bounds.tight_end_y : bounds.tight_end_x;
    const int minor_start = std::max(ml::truncate_unchecked(min_value), tight_min);
    const int minor_end = std::min(ml::truncate_unchecked(max_value) + 1, tight_max);

    if(minor_start >= minor_end)
    {
        return {};
    }

    return {
      lower_align_on_quad_size(minor_start),
      upper_align_on_quad_size(minor_end),
      minor_start,
      minor_end};
}

inline bool should_build_sparse_triangle_payload(
  const thin_span& minor_span,
  quad_bounds bounds)
{
    constexpr int max_tight_minor_span = 2;
    constexpr unsigned int min_candidate_quad_count = 4;

    if(minor_span.tight_end - minor_span.tight_start > max_tight_minor_span)
    {
        return false;
    }

    const unsigned int quad_width = (bounds.end_x - bounds.start_x) / rasterizer_quad_size;
    const unsigned int quad_height = (bounds.end_y - bounds.start_y) / rasterizer_quad_size;
    return quad_width * quad_height >= min_candidate_quad_count;
}

struct triangle_rasterization_classification
{
    tile_info::rasterization_mode mode{tile_info::rasterization_mode::checked};
    bool is_small_quad{false};
};

inline bool is_small_quad_triangle(
  const bounding_box& bounds,
  int quad_width,
  int quad_height)
{
    return quad_width <= small_triangle_footprint_size
           && quad_height <= small_triangle_footprint_size
           && tight_bounds_are_within_single_block(bounds);
}

inline triangle_rasterization_classification classify_triangle_rasterization(
  const bounding_box& bounds,
  const triangle_info& info)
{
    constexpr int thin_minor_axis_limit = 4;

    const int quad_start_x = lower_align_on_quad_size(bounds.tight_start_x);
    const int quad_start_y = lower_align_on_quad_size(bounds.tight_start_y);
    const int quad_end_x = upper_align_on_quad_size(bounds.tight_end_x);
    const int quad_end_y = upper_align_on_quad_size(bounds.tight_end_y);

    const int quad_width = quad_end_x - quad_start_x;
    const int quad_height = quad_end_y - quad_start_y;

    const int major_span = std::max(quad_width, quad_height);
    const int minor_span = std::min(quad_width, quad_height);

    if(is_small_quad_triangle(bounds, quad_width, quad_height))
    {
        return {
          tile_info::rasterization_mode::checked,
          true};
    }

    if(major_span <= thin_minor_axis_limit
       || minor_span <= 0)
    {
        return {
          tile_info::rasterization_mode::checked,
          false};
    }

    const float projected_minor_span =
      info.area / static_cast<float>(major_span);
    if(minor_span > thin_minor_axis_limit
       && projected_minor_span > static_cast<float>(thin_minor_axis_limit))
    {
        return {
          tile_info::rasterization_mode::checked,
          false};
    }

    return {
      (quad_width >= quad_height)
        ? tile_info::rasterization_mode::thin_x_major
        : tile_info::rasterization_mode::thin_y_major,
      false};
}

inline tile_info::rasterization_mode classify_thin_triangle(
  const bounding_box& bounds,
  const triangle_info& info)
{
    return classify_triangle_rasterization(bounds, info).mode;
}

/** Detect if a triangle's quad-aligned tight bounds cover at most 2x2 raster quads in one block. */
inline bool is_small_quad_triangle(const bounding_box& bounds)
{
    const int quad_start_x = lower_align_on_quad_size(bounds.tight_start_x);
    const int quad_start_y = lower_align_on_quad_size(bounds.tight_start_y);
    const int quad_end_x = upper_align_on_quad_size(bounds.tight_end_x);
    const int quad_end_y = upper_align_on_quad_size(bounds.tight_end_y);

    const int quad_width = quad_end_x - quad_start_x;
    const int quad_height = quad_end_y - quad_start_y;

    return is_small_quad_triangle(bounds, quad_width, quad_height);
}

enum class precomputed_triangle_payload_status : std::uint8_t
{
    ready,
    empty,
    unsupported,
    overflow
};

inline precomputed_triangle_payload_status build_precomputed_triangle_payload(
  unsigned int block_x,
  unsigned int block_y,
  quad_bounds bounds,
  geom::barycentric_coordinate_block lambdas,
  const rast::triangle_interpolator& attributes,
  sparse_triangle_payload& payload)
{
    payload = {};

    if(!small_triangle_interpolator::can_store_without_allocation(attributes))
    {
        return precomputed_triangle_payload_status::unsupported;
    }

    const auto block_end_x = block_x + swr::impl::rasterizer_block_size;
    const auto block_end_y = block_y + swr::impl::rasterizer_block_size;

    bounds.start_x = std::max(bounds.start_x, block_x);
    bounds.start_y = std::max(bounds.start_y, block_y);
    bounds.end_x = std::min(bounds.end_x, block_end_x);
    bounds.end_y = std::min(bounds.end_y, block_end_y);

    if(bounds.empty())
    {
        return precomputed_triangle_payload_status::empty;
    }

    payload.attributes = small_triangle_interpolator{attributes};

    lambdas.setup(1, 1);
    lambdas.step_y(static_cast<int>(bounds.start_y - block_y));
    lambdas.step_x(static_cast<int>(bounds.start_x - block_x));

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
                if(payload.quads.size() == payload.quads.max_size())
                {
                    return precomputed_triangle_payload_status::overflow;
                }

                payload.quads.push_back({
                  x,
                  y,
                  static_cast<std::uint8_t>(mask)});
            }
            else
            {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
                ++empty_quads;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            }

            lambdas.step_x(2);
        }

        lambdas.load_position(row_start[0], row_start[1], row_start[2]);
        lambdas.step_y(2);
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_checked_quad_tests.fetch_add(quad_tests, std::memory_order_relaxed);
    swr::impl::profile_checked_empty_quads.fetch_add(empty_quads, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if(payload.quads.empty())
    {
        return precomputed_triangle_payload_status::empty;
    }

    return precomputed_triangle_payload_status::ready;
}

/**
 * Emit a checked rasterizer block for triangles contained within a 2x2-quad
 * footprint. Checked quad iteration then visits only the tight quad bounds.
 */
template<typename F>
inline void for_each_small_quad_triangle(
  const swr::impl::render_states& states,
  const bounding_box& bounds,
  const triangle_info& info,
  std::span<const ml::vec4> base_varyings,
  float polygon_offset,
  F&& f)
{
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_callback = 0;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    const int quad_start_x = lower_align_on_quad_size(bounds.tight_start_x);
    const int quad_start_y = lower_align_on_quad_size(bounds.tight_start_y);
    const int quad_end_x = upper_align_on_quad_size(bounds.tight_end_x);
    const int quad_end_y = upper_align_on_quad_size(bounds.tight_end_y);

    assert(is_small_quad_triangle(bounds));

    const int block_x = swr::impl::lower_align_on_block_size(quad_start_x);
    const int block_y = swr::impl::lower_align_on_block_size(quad_start_y);
    const quad_bounds checked_quad_bounds{
      static_cast<unsigned int>(quad_start_x),
      static_cast<unsigned int>(quad_start_y),
      static_cast<unsigned int>(quad_end_x),
      static_cast<unsigned int>(quad_end_y)};

    const auto start_coord = ml::vec2_fixed<4>{
      ml::fixed_28_4_t{block_x} + ml::fixed_28_4_t{0.5f},
      ml::fixed_28_4_t{block_y} + ml::fixed_28_4_t{0.5f}};

    geom::barycentric_coordinate_block lambdas{
      -info.edges_fix[0].evaluate(start_coord),
      {-info.edges_fix[0].get_change_x(), -info.edges_fix[0].get_change_y()},
      -info.edges_fix[1].evaluate(start_coord),
      {-info.edges_fix[1].get_change_x(), -info.edges_fix[1].get_change_y()},
      -info.edges_fix[2].evaluate(start_coord),
      {-info.edges_fix[2].get_change_x(), -info.edges_fix[2].get_change_y()}};

    lambdas.setup(
      swr::impl::rasterizer_block_size,
      swr::impl::rasterizer_block_size);

    auto coverage_lambdas = lambdas;
    coverage_lambdas.setup(1, 1);
    coverage_lambdas.step_y(static_cast<int>(checked_quad_bounds.start_y - block_y));
    coverage_lambdas.step_x(static_cast<int>(checked_quad_bounds.start_x - block_x));

    std::array<
      small_triangle_quad_payload,
      max_small_triangle_quad_payloads>
      covered_quads{};
    std::uint8_t covered_quad_count = 0;
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t quad_tests = 0;
    std::uint64_t empty_quads = 0;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    for(unsigned int y = checked_quad_bounds.start_y; y < checked_quad_bounds.end_y; y += 2)
    {
        geom::barycentric_coordinate_block::fixed_24_8_array_4 row_start[3];
        coverage_lambdas.store_position(row_start[0], row_start[1], row_start[2]);

        for(unsigned int x = checked_quad_bounds.start_x; x < checked_quad_bounds.end_x; x += 2)
        {
            const int mask = geom::reduce_coverage_mask(coverage_lambdas.get_coverage_mask());
#ifdef SWR_ENABLE_PIPELINE_PROFILING
            ++quad_tests;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            if(mask)
            {
                assert(covered_quad_count < covered_quads.size());
                covered_quads[covered_quad_count++] = {
                  x,
                  y,
                  static_cast<std::uint8_t>(mask)};
            }
            else
            {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
                ++empty_quads;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            }

            coverage_lambdas.step_x(2);
        }

        coverage_lambdas.load_position(row_start[0], row_start[1], row_start[2]);
        coverage_lambdas.step_y(2);
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_checked_quad_tests.fetch_add(quad_tests, std::memory_order_relaxed);
    swr::impl::profile_checked_empty_quads.fetch_add(empty_quads, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if(covered_quad_count == 0)
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        swr::impl::profile_raster_small_quad_empty_primitives.fetch_add(1, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        return;
    }

    rast::triangle_interpolator attributes =
      compute_triangle_attributes_at_block(
        states,
        info,
        base_varyings,
        polygon_offset,
        block_x,
        block_y);

    small_triangle_payload small_payload{};
    const small_triangle_payload* small_payload_ptr = nullptr;
    if(small_triangle_interpolator::can_store_without_allocation(attributes))
    {
        small_payload.attributes = small_triangle_interpolator{attributes};
        small_payload.quads = covered_quads;
        small_payload.quad_count = covered_quad_count;
        small_payload_ptr = &small_payload;
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t callback_cycles = 0;
    utils::clock(callback_cycles);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    if constexpr(std::is_invocable_v<
                   F&,
                   int,
                   int,
                   const geom::barycentric_coordinate_block&,
                   const rast::triangle_interpolator&,
                   tile_info::rasterization_mode,
                   const quad_bounds*,
                   const small_triangle_payload*>)
    {
        f(block_x,
          block_y,
          lambdas,
          attributes,
          tile_info::rasterization_mode::checked,
          &checked_quad_bounds,
          small_payload_ptr);
    }
    else if constexpr(std::is_invocable_v<
                        F&,
                        int,
                        int,
                        const geom::barycentric_coordinate_block&,
                        const rast::triangle_interpolator&,
                        tile_info::rasterization_mode,
                        const quad_bounds*>)
    {
        f(block_x, block_y, lambdas, attributes, tile_info::rasterization_mode::checked, &checked_quad_bounds);
    }
    else
    {
        f(block_x, block_y, lambdas, attributes, tile_info::rasterization_mode::checked);
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(callback_cycles);
    stage_callback += callback_cycles;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_raster_setup_iter_callback_cycles.fetch_add(stage_callback, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

template<typename F>
inline void for_each_covered_triangle_block_general_with_bounds(
  const swr::impl::render_states& states,
  const bounding_box& bounds,
  const triangle_info& info,
  std::span<const ml::vec4> base_varyings,
  float polygon_offset,
  F&& f)
{
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_row_setup = 0;
    std::uint64_t stage_callback = 0;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    const auto start_coord = ml::vec2_fixed<4>{
      ml::fixed_28_4_t{bounds.start_x} + ml::fixed_28_4_t{0.5f},
      ml::fixed_28_4_t{bounds.start_y} + ml::fixed_28_4_t{0.5f}};

    geom::linear_interpolator_2d<ml::fixed_24_8_t> lambda_row_top_left[3] = {
      {{-info.edges_fix[0].evaluate(start_coord)},
       {-info.edges_fix[0].get_change_x(),
        -info.edges_fix[0].get_change_y()}},
      {{-info.edges_fix[1].evaluate(start_coord)},
       {-info.edges_fix[1].get_change_x(),
        -info.edges_fix[1].get_change_y()}},
      {{-info.edges_fix[2].evaluate(start_coord)},
       {-info.edges_fix[2].get_change_x(),
        -info.edges_fix[2].get_change_y()}}};

    rast::triangle_interpolator attributes =
      compute_triangle_attributes_at_block(
        states,
        info,
        base_varyings,
        polygon_offset,
        bounds.start_x,
        bounds.start_y);

    for(auto y = bounds.start_y; y < bounds.end_y; y += swr::impl::rasterizer_block_size)
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t row_setup_cycles = 0;
        utils::clock(row_setup_cycles);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
        geom::barycentric_coordinate_block lambdas_box{
          lambda_row_top_left[0].value, lambda_row_top_left[0].step,
          lambda_row_top_left[1].value, lambda_row_top_left[1].step,
          lambda_row_top_left[2].value, lambda_row_top_left[2].step};
        lambdas_box.setup(
          swr::impl::rasterizer_block_size,
          swr::impl::rasterizer_block_size);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(row_setup_cycles);
        stage_row_setup += row_setup_cycles;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        for(auto x = bounds.start_x; x < bounds.end_x; x += swr::impl::rasterizer_block_size)
        {
            const int mask = lambdas_box.get_coverage_mask();
            const int edge_mask0 = mask & 0xf;
            const int edge_mask1 = (mask >> 4) & 0xf;
            const int edge_mask2 = (mask >> 8) & 0xf;

            if(edge_mask0 && edge_mask1 && edge_mask2)
            {
                const int covered_corner_mask = edge_mask0 & edge_mask1 & edge_mask2;

                static_assert(static_cast<int>(tile_info::rasterization_mode::block) == 0);
                static_assert(static_cast<int>(tile_info::rasterization_mode::checked) == 1);

                const auto mode =
                  static_cast<tile_info::rasterization_mode>(static_cast<int>(covered_corner_mask != 0xf));

#ifdef SWR_ENABLE_PIPELINE_PROFILING
                std::uint64_t callback_cycles = 0;
                utils::clock(callback_cycles);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

                f(x, y, lambdas_box, attributes, mode);

#ifdef SWR_ENABLE_PIPELINE_PROFILING
                utils::unclock(callback_cycles);
                stage_callback += callback_cycles;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
            }

            lambdas_box.step_x(swr::impl::rasterizer_block_size);
            attributes.advance_x(swr::impl::rasterizer_block_size);
        }

        lambda_row_top_left[0].step_y(swr::impl::rasterizer_block_size);
        lambda_row_top_left[1].step_y(swr::impl::rasterizer_block_size);
        lambda_row_top_left[2].step_y(swr::impl::rasterizer_block_size);

        attributes.advance_y(swr::impl::rasterizer_block_size);
    }
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_raster_setup_iter_row_setup_cycles.fetch_add(stage_row_setup, std::memory_order_relaxed);
    swr::impl::profile_raster_setup_iter_callback_cycles.fetch_add(stage_callback, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

template<typename F>
inline void for_each_covered_triangle_block_with_bounds(
  const swr::impl::render_states& states,
  const bounding_box& bounds,
  const triangle_info& info,
  std::span<const ml::vec4> base_varyings,
  float polygon_offset,
  F&& f)
{
    if(is_small_quad_triangle(bounds))
    {
        for_each_small_quad_triangle(
          states,
          bounds,
          info,
          base_varyings,
          polygon_offset,
          std::forward<F>(f));
        return;
    }

    for_each_covered_triangle_block_general_with_bounds(
      states,
      bounds,
      info,
      base_varyings,
      polygon_offset,
      std::forward<F>(f));
}

template<typename F>
inline void for_each_thin_triangle_block_with_bounds(
  const swr::impl::render_states& states,
  const bounding_box& bounds,
  const triangle_info& info,
  std::span<const ml::vec4> base_varyings,
  float polygon_offset,
  tile_info::rasterization_mode mode,
  F&& f)
{
    assert(is_thin_rasterization_mode(mode));

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_callback = 0;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

    const int quad_start_x = lower_align_on_quad_size(bounds.tight_start_x);
    const int quad_start_y = lower_align_on_quad_size(bounds.tight_start_y);
    const int quad_end_x = upper_align_on_quad_size(bounds.tight_end_x);
    const int quad_end_y = upper_align_on_quad_size(bounds.tight_end_y);

    if(quad_start_x >= quad_end_x
       || quad_start_y >= quad_end_y)
    {
        return;
    }

    const int start_x = swr::impl::lower_align_on_block_size(quad_start_x);
    const int start_y = swr::impl::lower_align_on_block_size(quad_start_y);
    const int end_x = swr::impl::upper_align_on_block_size(quad_end_x);
    const int end_y = swr::impl::upper_align_on_block_size(quad_end_y);

    const auto emit_block = [&](int x, int y, quad_bounds thin_quad_bounds, bool allow_sparse_payload)
    {
        geom::barycentric_coordinate_block lambdas_box =
          compute_triangle_lambdas_at_block(info, x, y);
        rast::triangle_interpolator attributes =
          compute_triangle_attributes_at_block(
            states,
            info,
            base_varyings,
            polygon_offset,
            x,
            y);

        sparse_triangle_payload sparse_payload{};
        const sparse_triangle_payload* sparse_payload_ptr = nullptr;
        if(allow_sparse_payload)
        {
            const precomputed_triangle_payload_status payload_status =
              build_precomputed_triangle_payload(
                static_cast<unsigned int>(x),
                static_cast<unsigned int>(y),
                thin_quad_bounds,
                lambdas_box,
                attributes,
                sparse_payload);
            if(payload_status == precomputed_triangle_payload_status::empty)
            {
                return;
            }
            if(payload_status == precomputed_triangle_payload_status::ready)
            {
                sparse_payload_ptr = &sparse_payload;
            }
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        std::uint64_t callback_cycles = 0;
        utils::clock(callback_cycles);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */

        if constexpr(std::is_invocable_v<
                       F&,
                       int,
                       int,
                       const geom::barycentric_coordinate_block&,
                       const rast::triangle_interpolator&,
                       tile_info::rasterization_mode,
                       quad_bounds,
                       const sparse_triangle_payload*>)
        {
            f(x,
              y,
              lambdas_box,
              attributes,
              mode,
              thin_quad_bounds,
              sparse_payload_ptr);
        }
        else
        {
            f(x, y, lambdas_box, attributes, mode, thin_quad_bounds);
        }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(callback_cycles);
        stage_callback += callback_cycles;
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    };

    if(mode == tile_info::rasterization_mode::thin_x_major)
    {
        for(auto x = start_x; x < end_x; x += swr::impl::rasterizer_block_size)
        {
            const thin_span y_span =
              compute_thin_triangle_minor_span_in_major_range(
                bounds,
                info,
                x,
                x + swr::impl::rasterizer_block_size,
                true);
            if(y_span.empty())
            {
                continue;
            }

            const int y_start = swr::impl::lower_align_on_block_size(y_span.start);
            const int y_end = swr::impl::upper_align_on_block_size(y_span.end);
            for(auto y = y_start; y < y_end; y += swr::impl::rasterizer_block_size)
            {
                quad_bounds thin_quad_bounds{
                  static_cast<unsigned int>(std::max(x, quad_start_x)),
                  static_cast<unsigned int>(std::max(y, y_span.start)),
                  static_cast<unsigned int>(std::min(x + static_cast<int>(swr::impl::rasterizer_block_size), quad_end_x)),
                  static_cast<unsigned int>(std::min(y + static_cast<int>(swr::impl::rasterizer_block_size), y_span.end))};
                if(!thin_quad_bounds.empty())
                {
                    emit_block(
                      x,
                      y,
                      thin_quad_bounds,
                      should_build_sparse_triangle_payload(y_span, thin_quad_bounds));
                }
            }
        }
    }
    else
    {
        for(auto y = start_y; y < end_y; y += swr::impl::rasterizer_block_size)
        {
            const thin_span x_span =
              compute_thin_triangle_minor_span_in_major_range(
                bounds,
                info,
                y,
                y + swr::impl::rasterizer_block_size,
                false);
            if(x_span.empty())
            {
                continue;
            }

            const int x_start = swr::impl::lower_align_on_block_size(x_span.start);
            const int x_end = swr::impl::upper_align_on_block_size(x_span.end);
            for(auto x = x_start; x < x_end; x += swr::impl::rasterizer_block_size)
            {
                quad_bounds thin_quad_bounds{
                  static_cast<unsigned int>(std::max(x, x_span.start)),
                  static_cast<unsigned int>(std::max(y, quad_start_y)),
                  static_cast<unsigned int>(std::min(x + static_cast<int>(swr::impl::rasterizer_block_size), x_span.end)),
                  static_cast<unsigned int>(std::min(y + static_cast<int>(swr::impl::rasterizer_block_size), quad_end_y))};
                if(!thin_quad_bounds.empty())
                {
                    emit_block(
                      x,
                      y,
                      thin_quad_bounds,
                      should_build_sparse_triangle_payload(x_span, thin_quad_bounds));
                }
            }
        }
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    swr::impl::profile_raster_setup_iter_callback_cycles.fetch_add(stage_callback, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

template<typename F>
inline void for_each_covered_triangle_block(
  const swr::impl::render_states& states,
  const triangle_info& info,
  std::span<const ml::vec4> base_varyings,
  float polygon_offset,
  bool y_needs_flip,
  F&& f)
{
    const bounding_box bounds = compute_triangle_bounds(
      states,
      info,
      y_needs_flip);
    for_each_covered_triangle_block_with_bounds(
      states,
      bounds,
      info,
      base_varyings,
      polygon_offset,
      std::forward<F>(f));
}

}    // namespace rast
