/**
 * swr - a software rasterizer
 *
 * Implements Direct3D line rasterization using the diamond exit rule, fixed-point arithmetic,
 * and half-open endpoint ownership.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "../swr_internal.h"

namespace rast
{

/*
 * Helpers.
 */

using line_fixed_t = ml::fixed_24_8_t;
using line_fixed_vec2 = ml::vec2_fixed<
  ml::static_number_traits<line_fixed_t>::fractional_bits>;
using line_fixed_raw_t = ml::static_number_traits<line_fixed_t>::rep;
using line_fixed_wide_t = ml::static_number_traits<line_fixed_t>::wide_rep;

inline constexpr line_fixed_t half{0.5f};

/** Convert a `ml::vec2` into the fixed-point representation used by line rasterization. */
line_fixed_vec2 make_fixed_point(ml::vec2 point)
{
    return {point.x, point.y};
}

/** Classification of a point relative to a pixel diamond under the diamond exit rule. */
enum class diamond_point_class
{
    outside,           /** Point is outside the diamond. */
    inside,            /** Point is inside the diamond. */
    included_boundary, /** Point is on an included boundary of the diamond. */
    excluded_boundary  /** Point is on an excluded boundary of the diamond. */
};

/**
 * Classify a point relative to the origin-centered pixel diamond (`|x|+|y| = 0.5`),
 * applying the Direct3D diamond rule.
 *
 * Points strictly inside/outside are classified directly.
 * Points on the boundary are split into included/excluded depending on line major axis:
 * - upper half (y > 0) is always included
 * - for y-major lines, the right half of the horizontal edge is also included
 */
inline diamond_point_class classify_diamond_point(line_fixed_vec2 v, bool x_major)
{
    const auto d = cnl::abs(v.x) + cnl::abs(v.y);

    if(d < half)
    {
        return diamond_point_class::inside;
    }

    if(d > half)
    {
        return diamond_point_class::outside;
    }

    // v is on the boundary here.

    if(v.y > line_fixed_t{0})
    {
        return diamond_point_class::included_boundary;
    }

    // y-major lines include additional point.
    if(!x_major
       && v.y == line_fixed_t{0}
       && v.x > line_fixed_t{0})
    {
        return diamond_point_class::included_boundary;
    }

    return diamond_point_class::excluded_boundary;
}

/** Evaluate one signed diamond boundary constraint sx*x + sy*y for a point or direction vector. */
inline line_fixed_t signed_constraint(line_fixed_vec2 v, int sx, int sy)
{
    return (sx > 0 ? v.x : -v.x) + (sy > 0 ? v.y : -v.y);
}

/** Invoke a callable once for each of the four diamond boundary sign combinations. */
template<class F>
inline void for_each_diamond_constraint(F&& f)
{
    f(1, 1);
    f(1, -1);
    f(-1, 1);
    f(-1, -1);
}

/** Invoke `f` for each active diamond boundary constraint touched by `offset`. */
template<class F>
inline bool for_each_active_diamond_boundary_constraint(
  line_fixed_vec2 offset,
  bool x_major,
  F&& f)
{
    if(classify_diamond_point(offset, x_major) != diamond_point_class::included_boundary)
    {
        return false;
    }

    bool touched = false;
    for_each_diamond_constraint(
      [&](int sx, int sy)
      {
          if(signed_constraint(offset, sx, sy) == half)
          {
              touched = true;
              f(sx, sy);
          }
      });

    return touched;
}

inline bool exits_diamond_from_boundary(
  line_fixed_vec2 offset,
  line_fixed_vec2 delta,
  bool x_major)
{
    bool exits = false;

    for_each_active_diamond_boundary_constraint(
      offset,
      x_major,
      [&](int sx, int sy)
      {
          if(signed_constraint(delta, sx, sy) > line_fixed_t{0})
          {
              exits = true;
          }
      });

    return exits;
}

