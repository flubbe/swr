/**
 * swr - a software rasterizer
 * 
 * software renderer demonstration (simple particle system).
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

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

/* simple particle system. */
#include "particles.h"

/** demo title. */
const auto demo_title = "Motion Blur";

/** maximum number of particles. */
constexpr int max_particles = 128;

/** demo window. */
class demo_emitter : public swr_app::renderwindow
{
    /** normal mapping shader */
    shader::normal_mapping shader;

    /** shader used for blurring. */
    shader::im_texture blur_shader;

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

    /** particle system. */
    particles::particle_system particle_system;

    /** blur texture. */
    uint32_t blur_texture{0};

    /** texture shader for blur. */
    uint32_t blur_shader_id{0};

    /** light position. */
    ml::vec4 light_position{0, 3, -3, 1};

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
    demo_emitter()
    : swr_app::renderwindow(demo_title, width, height)
    , particle_system{{0, -8, -5}, 25, 0.2, 9, 2}
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

        blur_shader_id = swr::RegisterShader(&blur_shader);
        if(!blur_shader_id)
        {
            throw std::runtime_error("blur_shader registration failed");
        }

        // set projection matrix.
        proj = ml::matrices::perspective_projection(static_cast<float>(width) / static_cast<float>(height), static_cast<float>(M_PI / 2.f), 1.f, 10.f);

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
        uint32_t ret = lodepng::decode(img_data, w, h, "../textures/stone/32/ft_stone01_c.png");
        if(ret != 0)
        {
            platform::logf("[!!] lodepng error: {}", lodepng_error_text(ret));
            return false;
        }
        cube_tex = swr::CreateTexture(w, h, swr::pixel_format::rgba8888, swr::wrap_mode::repeat, swr::wrap_mode::mirrored_repeat, img_data);

        // cube normal map.
        img_data.clear();
        ret = lodepng::decode(img_data, w, h, "../textures/stone/32/ft_stone01_n.png");
        if(ret != 0)
        {
            platform::logf("[!!] lodepng error: {}", lodepng_error_text(ret));
            return false;
        }
        cube_normal_map = swr::CreateTexture(w, h, swr::pixel_format::rgba8888, swr::wrap_mode::repeat, swr::wrap_mode::mirrored_repeat, img_data);

        // empty blur texture.
        img_data.clear();
        w = 1024;
        h = 1024;
        img_data.resize(w * h * sizeof(uint32_t));
        blur_texture = swr::CreateTexture(w, h, swr::pixel_format::rgba8888, swr::wrap_mode::clamp_to_edge, swr::wrap_mode::clamp_to_edge, img_data);

        // set reference time for statistics and animation.
        reference_time = -SDL_GetTicks();

        // create particles.
        particle_system.delay_add(0.1f, max_particles);

        return true;
    }

    void destroy()
    {
        swr::ReleaseTexture(blur_texture);

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
         * update particles.
         */
        particle_system.update(delta_time);

        /*
         * every second, print some statistics.
         */
        static float print_active_particle_time = 0;
        print_active_particle_time += delta_time;
        if(print_active_particle_time > 1.f)
        {
            platform::logf("{} particles active, {} total particles (frame time: {} ms)", particle_system.get_active_particle_count(), particle_system.get_particle_count(), delta_time * 1000.f);
            print_active_particle_time = 0;
        }

        /*
         * render particles.
         */
        begin_render();
        for(auto it: particle_system.get_particles())
        {
            if(it.is_active)
            {
                draw_cube(it.position.xyz(), it.rotation_axis.xyz(), it.rotation_offset, it.scale);
            }
        }
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
        post_process();

        swr::Present();
        swr::CopyDefaultColorBuffer(context);

        update_blur();
    }

    void draw_cube(ml::vec3 pos, ml::vec3 axis, float angle, float scale)
    {
        ml::mat4x4 view = ml::mat4x4::identity();
        view *= ml::matrices::rotation_x(M_PI_2);
        view *= ml::matrices::rotation_y(M_PI);
        view *= ml::matrices::translation(pos.x, pos.y, pos.z);
        view *= ml::matrices::scaling(scale);
        view *= ml::matrices::rotation(axis, angle);

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

    void post_process()
    {
        swr::BindShader(blur_shader_id);

        swr::ActiveTexture(swr::texture_0);
        swr::BindTexture(swr::texture_target::texture_2d, blur_texture);

        swr::BindUniform(0, ml::matrices::orthographic_projection(0, width, height, 0, -1000, 1000));
        swr::BindUniform(1, ml::mat4x4::identity());

        swr::SetState(swr::state::blend, true);
        swr::SetBlendFunc(swr::blend_func::src_alpha, swr::blend_func::one_minus_src_alpha);

        swr::BeginPrimitives(swr::vertex_buffer_mode::quads);

        swr::SetTexCoord(0, 0);
        swr::InsertVertex(0, 0, 1, 1);

        swr::SetTexCoord(0, static_cast<float>(height) / 1024.f);
        swr::InsertVertex(0, height, 1, 1);

        swr::SetTexCoord(static_cast<float>(width) / 1024.f, static_cast<float>(height) / 1024.f);
        swr::InsertVertex(width, height, 1, 1);

        swr::SetTexCoord(static_cast<float>(width) / 1024.f, 0);
        swr::InsertVertex(width, 0, 1, 1);

        swr::EndPrimitives();

        swr::SetState(swr::state::blend, false);

        swr::BindShader(0);
    }

    void update_blur()
    {
        static std::vector<uint8_t> image;
        static std::vector<uint32_t> surface;

        //!!todo: we really should read the framebuffer here.
        get_surface_buffer_rgba32(surface);
        image.resize(width * height * sizeof(uint32_t));

        for(int y = 0; y < height; ++y)
        {
            for(int x = 0; x < width; ++x)
            {
                auto pixel = surface[y * width + x];
                *reinterpret_cast<uint32_t*>(&image[(y * width + x) * sizeof(uint32_t)]) = ((pixel & 0x000000ff) << 24) | ((pixel & 0x0000ff00) << 8) | ((pixel & 0x00ff0000) >> 8) | ((pixel & 0xff000000) >> 24);
            }
        }

        swr::SetSubImage(blur_texture, 0, 0, 0, width, height, swr::pixel_format::rgba8888, image);
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

        window = std::make_unique<demo_emitter>();
        window->create();
    }

    /** destroy the window. */
    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_emitter*>(window.get());
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
