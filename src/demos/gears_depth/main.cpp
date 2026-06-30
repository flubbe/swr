/**
 * swr - a software rasterizer
 *
 * software renderer demonstration rendering gears into a depth texture and displaying it.
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
const auto demo_title = "Gears Depth Texture";

/** collect a set of geometric data into a single object. */
class drawable_object
{
    std::vector<std::uint32_t> index_buffer;
    std::uint32_t vertex_buffer_id{0};
    std::uint32_t normal_buffer_id{0};
    bool has_data{false};

public:
    drawable_object() = default;

    drawable_object(
      std::vector<std::uint32_t> in_ib,
      std::uint32_t in_vb,
      std::uint32_t in_nb)
    : index_buffer{std::move(in_ib)}
    , vertex_buffer_id{in_vb}
    , normal_buffer_id{in_nb}
    , has_data{true}
    {
    }

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

struct gear_object
{
    drawable_object outside;
    drawable_object cylinder;
    shader::color_flat flat_shader;
    shader::color_smooth smooth_shader;
    std::uint32_t flat_shader_id{0};
    std::uint32_t smooth_shader_id{0};

    void release()
    {
        outside.release();
        cylinder.release();
        swr::UnregisterShader(flat_shader_id);
        swr::UnregisterShader(smooth_shader_id);
        flat_shader_id = 0;
        smooth_shader_id = 0;
    }

    void draw() const
    {
        swr::BindShader(flat_shader_id);
        outside.draw();

        swr::BindShader(smooth_shader_id);
        cylinder.draw();
    }

    void make_gear(
      float inner_radius,
      float outer_radius,
      float width,
      int teeth,
      float tooth_depth,
      ml::vec4 color)
    {
        release();

        float r0 = inner_radius;
        float r1 = outer_radius - tooth_depth / 2.f;
        float r2 = outer_radius + tooth_depth / 2.f;

        float da = 2.f * static_cast<float>(M_PI / teeth) / 4.f;

        std::vector<ml::vec4> vb;
        std::vector<ml::vec4> nb;
        std::vector<std::uint32_t> ib;

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
                ib.emplace_back(cur_idx - 1);
                ib.emplace_back(cur_idx - 3);
                ib.emplace_back(cur_idx - 2);

                ib.emplace_back(cur_idx - 1);
                ib.emplace_back(cur_idx - 2);
                ib.emplace_back(cur_idx);
            }

