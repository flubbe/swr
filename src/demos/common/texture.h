/**
 * swr - a software rasterizer
 *
 * texture loading.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include "swr/swr.h"
#include "common/utils.h"

namespace utils
{

/**
 * Create a possibly non-uniform texture, with dimensions possibly not being powers of two.
 * Data is RGBA with 8 bits per channel. The largest valid texture coordinates are written to max_u and max_v.
 *
 * @param w Width of the texture.
 * @param h Height of the texture.
 * @param data Image data (RGBA8888).
 * @param max_u Output: u coordinate corresponding to the texture's width.
 * @param max_u Output: v coordinate corresponding to the texture's height.
 * @return Returns the texture id.
 */
inline std::uint32_t create_non_uniform_texture(
  std::uint32_t w,
  std::uint32_t h,
  const std::vector<std::uint8_t>& data,
  float* max_u = nullptr,
  float* max_v = nullptr)
{
    int adjusted_w = utils::round_to_next_power_of_two(w);
    int adjusted_h = utils::round_to_next_power_of_two(h);

    std::vector<std::uint8_t> resized_tex;
    resized_tex.resize(adjusted_w * adjusted_h * sizeof(std::uint32_t)); /* sizeof(...) for RGBA */

    // copy texture.
    for(std::uint32_t j = 0; j < h; ++j)
    {
        for(std::uint32_t i = 0; i < w; ++i)
        {
            const auto to_index = (j * adjusted_w + i) * sizeof(std::uint32_t);
            const auto from_index = (j * w + i) * sizeof(std::uint32_t);
            *reinterpret_cast<std::uint32_t*>(&resized_tex[to_index]) =
              *reinterpret_cast<const std::uint32_t*>(&data[from_index]);
        }
    }

    auto tex_id = swr::CreateTexture();
    swr::SetImage(
      tex_id,
      0,
      adjusted_w,
      adjusted_h,
      swr::pixel_format::rgba8888,
      resized_tex);
    swr::SetTextureWrapMode(
      tex_id,
      swr::wrap_mode::repeat,
      swr::wrap_mode::repeat);

    if(tex_id)
    {
        if(max_u)
        {
            *max_u = (adjusted_w != 0) ? static_cast<float>(w) / static_cast<float>(adjusted_w) : 0;
        }
        if(max_v)
        {
            *max_v = (adjusted_h != 0) ? static_cast<float>(h) / static_cast<float>(adjusted_h) : 0;
        }
    }

    return tex_id;
}

}    // namespace utils