inline bool enters_diamond_towards_boundary_end(
  line_fixed_vec2 offset,
  line_fixed_vec2 delta,
  bool x_major)
{
    bool touched = false;
    bool valid = true;

    touched = for_each_active_diamond_boundary_constraint(
      offset,
      x_major,
      [&](int sx, int sy)
      {
          if(signed_constraint(delta, sx, sy) < line_fixed_t{0})
          {
              valid = false;
          }
      });

    return touched && valid;
}

/** Diamond-rule ownership decisions for the original start and end endpoints of a line segment. */
struct endpoint_rule_info
{
    std::optional<ml::tvec2<int>> start_pixel_to_emit;
    std::optional<ml::tvec2<int>> end_pixel_to_exclude;
};

/** Express a point in coordinates relative to the center of the given pixel. */
inline line_fixed_vec2 point_offset_from_pixel_center(
  line_fixed_vec2 point,
  ml::tvec2<int> pixel)
{
    return {
      point.x - (line_fixed_t{pixel.x} + half),
      point.y - (line_fixed_t{pixel.y} + half)};
}

/** Return true if the line end remains inside or on the included boundary of the owned start pixel's diamond. */
inline bool segment_stays_within_owned_start_diamond(
  line_fixed_vec2 end_point,
  ml::tvec2<int> owned_start_pixel,
  bool x_major)
{
    const auto end_offset =
      point_offset_from_pixel_center(end_point, owned_start_pixel);
    const auto end_class = classify_diamond_point(end_offset, x_major);

    return end_class == diamond_point_class::inside
           || end_class == diamond_point_class::included_boundary;
}

/**
 * Find the pixel whose diamond owns an endpoint.
 *
 * The search starts from the base pixel (floor of coordinates) and expands to the
 * 3x3 neighborhood. A pixel is selected if:
 * - the point lies inside its diamond, or
 * - it satisfies the supplied boundary predicate (entry/exit rule)
 *
 * Used for both start ownership and end exclusion depending on the predicate.
 */
template<class BoundaryPredicate>
inline std::optional<ml::tvec2<int>> resolve_endpoint_pixel(
  line_fixed_vec2 point,
  line_fixed_vec2 delta,
  bool x_major,
  BoundaryPredicate&& predicate)
{
    const int base_x = ml::integral_part(point.x);
    const int base_y = ml::integral_part(point.y);
    const ml::tvec2<int> base{base_x, base_y};

    auto test_pixel = [&](ml::tvec2<int> pixel) -> bool
    {
        const auto offset = point_offset_from_pixel_center(point, pixel);
        const auto cls = classify_diamond_point(offset, x_major);

        return cls == diamond_point_class::inside
               || predicate(offset, delta, x_major);
    };

    if(test_pixel(base))
    {
        return base;
    }

    for(int py = base_y - 1; py <= base_y + 1; ++py)
    {
        for(int px = base_x - 1; px <= base_x + 1; ++px)
        {
            ml::tvec2<int> pixel{px, py};
            if(pixel.x == base.x && pixel.y == base.y)
            {
                continue;
            }

            if(test_pixel(pixel))
            {
                return pixel;
            }
        }
    }

    return std::nullopt;
}

/** Determine which pixel, if any, owns the original start endpoint and must be emitted explicitly. */
inline std::optional<ml::tvec2<int>> resolve_start_pixel_ownership(
  line_fixed_vec2 point,
  line_fixed_vec2 delta,
  bool x_major)
{
    return resolve_endpoint_pixel(
      point,
      delta,
      x_major,
      exits_diamond_from_boundary);
}

/** Determine which pixel, if any, owns the original end endpoint and must be excluded from the walk. */
inline std::optional<ml::tvec2<int>> resolve_end_pixel_exclusion(
  line_fixed_vec2 point,
  line_fixed_vec2 delta,
  bool x_major)
{
    return resolve_endpoint_pixel(
      point,
      delta,
      x_major,
      enters_diamond_towards_boundary_end);
}

/**
 * Classify both endpoints of a line segment according to the Direct3D diamond rule.
 *
 * Determines:
 * - an optional start pixel that must be emitted explicitly (if the segment exits its diamond),
 * - an optional end pixel that must be excluded (if the segment enters its diamond).
 *
 * If the segment remains entirely within the owned start diamond, the start emission is suppressed.
 */
