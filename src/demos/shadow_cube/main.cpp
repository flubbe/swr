/**
 * swr - a software rasterizer
 *
 * shadow mapping demonstration with a rotating cube and plane receiver.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <print>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

/* software rasterizer headers. */
#include "swr/swr.h"
#include "swr/shaders.h"

/* shaders for this demo. */
#include "shader.h"

#include "swr_app/framework.h"        /* application framework. */
#include "common/platform/platform.h" /* logging. */

/** demo title. */
const auto demo_title = "Shadow Mapping Cube";

namespace
{
const std::array<ml::vec3, 8> cube_corners{{
  {-1.0f, -1.0f, -1.0f},
  {-1.0f, -1.0f, 1.0f},
  {-1.0f, 1.0f, -1.0f},
  {-1.0f, 1.0f, 1.0f},
  {1.0f, -1.0f, -1.0f},
  {1.0f, -1.0f, 1.0f},
  {1.0f, 1.0f, -1.0f},
  {1.0f, 1.0f, 1.0f},
}};

const std::array<ml::vec3, 4> plane_corners{{
  {-5.0f, 0.0f, -5.0f},
  {5.0f, 0.0f, -5.0f},
  {5.0f, 0.0f, 5.0f},
  {-5.0f, 0.0f, 5.0f},
}};

ml::vec3 transform_point(
  const ml::mat4x4& transform,
  const ml::vec3& point)
{
    return (transform * ml::vec4{point, 1.0f}).xyz();
}

ml::mat4x4 shadow_bias_matrix()
{
    return {
      {0.5f, 0.0f, 0.0f, 0.5f},
      {0.0f, 0.5f, 0.0f, 0.5f},
      {0.0f, 0.0f, 0.5f, 0.5f},
      {0.0f, 0.0f, 0.0f, 1.0f}};
}

float elapsed_milliseconds(
  std::chrono::steady_clock::time_point start,
  std::chrono::steady_clock::time_point end)
{
    return std::chrono::duration<float, std::milli>(end - start).count();
}

} /* namespace */

class demo_shadow_cube : public swr_app::renderwindow
{
    static constexpr int width = 640;
    static constexpr int height = 480;
    static constexpr int shadow_map_size = 1024;

    shader::depth_pass depth_shader;
    shader::scene_pass scene_shader;
    shader::debug_depth debug_shader;

    std::uint32_t depth_shader_id{0};
    std::uint32_t scene_shader_id{0};
    std::uint32_t debug_shader_id{0};

    std::uint32_t cube_verts{0};
    std::uint32_t cube_colors{0};
    std::uint32_t cube_normals{0};
    std::vector<std::uint32_t> cube_indices;

    std::uint32_t plane_verts{0};
    std::uint32_t plane_colors{0};
    std::uint32_t plane_normals{0};
    std::vector<std::uint32_t> plane_indices;

    std::uint32_t quad_verts{0};
    std::uint32_t quad_uvs{0};
    std::vector<std::uint32_t> quad_indices{0, 1, 2, 0, 2, 3};

    std::uint32_t shadow_texture{0};
    std::uint32_t shadow_fbo{0};

    ml::mat4x4 proj;
    ml::mat4x4 view;
    ml::mat4x4 light_proj;
    ml::mat4x4 light_view;
    ml::vec3 light_direction{0.45f, -1.0f, -0.35f};

    float cube_rotation{0.0f};
    bool show_depth_overlay{true};
    bool enable_pcf{true};
    bool enable_rotation{true};
    bool enable_stats_logging{false};
    float stats_log_elapsed{0.0f};
    std::uint32_t stats_log_frames{0};
    float stats_log_update_light_ms{0.0f};
    float stats_log_shadow_pass_ms{0.0f};
    float stats_log_scene_pass_ms{0.0f};
    float stats_log_copy_ms{0.0f};
    std::uint32_t frame_count{0};

public:
    demo_shadow_cube()
    : swr_app::renderwindow{demo_title, width, height}
    {
    }