            if(i < teeth)
            {
                vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), width * 0.5f);
                vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), width * 0.5f);

                nb.emplace_back(0, 0, 1, 0);
                nb.emplace_back(0, 0, 1, 0);

                auto cur_idx = vb.size() - 1;
                ib.emplace_back(cur_idx - 2);
                ib.emplace_back(cur_idx - 1);
                ib.emplace_back(cur_idx - 3);

                ib.emplace_back(cur_idx - 1);
                ib.emplace_back(cur_idx - 2);
                ib.emplace_back(cur_idx);
            }
        }

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
            ib.emplace_back(cur_idx - 3);
            ib.emplace_back(cur_idx - 2);
            ib.emplace_back(cur_idx - 1);

            ib.emplace_back(cur_idx - 3);
            ib.emplace_back(cur_idx - 1);
            ib.emplace_back(cur_idx);
        }

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
                ib.emplace_back(cur_idx - 3);
                ib.emplace_back(cur_idx - 2);
                ib.emplace_back(cur_idx - 1);

                ib.emplace_back(cur_idx - 1);
                ib.emplace_back(cur_idx - 2);
                ib.emplace_back(cur_idx);
            }

            if(i < teeth)
            {
                vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), -width * 0.5f);
                vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), -width * 0.5f);

                nb.emplace_back(0, 0, -1, 0);
                nb.emplace_back(0, 0, -1, 0);

                auto cur_idx = vb.size() - 1;
                ib.emplace_back(cur_idx - 3);
                ib.emplace_back(cur_idx - 1);
                ib.emplace_back(cur_idx - 2);

                ib.emplace_back(cur_idx - 2);
                ib.emplace_back(cur_idx - 1);
                ib.emplace_back(cur_idx);
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
            ib.emplace_back(cur_idx - 3);
            ib.emplace_back(cur_idx - 2);
            ib.emplace_back(cur_idx - 1);

            ib.emplace_back(cur_idx - 3);
            ib.emplace_back(cur_idx - 1);
            ib.emplace_back(cur_idx);
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
                ib.emplace_back(cur_idx - 2);
                ib.emplace_back(cur_idx - 1);
                ib.emplace_back(cur_idx - 3);

                ib.emplace_back(cur_idx - 2);
                ib.emplace_back(cur_idx);
                ib.emplace_back(cur_idx - 1);
            }

            vb.emplace_back(r2 * std::cos(angle + da), r2 * std::sin(angle + da), width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + da), r2 * std::sin(angle + da), -width * 0.5f);

            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);
            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);

            auto cur_idx = vb.size() - 1;
            ib.emplace_back(cur_idx - 2);
            ib.emplace_back(cur_idx - 1);
            ib.emplace_back(cur_idx - 3);

            ib.emplace_back(cur_idx - 2);
            ib.emplace_back(cur_idx);
            ib.emplace_back(cur_idx - 1);

            vb.emplace_back(r2 * std::cos(angle + 2 * da), r2 * std::sin(angle + 2 * da), width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + 2 * da), r2 * std::sin(angle + 2 * da), -width * 0.5f);

            uv = ml::vec4{
              r1 * std::sin(angle + 3 * da) - r2 * std::sin(angle + 2 * da),
              -r1 * std::cos(angle + 3 * da) + r2 * std::cos(angle + 2 * da),
              0, 0};
            nb.emplace_back(uv.normalized());
            nb.emplace_back(uv.normalized());

            cur_idx = vb.size() - 1;
            ib.emplace_back(cur_idx - 3);
            ib.emplace_back(cur_idx - 2);
            ib.emplace_back(cur_idx - 1);

            ib.emplace_back(cur_idx - 2);
            ib.emplace_back(cur_idx);
            ib.emplace_back(cur_idx - 1);

            vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), -width * 0.5f);

            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);
            nb.emplace_back(std::cos(angle), std::sin(angle), 0, 0);

            cur_idx = vb.size() - 1;
            ib.emplace_back(cur_idx - 2);
            ib.emplace_back(cur_idx - 1);
            ib.emplace_back(cur_idx - 3);

            ib.emplace_back(cur_idx - 2);
            ib.emplace_back(cur_idx);
            ib.emplace_back(cur_idx - 1);
        }

        vb.emplace_back(r1 * std::cos(0.f), r1 * std::sin(0.f), width * 0.5f);
        vb.emplace_back(r1 * std::cos(0.f), r1 * std::sin(0.f), -width * 0.5f);

        nb.emplace_back(std::cos(0.f), std::sin(0.f), 0, 0);
        nb.emplace_back(std::cos(0.f), std::sin(0.f), 0, 0);

        auto cur_idx = vb.size() - 1;
        ib.emplace_back(cur_idx - 2);
        ib.emplace_back(cur_idx - 1);
        ib.emplace_back(cur_idx - 3);

        ib.emplace_back(cur_idx - 2);
        ib.emplace_back(cur_idx);
        ib.emplace_back(cur_idx - 1);

        outside = drawable_object{
          std::move(ib),
          swr::CreateAttributeBuffer(vb),
          swr::CreateAttributeBuffer(nb)};

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
                auto inner_idx = vb.size() - 1;
                ib.emplace_back(inner_idx - 2);
                ib.emplace_back(inner_idx - 1);
                ib.emplace_back(inner_idx - 3);

                ib.emplace_back(inner_idx - 2);
                ib.emplace_back(inner_idx);
                ib.emplace_back(inner_idx - 1);
            }
        }

        cylinder = drawable_object{
          std::move(ib),
          swr::CreateAttributeBuffer(vb),
          swr::CreateAttributeBuffer(nb)};

        flat_shader = shader::color_flat{color};
        smooth_shader = shader::color_smooth{color};
        flat_shader_id = swr::RegisterShader(&flat_shader);
        smooth_shader_id = swr::RegisterShader(&smooth_shader);
    }
};

class demo_gears_depth_texture : public swr_app::renderwindow
{
    ml::vec4 light_pos{5.0f, 5.0f, 10.0f, 0.0f};
    ml::mat4x4 scene_proj;
    gear_object gears[3];
    ml::vec3 view_rotation = {20.f, 30.f, 0.f};
    float gear_rotation{0.0f};
    std::uint32_t frame_count{0};

    shader::depth_texture_display display_shader;
    std::uint32_t display_shader_id{0};
    std::uint32_t quad_vertices{0};
    std::uint32_t quad_uvs{0};
    std::vector<std::uint32_t> quad_indices{0, 1, 2, 0, 2, 3};