inline endpoint_rule_info classify_line_endpoints(
  const geom::vertex& start,
  const geom::vertex& end,
  bool x_major)
{
    endpoint_rule_info out;
    const auto start_point = make_fixed_point(start.coords.xy());
    const auto end_point = make_fixed_point(end.coords.xy());
    const auto delta = line_fixed_vec2{
      end_point.x - start_point.x,
      end_point.y - start_point.y};

    out.start_pixel_to_emit = resolve_start_pixel_ownership(
      start_point,
      delta,
      x_major);

    if(out.start_pixel_to_emit.has_value()
       && segment_stays_within_owned_start_diamond(
         end_point,
         *out.start_pixel_to_emit,
         x_major))
    {
        out.start_pixel_to_emit.reset();
    }

    out.end_pixel_to_exclude = resolve_end_pixel_exclusion(
      end_point,
      delta,
      x_major);

    return out;
}

/**
 * Compute ceil(raw / 2^N), interpreting `raw` as an N-fractional-bit fixed-point value,
 * where N denotes the fractional bits of `line_fixed_t`.
 *
 * This is used to convert from fixed-point edge coordinates to integer pixel indices
 * under half-open rasterization rules.
 */
inline int ceil_raw_to_int(std::int32_t raw)
{
    constexpr std::int32_t scale = 1 << ml::static_number_traits<line_fixed_t>::fractional_bits;
    const std::int32_t q = raw / scale;
    const std::int32_t r = raw % scale;
    return q + (r != 0 && raw > 0);
}

/**
 * Resolve the minor-axis pixel coordinate for a given major-axis sample.
 *
 * Computes the interpolated minor coordinate and applies the diamond-rule tie-breaking:
 * exact ties (sample lies exactly on a pixel boundary) are resolved to the previously
 * owned pixel (half-open convention).
 */
inline int choose_minor_pixel(
  line_fixed_wide_t numer,
  line_fixed_raw_t denom)
{
    const line_fixed_wide_t pixel_denom = line_fixed_wide_t{denom} << ml::static_number_traits<line_fixed_t>::fractional_bits;

    const line_fixed_wide_t q = numer / pixel_denom;
    const line_fixed_wide_t r = numer % pixel_denom;

    const line_fixed_wide_t base = (r != 0 && numer < 0) ? (q - 1) : q;
    const bool is_exact_tie = (r == 0);
    const line_fixed_wide_t pixel = is_exact_tie ? base - 1 : base;

    return static_cast<int>(pixel);
}

/*
 * line info.
 */

/** Precomputed orientation, endpoint ownership, and walk normalization data for a line segment. */
struct line_info
{
    const geom::vertex* v0;
    const geom::vertex* v1;

    float dx;
    float dy;

    float max_absolute_delta;
    bool is_x_major;
    bool swapped;
    endpoint_rule_info endpoint_rules;

    [[nodiscard]] static std::optional<line_info>
      make(const geom::vertex& in_v0, const geom::vertex& in_v1)
    {
        const float dx0 = in_v1.coords.x - in_v0.coords.x;
        const float dy0 = in_v1.coords.y - in_v0.coords.y;
        const float max_abs = std::max(std::abs(dx0), std::abs(dy0));

        if(max_abs == 0.f)
        {
            return std::nullopt;
        }

        line_info out{
          .v0 = &in_v0,
          .v1 = &in_v1,
          .dx = dx0,
          .dy = dy0,
          .max_absolute_delta = max_abs,
          .is_x_major = std::abs(dy0) <= std::abs(dx0),
          .swapped = false,
          .endpoint_rules = {}};

        out.normalize_and_classify();
        return out;
    }

private:
    void normalize_and_classify();
};

