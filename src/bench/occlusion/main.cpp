/**
 * swr - a software rasterizer
 *
 * occlusion benchmark.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <limits>
#include <print>
#include <string>
#include <vector>

#include "swr/swr.h"
#include "swr/shaders.h"

#include "shader.h"

#if defined(__clang__) || defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wnull-dereference"
#    pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_USE_MAPBOX_EARCUT
#include "tiny_obj_loader.h"
#if defined(__clang__) || defined(__GNUC__)
#    pragma GCC diagnostic pop
#endif

namespace
{

struct bench_config
{
    std::string model_path;
    std::uint32_t frames{600};
    std::uint32_t orbit_frames{0};
    std::uint32_t width{400};
    std::uint32_t height{400};
    std::uint32_t threads{0};
    bool cull_face{true};
    bool depth_test{true};
    bool show_window{false};
    swr::rasterizer_feature_mode block_early_depth_reject{swr::rasterizer_feature_mode::automatic};
    swr::rasterizer_feature_mode early_fragment_depth_test{swr::rasterizer_feature_mode::automatic};
    float camera_angle_deg{0.0f};
    float fov_deg{18.0f};
};

struct drawable_object
{
    std::uint32_t vertex_buffer_id{0};
    std::uint32_t color_buffer_id{0};
    std::size_t triangle_count{0};
};

[[noreturn]] void die(const std::string& msg)
{
    std::println(stderr, "error: {}", msg);
    std::exit(1);
}

bool parse_bool(const std::string& value, bool default_value)
{
    if(value == "1" || value == "true" || value == "on")
    {
        return true;
    }
    if(value == "0" || value == "false" || value == "off")
    {
        return false;
    }
    return default_value;
}

swr::rasterizer_feature_mode parse_rasterizer_feature_mode(
  const std::string& value,
  swr::rasterizer_feature_mode default_value)
{
    if(value == "auto" || value == "automatic")
    {
        return swr::rasterizer_feature_mode::automatic;
    }
    if(value == "1" || value == "true" || value == "on" || value == "enabled")
    {
        return swr::rasterizer_feature_mode::on;
    }
    if(value == "0" || value == "false" || value == "off" || value == "disabled")
    {
        return swr::rasterizer_feature_mode::off;
    }
    return default_value;
}

const char* rasterizer_feature_mode_name(swr::rasterizer_feature_mode mode)
{
    switch(mode)
    {
    case swr::rasterizer_feature_mode::automatic:
        return "auto";
    case swr::rasterizer_feature_mode::on:
        return "on";
    case swr::rasterizer_feature_mode::off:
        return "off";
    }
    return "unknown";
}

std::string arg_value(const std::string& arg, const std::string& name)
{
    const std::string prefix = name + "=";
    if(arg.rfind(prefix, 0) == 0)
    {
        return arg.substr(prefix.size());
    }
    return {};
}

bench_config parse_args(int argc, char** argv)
{
    bench_config cfg;
    bool frames_set = false;
    double run_time_seconds = -1.0;
    for(int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if(auto v = arg_value(arg, "--model"); !v.empty())
        {
            cfg.model_path = v;
        }
        else if(auto v = arg_value(arg, "--frames"); !v.empty())
        {
            cfg.frames = static_cast<std::uint32_t>(std::stoul(v));
            frames_set = true;
        }
        else if(auto v = arg_value(arg, "--run_time"); !v.empty())
        {
            run_time_seconds = std::stod(v);
        }
        else if(auto v = arg_value(arg, "--orbit_frames"); !v.empty())
        {
            cfg.orbit_frames = static_cast<std::uint32_t>(std::stoul(v));
        }
        else if(auto v = arg_value(arg, "--width"); !v.empty())
        {
            cfg.width = static_cast<std::uint32_t>(std::stoul(v));
        }
        else if(auto v = arg_value(arg, "--height"); !v.empty())
        {
            cfg.height = static_cast<std::uint32_t>(std::stoul(v));
        }
        else if(auto v = arg_value(arg, "--threads"); !v.empty())
        {
            cfg.threads = static_cast<std::uint32_t>(std::stoul(v));
        }
        else if(auto v = arg_value(arg, "--cull_face"); !v.empty())
        {
            cfg.cull_face = parse_bool(v, cfg.cull_face);
        }
        else if(auto v = arg_value(arg, "--depth_test"); !v.empty())
        {
            cfg.depth_test = parse_bool(v, cfg.depth_test);
        }
        else if(auto v = arg_value(arg, "--show_window"); !v.empty())
        {
            cfg.show_window = parse_bool(v, cfg.show_window);
        }
        else if(auto v = arg_value(arg, "--block_early_depth_reject"); !v.empty())
        {
            cfg.block_early_depth_reject =
              parse_rasterizer_feature_mode(v, cfg.block_early_depth_reject);
        }
        else if(auto v = arg_value(arg, "--early_fragment_depth_test"); !v.empty())
        {
            cfg.early_fragment_depth_test =
              parse_rasterizer_feature_mode(v, cfg.early_fragment_depth_test);
        }
        else if(auto v = arg_value(arg, "--camera_angle"); !v.empty())
        {
            cfg.camera_angle_deg = std::stof(v);
        }
        else if(auto v = arg_value(arg, "--fov"); !v.empty())
        {
            cfg.fov_deg = std::stof(v);
        }
    }

    if(cfg.model_path.empty())
    {
        die("missing required argument --model=/path/to/model.obj");
    }
    if(!frames_set && run_time_seconds > 0.0)
    {
        cfg.frames = static_cast<std::uint32_t>(std::max(1.0, std::round(run_time_seconds * 60.0)));
    }
    if(cfg.frames == 0)
    {
        die("--frames must be > 0");
    }
    if(cfg.orbit_frames == 0)
    {
        cfg.orbit_frames = cfg.frames;
    }
    if(cfg.width == 0 || cfg.height == 0)
    {
        die("--width and --height must be > 0");
    }
    if(!(cfg.fov_deg > 1.0f && cfg.fov_deg < 179.0f))
    {
        die("--fov must be in the range (1, 179) degrees");
    }
    return cfg;
}

ml::vec3 calc_normal(const ml::vec3& v0, const ml::vec3& v1, const ml::vec3& v2)
{
    return (v1 - v0).cross_product(v2 - v0).normalized();
}

void release_drawables(std::vector<drawable_object>& objects)
{
    for(auto& o: objects)
    {
        if(o.vertex_buffer_id)
        {
            swr::DeleteAttributeBuffer(o.vertex_buffer_id);
            o.vertex_buffer_id = 0;
        }
        if(o.color_buffer_id)
        {
            swr::DeleteAttributeBuffer(o.color_buffer_id);
            o.color_buffer_id = 0;
        }
        o.triangle_count = 0;
    }
    objects.clear();
}

bool load_obj_drawables(
  const std::string& path,
  std::vector<drawable_object>& out_objects,
  ml::vec3& bmin,
  ml::vec3& bmax)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    std::string base_dir = std::filesystem::path(path).parent_path().string();
    if(base_dir.empty())
    {
        base_dir = ".";
    }
#ifdef _WIN32
    base_dir += "\\";
#else
    base_dir += "/";
#endif

    if(!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), base_dir.c_str()))
    {
        if(!warn.empty())
        {
            std::println("WARN: {}", warn);
        }
        if(!err.empty())
        {
            std::println(stderr, "{}", err);
        }
        return false;
    }

    materials.emplace_back(tinyobj::material_t{});
    bmin = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    bmax = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};

    for(const auto& shape: shapes)
    {
        if(shape.mesh.indices.empty())
        {
            continue;
        }

        std::vector<ml::vec4> pos_buffer;
        std::vector<ml::vec4> color_buffer;
        pos_buffer.reserve(shape.mesh.indices.size());
        color_buffer.reserve(shape.mesh.indices.size());

        const std::size_t face_count = shape.mesh.indices.size() / 3;
        for(std::size_t f = 0; f < face_count; ++f)
        {
            const int mat_id_raw = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[f];
            const std::size_t mat_id = (mat_id_raw >= 0 && static_cast<std::size_t>(mat_id_raw) < materials.size())
                                         ? static_cast<std::size_t>(mat_id_raw)
                                         : (materials.size() - 1);
            ml::vec4 diffuse{
              materials[mat_id].diffuse[0],
              materials[mat_id].diffuse[1],
              materials[mat_id].diffuse[2],
              1.0f};
            if(diffuse.x <= 0.0f
               && diffuse.y <= 0.0f
               && diffuse.z <= 0.0f)
            {
                diffuse = {0.85f, 0.85f, 0.85f, 1.0f};
            }

            ml::vec3 tri_pos[3];
            for(std::size_t i = 0; i < 3; ++i)
            {
                const tinyobj::index_t idx = shape.mesh.indices[3 * f + i];
                if(idx.vertex_index < 0)
                {
                    continue;
                }
                const std::size_t vi = static_cast<std::size_t>(idx.vertex_index);
                const ml::vec3 p{
                  attrib.vertices[3 * vi + 0],
                  attrib.vertices[3 * vi + 1],
                  attrib.vertices[3 * vi + 2]};
                tri_pos[i] = p;
                pos_buffer.emplace_back(p.x, p.y, p.z, 1.0f);
                color_buffer.emplace_back(diffuse);

                bmin.x = std::min(bmin.x, p.x);
                bmin.y = std::min(bmin.y, p.y);
                bmin.z = std::min(bmin.z, p.z);
                bmax.x = std::max(bmax.x, p.x);
                bmax.y = std::max(bmax.y, p.y);
                bmax.z = std::max(bmax.z, p.z);
            }

            (void)calc_normal(tri_pos[0], tri_pos[1], tri_pos[2]);
        }

        if(pos_buffer.empty())
        {
            continue;
        }

        drawable_object obj;
        obj.vertex_buffer_id = swr::CreateAttributeBuffer(pos_buffer);
        obj.color_buffer_id = swr::CreateAttributeBuffer(color_buffer);
        obj.triangle_count = pos_buffer.size() / 3;
        out_objects.emplace_back(obj);
    }

    return !out_objects.empty();
}

} /* anonymous namespace */

