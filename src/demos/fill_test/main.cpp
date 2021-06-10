/**
 * swr - a software rasterizer
 * 
 * software renderer demonstration (colored rotating mesh).
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "fmt/format.h"

/* software rasterizer headers. */
#include "swr/swr.h"
#include "swr/shaders.h"

/* shaders for this demo. */
#include "shader.h"

/* triangle mesh */
#include "../common/mesh.h"

/* application framework. */
#include "swr_app/framework.h"

/* logging. */
#include "../common/platform/platform.h"

/** demo title. */
const auto demo_title = "Fill Test";

/** demo window. */
class demo_fill : public swr_app::renderwindow
{
    /** color shader */
    shader::color shader;

    /** mesh shader */
    shader::mesh_color mesh_shader;

    /** color shader id. */
    uint32_t shader_id{0};

    /** mesh shader id. */
    uint32_t mesh_shader_id{0};

    /** projection matrix. */
    ml::mat4x4 proj;

    /** whether to rotate the mesh. */
    bool update_rotation{true};

    /** a rotation offset for the mesh. */
    float mesh_rotation{1.4802573};

    /** a mesh. */
    mesh::mesh example_mesh;

    /** reference time to provide animation. */
    Uint32 reference_time{0};

    /** frame counter. */
    uint32_t frame_count{0};

    /** viewport width. */
    static const int width = 640;

    /** viewport height. */
    static const int height = 480;

public:
    /** constructor. */
    demo_fill()
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

        shader_id = swr::RegisterShader(&shader);
        if(!shader_id)
        {
            throw std::runtime_error("shader registration failed");
        }

        mesh_shader_id = swr::RegisterShader(&mesh_shader);
        if(!mesh_shader_id)
        {
            throw std::runtime_error("mesh shader registration failed");
        }

        // set projection matrix.
        proj = ml::matrices::perspective_projection(static_cast<float>(width) / static_cast<float>(height), static_cast<float>(M_PI / 2.f), 1.f, 10.f);

        // create a mesh.
        example_mesh = mesh::generate_random_tiling_mesh(-8, 8, -8, 8, 20, 20, 0, 0.3);
        example_mesh.upload();

        // set reference time for statistics and animation.
        reference_time = -SDL_GetTicks();

        return true;
    }

    void destroy()
    {
        example_mesh.unload();

        if(shader_id)
        {
            if(context)
            {
                swr::UnregisterShader(shader_id);
            }
            shader_id = 0;
        }

        if(mesh_shader_id)
        {
            if(context)
            {
                swr::UnregisterShader(mesh_shader_id);
            }
            mesh_shader_id = 0;
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
        if(update_rotation)
        {
            mesh_rotation += 0.2f * delta_time;
            if(mesh_rotation > 2 * M_PI)
            {
                mesh_rotation -= 2 * M_PI;

                // generate new mesh.
                example_mesh.unload();
                example_mesh = mesh::generate_random_tiling_mesh(-8, 8, -8, 8, 20, 20, 0, 0.3);
                example_mesh.upload();
            }
        }

        begin_render();
        draw_mesh(mesh_rotation, {0, 0, -2});
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

    void draw_mesh(float angle, ml::vec4 pos)
    {
        ml::mat4x4 view = ml::mat4x4::identity();
        view *= ml::matrices::rotation_z(angle);
        view *= ml::matrices::translation(pos.x, pos.y, pos.z);

        swr::BindShader(mesh_shader_id);

        swr::BindUniform(0, proj);
        swr::BindUniform(1, view);

        example_mesh.render();

        swr::BindShader(0);
    }

    int get_frame_count() const
    {
        return frame_count;
    }
};

/** Logging to stdout using fmt::print. */
class log_fmt : public platform::log_device
{
protected:
    void log_n(const std::string& message)
    {
        fmt::print("{}\n", message);
    }
};

/** demo application class. */
class demo_app : public swr_app::application
{
    log_fmt log;
    Uint32 run_time{0};

public:
    /** create a window. */
    void initialize()
    {
        platform::set_log(&log);

        run_time -= SDL_GetTicks();

        window = std::make_unique<demo_fill>();
        window->create();
    }

    /** destroy the window. */
    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_fill*>(window.get());
            run_time += SDL_GetTicks();
            float run_time_in_s = static_cast<float>(run_time) / 1000.f;
            float fps = static_cast<float>(w->get_frame_count()) / run_time_in_s;
            platform::logf("frames: {}     runtime: {:.2f}s     fps: {:.2f}     msec: {:.2f}", w->get_frame_count(), run_time_in_s, fps, 1000.f / fps);

            window->destroy();
            window.reset();
        }

        platform::set_log(nullptr);
    }
};

/** application instance. */
demo_app the_app;
