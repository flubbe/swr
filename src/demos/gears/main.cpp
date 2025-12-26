/**
 * swr - a software rasterizer
 *
 * software renderer demonstration (glxgears port).
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
#include "common/platform/platform.h"

/** demo title. */
const auto demo_title = "Gears";

/** collect a set of geometric data into a single object. */
class drawable_object
{
    /** index buffer. */
    std::vector<std::uint32_t> index_buffer;

    /** vertex buffer id. */
    std::uint32_t vertex_buffer_id{0};

    /** normal buffer id. */
    std::uint32_t normal_buffer_id{0};

    /** remember if we still store data. */
    bool has_data{false};

public:
    /** default constructor. */
    drawable_object() = default;

    /** initialize the object at least with an index buffer id. */
    drawable_object(std::vector<std::uint32_t> in_ib, std::uint32_t in_vb, std::uint32_t in_nb)
    : index_buffer{std::move(in_ib)}
    , vertex_buffer_id{in_vb}
    , normal_buffer_id{in_nb}
    , has_data{true}
    {
    }

    /** move data. */
    drawable_object(drawable_object&& other)
    : index_buffer{std::move(other.index_buffer)}
    , vertex_buffer_id{other.vertex_buffer_id}
    , normal_buffer_id{other.normal_buffer_id}
    , has_data{other.has_data}
    {
        other.has_data = false;
    }

    drawable_object(const drawable_object&) = default;
    drawable_object& operator=(const drawable_object&) = default;

    /** release all data. */
    void release()
    {
        if(has_data)
        {
            swr::DeleteAttributeBuffer(normal_buffer_id);
            swr::DeleteAttributeBuffer(vertex_buffer_id);
            index_buffer.clear();

            has_data = false;
        }
    }

    /** draw the object. */
    void draw() const
    {
        if(has_data)
        {
            swr::EnableAttributeBuffer(vertex_buffer_id, 0);
            swr::EnableAttributeBuffer(normal_buffer_id, 1);
            swr::DrawIndexedElements(swr::vertex_buffer_mode::triangles, index_buffer.size(), index_buffer);
            swr::DisableAttributeBuffer(normal_buffer_id);
            swr::DisableAttributeBuffer(vertex_buffer_id);
        }
    }
};

/** the gear's inner cylinder has smooth shading enabled, so we divide the meshes (and also the shaders) accordingly. */
struct gear_object
{
    /** outside of the gear. */
    drawable_object outside;

    /** inner cylinder of the gear. */
    drawable_object cylinder;

    /** flat shader for the outside. */
    shader::color_flat flat_shader;

    /** smooth shader for the cylinder. */
    shader::color_smooth smooth_shader;

    /** flat shader id. */
    uint32_t flat_shader_id{0};

    /** smooth shader id. */
    uint32_t smooth_shader_id{0};

    /** default constructor. */
    gear_object() = default;

    /** disable copying. */
    gear_object(const gear_object&) = delete;
    gear_object(gear_object&&) = delete;

    gear_object& operator=(const gear_object& other) = delete;

    /** release all data and unregister shaders. */
    void release()
    {
        outside.release();
        cylinder.release();

        swr::UnregisterShader(flat_shader_id);
        swr::UnregisterShader(smooth_shader_id);

        flat_shader_id = 0;
        smooth_shader_id = 0;
    }

    /** draw the gear. */
    void draw() const
    {
        swr::BindShader(flat_shader_id);
        outside.draw();

        swr::BindShader(smooth_shader_id);
        cylinder.draw();
    }

    /** create a gear and upload it to the graphics driver. the code here is adapted from glxgears.c. */
    void make_gear(float inner_radius, float outer_radius, float width, int teeth, float tooth_depth, ml::vec4 color)
    {
        release();

        float r0 = inner_radius;
        float r1 = outer_radius - tooth_depth / 2.f;
        float r2 = outer_radius + tooth_depth / 2.f;

        float da = 2.f * static_cast<float>(M_PI / teeth) / 4.f;

        std::vector<ml::vec4> vb;
        std::vector<ml::vec4> nb;
        std::vector<uint32_t> ib;

        /* draw front face */
        for(int i = 0; i <= teeth; ++i)
        {
            float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), width * 0.5f);

            nb.emplace_back(0, 0, 1, 0);
            nb.emplace_back(0, 0, 1, 0);

