/**
 * swr - a software rasterizer
 *
 * texture loading.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2025
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <optional>
#include <string_view>

#include "stb_image.h"

#include "swr/swr.h"
#include "common/utils.h"

namespace utils
{

/**
 * Load a square texture from a file. The texture has to have power-of-two dimensions.
 *
 * @param filename The texture filename.
 * @returns Returns the texture id on success. Returns `std::nullopt` on failure.
 *          Call `swr::GetLastError` for further error information.
 */
inline std::optional<std::uint32_t> load_uniform(
  std::string_view filename)
{
    std::string filename_str{filename};    // copy to get c-string.

    int w = 0, h = 0, comp = 0;
    unsigned char* image_data =
      stbi_load(
        filename_str.c_str(),
        &w, &h, &comp,
        STBI_rgb_alpha);
    if(!image_data)
    {
        return std::nullopt;
    }

    // image size: width*height*sizeof(RGBA).
    std::size_t image_size = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * sizeof(std::uint32_t);
    std::vector<std::uint8_t> image_vec{image_data, image_data + image_size};

    stbi_image_free(image_data);
    image_data = nullptr;

    auto texture_id = swr::CreateTexture();
    if(texture_id == 0)
    {
        return std::nullopt;
    }

    swr::SetImage(
      texture_id,
      0,
      w,
      h,
      swr::pixel_format::rgba8888,
      image_vec);

    return std::make_optional(texture_id);
}

/**
 * Load an image into a texture from a file. Sets the wrap mode to `repeat`.
 *
 * The image is loaded into a texture with power-of-two dimensions. These
 * dimensions are returned in `w` and `h`.
 *
 * @param filename The texture filename.
 * @param w Output: Generated texture width in pixels.
 * @param h Output: Generated texture height in pixels.
 * @param max_u Output: u coordinate corresponding to the texture's width.
 * @param max_u Output: v coordinate corresponding to the texture's height.
 * @returns Returns the texture id on success. Returns `std::nullopt` on failure.
 *          Call `swr::GetLastError` for further error information.
 */
inline std::optional<std::uint32_t> load_non_uniform(
  std::string_view filename,
  int* w = nullptr,
  int* h = nullptr,
  float* max_u = nullptr,
  float* max_v = nullptr)
{
    std::string filename_str{filename};    // copy to get c-string.

    int img_w = 0, img_h = 0, img_c = 0;
    unsigned char* image_data =
      stbi_load(
        filename_str.c_str(),
        &img_w, &img_h, &img_c,
        STBI_default);
    if(!image_data)
    {
        return std::nullopt;
    }

    auto target_w = utils::round_to_next_power_of_two(static_cast<std::uint32_t>(img_w));
    auto target_h = utils::round_to_next_power_of_two(static_cast<std::uint32_t>(img_h));

    std::vector<std::uint8_t> resized_tex;
    resized_tex.resize(target_w * target_h * sizeof(std::uint32_t)); /* sizeof(...) for RGBA */

    // copy texture.
    for(int j = 0; j < img_h; ++j)
    {
        for(int i = 0; i < img_w; ++i)
        {
            const auto to_index = (j * target_w + i) * sizeof(std::uint32_t);
            const auto from_index = (j * img_w + i) * sizeof(std::uint32_t);
            *reinterpret_cast<std::uint32_t*>(&resized_tex[to_index]) =
              *reinterpret_cast<const std::uint32_t*>(&image_data[from_index]);
        }
    }

    stbi_image_free(image_data);
    image_data = nullptr;

    auto texture_id = swr::CreateTexture();
    if(texture_id == 0)
    {
        return std::nullopt;
    }

    swr::SetImage(
      texture_id,
      0,
      target_w, target_h,
      swr::pixel_format::rgba8888,
      resized_tex);
    swr::SetTextureWrapMode(
      texture_id,
      swr::wrap_mode::repeat,
      swr::wrap_mode::repeat);

    if(w)
    {
        *w = target_w;
    }
    if(h)
    {
        *h = target_h;
    }

    if(max_u)
    {
        *max_u = (target_w != 0) ? static_cast<float>(img_w) / static_cast<float>(target_w) : 0;
    }
    if(max_v)
    {
        *max_v = (target_h != 0) ? static_cast<float>(img_h) / static_cast<float>(target_h) : 0;
    }

    return std::make_optional(texture_id);
}

}    // namespace utils
