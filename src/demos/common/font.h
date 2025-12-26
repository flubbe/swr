/**
 * swr - a software rasterizer
 *
 * bitmap ascii font support.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace font
{

/** A glyph which is stored inside a texture and can be accessed by knowning x/y coordinates and width/height. */
class glyph
{
    /** x and y positions in bitmap. */
    std::uint32_t x{0}, y{0};

    /** glyph width and height. */
    std::uint32_t width{0}, height{0};

public:
    /** default constructor. */
    glyph() = default;

    /** initializing constructor. */
    glyph(std::uint32_t in_x, std::uint32_t in_y, std::uint32_t in_width, std::uint32_t in_height)
    : x{in_x}
    , y{in_y}
    , width{in_width}
    , height{in_height}
    {
    }

    /**
     * getters.
     */

    std::uint32_t get_x() const
    {
        return x;
    }

    std::uint32_t get_y() const
    {
        return y;
    }

    std::uint32_t get_width() const
    {
        return width;
    }

    std::uint32_t get_height() const
    {
        return height;
    }
};

/** extended ascii bitmap font. */
class extended_ascii_bitmap_font
{
    friend class renderer;

    /** font texture id. */
    std::uint32_t tex_id{0};

    /** font map dimension. */
    std::uint32_t font_map_width{0}, font_map_height{0};

    /** font texture dimensions. */
    std::uint32_t tex_width{0}, tex_height{0};

    /** glyph list. */
    glyph font_glyphs[256];

public:
    static extended_ascii_bitmap_font create_uniform_font(std::uint32_t in_texture_id, std::uint32_t in_tex_width, std::uint32_t in_tex_height, std::uint32_t in_font_map_width, std::uint32_t in_font_map_height, std::uint32_t glyph_width, std::uint32_t glyph_height);

    /** get string dimensions. */
    void get_string_dimensions(const std::string& s, std::uint32_t& w, std::uint32_t& h) const;
};

/** font rendering. */
class renderer
{
    /** scratch vertex and tex coord buffer. */
    mutable std::vector<ml::vec4> vb, tc;

    /** scratch index buffer. */
    mutable std::vector<std::uint32_t> ib;

    /** index buffer id. */
    std::uint32_t text_index_buffer{static_cast<std::uint32_t>(-1)};

    /** vertex buffer id. */
    std::uint32_t text_vertex_buffer{static_cast<std::uint32_t>(-1)};

    /** attributes buffer id. */
    std::uint32_t text_texcoord_buffer{static_cast<std::uint32_t>(-1)};

    /** the shader used for font rendering. */
    std::uint32_t shader_id{0};

    /** font description. */
    extended_ascii_bitmap_font font;

    /** viewport width and height for string positioning. */
    int viewport_width{0}, viewport_height{0};

public:
    /** default constructor. */
    renderer() = default;

    /**
     * Initialize the font renderer.
     */
    void initialize(std::uint32_t in_shader_id, const extended_ascii_bitmap_font& in_font, int in_viewport_width, int in_viewport_height)
    {
        text_index_buffer = swr::CreateIndexBuffer({});
        text_vertex_buffer = swr::CreateAttributeBuffer({});
        text_texcoord_buffer = swr::CreateAttributeBuffer({});

        shader_id = in_shader_id;
        font = in_font;
        viewport_width = in_viewport_width;
        viewport_height = in_viewport_height;
    }

    /**
     * Shut down the font renderer.
     */
    void shutdown()
    {
        if(text_texcoord_buffer != static_cast<std::uint32_t>(-1))
        {
            swr::DeleteAttributeBuffer(text_texcoord_buffer);
            text_texcoord_buffer = static_cast<std::uint32_t>(-1);
        }
        if(text_vertex_buffer != static_cast<std::uint32_t>(-1))
        {
            swr::DeleteAttributeBuffer(text_vertex_buffer);
            text_vertex_buffer = static_cast<std::uint32_t>(-1);
        }
        if(text_index_buffer != static_cast<std::uint32_t>(-1))
        {
            swr::DeleteIndexBuffer(text_index_buffer);
            text_index_buffer = static_cast<std::uint32_t>(-1);
        }
    }

    /** draw a string at position (x,y). */
    void draw_string_at(const std::string& s, std::uint32_t x, std::uint32_t y) const;

    /** string alignment */
    enum string_alignment
    {
        none = 0,                          /** draw string at the specified (x,y) coordinates. */
        left = 1,                          /** ignore x and start at x=0. */
        right = 2,                         /** ignore x and align the string end with the right side of the viewport. */
        top = 4,                           /** ignore y and start at the top. */
        bottom = 8,                        /** ignore y and draw string at the bottom-most position. */
        center_horz = left | right,        /** ignore x and center horizontally. */
        center_vert = top | bottom,        /** ignore y and center vertically. */
        center = center_horz | center_vert /** ignore x and y and center horizontally and vertically. */
    };

    /** draw a string. */
    void draw_string(unsigned int alignment, const std::string& s, std::uint32_t x = 0, std::uint32_t y = 0) const;
};

} /* namespace font */