/** Finalize line setup by classifying endpoints and optionally swapping endpoints into walk order. */
inline void line_info::normalize_and_classify()
{
    dx = v1->coords.x - v0->coords.x;
    dy = v1->coords.y - v0->coords.y;

    if(max_absolute_delta == 0.0f)
    {
        return;
    }

    endpoint_rules = classify_line_endpoints(*v0, *v1, is_x_major);

    // normalize walking direction.
    swapped = false;
    if(is_x_major)
    {
        if(dx < 0.0f)
        {
            std::swap(v0, v1);
            dx = -dx;
            dy = -dy;
            swapped = true;
        }
    }
    else
    {
        if(dy < 0.0f)
        {
            std::swap(v0, v1);
            dx = -dx;
            dy = -dy;
            swapped = true;
        }
    }
}

/*
 * fragment emission and rasterization.
 */

/** Distinguishes pixels emitted during the walk from pixels emitted as deferred endpoint ownership fixes. */
enum class line_emit_kind
{
    walked_pixel,     /** emission during line walking */
    deferred_endpoint /** emission of deferred endpoint */
};

/** Precomputed integer stepping data for walking a line along its major axis. */
struct line_walk_plan
{
    line_fixed_raw_t p0_raw;
    line_fixed_raw_t v0_raw;
    line_fixed_raw_t dp_raw;
    line_fixed_raw_t dv_raw;
    int p_start;
    int p_end;
    bool is_x_major;
};

/**
 * Build the integer stepping plan for walking the line along its major axis.
 *
 * Converts endpoints to fixed-point raw form and computes:
 * - interpolation parameters (dp, dv),
 * - inclusive major-axis range [p_start, p_end],
 * - optional clipping of the walk range due to endpoint ownership rules.
 *
 * The walk samples pixel centers at half-integer offsets (p + 0.5).
 */
inline line_walk_plan make_line_walk_plan(
  line_fixed_vec2 start,
  line_fixed_vec2 end,
  bool is_x_major,
  std::optional<int> reserved_walk_start_major,
  std::optional<int> reserved_walk_end_major)
{
    const line_fixed_t p0 = is_x_major ? start.x : start.y;
    const line_fixed_t v0 = is_x_major ? start.y : start.x;
    const line_fixed_t p1 = is_x_major ? end.x : end.y;
    const line_fixed_t v1 = is_x_major ? end.y : end.x;
    const line_fixed_raw_t p0_raw = cnl::unwrap(p0);
    const line_fixed_raw_t v0_raw = cnl::unwrap(v0);
    const line_fixed_raw_t p1_raw = cnl::unwrap(p1);
    const line_fixed_raw_t v1_raw = cnl::unwrap(v1);

    line_walk_plan out{
      .p0_raw = p0_raw,
      .v0_raw = v0_raw,
      .dp_raw = p1_raw - p0_raw,
      .dv_raw = v1_raw - v0_raw,
      .p_start = ceil_raw_to_int(p0_raw - cnl::unwrap(half)),
      .p_end = ceil_raw_to_int(p1_raw - cnl::unwrap(half)) - 1,
      .is_x_major = is_x_major};

    if(reserved_walk_start_major.has_value())
    {
        out.p_start = std::max(out.p_start, *reserved_walk_start_major + 1);
    }

    if(reserved_walk_end_major.has_value())
    {
        out.p_end = std::min(out.p_end, *reserved_walk_end_major - 1);
    }

    return out;
}

/**
 * Walk the line across major-axis pixel centers and emit each covered pixel.
 *
 * For each integer major coordinate p in [p_start, p_end], computes the corresponding
 * minor pixel using integer interpolation and tie-breaking, and invokes the callback.
 */
template<class EmitWalkPixel>
void walk_line_pixels(
  const line_walk_plan& plan,
  EmitWalkPixel&& emit_walk_pixel)
{
    for(int p = plan.p_start; p <= plan.p_end; ++p)
    {
        const line_fixed_raw_t p_center_raw =
          cnl::unwrap(line_fixed_t{p} + half);
        const line_fixed_wide_t numer =
          static_cast<line_fixed_wide_t>(plan.v0_raw) * plan.dp_raw
          + static_cast<line_fixed_wide_t>(p_center_raw - plan.p0_raw) * plan.dv_raw;
        const int v_pix = choose_minor_pixel(
          numer,
          plan.dp_raw);
        emit_walk_pixel(p, v_pix);
    }
}

