/**
 * swr - a software rasterizer
 * 
 * software renderer demonstration (glxgears port).
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

/** demo title. */
const auto demo_title = "Gears";

/** collect a set of geometric data into a single object. */
class drawable_object
{
    /** index buffer id. */
    std::uint32_t index_buffer_id;

    /** vertex buffer id. */
    std::uint32_t vertex_buffer_id;

    /** normal buffer id. */
    std::uint32_t normal_buffer_id;

    /** remember if we still store data. */
    bool has_data;

public:
    /** default constructor. */
    drawable_object()
    : index_buffer_id{0}
    , vertex_buffer_id{0}
    , normal_buffer_id{0}
    , has_data{false}
    {
    }

    /** initialize the object at least with an index buffer id. */
    drawable_object(std::uint32_t in_ib, std::uint32_t in_vb, std::uint32_t in_nb)
    : index_buffer_id{in_ib}
    , vertex_buffer_id{in_vb}
    , normal_buffer_id{in_nb}
    , has_data{true}
    {
    }

    /* disallow moving, copying and assignment */
    drawable_object(drawable_object&& other) = default;
    drawable_object(const drawable_object&) = default;
    drawable_object& operator=(const drawable_object&) = default;

    /** release all data. */
    void release()
    {
        if(has_data)
        {
            swr::DeleteAttributeBuffer(normal_buffer_id);
            swr::DeleteAttributeBuffer(vertex_buffer_id);
            swr::DeleteIndexBuffer(index_buffer_id);

            has_data = false;
        }
    }

    /** draw the object. */
    void draw()
    {
        if(has_data)
        {
            swr::EnableAttributeBuffer(vertex_buffer_id, 0);
            swr::EnableAttributeBuffer(normal_buffer_id, 1);
            swr::DrawIndexedElements(index_buffer_id, swr::vertex_buffer_mode::triangles);
            swr::DisableAttributeBuffer(normal_buffer_id);
            swr::DisableAttributeBuffer(vertex_buffer_id);
        }
    }
};

/** demo window. */
class demo_gears : public swr_app::renderwindow
{
    /** color shader */
    shader::color shader_red{ml::vec4{0.8f, 0.1f, 0.0f, 1.0f}};
    shader::color shader_green{ml::vec4{0.0f, 0.8f, 0.2f, 1.0f}};
    shader::color shader_blue{ml::vec4{0.2f, 0.2f, 1.0f, 1.0f}};

    /** color shader ids. */
    uint32_t shader_ids[3] = {0, 0, 0};

    /** projection matrix. */
    ml::mat4x4 proj;

    /** the gears. */
    drawable_object gears[3];

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

    /** 
     * create a gear and upload it to the graphics driver. the code here is adapted from glxgears.c.
     */
    drawable_object make_gear(float inner_radius, float outer_radius, float width, int teeth, float tooth_depth)
    {
        float r0 = inner_radius;
        float r1 = outer_radius - tooth_depth / 2.f;
        float r2 = outer_radius + tooth_depth / 2.f;

        float da = 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth) / 4.f;

        std::vector<ml::vec4> vb;
        std::vector<ml::vec4> nb;
        std::vector<uint32_t> ib;

