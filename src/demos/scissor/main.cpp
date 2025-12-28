/**
 * swr - a software rasterizer
 *
 * software renderer demonstration (phong lighting).
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <print>

/* software rasterizer headers. */
#include "swr/swr.h"
#include "swr/shaders.h"

/* shaders for this demo. */
#include "shader.h"

#include "../common/texture.h"        /* texture utilities.*/
#include "swr_app/framework.h"        /* application framework. */
#include "common/platform/platform.h" /* logging. */

/** demo title. */
const auto demo_title = "Scissor Box";

/** demo window. */
class demo_cube : public swr_app::renderwindow
{
    /** color shader */
    shader::phong shader;

    /** color shader id. */
    std::uint32_t shader_id{0};

    /** projection matrix. */
    ml::mat4x4 proj;

    /** the cube's vertices. */
    std::uint32_t cube_verts{0};

    /** the cube's indices. */
    std::vector<std::uint32_t> cube_indices;

    /** texture coordinates. */
    std::uint32_t cube_uvs{0};

    /** vertex normals. */
    std::uint32_t cube_normals{0};

    /** texture. */
    std::uint32_t cube_tex{0};

    /** a rotation offset for the cube. */
    float cube_rotation{0};

    /** light position. */
    ml::vec4 light_position{0, 0, 0, 1};

    /** frame counter. */
    std::uint32_t frame_count{0};

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

        swr::SetClearDepth(1.0f);
        swr::SetViewport(0, 0, width, height);

        swr::SetState(swr::state::cull_face, true);
        swr::SetState(swr::state::depth_test, true);

        swr::SetScissorBox(120, 120, 400, 240);

        shader_id = swr::RegisterShader(&shader);
        if(!shader_id)
        {
            throw std::runtime_error("shader registration failed");
        }

        // set projection matrix.
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

        std::vector<ml::vec4> uvs = {
#define UV_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef UV_LIST
        };
        cube_uvs = swr::CreateAttributeBuffer(uvs);

        std::vector<ml::vec4> normals = {
#define NORMAL_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef NORMAL_LIST
        };
        cube_normals = swr::CreateAttributeBuffer(normals);

        // cube texture.
        const auto cube_texture_filename = "../textures/crate1/crate1_diffuse.png";
        auto ret = utils::load_uniform(cube_texture_filename);
        if(!ret.has_value())
        {
            platform::logf("[!!] Unable to load texture: {}", cube_texture_filename);
            return false;
        }

        cube_tex = ret.value();
        swr::SetTextureWrapMode(cube_tex, swr::wrap_mode::repeat, swr::wrap_mode::mirrored_repeat);

        return true;
    }

    void destroy()
    {
        swr::ReleaseTexture(cube_tex);
        swr::DeleteAttributeBuffer(cube_normals);
        swr::DeleteAttributeBuffer(cube_uvs);
        swr::DeleteAttributeBuffer(cube_verts);

        cube_tex = 0;
        cube_uvs = 0;
        cube_verts = 0;
        cube_indices.clear();

        if(shader_id)
        {
            if(context)
            {
                swr::UnregisterShader(shader_id);
            }
            shader_id = 0;
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

        /*
         * update animation.
         */
        cube_rotation += 0.2f * delta_time;
        if(cube_rotation > 2 * static_cast<float>(M_PI))
        {
            cube_rotation -= 2 * static_cast<float>(M_PI);
        }

        begin_render();
        draw_cube(ml::vec3{-4, 0, -7}, cube_rotation);
        draw_cube(ml::vec3{4, 0, -7}, cube_rotation);
        end_render();

        ++frame_count;
    }

    void begin_render()
    {
        swr::SetClearColor(0, 0, 0, 1);
        swr::SetState(swr::state::scissor_test, false);
        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();

        swr::SetClearColor(0.2, 0.2, 0.2, 1.0);
        swr::SetState(swr::state::scissor_test, true);
        swr::ClearColorBuffer();
    }

    void end_render()
    {
        swr::Present();
        swr::CopyDefaultColorBuffer(context);
    }

    void draw_cube(ml::vec3 pos, float angle)
    {
        ml::mat4x4 view = ml::matrices::translation(pos.x, pos.y, pos.z);
        view *= ml::matrices::scaling(2.0f);
        view *= ml::matrices::rotation_y(angle);
        view *= ml::matrices::rotation_z(2 * angle);
        view *= ml::matrices::rotation_x(3 * angle);

        swr::BindShader(shader_id);

        swr::EnableAttributeBuffer(cube_verts, 0);
        swr::EnableAttributeBuffer(cube_normals, 1);
        swr::EnableAttributeBuffer(cube_uvs, 2);

        swr::BindUniform(0, proj);
        swr::BindUniform(1, view);
        swr::BindUniform(2, light_position);

        swr::BindTexture(swr::texture_target::texture_2d, cube_tex);

        // draw the buffer.
        swr::DrawIndexedElements(swr::vertex_buffer_mode::triangles, cube_indices.size(), cube_indices);

        swr::DisableAttributeBuffer(cube_uvs);
        swr::DisableAttributeBuffer(cube_normals);
        swr::DisableAttributeBuffer(cube_verts);

        swr::BindShader(0);
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

        window = std::make_unique<demo_cube>();
        window->create();
    }

    /** destroy the window. */
    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_cube*>(window.get());
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
