/**
 * swr - a software rasterizer
 * 
 * software renderer demonstration (textured cubes).
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers */
#include <iostream>

/* boost */
#include <boost/container/static_vector.hpp>

#include "fmt/format.h"

/* software rasterizer headers. */
#include "swr/swr.h"
#include "swr/shaders.h"

/* shaders for this demo. */
#include "shader.h"

/* application framework. */
#include "swr_app/framework.h"

/* logging. */
#include "../common/platform/platform.h"

/* png loading. */
#include "lodepng.h"

/** demo title. */
const auto demo_title = "Alpha Blending";

/** demo window. */
class demo_cube : public swr_app::renderwindow
{
    /** color shader */
    shader::color color_shader;

    /** texture shader */
    shader::texture texture_shader;

    /** color shader id. */
    uint32_t color_shader_id{0};

    /** texture shader id. */
    uint32_t texture_shader_id{0};

    /** projection matrix. */
    ml::mat4x4 proj;

    /** the cube's vertices. */
    uint32_t cube_verts{0};

    /** the cube's indices. */
    uint32_t cube_indices{0};

    /** vertex colors. */
    uint32_t cube_colors{0};

    /** texture coordinates. */
    uint32_t cube_uvs{0};

    /** texture. */
    uint32_t cube_tex{0};

    /** a rotation offset for the cube. */
    float cube_rotation{0};

    /** reference time to provide animation and fps measurements. */
    Uint32 reference_time{0};

    /** frame counter. */
    uint32_t frame_count{0};

    /** viewport width. */
    static const int width = 640;

    /** viewport height. */
    static const int height = 480;

public:
    /** constructor. */
    demo_cube()
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

        context = swr::CreateSDLContext(sdl_window, sdl_renderer);
        if(!swr::MakeContextCurrent(context))
        {
            throw std::runtime_error("MakeContextCurrent failed");
        }

        swr::SetClearColor(0, 0, 0, 0);
        swr::SetClearDepth(1.0f);
        swr::SetViewport(0, 0, width, height);

        swr::SetState(swr::state::cull_face, true);
        swr::SetState(swr::state::depth_test, true);
        swr::SetBlendFunc(swr::blend_func::src_alpha, swr::blend_func::one_minus_src_alpha);

        color_shader_id = swr::RegisterShader(&color_shader);
        if(!color_shader_id)
        {
            throw std::runtime_error("color shader registration failed");
        }

        texture_shader_id = swr::RegisterShader(&texture_shader);
        if(!texture_shader_id)
        {
            throw std::runtime_error("texture shader registration failed");
        }

        // set projection matrix.
        proj = ml::matrices::perspective_projection(static_cast<float>(width) / static_cast<float>(height), static_cast<float>(M_PI / 2.f), 1.f, 10.f);

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

        std::vector<ml::vec4> uvs = {
#define UV_LIST(...) __VA_ARGS__
#include "../common/cube.geom"
#undef UV_LIST
        };
        cube_uvs = swr::CreateAttributeBuffer(uvs);

        // cube texture.
        std::vector<uint8_t> img_data;
        uint32_t w = 0, h = 0;
        uint32_t ret = lodepng::decode(img_data, w, h, "../textures/crate1/crate1_diffuse.png");
        if(ret != 0)
        {
            platform::logf("[!!] lodepng error: {}", lodepng_error_text(ret));
            return false;
        }
        cube_tex = swr::CreateTexture(w, h, swr::pixel_format::rgba8888, swr::wrap_mode::repeat, swr::wrap_mode::mirrored_repeat, img_data);

        // set reference time for statistics and animation.
        reference_time = -SDL_GetTicks();