        /* draw front face */
        for(int i = 0; i <= teeth; ++i)
        {
            float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), width * 0.5f);

            nb.emplace_back(0, 0, 1);
            nb.emplace_back(0, 0, 1);

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
                vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), width * 0.5f);
                vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), width * 0.5f);

                nb.emplace_back(0, 0, 1);
                nb.emplace_back(0, 0, 1);

                auto cur_idx = vb.size() - 1;
                ib.push_back(cur_idx - 3);
                ib.push_back(cur_idx - 2);
                ib.push_back(cur_idx - 1);

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

            nb.emplace_back(0, 0, 1);
            nb.emplace_back(0, 0, 1);
            nb.emplace_back(0, 0, 1);
            nb.emplace_back(0, 0, 1);

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

            nb.emplace_back(0, 0, -1);
            nb.emplace_back(0, 0, -1);

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

                nb.emplace_back(0, 0, -1);
                nb.emplace_back(0, 0, -1);

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

            nb.emplace_back(0, 0, -1);
            nb.emplace_back(0, 0, -1);
            nb.emplace_back(0, 0, -1);
            nb.emplace_back(0, 0, -1);

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

            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);
            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);

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

            vb.emplace_back(r2 * std::cos(angle + da), r2 * std::sin(angle + da), width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + da), r2 * std::sin(angle + da), -width * 0.5f);

            ml::vec4 uv{
              r2 * std::sin(angle + da) - r1 * std::sin(angle),
              -r2 * std::cos(angle + da) + r1 * std::cos(angle),
              0, 0};
            nb.emplace_back(uv.normalized());
            nb.emplace_back(uv.normalized());

            auto cur_idx = vb.size() - 1;
            ib.push_back(cur_idx - 3);
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx - 1);

            ib.push_back(cur_idx - 1);
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx);

            vb.emplace_back(r2 * std::cos(angle + 2 * da), r2 * std::sin(angle + 2 * da), width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + 2 * da), r2 * std::sin(angle + 2 * da), -width * 0.5f);

            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);
            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);

            cur_idx = vb.size() - 1;
            ib.push_back(cur_idx - 3);
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx - 1);

            ib.push_back(cur_idx - 1);
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx);

            vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), -width * 0.5f);

            uv = ml::vec4{
              r1 * std::sin(angle + 3 * da) - r2 * std::sin(angle + 2 * da),
              r1 * std::cos(angle + 3 * da) - r2 * std::cos(angle + 2 * da),
              0, 0};
            nb.emplace_back(uv.normalized());
            nb.emplace_back(uv.normalized());

            cur_idx = vb.size() - 1;
            ib.push_back(cur_idx - 3);
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx - 1);

            ib.push_back(cur_idx - 1);
            ib.push_back(cur_idx - 2);
            ib.push_back(cur_idx);
        }

        vb.emplace_back(r1 * std::cos(0.f), r1 * std::sin(0.f), width * 0.5f);
        vb.emplace_back(r1 * std::cos(0.f), r1 * std::sin(0.f), -width * 0.5f);

        nb.emplace_back(r1 * std::cos(0.f), r1 * std::sin(0.f), 0.0f, 0.0f);
        nb.emplace_back(r1 * std::cos(0.f), r1 * std::sin(0.f), 0.0f, 0.0f);

        auto cur_idx = vb.size() - 1;
        ib.push_back(cur_idx - 3);
        ib.push_back(cur_idx - 2);
        ib.push_back(cur_idx - 1);

        ib.push_back(cur_idx - 1);
        ib.push_back(cur_idx - 2);
        ib.push_back(cur_idx);

        /* draw inside radius cylinder */
        for(int i = 0; i <= teeth; i++)
        {
            float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), -width * 0.5f);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), width * 0.5f);

            nb.emplace_back(-std::cos(angle), -std::sin(angle), 0.f);
            nb.emplace_back(-std::cos(angle), -std::sin(angle), 0.f);

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
        }

        return {swr::CreateIndexBuffer(ib), swr::CreateAttributeBuffer(vb), swr::CreateAttributeBuffer(nb)};
    }

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

        swr::SetClearColor(0, 0, 0, 0);
        swr::SetClearDepth(1.0f);
        swr::SetViewport(0, 0, width, height);

        swr::SetState(swr::state::cull_face, true);
        swr::SetState(swr::state::depth_test, true);

        shader_ids[0] = swr::RegisterShader(&shader_red);
        if(!shader_ids[0])
        {
            throw std::runtime_error("shader registration failed (red)");
        }
        shader_ids[1] = swr::RegisterShader(&shader_green);
        if(!shader_ids[1])
        {
            throw std::runtime_error("shader registration failed (green)");
        }
        shader_ids[2] = swr::RegisterShader(&shader_blue);
        if(!shader_ids[2])
        {
            throw std::runtime_error("shader registration failed (blue)");
        }

        // set projection matrix.
        proj = ml::matrices::perspective_projection(static_cast<float>(width) / static_cast<float>(height), static_cast<float>(M_PI) / 8, 5.f, 60.f);

        //!!todo: create gears.
        gears[0] = make_gear(1.0, 4.0, 1.0, 20, 0.7);
        gears[1] = make_gear(0.5, 2.0, 2.0, 10, 0.7);
        gears[2] = make_gear(1.3, 2.0, 0.5, 10, 0.7);

        return true;
    }

    void destroy()
    {
        gears[0].release();
        gears[1].release();
        gears[2].release();

        for(int i = 0; i < 3; ++i)
        {
            if(shader_ids[i])
            {
                if(context)
                {
                    swr::UnregisterShader(shader_ids[i]);
                }
                shader_ids[i] = 0;
            }
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

        // radians to angles.
        auto view_radians = view_rotation * static_cast<float>(M_PI) / 180.f;
        view *= ml::matrices::rotation_x(view_radians.x);
        view *= ml::matrices::rotation_y(view_radians.y);
        view *= ml::matrices::rotation_z(view_radians.z);

        /*
         * gear 1
         */
        ml::mat4x4 temp = view;
        temp *= ml::matrices::translation(-3.f, -2.f, 0.f);
        temp *= ml::matrices::rotation_z(gear_rotation);

        swr::BindShader(shader_ids[0]);
        swr::BindUniform(1, temp);
        gears[0].draw();

        /*
         * gear 2
         */
        temp = view;
        temp *= ml::matrices::translation(3.1f, -2.f, 0.f);
        temp *= ml::matrices::rotation_z(-2.f * gear_rotation - 9.f);

        swr::BindShader(shader_ids[1]);
        swr::BindUniform(1, temp);
        gears[1].draw();

        /*
         * gear 3
         */
        temp = view;
        temp *= ml::matrices::translation(-3.1f, 4.2f, 0.f);
        temp *= ml::matrices::rotation_z(-2.f * gear_rotation - 25.f);

        swr::BindShader(shader_ids[2]);
        swr::BindUniform(1, temp);
        gears[2].draw();

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
