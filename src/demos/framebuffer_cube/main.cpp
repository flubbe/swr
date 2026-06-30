/**
 * swr - a software rasterizer
 *
 * software renderer demonstration (cube rendered to a framebuffer, displayed on a rotating quad).
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
const auto demo_title = "Framebuffer Cube";

/** demo window. */
class demo_framebuffer_cube : public swr_app::renderwindow
{
    /** shader used to render the offscreen cube. */
    shader::color color_shader;

    /** shader used to render the textured quad. */
    shader::texture texture_shader;

    /** color shader id. */
    std::uint32_t color_shader_id{0};

    /** texture shader id. */
    std::uint32_t texture_shader_id{0};

    /** projection matrix for the offscreen cube. */
    ml::mat4x4 cube_proj;

    /** projection matrix for the onscreen quad. */
    ml::mat4x4 quad_proj;

    /** the cube's vertices. */
    std::uint32_t cube_verts{0};

    /** the cube's indices. */
    std::vector<std::uint32_t> cube_indices;

    /** vertex colors. */
    std::uint32_t cube_colors{0};

    /** quad vertices. */
    std::uint32_t quad_verts{0};

    /** quad texture coordinates. */
    std::uint32_t quad_uvs{0};

    /** quad indices. */
    std::vector<std::uint32_t> quad_indices;

    /** framebuffer object. */
    std::uint32_t cube_fbo{0};

    /** color texture attached to the framebuffer. */
    std::uint32_t cube_texture{0};

    /** depth renderbuffer attached to the framebuffer. */
    std::uint32_t cube_depth{0};

    /** cube rotation around the y axis. */
    float cube_rotation_y{0.0f};

    /** cube rotation around the z axis. */
    float cube_rotation_z{0.0f};

    /** cube rotation around the x axis. */
    float cube_rotation_x{0.0f};

    /** quad rotation around the y axis. */
    float quad_rotation_y{0.0f};

    /** quad rotation around the x axis. */
    float quad_rotation_x{0.0f};

    /** frame counter. */
    std::uint32_t frame_count{0};

    /** viewport width. */
    static constexpr int width = 640;

    /** viewport height. */
    static constexpr int height = 480;

    /** framebuffer width. */
    static constexpr int framebuffer_width = 512;

    /** framebuffer height. */
    static constexpr int framebuffer_height = 512;

public:
    demo_framebuffer_cube()
    : swr_app::renderwindow{demo_title, width, height}
    {
    }

