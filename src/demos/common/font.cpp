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
#include "common/platform/platform.h"

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
    // store renderer state
    bool bDepthTest = swr::GetState(swr::state::depth_test);
    bool bCulling = swr::GetState(swr::state::cull_face);
    swr::polygon_mode PolygonMode = swr::GetPolygonMode();
    bool bBlend = swr::GetState(swr::state::blend);

    // set renderer state
    swr::SetState(swr::state::depth_test, false);
    swr::SetState(swr::state::cull_face, false);
    swr::SetPolygonMode(swr::polygon_mode::fill);
    swr::SetState(swr::state::blend, true);
    swr::SetBlendFunc(swr::blend_func::src_alpha, swr::blend_func::one_minus_src_alpha);

    // render string.
    swr::BindShader(shader_id);
    swr::BindTexture(swr::texture_target::texture_2d, font.tex_id);
    uint32_t cur_x = x;

    float inv_w = 1.f / static_cast<float>(font.tex_width);
    float inv_h = 1.f / static_cast<float>(font.tex_height);
    const ml::vec4 inv = {inv_w, inv_h, inv_w, inv_h};

    vb.clear();
    tc.clear();

    vb.reserve(4 * s.size());
    tc.reserve(4 * s.size());
    ib.reserve(6 * s.size());

    for(auto& it: s)
    {
        const glyph& cur_glyph = font.font_glyphs[static_cast<std::uint8_t>(it)];

        // calculate correct texture coordinates.
        const ml::vec4 tex_coords = ml::vec4{
                                      static_cast<float>(cur_glyph.get_x()),
                                      static_cast<float>(cur_glyph.get_y()),
                                      static_cast<float>(cur_glyph.get_width()),
                                      static_cast<float>(cur_glyph.get_height())}
                                    * inv;

        tc.emplace_back(tex_coords.x, tex_coords.y, 0, 0);
        tc.emplace_back(tex_coords.x, tex_coords.y + tex_coords.w, 0, 0);
        tc.emplace_back(tex_coords.x + tex_coords.z, tex_coords.y + tex_coords.w, 0, 0);
        tc.emplace_back(tex_coords.x + tex_coords.z, tex_coords.y, 0, 0);

        vb.emplace_back(ml::vec4{static_cast<float>(cur_x), static_cast<float>(y), 1.f, 1.f});
        vb.emplace_back(ml::vec4{static_cast<float>(cur_x), static_cast<float>(y + cur_glyph.get_height()), 1.f, 1.f});
        vb.emplace_back(ml::vec4{static_cast<float>(cur_x + cur_glyph.get_width()), static_cast<float>(y + cur_glyph.get_height()), 1.f, 1.f});
        vb.emplace_back(ml::vec4{static_cast<float>(cur_x + cur_glyph.get_width()), static_cast<float>(y), 1.f, 1.f});

        // advance x position.
        cur_x += cur_glyph.get_width();
    }

    // update index buffer if necessary.
    std::size_t quad_start_index = 2 * ib.size() / 3;
    while(ib.size() < 6 * s.size())
    {
        ib.emplace_back(quad_start_index);
        ib.emplace_back(quad_start_index + 1);
        ib.emplace_back(quad_start_index + 3);

        ib.emplace_back(quad_start_index + 1);
        ib.emplace_back(quad_start_index + 2);
        ib.emplace_back(quad_start_index + 3);

        quad_start_index += 4;
    }

    swr::UpdateAttributeBuffer(text_vertex_buffer, vb);
    swr::UpdateAttributeBuffer(text_texcoord_buffer, tc);

    swr::EnableAttributeBuffer(text_vertex_buffer, 0);
    swr::EnableAttributeBuffer(text_texcoord_buffer, 1);
    swr::DrawIndexedElements(swr::vertex_buffer_mode::triangles, 6 * s.size(), ib);
    swr::DisableAttributeBuffer(text_texcoord_buffer);
    swr::DisableAttributeBuffer(text_vertex_buffer);

    // restore render states.
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