        return true;
    }

    void destroy()
    {
        swr::ReleaseTexture(cube_tex);
        swr::DeleteAttributeBuffer(cube_uvs);
        swr::DeleteAttributeBuffer(cube_colors);
        swr::DeleteAttributeBuffer(cube_verts);
        swr::DeleteIndexBuffer(cube_indices);

        cube_tex = 0;
        cube_uvs = 0;
        cube_colors = 0;
        cube_verts = 0;
        cube_indices = 0;

        if(texture_shader_id)
        {
            if(context)
            {
                swr::UnregisterShader(texture_shader_id);
            }
            texture_shader_id = 0;
        }

        if(color_shader_id)
        {
            if(context)
            {
                swr::UnregisterShader(color_shader_id);
            }
            color_shader_id = 0;
        }

        if(context)
        {
            swr::DestroyContext(context);
            context = nullptr;
        }

        renderwindow::destroy();
    }

    void update()
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
         * update time.
         */
        Uint32 ticks = SDL_GetTicks();
        float delta_time = static_cast<float>(ticks + reference_time) / 1000.f;
        reference_time = -ticks;

        /*
         * update animation.
         */
        cube_rotation += 0.2f * delta_time;
        if(cube_rotation > 2 * M_PI)
        {
            cube_rotation -= 2 * M_PI;
        }

        begin_render();
        draw_textured_cube(ml::vec3{0, 0, -7}, 1.0f, cube_rotation);

        swr::SetState(swr::state::blend, true);
        draw_colored_cube(ml::vec3{0, 0, -7}, 2.0f, -cube_rotation);
        swr::SetState(swr::state::blend, false);

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

    void draw_colored_cube(ml::vec3 pos, float scale, float angle)
    {
        ml::mat4x4 view = ml::matrices::translation(pos.x, pos.y, pos.z);
        view *= ml::matrices::scaling(scale);
        view *= ml::matrices::rotation_y(angle);
        view *= ml::matrices::rotation_z(2 * angle);
        view *= ml::matrices::rotation_x(3 * angle);

        swr::BindShader(color_shader_id);

        swr::EnableAttributeBuffer(cube_verts, 0);
        swr::EnableAttributeBuffer(cube_colors, 1);

        swr::BindUniform(0, proj);
        swr::BindUniform(1, view);
        swr::BindUniform(2, static_cast<int>(cube_tex));

        // draw the buffer.
        swr::DrawIndexedElements(cube_indices, swr::vertex_buffer_mode::triangles);

        swr::DisableAttributeBuffer(cube_uvs);
        swr::DisableAttributeBuffer(cube_verts);

        swr::BindShader(0);
    }

    void draw_textured_cube(ml::vec3 pos, float scale, float angle)
    {
        ml::mat4x4 view = ml::matrices::translation(pos.x, pos.y, pos.z);
        view *= ml::matrices::scaling(scale);
        view *= ml::matrices::rotation_y(angle);
        view *= ml::matrices::rotation_z(2 * angle);
        view *= ml::matrices::rotation_x(3 * angle);

        swr::BindShader(texture_shader_id);

        swr::EnableAttributeBuffer(cube_verts, 0);
        swr::EnableAttributeBuffer(cube_uvs, 1);

        swr::BindUniform(0, proj);
        swr::BindUniform(1, view);
        swr::BindUniform(2, static_cast<int>(cube_tex));

        // draw the buffer.
        swr::DrawIndexedElements(cube_indices, swr::vertex_buffer_mode::triangles);

        swr::DisableAttributeBuffer(cube_uvs);
        swr::DisableAttributeBuffer(cube_verts);

        swr::BindShader(0);
    }

    int get_frame_count() const
    {
        return frame_count;
    }
};

/** Logging to stdout using C++ iostream. */
class log_iostream : public platform::log_device
{
    std::mutex mtx;

protected:
    void log_n(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mtx); /* prevent unpredictable output interleaving */
        std::cout << message << std::endl;
    }
};

/** demo application class. */
class demo_app : public swr_app::application
{
    log_iostream log;
    Uint32 run_time{0};

public:
    /** create a window. */
    void initialize()
    {
        platform::set_log(&log);

        run_time -= SDL_GetTicks();

        window = new demo_cube();
        window->create();
    }

    /** destroy the window. */
    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_cube*>(window);
            run_time += SDL_GetTicks();
            float run_time_in_s = static_cast<float>(run_time) / 1000.f;
            float fps = static_cast<float>(w->get_frame_count()) / run_time_in_s;
            platform::logf("frames: {}     runtime: {:.2f}s     fps: {:.2f}", w->get_frame_count(), run_time_in_s, fps);

            window->destroy();

            delete window;
            window = nullptr;
        }

        platform::set_log(nullptr);
    }
};

/** application instance. */
demo_app the_app;