/**
 * swr - a software rasterizer
 *
 * software renderer demonstration (bitmap font).
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <format>

/* boost */
#include <boost/container/static_vector.hpp>

/* software rasterizer headers. */
#include "swr/swr.h"
#include "swr/shaders.h"

/* shaders for this demo. */
#include "shader.h"

/* bitmap font support. */
#include "../common/font.h"

/* application framework. */
#include "swr_app/framework.h"

/* logging. */
#include "../common/platform/platform.h"

/* png loading. */
#include "lodepng.h"

/** demo title. */
const auto demo_title = "Bitmap Font";

/**
 * get the next power of two of a 32-bit unsigned integer.
 *
 * source: https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
 */
static uint32_t next_power_of_two(uint32_t n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/**
 * load textures, with dimensions possibly not being powers of two. data is RGBA with 8 bits per channel.
 * the largest valid texture coordinates are written to max_u and max_v.
 */
static uint32_t load_texture(uint32_t w, uint32_t h, const std::vector<uint8_t>& data, float* max_u = nullptr, float* max_v = nullptr)
{
    int adjusted_w = next_power_of_two(w);
    int adjusted_h = next_power_of_two(h);

    std::vector<uint8_t> resized_tex;
    resized_tex.resize(adjusted_w * adjusted_h * sizeof(uint32_t)); /* sizeof(...) for RGBA */

    // copy texture.
    for(uint32_t j = 0; j < h; ++j)
    {
        for(uint32_t i = 0; i < w; ++i)
        {
            *reinterpret_cast<uint32_t*>(&resized_tex[(j * adjusted_w + i) * sizeof(uint32_t)]) = *reinterpret_cast<const uint32_t*>(&data[(j * w + i) * sizeof(uint32_t)]);
        }
    }

    auto tex_id = swr::CreateTexture();
    swr::SetImage(tex_id, 0, adjusted_w, adjusted_h, swr::pixel_format::rgba8888, resized_tex);
    swr::SetTextureWrapMode(tex_id, swr::wrap_mode::repeat, swr::wrap_mode::repeat);
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

/** demo window. */
class demo_bitmap_font : public swr_app::renderwindow
{
    /** font shader */
    shader::im_texture font_shader;

    /** cube shader */
    shader::color cube_shader;

    /** font shader id. */
    uint32_t font_shader_id{0};

    /** cube shader id. */
    uint32_t cube_shader_id{0};

    /** font texture id. */
    uint32_t font_tex_id{0};

    /** bitmap font. */
    font::extended_ascii_bitmap_font font;

    /** bitmap font renderer. */
    font::renderer font_rend;

    /** orthographic projection matrix. */
    ml::mat4x4 ortho;

    /** perspective projection matrix. */
    ml::mat4x4 proj;

    /** the cube's vertices. */
    uint32_t cube_verts{0};

    /** the cube's indices. */
    uint32_t cube_indices{0};

    /** vertex colors. */
    uint32_t cube_colors{0};

    /** a rotation offset for the cube. */
    float cube_rotation{0};

    /** frame counter. */
    uint32_t frame_count{0};

    /** viewport width. */
    static const int width = 640;

    /** viewport height. */
    static const int height = 480;

public:
    /** constructor. */
    demo_bitmap_font()
    : swr_app::renderwindow(demo_title, width, height)
    {
    }

    bool create()
    {
        if(!renderwindow::create())
        {
            return false;
        }

        if(context)
        {
            // something went wrong here. the context should not exist.
            return false;
        }

        int thread_hint = swr_app::application::get_instance().get_argument("--threads", 0);
        if(thread_hint > 0)
        {
            platform::logf("suggesting rasterizer to use {} thread{}", thread_hint, ((thread_hint > 1) ? "s" : ""));
        }

        context = swr::CreateSDLContext(sdl_window, sdl_renderer, thread_hint);
        if(!swr::MakeContextCurrent(context))
        {
            throw std::runtime_error("MakeContextCurrent failed");
        }

        swr::SetClearColor(0, 0, 0, 0);
        swr::SetClearDepth(1.0f);
        swr::SetViewport(0, 0, width, height);

        swr::SetState(swr::state::cull_face, true);
        swr::SetState(swr::state::depth_test, true);

        font_shader_id = swr::RegisterShader(&font_shader);
        if(!font_shader_id)
        {
            throw std::runtime_error("font shader registration failed");
        }

        cube_shader_id = swr::RegisterShader(&cube_shader);
        if(!cube_shader_id)
        {
            throw std::runtime_error("cube shader registration failed");
        }

        // set projection matrices.
        ortho = ml::matrices::orthographic_projection(0, static_cast<float>(width), static_cast<float>(height), 0, -1, 1);
        proj = ml::matrices::perspective_projection(static_cast<float>(width) / static_cast<float>(height), static_cast<float>(M_PI) / 2, 1.f, 10.f);

        // load cube.
        std::vector<uint32_t> indices = {
#define FACE_LIST(...) __VA_ARGS__
#include "../common/cube.geom"
#undef FACE_LIST
        };
        cube_indices = swr::CreateIndexBuffer(indices);

        std::vector<ml::vec4> vertices = {
#define VERTEX_LIST(...) __VA_ARGS__
#include "../common/cube.geom"
#undef VERTEX_LIST
        };
        cube_verts = swr::CreateAttributeBuffer(vertices);

        std::vector<ml::vec4> colors = {
#define COLOR_LIST(...) __VA_ARGS__
#include "../common/cube.geom"
#undef COLOR_LIST
        };
        cube_colors = swr::CreateAttributeBuffer(colors);

        // load font.
        std::vector<uint8_t> image_data;
        uint32_t font_tex_width = 0, font_tex_height = 0;
        auto err = lodepng::decode(image_data, font_tex_width, font_tex_height, "../textures/fonts/cp437_16x16_alpha.png");
        if(err != 0)
        {
            throw std::runtime_error(std::format("lodepng error: {}", lodepng_error_text(err)));
        }
        font_tex_id = load_texture(font_tex_width, font_tex_height, image_data);

        swr::BindTexture(swr::texture_target::texture_2d, font_tex_id);
        swr::SetTextureMagnificationFilter(swr::texture_filter::nearest);
        swr::SetTextureMinificationFilter(swr::texture_filter::nearest);

        // create font. the image has to have dimensions 256x256 with 16x16 glyphs.
        font = font::extended_ascii_bitmap_font::create_uniform_font(font_tex_id, font_tex_width, font_tex_height, 256, 256, 16, 16);
        font_rend.update(font_shader_id, font, width, height);

        return true;
    }

    void destroy()
    {
        swr::DeleteAttributeBuffer(cube_colors);
        swr::DeleteAttributeBuffer(cube_verts);
        swr::DeleteIndexBuffer(cube_indices);

        cube_colors = 0;
        cube_verts = 0;
        cube_indices = 0;

        if(cube_shader_id)
        {
            if(context)
            {
                swr::UnregisterShader(cube_shader_id);
            }
            cube_shader_id = 0;
        }

        swr::ReleaseTexture(font_tex_id);
        font_tex_id = 0;

        if(font_shader_id)
        {
            if(context)
            {
                swr::UnregisterShader(font_shader_id);
            }
            font_shader_id = 0;
        }

        if(context)
        {
            swr::DestroyContext(context);
            context = nullptr;
        }

        renderwindow::destroy();
    }

    void update(float delta_time)
    {
        // gracefully exit when asked.
        SDL_Event e;
        if(SDL_PollEvent(&e))
        {
            if(e.type == SDL_QUIT)
            {
                swr_app::application::quit();
                return;
            }
        }

        /*
         * update animation.
         */
        cube_rotation += 0.2f * delta_time;
        if(cube_rotation > 2 * static_cast<float>(M_PI))
        {
            cube_rotation -= 2 * static_cast<float>(M_PI);
        }

        /*
         * do rendering.
         */
        begin_render();
        draw_cube(ml::vec3{0, 0, -7}, cube_rotation);
        draw_text();
        end_render();

        ++frame_count;
    }

    void begin_render()
    {
        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();
    }

    void end_render()
    {
        swr::Present();
        swr::CopyDefaultColorBuffer(context);
    }

    void draw_cube(ml::vec3 pos, float angle)
    {
        ml::mat4x4 view = ml::mat4x4::identity();
        view *= ml::matrices::rotation_z(angle);

        view *= ml::matrices::translation(pos.x, pos.y, pos.z);
        view *= ml::matrices::scaling(2.0f);
        view *= ml::matrices::rotation_y(angle);
        view *= ml::matrices::rotation_z(2 * angle);
        view *= ml::matrices::rotation_x(3 * angle);

        swr::BindShader(cube_shader_id);

        swr::EnableAttributeBuffer(cube_verts, 0);
        swr::EnableAttributeBuffer(cube_colors, 1);

        swr::BindUniform(0, proj);
        swr::BindUniform(1, view);

        // draw the buffer.
        swr::DrawIndexedElements(cube_indices, swr::vertex_buffer_mode::triangles);

        swr::DisableAttributeBuffer(cube_colors);
        swr::DisableAttributeBuffer(cube_verts);

        swr::BindShader(0);
    }

    void draw_text()
    {
        swr::BindUniform(0, ml::matrices::orthographic_projection(0, width, height, 0, -1000, 1000));
        swr::BindUniform(1, ml::mat4x4::identity());

        font_rend.draw_string(font::renderer::string_alignment::left | font::renderer::string_alignment::top, "top left");
        font_rend.draw_string(font::renderer::string_alignment::right | font::renderer::string_alignment::top, "top right");
        font_rend.draw_string(font::renderer::string_alignment::center_horz | font::renderer::string_alignment::top, "top center");

        font_rend.draw_string(font::renderer::string_alignment::left | font::renderer::string_alignment::center_vert, "center left");
        font_rend.draw_string(font::renderer::string_alignment::right | font::renderer::string_alignment::center_vert, "center right");
        font_rend.draw_string(font::renderer::string_alignment::center, "center");

        font_rend.draw_string(font::renderer::string_alignment::left | font::renderer::string_alignment::bottom, "bottom left");
        font_rend.draw_string(font::renderer::string_alignment::right | font::renderer::string_alignment::bottom, "bottom right");
        font_rend.draw_string(font::renderer::string_alignment::center_horz | font::renderer::string_alignment::bottom, "bottom center");
    }

    int get_frame_count() const
    {
        return frame_count;
    }
};

/** Logging to stdout using std::print. */
class log_std : public platform::log_device
{
protected:
    void log_n(const std::string& message)
    {
        std::println("{}", message);
    }
};

/** demo application class. */
class demo_app : public swr_app::application
{
    log_std log;

public:
    /** create a window. */
    void initialize()
    {
        application::initialize();
        platform::set_log(&log);

        window = std::make_unique<demo_bitmap_font>();
        window->create();
    }

    /** destroy the window. */
    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_bitmap_font*>(window.get());
            float fps = static_cast<float>(w->get_frame_count()) / get_run_time();
            platform::logf("frames: {}     runtime: {:.2f}s     fps: {:.2f}     msec: {:.2f}", w->get_frame_count(), get_run_time(), fps, 1000.f / fps);

            window->destroy();
            window.reset();
        }

        platform::set_log(nullptr);
    }
};

/** application instance. */
demo_app the_app;
