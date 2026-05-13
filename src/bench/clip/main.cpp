/**
 * swr - a software rasterizer
 *
 * offscreen obj benchmark (no SDL window/framework).
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
    std::uint32_t width{400};
    std::uint32_t height{400};
    std::uint32_t threads{0};
    bool cull_face{true};
    bool depth_test{true};
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
    if(cfg.width == 0 || cfg.height == 0)
    {
        die("--width and --height must be > 0");
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
            const ml::vec4 diffuse{
              materials[mat_id].diffuse[0],
              materials[mat_id].diffuse[1],
              materials[mat_id].diffuse[2],
              1.0f};

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

    const auto context = swr::CreateOffscreenContext(cfg.width, cfg.height, cfg.threads);
    if(!context || !swr::MakeContextCurrent(context))
    {
        die("failed to create/make-current offscreen context");
    }

    shader::color_flat flat_shader;
    const std::uint32_t flat_shader_id = swr::RegisterShader(&flat_shader);
    if(!flat_shader_id)
    {
        swr::DestroyContext(context);
        die("failed to register flat shader");
    }

    swr::SetClearColor(0, 0, 0, 1);
    swr::SetClearDepth(1.0f);
    swr::SetViewport(0, 0, static_cast<int>(cfg.width), static_cast<int>(cfg.height));
    swr::SetState(swr::state::cull_face, cfg.cull_face);
    swr::SetState(swr::state::depth_test, cfg.depth_test);

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

    const ml::mat4x4 proj = ml::matrices::perspective_projection(
      static_cast<float>(cfg.width) / static_cast<float>(cfg.height),
      static_cast<float>(M_PI) / 4.0f,
      0.01f,
      100.0f);

    const auto start = std::chrono::steady_clock::now();
    for(std::uint32_t frame = 0; frame < cfg.frames; ++frame)
    {
        ml::mat4x4 view;
        // Keep the camera close and oscillate around the frustum boundary
        // so each frame contains partially clipped geometry.
        const float clip_angle = static_cast<float>(frame) * (1.0f / 60.0f) * 1.1f;
        const float cam_radius = 1.6f;
        const float cam_y = 0.45f;
        view = ml::matrices::look_at(
          {cam_radius * std::sin(clip_angle), cam_y, cam_radius * std::cos(clip_angle)},
          {0.75f * std::sin(0.61f * clip_angle), 0.0f, 0.0f},
          {0.0f, 1.0f, 0.0f});
        view *= ml::matrices::scaling(scale_factor);
        view *= ml::matrices::translation(-center[0], -center[1], -center[2]);

        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();

        swr::BindShader(flat_shader_id);
        swr::BindUniform(0, proj);
        swr::BindUniform(1, view);

        for(const auto& o: objects)
        {
            swr::EnableAttributeBuffer(o.vertex_buffer_id, 0);
            swr::EnableAttributeBuffer(o.color_buffer_id, 2);
            swr::DrawElements(swr::vertex_buffer_mode::triangles, 3 * o.triangle_count);
            swr::DisableAttributeBuffer(o.color_buffer_id);
            swr::DisableAttributeBuffer(o.vertex_buffer_id);
        }

        swr::BindShader(0);
        swr::Present();
    }
    const auto end = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(end - start).count();
    const double fps = static_cast<double>(cfg.frames) / seconds;
    const double msec = 1000.0 / fps;
    std::println(
      "frames: {} runtime: {:.2f}s fps: {:.2f} msec: {:.2f} model: {} size: {}x{} threads: {} benchmark: bench_clip",
      cfg.frames,
      seconds,
      fps,
      msec,
      cfg.model_path,
      cfg.width,
      cfg.height,
      cfg.threads);

    release_drawables(objects);
    swr::UnregisterShader(flat_shader_id);
    swr::DestroyContext(context);
    return 0;
}