    bool create() override
    {
        if(!renderwindow::create())
        {
            return false;
        }

        int thread_hint =
          swr_app::application::get_instance().get_argument("--threads", 0);
        context = swr::CreateSDLContext(sdl_window, sdl_renderer, thread_hint);
        if(!swr::MakeContextCurrent(context))
        {
            throw std::runtime_error("MakeContextCurrent failed");
        }

        swr::SetState(swr::state::cull_face, true);
        swr::SetState(swr::state::depth_test, true);
        swr::SetClearColor(0.08f, 0.09f, 0.12f, 1.0f);
        swr::SetClearDepth(1.0f);
        swr::SetViewport(0, 0, width, height);

        depth_shader_id = swr::RegisterShader(&depth_shader);
        scene_shader_id = swr::RegisterShader(&scene_shader);
        debug_shader_id = swr::RegisterShader(&debug_shader);
        if(!depth_shader_id || !scene_shader_id || !debug_shader_id)
        {
            throw std::runtime_error("shader registration failed");
        }

        proj = ml::matrices::perspective_projection(
          static_cast<float>(width) / static_cast<float>(height),
          static_cast<float>(M_PI) / 3.0f,
          0.5f,
          16.0f);
        view = ml::matrices::look_at(
          ml::vec3{4.0f, 3.0f, 6.5f},
          ml::vec3{0.0f, -0.2f, 0.0f},
          ml::vec3{0.0f, 1.0f, 0.0f});

        update_light_matrices();

        create_cube();
        create_plane();
        create_debug_quad();
        create_shadow_resources();

        return true;
    }

    void destroy() override
    {
        if(context != nullptr)
        {
            if(shadow_fbo)
            {
                swr::ReleaseFramebufferObject(shadow_fbo);
            }
            if(shadow_texture)
            {
                swr::ReleaseTexture(shadow_texture);
            }

            if(quad_uvs)
            {
                swr::DeleteAttributeBuffer(quad_uvs);
            }
            if(quad_verts)
            {
                swr::DeleteAttributeBuffer(quad_verts);
            }

            if(plane_normals)
            {
                swr::DeleteAttributeBuffer(plane_normals);
            }
            if(plane_colors)
            {
                swr::DeleteAttributeBuffer(plane_colors);
            }
            if(plane_verts)
            {
                swr::DeleteAttributeBuffer(plane_verts);
            }

            if(cube_normals)
            {
                swr::DeleteAttributeBuffer(cube_normals);
            }
            if(cube_colors)
            {
                swr::DeleteAttributeBuffer(cube_colors);
            }
            if(cube_verts)
            {
                swr::DeleteAttributeBuffer(cube_verts);
            }

            if(debug_shader_id)
            {
                swr::UnregisterShader(debug_shader_id);
            }
            if(scene_shader_id)
            {
                swr::UnregisterShader(scene_shader_id);
            }
            if(depth_shader_id)
            {
                swr::UnregisterShader(depth_shader_id);
            }

            debug_shader_id = 0;
            scene_shader_id = 0;
            depth_shader_id = 0;

            swr::DestroyContext(context);
            context = nullptr;
        }

        renderwindow::destroy();
    }

