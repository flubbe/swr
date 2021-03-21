/**
 * swr - a software rasterizer
 * 
 * bitmap ascii font support.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers */
#include "../common/platform/platform.h"

#include "swr/swr.h"
#include "swr/shaders.h"

#include "font.h"

namespace font
{

/*
 * extended ascii fonts.
 */

extended_ascii_bitmap_font extended_ascii_bitmap_font::create_uniform_font(uint32_t in_texture_id, uint32_t in_tex_width, uint32_t in_tex_height, uint32_t in_font_map_width, uint32_t in_font_map_height, uint32_t glyph_width, uint32_t glyph_height)
{
    // create ascii font.
    extended_ascii_bitmap_font font;

    // upload texture to graphics driver.
    font.tex_id = in_texture_id;

    font.font_map_width = in_font_map_width;
    font.font_map_height = in_font_map_height;
    font.tex_width = in_tex_width;
    font.tex_height = in_tex_height;

    // create glyphs for each ASCII character.
    uint32_t chars_x = in_font_map_width / glyph_width;
    uint32_t chars_y = in_font_map_height / glyph_height;

    if(chars_x * chars_y != 256)
    {
        platform::logf("Invalid character count for font: {} characters", chars_x * chars_y);
        platform::logf("skipping loading of glyphs.");
        return font;
    }

    for(uint32_t j = 0; j < chars_y; ++j)
    {
        for(uint32_t i = 0; i < chars_x; ++i)
        {
            // calculate the glyph's position in the texture.
            uint32_t x = i * glyph_width;
            uint32_t y = j * glyph_height;

            // do not add this glyph if its dimensions are outside the texture.
            if(x + glyph_width > in_font_map_width || y + glyph_height > in_font_map_height)
            {
                platform::logf("Invalid glyph dimensions for ASCII {}", j * 8 + i);
                platform::logf("Glyph dimensions: {}, {}, {}, {}", x, y, glyph_width, glyph_height);
                platform::logf("Font texture dimensions: {}, {}", in_font_map_width, in_font_map_height);
                continue;
            }

            // add glyph.
            font.font_glyphs[j * chars_x + i] = {x, y, glyph_width, glyph_height};
        }
    }

    return font;
}

void extended_ascii_bitmap_font::get_string_dimensions(const std::string& s, uint32_t& w, uint32_t& h) const
{
    w = 0;
    h = 0;
    for(auto it: s)
    {
        w += font_glyphs[static_cast<int>(it)].get_width();
        h = std::max(h, font_glyphs[static_cast<int>(it)].get_height());
    }
}

/*
 * font rendering.
 */

void renderer::draw_string_at(const std::string& s, uint32_t x, uint32_t y) const
{
    // set up the render states for font rendering.
    bool bDepthTest = swr::GetState(swr::state::depth_test);
    swr::SetState(swr::state::depth_test, false);

    bool bCulling = swr::GetState(swr::state::cull_face);
    swr::SetState(swr::state::cull_face, false);

    swr::polygon_mode PolygonMode = swr::GetPolygonMode();
    swr::SetPolygonMode(swr::polygon_mode::fill);

    bool bBlend = swr::GetState(swr::state::blend);
    swr::SetState(swr::state::blend, true);
    swr::SetBlendFunc(swr::blend_func::src_alpha, swr::blend_func::one_minus_src_alpha);

    // render string.
    swr::BindShader(shader_id);
    swr::BindUniform(2, static_cast<int>(font.tex_id));
    uint32_t cur_x = x;

    for(auto& it: s)
    {
        const glyph& cur_glyph = font.font_glyphs[static_cast<uint8_t>(it)];

        // calculate correct texture coordinates.
        float tex_x = static_cast<float>(cur_glyph.get_x()) / static_cast<float>(font.tex_width);
        float tex_y = static_cast<float>(cur_glyph.get_y()) / static_cast<float>(font.tex_height);
        float tex_w = static_cast<float>(cur_glyph.get_width()) / static_cast<float>(font.tex_width);
        float tex_h = static_cast<float>(cur_glyph.get_height()) / static_cast<float>(font.tex_height);

        // draw corresponding character and advance cursor.
        swr::BeginPrimitives(swr::vertex_buffer_mode::quads);

        swr::SetColor(1, 1, 1, 1);

        swr::SetTexCoord(tex_x, tex_y);
        swr::InsertVertex(cur_x, y, 1, 1);

        swr::SetTexCoord(tex_x, tex_y + tex_h);
        swr::InsertVertex(cur_x, y + cur_glyph.get_height(), 1, 1);

        swr::SetTexCoord(tex_x + tex_w, tex_y + tex_h);
        swr::InsertVertex(cur_x + cur_glyph.get_width(), y + cur_glyph.get_height(), 1, 1);

        swr::SetTexCoord(tex_x + tex_w, tex_y);
        swr::InsertVertex(cur_x + cur_glyph.get_width(), y, 1, 1);

        swr::EndPrimitives();

        // advance x position.
        cur_x += cur_glyph.get_width();
    }

    // reset render states.
    swr::BindShader(0);

    swr::SetState(swr::state::blend, bBlend);
    swr::SetPolygonMode(PolygonMode);
    swr::SetState(swr::state::cull_face, bCulling);
    swr::SetState(swr::state::depth_test, bDepthTest);
}

void renderer::draw_string(unsigned int alignment, const std::string& s, uint32_t x, uint32_t y) const
{
    uint32_t w{0}, h{0};
    font.get_string_dimensions(s, w, h);

    if((alignment & string_alignment::center_horz) == string_alignment::center_horz)
    {
        x = (viewport_width - w) / 2;
    }
    else if((alignment & string_alignment::left) == string_alignment::left)
    {
        x = 0;
    }
    else if((alignment & string_alignment::right) == string_alignment::right)
    {
        x = viewport_width - w;
    }

    if((alignment & string_alignment::center_vert) == string_alignment::center_vert)
    {
        y = (viewport_height - h) / 2;
    }
    else if((alignment & string_alignment::top) == string_alignment::top)
    {
        y = 0;
    }
    else if((alignment & string_alignment::bottom) == string_alignment::bottom)
    {
        y = viewport_height - h;
    }

    draw_string_at(s, x, y);
}

} /* namespace font */