    std::uint32_t scene_fbo{0};
    std::uint32_t scene_color_texture{0};
    std::uint32_t scene_depth_texture{0};

    static constexpr int width = 800;
    static constexpr int height = 400;
    static constexpr int depth_texture_width = 512;
    static constexpr int depth_texture_height = 512;
    static constexpr float near_plane = 5.0f;
    static constexpr float far_plane = 60.0f;

public:
    demo_gears_depth_texture()
    : swr_app::renderwindow{demo_title, width, height}
    {
    }

    bool create()
    {
        if(!renderwindow::create())
        {
            return false;
        }

        int thread_hint = swr_app::application::get_instance().get_argument("--threads", 0);
        if(thread_hint > 0)
        {
            platform::logf(
              "suggesting rasterizer to use {} thread{}",
              thread_hint,
              ((thread_hint > 1) ? "s" : ""));
        }

        context = swr::CreateSDLContext(sdl_window, sdl_renderer, thread_hint);
        if(!swr::MakeContextCurrent(context))
        {
            throw std::runtime_error("MakeContextCurrent failed");
        }

        swr::SetClearColor(0.03f, 0.03f, 0.05f, 1.0f);
        swr::SetClearDepth(1.0f);
        swr::SetViewport(0, 0, width, height);
        swr::SetState(swr::state::cull_face, true);
        swr::SetState(swr::state::depth_test, true);

        scene_proj = ml::matrices::perspective_projection(
          static_cast<float>(depth_texture_width) / static_cast<float>(depth_texture_height),
          static_cast<float>(M_PI) / 8,
          near_plane,
          far_plane);

        gears[0].make_gear(1.0, 4.0, 1.0, 20, 0.7, {0.8f, 0.1f, 0.0f, 1.0f});
        gears[1].make_gear(0.5, 2.0, 2.0, 10, 0.7, {0.0f, 0.8f, 0.2f, 1.0f});
        gears[2].make_gear(1.3, 2.0, 0.5, 10, 0.7, {0.2f, 0.2f, 1.0f, 1.0f});

        display_shader_id = swr::RegisterShader(&display_shader);

        quad_vertices = swr::CreateAttributeBuffer({
          ml::vec4{-1.0f, -1.0f, 0.0f, 1.0f},
          ml::vec4{1.0f, -1.0f, 0.0f, 1.0f},
          ml::vec4{1.0f, 1.0f, 0.0f, 1.0f},
          ml::vec4{-1.0f, 1.0f, 0.0f, 1.0f},
        });
        quad_uvs = swr::CreateAttributeBuffer({
          ml::vec4{0.0f, 0.0f, 0.0f, 0.0f},
          ml::vec4{1.0f, 0.0f, 0.0f, 0.0f},
          ml::vec4{1.0f, 1.0f, 0.0f, 0.0f},
          ml::vec4{0.0f, 1.0f, 0.0f, 0.0f},
        });

        scene_color_texture = swr::CreateTexture();
        scene_depth_texture = swr::CreateTexture();

        swr::SetImage(
          scene_color_texture,
          0,
          depth_texture_width,
          depth_texture_height,
          swr::pixel_format::rgba8888,
          {});
        swr::SetImage(
          scene_depth_texture,
          0,
          depth_texture_width,
          depth_texture_height,
          swr::pixel_format::depth32f,
          {});
        swr::SetTextureWrapMode(
          scene_color_texture,
          swr::wrap_mode::clamp_to_edge,
          swr::wrap_mode::clamp_to_edge);
        swr::SetTextureWrapMode(
          scene_depth_texture,
          swr::wrap_mode::clamp_to_edge,
          swr::wrap_mode::clamp_to_edge);

        swr::BindTexture(swr::texture_target::texture_2d, scene_depth_texture);
        swr::SetTextureMagnificationFilter(swr::texture_filter::linear);
        swr::SetTextureMinificationFilter(swr::texture_filter::linear);
        swr::BindTexture(swr::texture_target::texture_2d, 0);

        scene_fbo = swr::CreateFramebufferObject();
        swr::FramebufferTexture(
          scene_fbo,
          swr::framebuffer_attachment::color_attachment_0,
          scene_color_texture,
          0);
        swr::FramebufferTexture(
          scene_fbo,
          swr::framebuffer_attachment::depth_attachment,
          scene_depth_texture,
          0);

        return swr::GetLastError() == swr::error::none;
    }

