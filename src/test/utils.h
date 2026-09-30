/**
 * swr - a software rasterizer
 *
 * Test utilities.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <ostream>
#include <tuple>

#include "swr/swr.h"
#include "rasterizer/early_depth_policy.h"
#include "rasterizer/tile_cache.h"

/*
 * Output helpers for std.
 */

namespace std
{

template<typename... Args>
std::ostream& operator<<(
  std::ostream& os,
  const std::tuple<Args...>& t)
{
    os << "(";
    std::apply(
      [&os](const auto&... args)
      {
          std::size_t n = 0;
          ((os << args << (++n < sizeof...(Args) ? ", " : "")), ...);
      },
      t);
    return os << ")";
}

}    // namespace std

/*
 * Output helpers for ml.
 */

namespace ml
{

template<typename T>
inline std::ostream& operator<<(
  std::ostream& os,
  const tvec2<T>& v)
{
    return os << "(" << v.x << ", " << v.y << ")";
}

}    // namespace ml

/*
 * Output helpers for swr.
 */

namespace swr
{

inline std::ostream& operator<<(
  std::ostream& os,
  comparison_func func)
{
    switch(func)
    {
    case comparison_func::pass: return os << "comparison_func::pass";
    case comparison_func::fail: return os << "comparison_func::fail";
    case comparison_func::equal: return os << "comparison_func::equal";
    case comparison_func::not_equal: return os << "comparison_func::not_equal";
    case comparison_func::less: return os << "comparison_func::less";
    case comparison_func::less_equal: return os << "comparison_func::less_equal";
    case comparison_func::greater: return os << "comparison_func::greater";
    case comparison_func::greater_equal: return os << "comparison_func::greater_equal";
    }

    return os << "comparison_func::<" << static_cast<int>(func) << ">";
}

inline std::ostream& operator<<(
  std::ostream& os,
  error e)
{
    switch(e)
    {
    case error::none: return os << "error::none";
    case error::invalid_value: return os << "error::invalid_value";
    case error::invalid_operation: return os << "error::invalid_operation";
    case error::unimplemented: return os << "error::unimplemented";
    }

    return os << "error::<" << static_cast<int>(e) << ">";
}

inline std::ostream& operator<<(
  std::ostream& os,
  rasterizer_feature_mode mode)
{
    switch(mode)
    {
    case rasterizer_feature_mode::automatic: return os << "rasterizer_feature_mode::automatic";
    case rasterizer_feature_mode::on: return os << "rasterizer_feature_mode::on";
    case rasterizer_feature_mode::off: return os << "rasterizer_feature_mode::off";
    }

    return os << "rasterizer_feature_mode::<" << static_cast<int>(mode) << ">";
}

inline std::ostream& operator<<(
  std::ostream& os,
  texture_compare_mode mode)
{
    switch(mode)
    {
    case texture_compare_mode::none: return os << "texture_compare_mode::none";
    case texture_compare_mode::ref_to_texture: return os << "texture_compare_mode::ref_to_texture";
    }

    return os << "texture_compare_mode::<" << static_cast<int>(mode) << ">";
}

namespace impl
{

inline std::ostream& operator<<(
  std::ostream& os,
  clear_kind kind)
{
    switch(kind)
    {
    case clear_kind::color: return os << "clear_kind::color";
    case clear_kind::depth: return os << "clear_kind::depth";
    }

    return os << "clear_kind::<" << static_cast<int>(kind) << ">";
}

}    // namespace impl

}    // namespace swr

/*
 * Output helpers for rast.
 */

namespace rast
{

inline std::ostream& operator<<(
  std::ostream& os,
  tile_info::rasterization_mode mode)
{
    switch(mode)
    {
    case tile_info::rasterization_mode::block: return os << "rasterization_mode::block";
    case tile_info::rasterization_mode::checked: return os << "rasterization_mode::checked";
    case tile_info::rasterization_mode::thin_x_major: return os << "raterization_mode::thin_x_major";
    case tile_info::rasterization_mode::thin_y_major: return os << "raterization_mode::thin_y_major";
    case tile_info::rasterization_mode::small_checked: return os << "rasterization_mode::small_checked";
    case tile_info::rasterization_mode::sparse_checked: return os << "rasterization_mode::sparse_checked";
    }

    return os << "rasterization_mode::<" << static_cast<int>(mode) << ">";
}

inline std::ostream& operator<<(
  std::ostream& os,
  early_fragment_depth_test_auto_action e)
{
    switch(e)
    {
    case early_fragment_depth_test_auto_action::disabled:
        return os << "early_fragment_depth_test_auto_action::disabled";
    case early_fragment_depth_test_auto_action::enabled_collect:
        return os << "early_fragment_depth_test_auto_action::enabled_collect";
    case early_fragment_depth_test_auto_action::enabled_fast:
        return os << "early_fragment_depth_test_auto_action::enabled_fast";
    }

    return os << "early_fragment_depth_test_auto_action::<" << static_cast<int>(e) << ">";
}

}    // namespace rast