            if(i != 0)
            {
                auto cur_idx = vb.size() - 1;
                ib.push_back(cur_idx - 1);
                ib.push_back(cur_idx - 3);
                ib.push_back(cur_idx - 2);

                ib.push_back(cur_idx - 1);
                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx);
            }

            if(i < teeth)
            {
                vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), width * 0.5f);
                vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), width * 0.5f);

                nb.emplace_back(0, 0, 1, 0);
                nb.emplace_back(0, 0, 1, 0);

                auto cur_idx = vb.size() - 1;
                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx - 1);
                ib.push_back(cur_idx - 3);

                ib.push_back(cur_idx - 1);
                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx);
            }
        }

        /* draw front sides of teeth */
        da = 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth) / 4.f;
        for(int i = 0; i < teeth; ++i)
        {
            float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);

            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + da), r2 * std::sin(angle + da), width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + 2 * da), r2 * std::sin(angle + 2 * da), width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), width * 0.5f);

            nb.emplace_back(0, 0, 1, 0);
            nb.emplace_back(0, 0, 1, 0);
            nb.emplace_back(0, 0, 1, 0);
            nb.emplace_back(0, 0, 1, 0);

            auto cur_idx = vb.size() - 1;
            ib.push_back(cur_idx - 3);
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx - 1);

            ib.push_back(cur_idx - 3);
            ib.push_back(cur_idx - 1);
            ib.push_back(cur_idx);
        }

        /* draw back face */
        for(int i = 0; i <= teeth; ++i)
        {
            float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);
            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), -width * 0.5f);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), -width * 0.5f);

            nb.emplace_back(0, 0, -1, 0);
            nb.emplace_back(0, 0, -1, 0);

            if(i != 0)
            {
                auto cur_idx = vb.size() - 1;
                ib.push_back(cur_idx - 3);
                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx - 1);

                ib.push_back(cur_idx - 1);
                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx);
            }

            if(i < teeth)
            {
                vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), -width * 0.5f);
                vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), -width * 0.5f);

                nb.emplace_back(0, 0, -1, 0);
                nb.emplace_back(0, 0, -1, 0);

                auto cur_idx = vb.size() - 1;
                ib.push_back(cur_idx - 3);
                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx - 1);

                ib.push_back(cur_idx - 1);
                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx);
            }
        }

        /* draw back sides of teeth */
        da = 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth) / 4.f;
        for(int i = 0; i < teeth; ++i)
        {
            float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);

            vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), -width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + 2 * da), r2 * std::sin(angle + 2 * da), -width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + da), r2 * std::sin(angle + da), -width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), -width * 0.5f);

            nb.emplace_back(0, 0, -1, 0);
            nb.emplace_back(0, 0, -1, 0);
            nb.emplace_back(0, 0, -1, 0);
            nb.emplace_back(0, 0, -1, 0);

            auto cur_idx = vb.size() - 1;
            ib.push_back(cur_idx - 3);
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx - 1);

            ib.push_back(cur_idx - 3);
            ib.push_back(cur_idx - 1);
            ib.push_back(cur_idx);
        }

        /* draw outward faces of teeth */
        for(int i = 0; i < teeth; ++i)
        {
            float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);

            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), -width * 0.5f);

            ml::vec4 uv{
              r2 * std::sin(angle + da) - r1 * std::sin(angle),
              -r2 * std::cos(angle + da) + r1 * std::cos(angle),
              0, 0};
            nb.emplace_back(uv.normalized());
            nb.emplace_back(uv.normalized());

            if(i != 0)
            {
                auto cur_idx = vb.size() - 1;
                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx - 1);
                ib.push_back(cur_idx - 3);

                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx);
                ib.push_back(cur_idx - 1);
            }

            vb.emplace_back(r2 * std::cos(angle + da), r2 * std::sin(angle + da), width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + da), r2 * std::sin(angle + da), -width * 0.5f);

            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);
            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);

            auto cur_idx = vb.size() - 1;
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx - 1);
            ib.push_back(cur_idx - 3);

            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx);
            ib.push_back(cur_idx - 1);

            vb.emplace_back(r2 * std::cos(angle + 2 * da), r2 * std::sin(angle + 2 * da), width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + 2 * da), r2 * std::sin(angle + 2 * da), -width * 0.5f);

            uv = ml::vec4{
              r1 * std::sin(angle + 3 * da) - r2 * std::sin(angle + 2 * da),
              -r1 * std::cos(angle + 3 * da) + r2 * std::cos(angle + 2 * da),
              0, 0};
            nb.emplace_back(uv.normalized());
            nb.emplace_back(uv.normalized());

            cur_idx = vb.size() - 1;
            ib.push_back(cur_idx - 3);
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx - 1);

            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx);
            ib.push_back(cur_idx - 1);

            vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), -width * 0.5f);

            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);
            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);

            cur_idx = vb.size() - 1;
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx - 1);
            ib.push_back(cur_idx - 3);

            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx);
            ib.push_back(cur_idx - 1);
        }

        vb.emplace_back(r1 * std::cos(0.f), r1 * std::sin(0.f), width * 0.5f);
        vb.emplace_back(r1 * std::cos(0.f), r1 * std::sin(0.f), -width * 0.5f);

        nb.emplace_back(std::cos(0.f), std::sin(0.f), 0, 0);
        nb.emplace_back(std::cos(0.f), std::sin(0.f), 0, 0);

        auto cur_idx = vb.size() - 1;
        ib.push_back(cur_idx - 2);
        ib.push_back(cur_idx - 1);
        ib.push_back(cur_idx - 3);

        ib.push_back(cur_idx - 2);
        ib.push_back(cur_idx);
        ib.push_back(cur_idx - 1);

        /* create outside of the gear. */
        outside = {std::move(ib), swr::CreateAttributeBuffer(vb), swr::CreateAttributeBuffer(nb)};

        /* clear buffers for the inner cylinder. */
        vb.clear();
        nb.clear();
        ib.clear();

        /* draw inside radius cylinder */
        for(int i = 0; i <= teeth; i++)
        {
            float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), -width * 0.5f);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), width * 0.5f);

            nb.emplace_back(-std::cos(angle), -std::sin(angle), 0, 0);
            nb.emplace_back(-std::cos(angle), -std::sin(angle), 0, 0);

            if(i != 0)
            {
                auto cur_idx = vb.size() - 1;
                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx - 1);
                ib.push_back(cur_idx - 3);

                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx);
                ib.push_back(cur_idx - 1);
            }
        }

        /* create inner cylinder. */
        cylinder = {std::move(ib), swr::CreateAttributeBuffer(vb), swr::CreateAttributeBuffer(nb)};

        /* create shaders. */
        smooth_shader = {color};
        flat_shader = {color};

        flat_shader_id = swr::RegisterShader(&flat_shader);
        smooth_shader_id = swr::RegisterShader(&smooth_shader);

        if(!flat_shader_id || !smooth_shader_id)
        {
            throw std::runtime_error("gear_object: shader registration failed.");
        }
    }
};

