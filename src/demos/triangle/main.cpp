/**
 * swr - a software rasterizer
 *
 * software renderer demonstration (colored triangle).
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <print>

/* software rasterizer headers. */
#include "swr/swr.h"
#include "swr/shaders.h"

/* shaders for this demo. */
#include "shader.h"

/* application framework. */
#include "swr_app/framework.h"

/* logging. */
#include "common/platform/platform.h"

/** demo title. */
const auto demo_title = "Colored Triangle";

/** demo window. */
class demo_triangle : public swr_app::renderwindow
{
    /** color shader */
    shader::color shader;

    /** color shader id. */
    std::uint32_t shader_id{0};

    /** projection matrix. */
    ml::mat4x4 proj;

    /** the triangle's vertices. */
    std::uint32_t triangle_verts{0};

    /** vertex colors. */
    std::uint32_t triangle_colors{0};

    /** frame counter. */
    std::uint32_t frame_count{0};

    /** viewport width. */
    static const int width = 640;

    /** viewport height. */
    static const int height = 480;

public:
    /** constructor. */
    demo_triangle()
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

        shader_id = swr::RegisterShader(&shader);
        if(!shader_id)
        {
            throw std::runtime_error("shader registration failed");
        }

        // set projection matrix.
        proj = ml::matrices::perspective_projection(
          static_cast<float>(width) / static_cast<float>(height),
          static_cast<float>(M_PI) / 2,
          1.f,
          10.f);

        // set up triangle.
        triangle_verts = swr::CreateAttributeBuffer(
          {{{0, 1, 0}, {-1, -1, 0}, {1, -1, 0}}});

        triangle_colors = swr::CreateAttributeBuffer(
          {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}});

        return true;
    }

    void destroy()
    {
        swr::DeleteAttributeBuffer(triangle_colors);
        swr::DeleteAttributeBuffer(triangle_verts);

        triangle_colors = 0;
        triangle_verts = 0;

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

    void update([[maybe_unused]] float delta_time)
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

        begin_render();
        draw_triangle();
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

    void draw_triangle()
    {
        ml::mat4x4 view = ml::mat4x4::identity();
        view *= ml::matrices::scaling(2);
        view *= ml::matrices::translation(0, 0, -2);

        swr::BindShader(shader_id);

        swr::EnableAttributeBuffer(triangle_verts, 0);
        swr::EnableAttributeBuffer(triangle_colors, 1);

        swr::BindUniform(0, proj);
        swr::BindUniform(1, view);

        // draw the buffer.
        swr::DrawElements(swr::vertex_buffer_mode::triangles, 3);

        swr::DisableAttributeBuffer(triangle_colors);
        swr::DisableAttributeBuffer(triangle_verts);

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

        window = std::make_unique<demo_triangle>();
        window->create();
    }

    /** destroy the window. */
    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_triangle*>(window.get());
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
