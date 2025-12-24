/**
 * swr - a software rasterizer
 *
 * software renderer demonstration (normal mapping).
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <print>

/* boost */
#include <boost/container/static_vector.hpp>

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
const auto demo_title = "Normal Mapping";

/** demo window. */
class demo_cube : public swr_app::renderwindow
{
    /** normal mapping shader */
    shader::normal_mapping shader;

    /** normal mapping shader id. */
    uint32_t shader_id{0};

    /** projection matrix. */
    ml::mat4x4 proj;

    /** the cube's vertices. */
    uint32_t cube_verts{0};

    /** the cube's indices. */
    uint32_t cube_indices{0};

    /** texture coordinates. */
    uint32_t cube_uvs{0};

    /** normals. */
    uint32_t cube_normals{0};

    /** tangents. */
    uint32_t cube_tangents{0};

    /** bitangents. */
    uint32_t cube_bitangents{0};

    /** texture. */
    uint32_t cube_tex{0};

    /** normal map. */
    uint32_t cube_normal_map{0};

    /** a rotation offset for the cube. */
    float cube_rotation{0};

    /** light position. */
    ml::vec4 light_position{0, 0, 0, 1};

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

        shader_id = swr::RegisterShader(&shader);
        if(!shader_id)
        {
            throw std::runtime_error("shader registration failed");
        }

        // set projection matrix.
        proj = ml::matrices::perspective_projection(static_cast<float>(width) / static_cast<float>(height), static_cast<float>(M_PI) / 2, 1.f, 10.f);

        // load cube.
        std::vector<uint32_t> indices = {
#define FACE_LIST(...) __VA_ARGS__
#include "../common/cube_uniform_uv.geom"
#undef FACE_LIST
        };
        cube_indices = swr::CreateIndexBuffer(indices);

        std::vector<ml::vec4> vertices = {
#define VERTEX_LIST(...) __VA_ARGS__
#include "../common/cube_uniform_uv.geom"
#undef VERTEX_LIST
        };
        cube_verts = swr::CreateAttributeBuffer(vertices);

        std::vector<ml::vec4> uvs = {
#define UV_LIST(...) __VA_ARGS__
#include "../common/cube_uniform_uv.geom"
#undef UV_LIST
        };
        cube_uvs = swr::CreateAttributeBuffer(uvs);

        std::vector<ml::vec4> normals = {
#define NORMAL_LIST(...) __VA_ARGS__
#include "../common/cube_uniform_uv.geom"
#undef NORMAL_LIST
        };
        cube_normals = swr::CreateAttributeBuffer(normals);

        std::vector<ml::vec4> tangents = {
#define TANGENT_LIST(...) __VA_ARGS__
#include "../common/cube_uniform_uv.geom"
#undef TANGENT_LIST
        };
        cube_tangents = swr::CreateAttributeBuffer(tangents);

        std::vector<ml::vec4> bitangents = {
#define BITANGENT_LIST(...) __VA_ARGS__
#include "../common/cube_uniform_uv.geom"
#undef BITANGENT_LIST
        };
        cube_bitangents = swr::CreateAttributeBuffer(bitangents);

        // cube texture.
        std::vector<uint8_t> img_data;
        uint32_t w = 0, h = 0;
        uint32_t ret = lodepng::decode(img_data, w, h, "../textures/stone/256/ft_stone01_c.png");
        if(ret != 0)
        {
            platform::logf("[!!] lodepng error: {}", lodepng_error_text(ret));
            return false;
        }
        cube_tex = swr::CreateTexture();
        swr::SetImage(cube_tex, 0, w, h, swr::pixel_format::srgb8_alpha8, img_data);
        swr::SetTextureWrapMode(cube_tex, swr::wrap_mode::repeat, swr::wrap_mode::mirrored_repeat);

        // cube normal map.
        img_data.clear();
        ret = lodepng::decode(img_data, w, h, "../textures/stone/256/ft_stone01_n.png");
        if(ret != 0)
        {
            platform::logf("[!!] lodepng error: {}", lodepng_error_text(ret));
            return false;
        }
        cube_normal_map = swr::CreateTexture();
        swr::SetImage(cube_normal_map, 0, w, h, swr::pixel_format::srgb8_alpha8, img_data);
        swr::SetTextureWrapMode(cube_normal_map, swr::wrap_mode::repeat, swr::wrap_mode::mirrored_repeat);

        return true;
    }

    void destroy()
    {
        swr::ReleaseTexture(cube_normal_map);
        swr::ReleaseTexture(cube_tex);
        swr::DeleteAttributeBuffer(cube_bitangents);
        swr::DeleteAttributeBuffer(cube_tangents);
        swr::DeleteAttributeBuffer(cube_normals);
        swr::DeleteAttributeBuffer(cube_uvs);
        swr::DeleteAttributeBuffer(cube_verts);
        swr::DeleteIndexBuffer(cube_indices);

        cube_normal_map = 0;
        cube_tex = 0;
        cube_bitangents = 0;
        cube_tangents = 0;
        cube_normals = 0;
        cube_uvs = 0;
        cube_verts = 0;
        cube_indices = 0;

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
        cube_rotation += 0.1f * delta_time;
        if(cube_rotation > 2 * static_cast<float>(M_PI))
        {
            cube_rotation -= 2 * static_cast<float>(M_PI);
        }
        light_position = ml::vec4{4 * cos(4 * cube_rotation), 4 * sin(4 * cube_rotation), -1};

        begin_render();
        draw_cube(ml::vec3{0, 0, -6}, cube_rotation);
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
        ml::mat4x4 view = ml::matrices::translation(pos.x, pos.y, pos.z);
        view *= ml::matrices::scaling(2.0f);
        view *= ml::matrices::rotation_y(angle);
        view *= ml::matrices::rotation_z(2 * angle);
        view *= ml::matrices::rotation_x(3 * angle);

        swr::BindShader(shader_id);

        swr::EnableAttributeBuffer(cube_verts, 0);
        swr::EnableAttributeBuffer(cube_normals, 1);
        swr::EnableAttributeBuffer(cube_tangents, 2);
        swr::EnableAttributeBuffer(cube_bitangents, 3);
        swr::EnableAttributeBuffer(cube_uvs, 4);

        swr::BindUniform(0, proj);
        swr::BindUniform(1, view);
        swr::BindUniform(2, light_position);

        swr::ActiveTexture(swr::texture_0);
        swr::BindTexture(swr::texture_target::texture_2d, cube_tex);

        swr::ActiveTexture(swr::texture_1);
        swr::BindTexture(swr::texture_target::texture_2d, cube_normal_map);

        // draw the buffer.
        swr::DrawIndexedElements(cube_indices, swr::vertex_buffer_mode::triangles);

        swr::DisableAttributeBuffer(cube_uvs);
        swr::DisableAttributeBuffer(cube_bitangents);
        swr::DisableAttributeBuffer(cube_tangents);
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
