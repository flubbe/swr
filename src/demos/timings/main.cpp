/**
 * swr - a software rasterizer
 *
 * software renderer demonstration (timings/bitmap font).
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers */
#include <chrono>
#include <format>
#include <print>

/* software rasterizer headers. */
#include "swr/swr.h"
#include "swr/shaders.h"

/* shaders for this demo. */
#include "shader.h"

#include "../common/font.h"           /* bitmap font support. */
#include "../common/texture.h"        /* texture utilities.*/
#include "swr_app/framework.h"        /* application framework. */
#include "common/platform/platform.h" /* logging. */
#include "common/utils.h"

/* png loading. */
#include "lodepng.h"

/** demo title. */
const auto demo_title = "Display Frame Times";

/** demo window. */
class demo_timings : public swr_app::renderwindow
{
    /** font shader */
    shader::texture font_shader;

    /** cube shader */
    shader::color cube_shader;

    /** font shader id. */
    std::uint32_t font_shader_id{0};

    /** cube shader id. */
    std::uint32_t cube_shader_id{0};

    /** font texture id. */
    std::uint32_t font_tex_id{0};

    /** bitmap font. */
    font::extended_ascii_bitmap_font font;

    /** bitmap font renderer. */
    font::renderer font_rend;

    /** orthographic projection matrix. */
    ml::mat4x4 ortho;

    /** perspective projection matrix. */
    ml::mat4x4 proj;

    /** the cube's vertices. */
    std::uint32_t cube_verts{0};

    /** the cube's indices. */
    std::vector<std::uint32_t> cube_indices;

    /** vertex colors. */
    std::uint32_t cube_colors{0};

    /** a rotation offset for the cube. */
    float cube_rotation{0};

    /** runtime msec/fps measurement timer. */
    std::chrono::steady_clock timer;

    /** runtime msec/fps measurement reference time. */
    std::chrono::steady_clock::time_point msec_reference_time;

    /** frame counter. */
    std::uint32_t frame_count{0};

    /** viewport width. */
    static const int width = 640;

    /** viewport height. */
    static const int height = 480;

public:
    /** constructor. */
    demo_timings()
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

        swr::SetClearColor(0, 0, 0, 1);
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
        std::vector<std::uint32_t> indices = {
#define FACE_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef FACE_LIST
        };
        cube_indices = std::move(indices);

        std::vector<ml::vec4> vertices = {
#define VERTEX_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef VERTEX_LIST
        };
        cube_verts = swr::CreateAttributeBuffer(vertices);

        std::vector<ml::vec4> colors = {
#define COLOR_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef COLOR_LIST
        };
        cube_colors = swr::CreateAttributeBuffer(colors);

        // load font.
        std::vector<std::uint8_t> image_data;
        std::uint32_t font_tex_width = 0, font_tex_height = 0;
        auto err = lodepng::decode(image_data, font_tex_width, font_tex_height, "../textures/fonts/cp437_16x16_alpha.png");
        if(err != 0)
        {
            throw std::runtime_error(std::format("lodepng error: {}", lodepng_error_text(err)));
        }
        font_tex_id = utils::create_non_uniform_texture(font_tex_width, font_tex_height, image_data);

        swr::BindTexture(swr::texture_target::texture_2d, font_tex_id);
        swr::SetTextureMagnificationFilter(swr::texture_filter::nearest);
        swr::SetTextureMinificationFilter(swr::texture_filter::nearest);

        // create font. the image has to have dimensions 256x256 with 16x16 glyphs.
        font = font::extended_ascii_bitmap_font::create_uniform_font(font_tex_id, font_tex_width, font_tex_height, 256, 256, 16, 16);
        font_rend.initialize(font_shader_id, font, width, height);

        // set reference time for fps measurements.
        msec_reference_time = timer.now();

        return true;
    }

    void destroy()
    {
        font_rend.shutdown();

        swr::DeleteAttributeBuffer(cube_colors);
        swr::DeleteAttributeBuffer(cube_verts);

        cube_colors = 0;
        cube_verts = 0;
        cube_indices.clear();

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
            if(e.type == SDL_EVENT_QUIT)
            {
                swr_app::application::quit();
                return;
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto msec_delta_time = std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(now - msec_reference_time).count();
        msec_reference_time = now;

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
        draw_delta_time(msec_delta_time);
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
        swr::DrawIndexedElements(swr::vertex_buffer_mode::triangles, cube_indices.size(), cube_indices);

        swr::DisableAttributeBuffer(cube_colors);
        swr::DisableAttributeBuffer(cube_verts);

        swr::BindShader(0);
    }

    void draw_delta_time(float delta_time)
    {
        if(delta_time == 0)
        {
            return;
        }

        // only update after about 0.5 sec. */
        static float accum{0};
        static int frame_count{0};

        accum += delta_time;
        ++frame_count;

        static float display_msec{delta_time};

        if(accum > 500.f)
        {
            display_msec = accum / static_cast<float>(frame_count);

            accum -= 500.f;
            frame_count = 0;
        }

        swr::BindUniform(0, ml::matrices::orthographic_projection(0, width, height, 0, -1000, 1000));
        swr::BindUniform(1, ml::mat4x4::identity());

        std::string str = std::format("msec: {: #6.2f}", display_msec);
        font_rend.draw_string(font::renderer::string_alignment::right | font::renderer::string_alignment::top, str);

        std::uint32_t w{0}, h{0};
        font.get_string_dimensions(str, w, h);
        str = std::format(" fps: {: #6.1f}", 1000.0f / display_msec);
        font_rend.draw_string(font::renderer::string_alignment::right, str, 0 /* ignored */, h);
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

        window = std::make_unique<demo_timings>();
        window->create();
    }

    /** destroy the window. */
    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_timings*>(window.get());
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