    void update(float delta_time) override
    {
        SDL_Event e;
        while(SDL_PollEvent(&e))
        {
            if(e.type == SDL_EVENT_QUIT)
            {
                swr_app::application::quit();
                return;
            }

            if(e.type == SDL_EVENT_KEY_DOWN
               && e.key.key == SDLK_D)
            {
                show_depth_overlay = !show_depth_overlay;
            }

            if(e.type == SDL_EVENT_KEY_DOWN
               && e.key.key == SDLK_P)
            {
                enable_pcf = !enable_pcf;
                std::println("PCF: {}", enable_pcf ? "ON" : "OFF");
            }

            if(e.type == SDL_EVENT_KEY_DOWN
               && e.key.key == SDLK_SPACE)
            {
                enable_rotation = !enable_rotation;
                std::println("Rotation: {}", enable_rotation ? "ON" : "OFF");
            }

            if(e.type == SDL_EVENT_KEY_DOWN
               && e.key.key == SDLK_S)
            {
                enable_stats_logging = !enable_stats_logging;
                stats_log_elapsed = 0.0f;
                stats_log_frames = 0;
                stats_log_update_light_ms = 0.0f;
                stats_log_shadow_pass_ms = 0.0f;
                stats_log_scene_pass_ms = 0.0f;
                stats_log_copy_ms = 0.0f;
                std::println("Stats logging: {}", enable_stats_logging ? "ON" : "OFF");
            }
        }

        if(enable_rotation)
        {
            cube_rotation += 0.4f * delta_time;
            if(cube_rotation > 2.0f * static_cast<float>(M_PI))
            {
                cube_rotation -= 2.0f * static_cast<float>(M_PI);
            }
        }

        float update_light_ms = 0.0f;
        float shadow_pass_ms = 0.0f;
        float scene_pass_ms = 0.0f;
        float present_ms = 0.0f;

        {
            const auto start = std::chrono::steady_clock::now();
            update_light_matrices();
            update_light_ms =
              elapsed_milliseconds(start, std::chrono::steady_clock::now());
        }
        {
            const auto start = std::chrono::steady_clock::now();
            render_shadow_pass();
            shadow_pass_ms =
              elapsed_milliseconds(start, std::chrono::steady_clock::now());
        }
        {
            const auto start = std::chrono::steady_clock::now();
            render_scene_pass();
            scene_pass_ms =
              elapsed_milliseconds(start, std::chrono::steady_clock::now());
        }
        {
            const auto start = std::chrono::steady_clock::now();
            swr::CopyDefaultColorBuffer(context);
            present_ms =
              elapsed_milliseconds(start, std::chrono::steady_clock::now());
        }

        if(enable_stats_logging)
        {
            stats_log_elapsed += delta_time;
            ++stats_log_frames;
            stats_log_update_light_ms += update_light_ms;
            stats_log_shadow_pass_ms += shadow_pass_ms;
            stats_log_scene_pass_ms += scene_pass_ms;
            stats_log_copy_ms += present_ms;
            if(stats_log_elapsed >= 1.0f)
            {
                const float average_fps =
                  static_cast<float>(stats_log_frames) / stats_log_elapsed;
                const float average_update_light_ms =
                  stats_log_update_light_ms / static_cast<float>(stats_log_frames);
                const float average_shadow_pass_ms =
                  stats_log_shadow_pass_ms / static_cast<float>(stats_log_frames);
                const float average_scene_pass_ms =
                  stats_log_scene_pass_ms / static_cast<float>(stats_log_frames);
                const float average_copy_ms =
                  stats_log_copy_ms / static_cast<float>(stats_log_frames);
                std::println(
                  "Average FPS: {:.2f}    frame: {:.2f} ms    light: {:.2f} ms    shadow: {:.2f} ms    scene: {:.2f} ms    copy: {:.2f} ms",
                  average_fps,
                  1000.f / average_fps,
                  average_update_light_ms,
                  average_shadow_pass_ms,
                  average_scene_pass_ms,
                  average_copy_ms);
                stats_log_elapsed = 0.0f;
                stats_log_frames = 0;
                stats_log_update_light_ms = 0.0f;
                stats_log_shadow_pass_ms = 0.0f;
                stats_log_scene_pass_ms = 0.0f;
                stats_log_copy_ms = 0.0f;
            }
        }

        ++frame_count;
    }

    int get_frame_count() const
    {
        return static_cast<int>(frame_count);
    }

private:
    void create_cube()
    {
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
        for(auto& v: vertices)
        {
            v.w = 1.0f;
        }
        cube_verts = swr::CreateAttributeBuffer(vertices);

        std::vector<ml::vec4> colors = {
#define COLOR_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef COLOR_LIST
        };
        cube_colors = swr::CreateAttributeBuffer(colors);

        std::vector<ml::vec4> normals = {
#define NORMAL_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef NORMAL_LIST
        };
        cube_normals = swr::CreateAttributeBuffer(normals);
    }