int main(int argc, char** argv)
{
    const bench_config cfg = parse_args(argc, argv);

    if(!std::filesystem::exists(cfg.model_path))
    {
        die(std::format("model path does not exist: {}", cfg.model_path));
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    swr::context_handle context = nullptr;

    if(cfg.show_window)
    {
        if(!SDL_Init(SDL_INIT_VIDEO))
        {
            die(std::format("SDL_Init failed: {}", SDL_GetError()));
        }
        if(!SDL_CreateWindowAndRenderer(
             "swr bench_occlusion",
             static_cast<int>(cfg.width),
             static_cast<int>(cfg.height),
             SDL_WINDOW_RESIZABLE,
             &window,
             &renderer))
        {
            SDL_Quit();
            die(std::format("SDL_CreateWindowAndRenderer failed: {}", SDL_GetError()));
        }
        context = swr::CreateSDLContext(window, renderer, cfg.threads);
    }
    else
    {
        context = swr::CreateOffscreenContext(cfg.width, cfg.height, cfg.threads);
    }

    if(!context || !swr::MakeContextCurrent(context))
    {
        if(renderer)
        {
            SDL_DestroyRenderer(renderer);
        }
        if(window)
        {
            SDL_DestroyWindow(window);
        }
        if(cfg.show_window)
        {
            SDL_Quit();
        }
        die("failed to create/make-current context");
    }

    shader::color_flat flat_shader;
    const std::uint32_t flat_shader_id = swr::RegisterShader(&flat_shader);
    if(!flat_shader_id)
    {
        swr::DestroyContext(context);
        die("failed to register flat shader");
    }

    swr::SetClearColor(0.08f, 0.10f, 0.12f, 1.0f);
    swr::SetClearDepth(1.0f);
    swr::SetViewport(0, 0, static_cast<int>(cfg.width), static_cast<int>(cfg.height));
    swr::SetState(swr::state::cull_face, cfg.cull_face);
    swr::SetState(swr::state::depth_test, cfg.depth_test);
    swr::SetRasterizerFeature(
      swr::rasterizer_feature::block_early_depth_reject,
      cfg.block_early_depth_reject);
    swr::SetRasterizerFeature(
      swr::rasterizer_feature::early_fragment_depth_test,
      cfg.early_fragment_depth_test);

    std::vector<drawable_object> objects;
    ml::vec3 bmin;
    ml::vec3 bmax;
    if(!load_obj_drawables(cfg.model_path, objects, bmin, bmax))
    {
        swr::UnregisterShader(flat_shader_id);
        swr::DestroyContext(context);
        die("failed to load or convert obj");
    }

    float max_extent = 0.5f * (bmax.x - bmin.x);
    max_extent = std::max(max_extent, 0.5f * (bmax.y - bmin.y));
    max_extent = std::max(max_extent, 0.5f * (bmax.z - bmin.z));
    const float scale_factor = (max_extent > 0.0f) ? (1.0f / max_extent) : 1.0f;
    const ml::vec3 center = (bmin + bmax) * 0.5f;

    const float camera_angle_radians = cfg.camera_angle_deg * static_cast<float>(M_PI) / 180.0f;
    const float grid_spacing = 1.8f;
    const float scaled_model_radius = std::sqrt(3.0f);
    const float grid_half_span = 2.0f * grid_spacing;
    const float scene_radius = std::sqrt(2.0f * grid_half_span * grid_half_span) + scaled_model_radius;
    const float camera_radius = scene_radius * 2.2f;

    const ml::mat4x4 proj = ml::matrices::perspective_projection(
      static_cast<float>(cfg.width) / static_cast<float>(cfg.height),
      cfg.fov_deg * static_cast<float>(M_PI) / 180.0f,
      0.01f,
      std::max(50.0f, camera_radius * 4.0f));

    const auto start = std::chrono::steady_clock::now();
    std::uint32_t frames_rendered = 0;
    for(std::uint32_t frame = 0; frame < cfg.frames; ++frame)
    {
        if(cfg.show_window)
        {
            SDL_Event event;
            while(SDL_PollEvent(&event))
            {
                if(event.type == SDL_EVENT_QUIT)
                {
                    frame = cfg.frames;
                    break;
                }
            }
            if(frame >= cfg.frames)
            {
                break;
            }
        }

        const float t = static_cast<float>(frame % cfg.orbit_frames) / static_cast<float>(cfg.orbit_frames);
        const float orbit_angle = 2.0f * static_cast<float>(M_PI) * t;
        const float y = camera_radius * std::sin(camera_angle_radians);
        const float xz_radius = camera_radius * std::cos(camera_angle_radians);

        const ml::mat4x4 view = ml::matrices::look_at(
          {xz_radius * std::sin(orbit_angle), y, xz_radius * std::cos(orbit_angle)},
          {0.0f, 0.0f, 0.0f},
          {0.0f, 1.0f, 0.0f});

        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();

        swr::BindShader(flat_shader_id);
        swr::BindUniform(0, proj);

        for(int gz = -2; gz <= 2; ++gz)
        {
            for(int gx = -2; gx <= 2; ++gx)
            {
                ml::mat4x4 model_view = view;
                model_view *= ml::matrices::translation(gx * grid_spacing, 0.0f, gz * grid_spacing);
                model_view *= ml::matrices::scaling(scale_factor);
                model_view *= ml::matrices::translation(-center[0], -center[1], -center[2]);
                swr::BindUniform(1, model_view);

                for(const auto& o: objects)
                {
                    swr::EnableAttributeBuffer(o.vertex_buffer_id, 0);
                    swr::EnableAttributeBuffer(o.color_buffer_id, 1);
                    swr::DrawElements(swr::vertex_buffer_mode::triangles, 3 * o.triangle_count);
                    swr::DisableAttributeBuffer(o.color_buffer_id);
                    swr::DisableAttributeBuffer(o.vertex_buffer_id);
                }
            }
        }

        swr::BindShader(0);
        swr::Present();
        if(cfg.show_window)
        {
            swr::CopyDefaultColorBuffer(context);
        }
        ++frames_rendered;
    }
    const auto end = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(end - start).count();
    const double fps = (seconds > 0.0) ? (static_cast<double>(frames_rendered) / seconds) : 0.0;
    const double msec = (fps > 0.0) ? (1000.0 / fps) : 0.0;
    std::println(
      "frames: {} runtime: {:.2f}s fps: {:.2f} msec: {:.2f} model: {} size: {}x{} threads: {} block_early_depth_reject: {} early_fragment_depth_test: {} camera_angle: {:.2f} fov: {:.2f} show_window: {} orbit_frames: {} benchmark: bench_occlusion",
      frames_rendered,
      seconds,
      fps,
      msec,
      cfg.model_path,
      cfg.width,
      cfg.height,
      cfg.threads,
      rasterizer_feature_mode_name(cfg.block_early_depth_reject),
      rasterizer_feature_mode_name(cfg.early_fragment_depth_test),
      cfg.camera_angle_deg,
      cfg.fov_deg,
      cfg.show_window,
      cfg.orbit_frames);

    release_drawables(objects);
    swr::UnregisterShader(flat_shader_id);
    swr::DestroyContext(context);
    if(renderer)
    {
        SDL_DestroyRenderer(renderer);
    }
    if(window)
    {
        SDL_DestroyWindow(window);
    }
    if(cfg.show_window)
    {
        SDL_Quit();
    }
    return 0;
}
