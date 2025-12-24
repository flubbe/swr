/**
 * swr - a software rasterizer
 *
 * support for different pixel formats.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace swr
{

/** Pixel format descriptor. */
struct pixel_format_descriptor
{
    /** pixel format name. */
    pixel_format name{pixel_format::unsupported};

    /** red color bits. */
    uint32_t red_bits{0};

    /** red color bit shift. */
    uint32_t red_shift{0};

    /** green color bits. */
    uint32_t green_bits{0};

    /** green color bit shift. */
    uint32_t green_shift{0};

    /** blue color bits. */
    uint32_t blue_bits{0};

    /** blue color bit shift. */
    uint32_t blue_shift{0};

    /** alpha bits. */
    uint32_t alpha_bits{0};

    /** Alpha shift. */
    uint32_t alpha_shift{0};

    /** default constuctor . */
    pixel_format_descriptor() = default;

    /** initializing constructor. */
    pixel_format_descriptor(
      pixel_format name,
      uint32_t in_red_bits,
      uint32_t in_red_shift,
      uint32_t in_green_bits,
      uint32_t in_green_shift,
      uint32_t in_blue_bits,
      uint32_t in_blue_shift,
      uint32_t in_alpha_bits,
      uint32_t in_alpha_shift)
    : name{name}
    , red_bits(in_red_bits)
    , red_shift(in_red_shift)
    , green_bits(in_green_bits)
    , green_shift(in_green_shift)
    , blue_bits(in_blue_bits)
    , blue_shift(in_blue_shift)
    , alpha_bits(in_alpha_bits)
    , alpha_shift(in_alpha_shift)
    {
    }

    /** return pixel format description of named formats. */
    static pixel_format_descriptor named_format(pixel_format name)
    {
        if(name == pixel_format::argb8888)
        {
            /* {name, red, green, blue, alpha */
            return {name, 8, 16, 8, 8, 8, 0, 8, 24};
        }
        else if(name == pixel_format::bgra8888)
        {
            /* {name, red, green, blue, alpha */
            return {name, 8, 8, 8, 16, 8, 24, 8, 0};
        }
        else if(name == pixel_format::rgba8888)
        {
            /* {name, red, green, blue, alpha */
            return {name, 8, 24, 8, 16, 8, 8, 8, 0};
        }

        // return empty pixel_format_descriptor for unknown formats.
        return {};
    }
};

/** convert between colors and pixels. */
class pixel_format_converter
{
public:
    /** pixel format. */
    pixel_format_descriptor pf;

    /** the maximum representable color per channel, e.g., for rgba8888 it is {255,255,255,255}. */
    ml::vec4 max_per_channel;

    /** color masks. */
    uint32_t red_mask{0}, green_mask{0}, blue_mask{0}, alpha_mask{0};

    /** named pixel formats. */
    pixel_format name{pixel_format::unsupported};

    /** update the helper variables above based on the pixel format. */
    void update()
    {
        // calculate the maximal representable color per channel.
        int max_rgba[4] = {(1 << pf.red_bits) - 1, (1 << pf.green_bits) - 1, (1 << pf.blue_bits) - 1, (1 << pf.alpha_bits) - 1};
        max_per_channel = {static_cast<float>(max_rgba[0]), static_cast<float>(max_rgba[1]), static_cast<float>(max_rgba[2]), static_cast<float>(max_rgba[3])};

        // set color masks.
        red_mask = static_cast<uint32_t>(max_rgba[0] << pf.red_shift);
        green_mask = static_cast<uint32_t>(max_rgba[1] << pf.green_shift);
        blue_mask = static_cast<uint32_t>(max_rgba[2] << pf.blue_shift);
        alpha_mask = static_cast<uint32_t>(max_rgba[3] << pf.alpha_shift);

        // get pixel format name.
        name = pf.name;
    }

    /** default constructor. */
    pixel_format_converter() = default;

    /** constructor. */
    pixel_format_converter(const pixel_format_descriptor& in_pf)
    : pf{in_pf}
    {
        update();
    }

    /** set a new pixel format. */
    void set_pixel_format(const pixel_format_descriptor& in_pf)
    {
        pf = in_pf;
        update();
    }

    /** get name for pixel format. */
    pixel_format get_name() const
    {
        return name;
    }

    /** convert to pixel format. */
    uint32_t to_pixel(const ml::vec4 color) const
    {
        const ml::vec4 scaled_color{color * max_per_channel};
        uint8_t r{static_cast<uint8_t>(scaled_color.r)};
        uint8_t g{static_cast<uint8_t>(scaled_color.g)};
        uint8_t b{static_cast<uint8_t>(scaled_color.b)};
        uint8_t a{static_cast<uint8_t>(scaled_color.a)};
        return (r << pf.red_shift) | (g << pf.green_shift) | (b << pf.blue_shift) | (a << pf.alpha_shift);
    }

    /** convert to color. */
    const ml::vec4 to_color(uint32_t pixel) const
    {
        uint32_t r{(pixel & red_mask) >> pf.red_shift};
        uint32_t g{(pixel & green_mask) >> pf.green_shift};
        uint32_t b{(pixel & blue_mask) >> pf.blue_shift};
        uint32_t a{(pixel & alpha_mask) >> pf.alpha_shift};
        return ml::vec4{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a)} / max_per_channel;
    }
};

} /* namespace swr */