/** demo window. */
class demo_gears : public swr_app::renderwindow
{
    /** light position. */
    ml::vec4 light_pos{5.0f, 5.0f, 10.0f, 0.0f};

    /** projection matrix. */
    ml::mat4x4 proj;

    /** the gears. */
    gear_object gears[3];

    /** view rotation. */
    ml::vec3 view_rotation = {20.f, 30.f, 0.f};

    /** a rotation offset for the gears. */
    float gear_rotation{0};

    /** frame counter. */
    uint32_t frame_count{0};

    /** viewport width. */
    static const int width = 640;

    /** viewport height. */
    static const int height = 480;

public:
    /** constructor. */
    demo_gears()
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

        // set projection matrix.
        proj = ml::matrices::perspective_projection(static_cast<float>(width) / static_cast<float>(height), static_cast<float>(M_PI) / 8, 5.f, 60.f);

        // create gears.
        gears[0].make_gear(1.0, 4.0, 1.0, 20, 0.7, {0.8f, 0.1f, 0.0f, 1.0f});
        gears[1].make_gear(0.5, 2.0, 2.0, 10, 0.7, {0.0f, 0.8f, 0.2f, 1.0f});
        gears[2].make_gear(1.3, 2.0, 0.5, 10, 0.7, {0.2f, 0.2f, 1.0f, 1.0f});

        return true;
    }

    void destroy()
    {
        gears[0].release();
        gears[1].release();
        gears[2].release();

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
        gear_rotation += delta_time;
        if(gear_rotation >= 2 * static_cast<float>(M_PI))
        {
            gear_rotation -= 2 * static_cast<float>(M_PI);
        }

        begin_render();
        draw_gears();
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

    void draw_gears()
    {
        // set projection matrix.
        swr::BindUniform(0, proj);

        ml::mat4x4 view = ml::mat4x4::identity();
        view *= ml::matrices::translation(0.f, 0.f, -40.f);

        swr::BindUniform(2, view * light_pos);

        view *= ml::matrices::rotation_x(ml::to_radians(view_rotation.x));
        view *= ml::matrices::rotation_y(ml::to_radians(view_rotation.y));
        view *= ml::matrices::rotation_z(ml::to_radians(view_rotation.z));

        /*
         * gear 1
         */
        ml::mat4x4 temp = view;
        temp *= ml::matrices::translation(-3.f, -2.f, 0.f);
        temp *= ml::matrices::rotation_z(gear_rotation);

        swr::BindUniform(1, temp);
        gears[0].draw();

        /*
         * gear 2
         */
        temp = view;
        temp *= ml::matrices::translation(3.1f, -2.f, 0.f);
        temp *= ml::matrices::rotation_z(-2.f * gear_rotation - 9.f);

        swr::BindUniform(1, temp);
        gears[1].draw();

        /*
         * gear 3
         */
        temp = view;
        temp *= ml::matrices::translation(-3.1f, 4.2f, 0.f);
        temp *= ml::matrices::rotation_z(-2.f * gear_rotation - 25.f);

        swr::BindUniform(1, temp);
        gears[2].draw();

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

        window = std::make_unique<demo_gears>();
        window->create();
    }

    /** destroy the window. */
    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_gears*>(window.get());
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