    void create_plane()
    {
        std::vector<std::uint32_t> indices = {
#define FACE_LIST(...) __VA_ARGS__
#include "common/plane.geom"
#undef FACE_LIST
        };
        plane_indices = std::move(indices);

        std::vector<ml::vec4> vertices = {
#define VERTEX_LIST(...) __VA_ARGS__
#include "common/plane.geom"
#undef VERTEX_LIST
        };
        for(auto& v: vertices)
        {
            v.w = 1.0f;
        }
        plane_verts = swr::CreateAttributeBuffer(vertices);

        std::vector<ml::vec4> normals = {
#define NORMAL_LIST(...) __VA_ARGS__
#include "common/plane.geom"
#undef NORMAL_LIST
        };
        plane_normals = swr::CreateAttributeBuffer(normals);

        std::vector<ml::vec4> colors(
          vertices.size(),
          ml::vec4{0.82f, 0.78f, 0.67f, 1.0f});
        plane_colors = swr::CreateAttributeBuffer(colors);
    }

    void create_debug_quad()
    {
        std::vector<ml::vec4> quad_positions{
          {-1.0f, -1.0f, 0.0f, 1.0f},
          {1.0f, -1.0f, 0.0f, 1.0f},
          {1.0f, 1.0f, 0.0f, 1.0f},
          {-1.0f, 1.0f, 0.0f, 1.0f}};
        std::vector<ml::vec4> quad_texcoords{
          {0.0f, 0.0f, 0.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f},
          {1.0f, 1.0f, 0.0f, 0.0f},
          {0.0f, 1.0f, 0.0f, 0.0f}};
        quad_verts = swr::CreateAttributeBuffer(quad_positions);
        quad_uvs = swr::CreateAttributeBuffer(quad_texcoords);
    }

    void create_shadow_resources()
    {
        shadow_texture = swr::CreateTexture();
        shadow_fbo = swr::CreateFramebufferObject();
        if(!shadow_texture || !shadow_fbo)
        {
            throw std::runtime_error("shadow resources creation failed");
        }

        swr::SetImage(
          shadow_texture,
          0,
          shadow_map_size,
          shadow_map_size,
          swr::pixel_format::depth32f,
          {});
        swr::BindTexture(
          swr::texture_target::texture_2d,
          shadow_texture);
        swr::SetTextureMagnificationFilter(swr::texture_filter::nearest);
        swr::SetTextureMinificationFilter(swr::texture_filter::nearest);
        swr::BindTexture(
          swr::texture_target::texture_2d,
          0);
        swr::SetTextureWrapMode(
          shadow_texture,
          swr::wrap_mode::clamp_to_edge,
          swr::wrap_mode::clamp_to_edge);
        swr::SetTextureCompareMode(
          shadow_texture,
          swr::texture_compare_mode::ref_to_texture);
        swr::SetTextureCompareFunc(
          shadow_texture,
          swr::comparison_func::less_equal);
        swr::FramebufferTexture(
          shadow_fbo,
          swr::framebuffer_attachment::depth_attachment,
          shadow_texture,
          0);

        if(const auto error = swr::GetLastError(); error != swr::error::none)
        {
            throw std::runtime_error("shadow framebuffer setup failed");
        }
    }

    ml::mat4x4 cube_model_matrix() const
    {
        ml::mat4x4 model = ml::mat4x4::identity();
        model *= ml::matrices::translation(0.0f, 0.4f, 0.0f);
        model *= ml::matrices::rotation_y(2 * cube_rotation);
        model *= ml::matrices::rotation_x(cube_rotation);
        model *= ml::matrices::scaling(0.85f);
        return model;
    }

    ml::mat4x4 plane_model_matrix() const
    {
        ml::mat4x4 model = ml::mat4x4::identity();
        model *= ml::matrices::translation(0.0f, -1.5f, 0.0f);
        model *= ml::matrices::scaling(1.0f, 1.0f, 1.0f);
        return model;
    }