    bool create()
    {
        if(!renderwindow::create())
        {
            return false;
        }

        if(context != nullptr)
        {
            return false;
        }

        const int thread_hint = swr_app::application::get_instance().get_argument("--threads", 0);
        if(thread_hint > 0)
        {
            platform::logf("suggesting rasterizer to use {} thread{}", thread_hint, ((thread_hint > 1) ? "s" : ""));
        }

        context = swr::CreateSDLContext(sdl_window, sdl_renderer, thread_hint);
        if(!swr::MakeContextCurrent(context))
        {
            throw std::runtime_error("MakeContextCurrent failed");
        }

        swr::SetClearColor(0.08f, 0.08f, 0.1f, 1.0f);
        swr::SetClearDepth(1.0f);
        swr::SetViewport(0, 0, width, height);

        swr::SetState(swr::state::cull_face, true);
        swr::SetState(swr::state::depth_test, true);

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

        cube_proj = ml::matrices::perspective_projection(1.0f, static_cast<float>(M_PI) / 2, 1.0f, 10.0f);
        quad_proj = ml::matrices::perspective_projection(static_cast<float>(width) / static_cast<float>(height), static_cast<float>(M_PI) / 2, 1.0f, 20.0f);

        std::vector<std::uint32_t> cube_index_data = {
#define FACE_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef FACE_LIST
        };
        cube_indices = std::move(cube_index_data);

        std::vector<ml::vec4> cube_vertex_data = {
#define VERTEX_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef VERTEX_LIST
        };
        cube_verts = swr::CreateAttributeBuffer(cube_vertex_data);

        std::vector<ml::vec4> cube_color_data = {
#define COLOR_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef COLOR_LIST
        };
        cube_colors = swr::CreateAttributeBuffer(cube_color_data);

        quad_verts = swr::CreateAttributeBuffer({
          ml::vec4{-2.5f, -2.5f, 0.0f, 1.0f},
          ml::vec4{2.5f, -2.5f, 0.0f, 1.0f},
          ml::vec4{2.5f, 2.5f, 0.0f, 1.0f},
          ml::vec4{-2.5f, 2.5f, 0.0f, 1.0f},
        });

        quad_uvs = swr::CreateAttributeBuffer({
          ml::vec4{0.0f, 0.0f, 0.0f, 0.0f},
          ml::vec4{1.0f, 0.0f, 0.0f, 0.0f},
          ml::vec4{1.0f, 1.0f, 0.0f, 0.0f},
          ml::vec4{0.0f, 1.0f, 0.0f, 0.0f},
        });
        quad_indices = {0, 1, 2, 0, 2, 3};

        std::vector<std::uint8_t> image_data(framebuffer_width * framebuffer_height * sizeof(std::uint32_t));
        cube_texture = swr::CreateTexture();

        swr::BindTexture(swr::texture_target::texture_2d, cube_texture);
        swr::SetTextureMagnificationFilter(swr::texture_filter::linear);
        swr::SetTextureMinificationFilter(swr::texture_filter::nearest);
        swr::BindTexture(swr::texture_target::texture_2d, 0);

        swr::SetImage(cube_texture, 0, framebuffer_width, framebuffer_height, swr::pixel_format::rgba8888, image_data);
        if(const auto error = swr::GetLastError();
           error != swr::error::none)
        {
            platform::logf("[!!] SetImage failed with error code {}", static_cast<int>(error));
            return false;
        }

        swr::SetTextureWrapMode(cube_texture, swr::wrap_mode::clamp_to_edge, swr::wrap_mode::clamp_to_edge);
        if(const auto error = swr::GetLastError();
           error != swr::error::none)
        {
            platform::logf("[!!] SetTextureWrapMode failed with error code {}", static_cast<int>(error));
            return false;
        }

        cube_fbo = swr::CreateFramebufferObject();
        swr::FramebufferTexture(cube_fbo, swr::framebuffer_attachment::color_attachment_0, cube_texture, 0);

        cube_depth = swr::CreateDepthRenderbuffer(framebuffer_width, framebuffer_height);
        swr::FramebufferRenderbuffer(cube_fbo, swr::framebuffer_attachment::depth_attachment, cube_depth);

        return true;
    }

    void destroy()
    {
        if(context != nullptr)
        {

            swr::ReleaseDepthRenderbuffer(cube_depth);
            swr::ReleaseFramebufferObject(cube_fbo);
            swr::ReleaseTexture(cube_texture);
            swr::DeleteAttributeBuffer(quad_uvs);
            swr::DeleteAttributeBuffer(quad_verts);
            swr::DeleteAttributeBuffer(cube_colors);
            swr::DeleteAttributeBuffer(cube_verts);

            cube_depth = 0;
            cube_fbo = 0;
            cube_texture = 0;
            quad_uvs = 0;
            quad_verts = 0;
            cube_colors = 0;
            cube_verts = 0;
            quad_indices.clear();
            cube_indices.clear();

            if(texture_shader_id)
            {
                swr::UnregisterShader(texture_shader_id);
                texture_shader_id = 0;
            }

            if(color_shader_id)
            {
                swr::UnregisterShader(color_shader_id);
                color_shader_id = 0;
            }

            swr::DestroyContext(context);
            context = nullptr;
        }

        renderwindow::destroy();
    }

    void update(float delta_time)
    {
        SDL_Event e;
        if(SDL_PollEvent(&e))
        {
            if(e.type == SDL_EVENT_QUIT)
            {
                swr_app::application::quit();
                return;
            }
        }

        constexpr float tau = 2.0f * static_cast<float>(M_PI);

        cube_rotation_y += 0.2f * delta_time;
        cube_rotation_z += 0.4f * delta_time;
        cube_rotation_x += 0.6f * delta_time;
        quad_rotation_y += 0.3f * delta_time;
        quad_rotation_x += 0.105f * delta_time;

        if(cube_rotation_y > tau)
        {
            cube_rotation_y -= tau;
        }
        if(cube_rotation_z > tau)
        {
            cube_rotation_z -= tau;
        }
        if(cube_rotation_x > tau)
        {
            cube_rotation_x -= tau;
        }
        if(quad_rotation_y > tau)
        {
            quad_rotation_y -= tau;
        }
        if(quad_rotation_x > tau)
        {
            quad_rotation_x -= tau;
        }

        begin_render();
        render_cube_to_framebuffer();
        render_textured_quad();
        end_render();

        ++frame_count;
    }