/**
 * Rasterize a line segment using the Direct3D diamond rule.
 *
 * The algorithm:
 * 1. Classifies endpoints and determines ownership adjustments.
 * 2. Normalizes the segment to a monotonic major-axis walk.
 * 3. Emits an optional deferred start pixel.
 * 4. Walks the line and emits covered pixels.
 * 5. Emits an optional deferred end pixel (if not already emitted).
 *
 * The callback receives each pixel and whether it was produced by the walk or by
 * deferred endpoint handling.
 */
template<class EmitFragment>
void rasterize_line_coverage(
  const line_info& info,
  EmitFragment&& emit_fragment)
{
    const auto start = make_fixed_point(info.v0->coords.xy());
    const auto end = make_fixed_point(info.v1->coords.xy());

    std::optional<ml::tvec2<int>> deferred_walk_start_pixel;
    std::optional<ml::tvec2<int>> deferred_walk_end_pixel;
    std::optional<int> reserved_walk_start_major;
    std::optional<int> reserved_walk_end_major;

    if(!info.swapped)
    {
        if(info.endpoint_rules.start_pixel_to_emit.has_value())
        {
            deferred_walk_start_pixel = info.endpoint_rules.start_pixel_to_emit;
            reserved_walk_start_major =
              info.is_x_major
                ? info.endpoint_rules.start_pixel_to_emit->x
                : info.endpoint_rules.start_pixel_to_emit->y;
        }

        if(info.endpoint_rules.end_pixel_to_exclude.has_value())
        {
            reserved_walk_end_major =
              info.is_x_major
                ? info.endpoint_rules.end_pixel_to_exclude->x
                : info.endpoint_rules.end_pixel_to_exclude->y;
        }
    }
    else
    {
        // original start maps to walk end.
        if(info.endpoint_rules.start_pixel_to_emit.has_value())
        {
            deferred_walk_end_pixel = info.endpoint_rules.start_pixel_to_emit;
            reserved_walk_end_major =
              info.is_x_major
                ? info.endpoint_rules.start_pixel_to_emit->x
                : info.endpoint_rules.start_pixel_to_emit->y;
        }

        // original end maps to walk start.
        if(info.endpoint_rules.end_pixel_to_exclude.has_value())
        {
            reserved_walk_start_major =
              info.is_x_major
                ? info.endpoint_rules.end_pixel_to_exclude->x
                : info.endpoint_rules.end_pixel_to_exclude->y;
        }
    }

    std::optional<ml::tvec2<int>> last_emitted_pixel;

    auto emit = [&](int x, int y, line_emit_kind kind)
    {
        emit_fragment(x, y, kind);
        last_emitted_pixel = ml::tvec2<int>(x, y);
    };

    if(deferred_walk_start_pixel.has_value())
    {
        emit(
          deferred_walk_start_pixel->x,
          deferred_walk_start_pixel->y,
          line_emit_kind::deferred_endpoint);
    }

    const auto walk_plan = make_line_walk_plan(
      start,
      end,
      info.is_x_major,
      reserved_walk_start_major,
      reserved_walk_end_major);

    walk_line_pixels(
      walk_plan,
      [&](int p, int v_pix)
      {
          if(info.is_x_major)
          {
              emit(p, v_pix, line_emit_kind::walked_pixel);
          }
          else
          {
              emit(v_pix, p, line_emit_kind::walked_pixel);
          }
      });

    if(deferred_walk_end_pixel.has_value()
       && (!last_emitted_pixel.has_value()
           || last_emitted_pixel->x != deferred_walk_end_pixel->x
           || last_emitted_pixel->y != deferred_walk_end_pixel->y))
    {
        emit(
          deferred_walk_end_pixel->x,
          deferred_walk_end_pixel->y,
          line_emit_kind::deferred_endpoint);
    }
}

}    // namespace rast