    void update_light_matrices()
    {
        const ml::mat4x4 cube_model = cube_model_matrix();
        const ml::mat4x4 plane_model = plane_model_matrix();

        std::array<ml::vec3, cube_corners.size() + plane_corners.size()> world_points{};
        std::size_t world_point_count = 0;

        for(const auto& point: cube_corners)
        {
            world_points[world_point_count++] = transform_point(cube_model, point);
        }
        for(const auto& point: plane_corners)
        {
            world_points[world_point_count++] = transform_point(plane_model, point);
        }

        ml::vec3 scene_center = ml::vec3::zero();
        for(std::size_t i = 0; i < world_point_count; ++i)
        {
            scene_center += world_points[i];
        }
        scene_center /= static_cast<float>(world_point_count);

        float scene_radius = 0.0f;
        for(std::size_t i = 0; i < world_point_count; ++i)
        {
            scene_radius = std::max(
              scene_radius,
              (world_points[i] - scene_center).length());
        }

        const ml::vec3 light_dir = light_direction.normalized();
        const ml::vec3 light_eye = scene_center - light_dir * 8.0f;
        light_view = ml::matrices::look_at(
          light_eye,
          scene_center,
          ml::vec3{0.0f, 1.0f, 0.0f});

        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float min_z = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        float max_z = std::numeric_limits<float>::lowest();

        for(std::size_t i = 0; i < world_point_count; ++i)
        {
            const ml::vec3 light_space_point =
              (light_view * ml::vec4{world_points[i], 1.0f}).xyz();
            min_x = std::min(min_x, light_space_point.x);
            min_y = std::min(min_y, light_space_point.y);
            min_z = std::min(min_z, light_space_point.z);
            max_x = std::max(max_x, light_space_point.x);
            max_y = std::max(max_y, light_space_point.y);
            max_z = std::max(max_z, light_space_point.z);
        }

        constexpr float xy_margin = 0.75f;
        constexpr float z_margin = 1.5f;
        const float stable_half_extent = scene_radius + xy_margin;
        const float stable_extent = 2.0f * stable_half_extent;
        const ml::vec3 light_space_center =
          (light_view * ml::vec4{scene_center, 1.0f}).xyz();
        const float texel_size =
          stable_extent / static_cast<float>(shadow_map_size);

        const float snapped_center_x =
          std::floor(light_space_center.x / texel_size + 0.5f) * texel_size;
        const float snapped_center_y =
          std::floor(light_space_center.y / texel_size + 0.5f) * texel_size;

        min_x = snapped_center_x - stable_half_extent;
        max_x = snapped_center_x + stable_half_extent;
        min_y = snapped_center_y - stable_half_extent;
        max_y = snapped_center_y + stable_half_extent;

        const float near_plane =
          std::max(0.25f, -max_z - z_margin);
        const float far_plane =
          std::max(near_plane + 0.5f, -min_z + z_margin);

        light_proj = ml::matrices::orthographic_projection(
          min_x,
          max_x,
          min_y,
          max_y,
          near_plane,
          far_plane);
    }

    void draw_object(
      std::uint32_t verts,
      std::uint32_t colors,
      std::uint32_t normals,
      const std::vector<std::uint32_t>& indices,
      const ml::mat4x4& model)
    {
        swr::EnableAttributeBuffer(verts, 0);
        swr::EnableAttributeBuffer(colors, 1);
        swr::EnableAttributeBuffer(normals, 2);

        const ml::mat4x4 camera_vp = proj * view;
        const ml::mat4x4 light_vp = light_proj * light_view;
        const ml::mat4x4 shadow_bias_light_vp = shadow_bias_matrix() * light_vp;

        swr::BindUniform(0, camera_vp * model);
        swr::BindUniform(1, model);
        swr::BindUniform(2, shadow_bias_light_vp * model);
        swr::BindUniform(3, ml::vec4{light_direction.normalized(), 0.0f});
        swr::BindUniform(4, enable_pcf ? 1.0f : 0.0f);
        swr::BindUniform(5, shadow_bias_light_vp);
        swr::BindUniform(
          6,
          1.0f / static_cast<float>(shadow_map_size));

        swr::DrawIndexedElements(
          swr::vertex_buffer_mode::triangles,
          indices.size(),
          indices);

        swr::DisableAttributeBuffer(normals);
        swr::DisableAttributeBuffer(colors);
        swr::DisableAttributeBuffer(verts);
    }

