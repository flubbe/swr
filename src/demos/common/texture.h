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

/* image loading */
#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wdouble-promotion"
#    pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif

#include "stb_image.h"

#if defined(__clang__) || defined(__GNUC__)
#    pragma GCC diagnostic pop
#endif

#include "swr/swr.h"
#include "common/utils.h"

namespace utils
{

/**
 * Load a square texture from a file. The texture has to have power-of-two
 * dimensions.
 *
 * @param filename The texture filename.
 * @param downsample Number of times to halve the texture dimensions.
 *     0 keeps the original size, 1 produces half resolution,
 *     2 produces quarter resolution, etc.
 * @param linear_interpolation Whether to use bilinear interpolation when
 *     downsampling. If false, nearest-neighbor sampling is used.
 * @returns Returns the texture id on success. Returns `std::nullopt` on failure.
 *     Call `swr::GetLastError` for further error information.
 */
inline std::optional<std::uint32_t> load_uniform(
  std::string_view filename,
  unsigned int downsample = 0,
  bool linear_interpolation = true)
{
    std::string filename_str{filename};    // copy to get c-string.

    int w = 0, h = 0, comp = 0;
    unsigned char* image_data =
      stbi_load(
        filename_str.c_str(),
        &w,
        &h,
        &comp,
        STBI_rgb_alpha);
    if(!image_data)
    {
        return std::nullopt;
    }

    // The source is required to be square and power-of-two.
    const bool valid_dimension =
      w > 0
      && w == h
      && (static_cast<unsigned int>(w) & (static_cast<unsigned int>(w) - 1)) == 0;

    if(!valid_dimension)
    {
        stbi_image_free(image_data);
        return std::nullopt;
    }

    const unsigned int source_size = static_cast<unsigned int>(w);

    // A power-of-two texture can only be halved log2(size) times before
    // reaching a 1x1 texture.
    unsigned int max_downsample = 0;
    for(unsigned int size = source_size; size > 1; size >>= 1)
    {
        ++max_downsample;
    }

    if(downsample > max_downsample)
    {
        stbi_image_free(image_data);
        return std::nullopt;
    }

    const unsigned int size = source_size >> downsample;

    const std::size_t source_stride =
      static_cast<std::size_t>(source_size) * 4;
    const std::size_t image_size =
      static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * sizeof(std::uint32_t);

    std::vector<std::uint8_t> image_vec(image_size);

    if(downsample == 0)
    {
        std::copy(
          image_data,
          image_data + image_size,
          image_vec.begin());
    }
    else
    {
        const float scale =
          static_cast<float>(source_size) / static_cast<float>(size);

        auto sample = [&](float x, float y, unsigned int channel)
        {
            if(!linear_interpolation)
            {
                const auto sx = std::min(
                  source_size - 1,
                  static_cast<unsigned int>(x + 0.5f));
                const auto sy = std::min(
                  source_size - 1,
                  static_cast<unsigned int>(y + 0.5f));

                return static_cast<float>(
                  image_data[static_cast<std::size_t>(sy) * source_stride + static_cast<std::size_t>(sx) * 4 + channel]);
            }

            // Clamp to the source image.
            x = std::clamp(x, 0.0f, static_cast<float>(source_size - 1));
            y = std::clamp(y, 0.0f, static_cast<float>(source_size - 1));

            const auto x0 = static_cast<unsigned int>(x);
            const auto y0 = static_cast<unsigned int>(y);
            const auto x1 = std::min(x0 + 1, source_size - 1);
            const auto y1 = std::min(y0 + 1, source_size - 1);

            const float tx = x - static_cast<float>(x0);
            const float ty = y - static_cast<float>(y0);

            const auto pixel = [&](unsigned int px, unsigned int py)
            {
                return static_cast<float>(
                  image_data[static_cast<std::size_t>(py) * source_stride + static_cast<std::size_t>(px) * 4 + channel]);
            };

            const float top =
              pixel(x0, y0) * (1.0f - tx) + pixel(x1, y0) * tx;

            const float bottom =
              pixel(x0, y1) * (1.0f - tx) + pixel(x1, y1) * tx;

            return top * (1.0f - ty) + bottom * ty;
        };

        for(unsigned int y = 0; y < size; ++y)
        {
            for(unsigned int x = 0; x < size; ++x)
            {
                // Map the destination pixel center into source space.
                const float source_x =
                  (static_cast<float>(x) + 0.5f) * scale - 0.5f;
                const float source_y =
                  (static_cast<float>(y) + 0.5f) * scale - 0.5f;

                auto* dst =
                  image_vec.data() + (static_cast<std::size_t>(y) * size + x) * 4;

                for(unsigned int channel = 0; channel < 4; ++channel)
                {
                    dst[channel] = static_cast<std::uint8_t>(
                      std::clamp(
                        sample(source_x, source_y, channel),
                        0.0f,
                        255.0f));
                }
            }
        }
    }

    stbi_image_free(image_data);

    auto texture_id = swr::CreateTexture();
    if(texture_id == 0)
    {
        return std::nullopt;
    }

    swr::SetImage(
      texture_id,
      0,
      static_cast<int>(size),
      static_cast<int>(size),
      swr::pixel_format::rgba8888,
      image_vec);

    if(swr::GetLastError() != swr::error::none)
    {
        swr::ReleaseTexture(texture_id);
        return std::nullopt;
    }

    return std::make_optional(texture_id);
}

/**
 * Load a normal map texture from a file, applying OpenGL-style Y-flip by inverting
 * the green channel. This ensures correct tangent-space orientation after the global
 * texture V-flip.
 *
 * @param filename The normal map filename.
 * @returns Returns the texture id on success. Returns `std::nullopt` on failure.
 *          Call `swr::GetLastError` for further error information.
 */
inline std::optional<std::uint32_t> load_normal_map_uniform(
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

    // Flip green channel (Y in tangent space) for OpenGL-style normal maps.
    std::size_t image_size = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * sizeof(std::uint32_t);
    for(std::size_t i = 1; i < image_size; i += 4)    // i=1 is the green channel in RGBA
    {
        image_data[i] = 255 - image_data[i];
    }

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
    if(swr::GetLastError() != swr::error::none)
    {
        swr::ReleaseTexture(texture_id);
        return std::nullopt;
    }

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
    if(swr::GetLastError() != swr::error::none)
    {
        swr::ReleaseTexture(texture_id);
        return std::nullopt;
    }

    swr::SetTextureWrapMode(
      texture_id,
      swr::wrap_mode::repeat,
      swr::wrap_mode::repeat);
    if(swr::GetLastError() != swr::error::none)
    {
        swr::ReleaseTexture(texture_id);
        return std::nullopt;
    }

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