    void destroy()
    {
        gears[0].release();
        gears[1].release();
        gears[2].release();

        if(context != nullptr)
        {
            swr::ReleaseFramebufferObject(scene_fbo);
            swr::ReleaseTexture(scene_depth_texture);
            swr::ReleaseTexture(scene_color_texture);
            swr::DeleteAttributeBuffer(quad_uvs);
            swr::DeleteAttributeBuffer(quad_vertices);
            swr::UnregisterShader(display_shader_id);
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

        gear_rotation += delta_time;
        if(gear_rotation >= 2 * static_cast<float>(M_PI))
        {
            gear_rotation -= 2 * static_cast<float>(M_PI);
        }

        render_depth_scene();
        display_depth_texture();

        ++frame_count;
    }

    void render_depth_scene()
    {
        swr::BindFramebufferObject(swr::framebuffer_target::draw, scene_fbo);
        swr::SetViewport(0, 0, depth_texture_width, depth_texture_height);
        swr::SetState(swr::state::cull_face, true);
        swr::SetState(swr::state::depth_test, true);
        swr::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        swr::SetClearDepth(1.0f);
        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();
        draw_gears(scene_proj);
    }

    void display_depth_texture()
    {
        swr::BindFramebufferObject(swr::framebuffer_target::draw, 0);
        swr::SetViewport(0, 0, width, height);
        swr::SetState(swr::state::cull_face, false);
        swr::SetState(swr::state::depth_test, false);
        swr::SetClearColor(0.03f, 0.03f, 0.05f, 1.0f);
        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();

        swr::ActiveTexture(swr::texture_0);
        swr::BindTexture(swr::texture_target::texture_2d, scene_color_texture);
        swr::ActiveTexture(swr::texture_1);
        swr::BindTexture(swr::texture_target::texture_2d, scene_depth_texture);

        swr::BindShader(display_shader_id);
        swr::BindUniform(0, ml::vec4{near_plane, far_plane, 0.0f, 0.0f});
        swr::EnableAttributeBuffer(quad_vertices, 0);
        swr::EnableAttributeBuffer(quad_uvs, 1);
        swr::DrawIndexedElements(
          swr::vertex_buffer_mode::triangles,
          quad_indices.size(),
          quad_indices);
        swr::DisableAttributeBuffer(quad_uvs);
        swr::DisableAttributeBuffer(quad_vertices);
        swr::BindShader(0);
        swr::ActiveTexture(swr::texture_1);
        swr::BindTexture(swr::texture_target::texture_2d, 0);
        swr::ActiveTexture(swr::texture_0);
        swr::BindTexture(swr::texture_target::texture_2d, 0);

        swr::Present();
        swr::CopyDefaultColorBuffer(context);
    }

    void draw_gears(const ml::mat4x4& proj)
    {
        swr::BindUniform(0, proj);

        ml::mat4x4 view = ml::mat4x4::identity();
        view *= ml::matrices::translation(0.f, 0.f, -40.f);
        swr::BindUniform(2, view * light_pos);

        view *= ml::matrices::rotation_x(ml::to_radians(view_rotation.x));
        view *= ml::matrices::rotation_y(ml::to_radians(view_rotation.y));
        view *= ml::matrices::rotation_z(ml::to_radians(view_rotation.z));

        ml::mat4x4 temp = view;
        temp *= ml::matrices::translation(-3.f, -2.f, 0.f);
        temp *= ml::matrices::rotation_z(gear_rotation);
        swr::BindUniform(1, temp);
        gears[0].draw();

        temp = view;
        temp *= ml::matrices::translation(3.1f, -2.f, 0.f);
        temp *= ml::matrices::rotation_z(-2.f * gear_rotation - 9.f);
        swr::BindUniform(1, temp);
        gears[1].draw();

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

class log_std : public platform::log_device
{
protected:
    void log_n(const std::string& message)
    {
        std::println("{}", message);
    }
};

class demo_app final : public swr_app::application
{
    log_std log;

public:
    void initialize()
    {
        application::initialize();
        platform::set_log(&log);

        window = std::make_unique<demo_gears_depth_texture>();
        window->create();
    }

    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_gears_depth_texture*>(window.get());
            float fps = static_cast<float>(w->get_frame_count()) / get_run_time();
            platform::logf(
              "frames: {}     runtime: {:.2f}s     fps: {:.2f}     msec: {:.2f}",
              w->get_frame_count(),
              get_run_time(),
              fps,
              1000.f / fps);

            window->destroy();
            window.reset();
        }

        platform::set_log(nullptr);
    }
};

demo_app the_app;