    void begin_render()
    {
        swr::BindFramebufferObject(swr::framebuffer_target::draw, cube_fbo);
        swr::SetViewport(0, 0, framebuffer_width, framebuffer_height);
        swr::SetClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();

        // bind default framebuffer
        swr::BindFramebufferObject(swr::framebuffer_target::draw, 0);
        swr::SetViewport(0, 0, width, height);
        swr::SetClearColor(0.08f, 0.08f, 0.1f, 1.0f);
        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();
    }

    void end_render()
    {
        swr::Present();
        swr::CopyDefaultColorBuffer(context);
    }

    void render_cube_to_framebuffer()
    {
        swr::BindFramebufferObject(swr::framebuffer_target::draw, cube_fbo);
        swr::SetViewport(0, 0, framebuffer_width, framebuffer_height);

        draw_cube(ml::vec3{0.0f, 0.0f, -5.0f});

        // bind default framebuffer
        swr::BindFramebufferObject(swr::framebuffer_target::draw, 0);
        swr::SetViewport(0, 0, width, height);
    }

    void render_textured_quad()
    {
        ml::mat4x4 view = ml::matrices::translation(0.0f, 0.0f, -6.5f);
        view *= ml::matrices::rotation_y(quad_rotation_y);
        view *= ml::matrices::rotation_x(quad_rotation_x);

        swr::SetState(swr::state::cull_face, false);
        swr::BindShader(texture_shader_id);

        swr::EnableAttributeBuffer(quad_verts, 0);
        swr::EnableAttributeBuffer(quad_uvs, 1);

        swr::BindUniform(0, quad_proj);
        swr::BindUniform(1, view);

        swr::ActiveTexture(swr::texture_0);
        swr::BindTexture(swr::texture_target::texture_2d, cube_texture);

        swr::DrawIndexedElements(swr::vertex_buffer_mode::triangles, quad_indices.size(), quad_indices);

        swr::DisableAttributeBuffer(quad_uvs);
        swr::DisableAttributeBuffer(quad_verts);
        swr::BindShader(0);
        swr::SetState(swr::state::cull_face, true);
    }

    void draw_cube(ml::vec3 pos)
    {
        ml::mat4x4 view = ml::matrices::translation(pos.x, pos.y, pos.z);
        view *= ml::matrices::scaling(1.4f);
        view *= ml::matrices::rotation_y(cube_rotation_y);
        view *= ml::matrices::rotation_z(cube_rotation_z);
        view *= ml::matrices::rotation_x(cube_rotation_x);

        swr::BindShader(color_shader_id);

        swr::EnableAttributeBuffer(cube_verts, 0);
        swr::EnableAttributeBuffer(cube_colors, 1);

        swr::BindUniform(0, cube_proj);
        swr::BindUniform(1, view);

        swr::DrawIndexedElements(swr::vertex_buffer_mode::triangles, cube_indices.size(), cube_indices);

        swr::DisableAttributeBuffer(cube_colors);
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
class demo_app final : public swr_app::application
{
    log_std log;

public:
    void initialize()
    {
        application::initialize();
        platform::set_log(&log);

        window = std::make_unique<demo_framebuffer_cube>();
        window->create();
    }

    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_framebuffer_cube*>(window.get());
            const float fps = static_cast<float>(w->get_frame_count()) / get_run_time();
            platform::logf("frames: {}     runtime: {:.2f}s     fps: {:.2f}     msec: {:.2f}", w->get_frame_count(), get_run_time(), fps, 1000.0f / fps);

            window->destroy();
            window.reset();
        }

        platform::set_log(nullptr);
    }
};

/** application instance. */
demo_app the_app;