    void draw_depth_object(
      std::uint32_t verts,
      const std::vector<std::uint32_t>& indices,
      const ml::mat4x4& model)
    {
        swr::EnableAttributeBuffer(verts, 0);
        swr::BindUniform(0, (light_proj * light_view) * model);

        swr::DrawIndexedElements(
          swr::vertex_buffer_mode::triangles,
          indices.size(),
          indices);

        swr::DisableAttributeBuffer(verts);
    }

    void render_shadow_pass()
    {
        swr::BindFramebufferObject(swr::framebuffer_target::draw, shadow_fbo);
        swr::SetViewport(0, 0, shadow_map_size, shadow_map_size);
        swr::SetState(swr::state::cull_face, true);
        swr::SetState(swr::state::depth_test, true);
        swr::SetState(swr::state::polygon_offset_fill, true);
        swr::PolygonOffset(2.0f, 4.0f);
        swr::ClearDepthBuffer();

        swr::BindShader(depth_shader_id);
        draw_depth_object(cube_verts, cube_indices, cube_model_matrix());
        draw_depth_object(plane_verts, plane_indices, plane_model_matrix());
        swr::BindShader(0);
        swr::SetState(swr::state::polygon_offset_fill, false);

        swr::Present();
    }

    void render_scene_pass()
    {
        swr::BindFramebufferObject(swr::framebuffer_target::draw, 0);
        swr::SetViewport(0, 0, width, height);
        swr::SetState(swr::state::depth_test, true);
        swr::SetState(swr::state::cull_face, true);
        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();

        swr::BindShader(scene_shader_id);
        swr::BindTexture(swr::texture_target::texture_2d, shadow_texture);

        draw_object(
          plane_verts,
          plane_colors,
          plane_normals,
          plane_indices,
          plane_model_matrix());
        draw_object(
          cube_verts,
          cube_colors,
          cube_normals,
          cube_indices,
          cube_model_matrix());

        swr::BindShader(0);

        if(show_depth_overlay)
        {
            render_depth_overlay();
        }

        swr::Present();
    }

    void render_depth_overlay()
    {
        constexpr int overlay_size = 170;
        constexpr int overlay_margin = 14;

        swr::SetState(swr::state::depth_test, false);
        swr::SetState(swr::state::cull_face, false);
        swr::SetViewport(
          width - overlay_size - overlay_margin,
          overlay_margin,
          overlay_size,
          overlay_size);

        swr::BindShader(debug_shader_id);
        swr::BindTexture(swr::texture_target::texture_2d, shadow_texture);
        swr::EnableAttributeBuffer(quad_verts, 0);
        swr::EnableAttributeBuffer(quad_uvs, 1);
        swr::DrawIndexedElements(
          swr::vertex_buffer_mode::triangles,
          quad_indices.size(),
          quad_indices);
        swr::DisableAttributeBuffer(quad_uvs);
        swr::DisableAttributeBuffer(quad_verts);
        swr::BindShader(0);

        swr::SetViewport(0, 0, width, height);
        swr::SetState(swr::state::depth_test, true);
        swr::SetState(swr::state::cull_face, true);
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

class demo_app final : public swr_app::application
{
    log_std log;

public:
    void initialize()
    {
        application::initialize();
        platform::set_log(&log);

        window = std::make_unique<demo_shadow_cube>();
        window->create();

        std::print(
          "SPACE    Toggle cube rotation\n"
          "D        Toggle shadow map display\n"
          "P        Toggle PCF\n"
          "S        Toggle stats logging\n");
    }

    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_shadow_cube*>(window.get());
            const float fps =
              static_cast<float>(w->get_frame_count()) / get_run_time();
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
