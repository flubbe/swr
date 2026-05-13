/**
 * swr - a software rasterizer
 *
 * offscreen gears benchmark (no SDL window/framework).
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "swr/swr.h"
#include "swr/shaders.h"

#include "shader.h"

namespace
{

struct bench_config
{
    std::uint32_t frames{600};
    std::uint32_t width{640};
    std::uint32_t height{480};
    std::uint32_t threads{0};
    float view_z{-40.0f};
    float clip_orbit_amplitude{0.0f};
    float gear_rotation_speed{1.0f};
    float gear0_width{50.0f};
    float gear1_width{2.0f};
    float gear2_width{0.5f};
};

class drawable_object
{
    std::vector<std::uint32_t> index_buffer;
    std::uint32_t vertex_buffer_id{0};
    std::uint32_t normal_buffer_id{0};
    bool has_data{false};

public:
    drawable_object() = default;

    drawable_object(std::vector<std::uint32_t> in_ib, std::uint32_t in_vb, std::uint32_t in_nb)
    : index_buffer{std::move(in_ib)}
    , vertex_buffer_id{in_vb}
    , normal_buffer_id{in_nb}
    , has_data{true}
    {
    }

    drawable_object(drawable_object&& other) noexcept
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
        if(!has_data)
        {
            return;
        }

        swr::EnableAttributeBuffer(vertex_buffer_id, 0);
        swr::EnableAttributeBuffer(normal_buffer_id, 1);
        swr::DrawIndexedElements(swr::vertex_buffer_mode::triangles, index_buffer.size(), index_buffer);
        swr::DisableAttributeBuffer(normal_buffer_id);
        swr::DisableAttributeBuffer(vertex_buffer_id);
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

    void make_gear(float inner_radius, float outer_radius, float width, int teeth, float tooth_depth, ml::vec4 color)
    {
        release();

        const float r0 = inner_radius;
        const float r1 = outer_radius - tooth_depth / 2.f;
        const float r2 = outer_radius + tooth_depth / 2.f;

        float da = 2.f * static_cast<float>(M_PI / teeth) / 4.f;

        std::vector<ml::vec4> vb;
        std::vector<ml::vec4> nb;
        std::vector<std::uint32_t> ib;

        for(int i = 0; i <= teeth; ++i)
        {
            const float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), width * 0.5f);
            nb.emplace_back(0, 0, 1, 0);
            nb.emplace_back(0, 0, 1, 0);

            if(i != 0)
            {
                const auto cur_idx = vb.size() - 1;
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

                const auto cur_idx = vb.size() - 1;
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
            const float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);
            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + da), r2 * std::sin(angle + da), width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + 2 * da), r2 * std::sin(angle + 2 * da), width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), width * 0.5f);
            nb.emplace_back(0, 0, 1, 0);
            nb.emplace_back(0, 0, 1, 0);
            nb.emplace_back(0, 0, 1, 0);
            nb.emplace_back(0, 0, 1, 0);

            const auto cur_idx = vb.size() - 1;
            ib.emplace_back(cur_idx - 3);
            ib.emplace_back(cur_idx - 2);
            ib.emplace_back(cur_idx - 1);
            ib.emplace_back(cur_idx - 3);
            ib.emplace_back(cur_idx - 1);
            ib.emplace_back(cur_idx);
        }

        for(int i = 0; i <= teeth; ++i)
        {
            const float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);
            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), -width * 0.5f);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), -width * 0.5f);
            nb.emplace_back(0, 0, -1, 0);
            nb.emplace_back(0, 0, -1, 0);

            if(i != 0)
            {
                const auto cur_idx = vb.size() - 1;
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

                const auto cur_idx = vb.size() - 1;
                ib.emplace_back(cur_idx - 3);
                ib.emplace_back(cur_idx - 2);
                ib.emplace_back(cur_idx - 1);
                ib.emplace_back(cur_idx - 2);
                ib.emplace_back(cur_idx);
                ib.emplace_back(cur_idx - 1);
            }
        }

        for(int i = 0; i < teeth; ++i)
        {
            const float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);
            vb.emplace_back(r1 * std::cos(angle + 3 * da), r1 * std::sin(angle + 3 * da), -width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + 2 * da), r2 * std::sin(angle + 2 * da), -width * 0.5f);
            vb.emplace_back(r2 * std::cos(angle + da), r2 * std::sin(angle + da), -width * 0.5f);
            vb.emplace_back(r1 * std::cos(angle), r1 * std::sin(angle), -width * 0.5f);
            nb.emplace_back(0, 0, -1, 0);
            nb.emplace_back(0, 0, -1, 0);
            nb.emplace_back(0, 0, -1, 0);
            nb.emplace_back(0, 0, -1, 0);

            const auto cur_idx = vb.size() - 1;
            ib.emplace_back(cur_idx - 3);
            ib.emplace_back(cur_idx - 2);
            ib.emplace_back(cur_idx - 1);
            ib.emplace_back(cur_idx - 3);
            ib.emplace_back(cur_idx - 1);
            ib.emplace_back(cur_idx);
        }

        for(int i = 0; i < teeth; ++i)
        {
            const float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);

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
                const auto cur_idx = vb.size() - 1;
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

        const auto cur_idx = vb.size() - 1;
        ib.emplace_back(cur_idx - 2);
        ib.emplace_back(cur_idx - 1);
        ib.emplace_back(cur_idx - 3);
        ib.emplace_back(cur_idx - 2);
        ib.emplace_back(cur_idx);
        ib.emplace_back(cur_idx - 1);

        outside = {std::move(ib), swr::CreateAttributeBuffer(vb), swr::CreateAttributeBuffer(nb)};
        vb.clear();
        nb.clear();
        ib.clear();

        for(int i = 0; i <= teeth; ++i)
        {
            const float angle = i * 2.f * static_cast<float>(M_PI) / static_cast<float>(teeth);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), -width * 0.5f);
            vb.emplace_back(r0 * std::cos(angle), r0 * std::sin(angle), width * 0.5f);
            nb.emplace_back(-std::cos(angle), -std::sin(angle), 0, 0);
            nb.emplace_back(-std::cos(angle), -std::sin(angle), 0, 0);

            if(i != 0)
            {
                const auto cylinder_idx = vb.size() - 1;
                ib.emplace_back(cylinder_idx - 2);
                ib.emplace_back(cylinder_idx - 1);
                ib.emplace_back(cylinder_idx - 3);
                ib.emplace_back(cylinder_idx - 2);
                ib.emplace_back(cylinder_idx);
                ib.emplace_back(cylinder_idx - 1);
            }
        }

        cylinder = {std::move(ib), swr::CreateAttributeBuffer(vb), swr::CreateAttributeBuffer(nb)};

        smooth_shader = {color};
        flat_shader = {color};
        flat_shader_id = swr::RegisterShader(&flat_shader);
        smooth_shader_id = swr::RegisterShader(&smooth_shader);
        if(!flat_shader_id || !smooth_shader_id)
        {
            throw std::runtime_error("gear_object: shader registration failed");
        }
    }
};

[[noreturn]] void die(const std::string& msg)
{
    std::cerr << std::format("error: {}\n", msg);
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
    for(int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if(auto v = arg_value(arg, "--frames"); !v.empty())
        {
            cfg.frames = static_cast<std::uint32_t>(std::stoul(v));
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
        else if(auto v = arg_value(arg, "--view_z"); !v.empty())
        {
            cfg.view_z = std::stof(v);
        }
        else if(auto v = arg_value(arg, "--clip_orbit_amplitude"); !v.empty())
        {
            cfg.clip_orbit_amplitude = std::stof(v);
        }
        else if(auto v = arg_value(arg, "--gear_rotation_speed"); !v.empty())
        {
            cfg.gear_rotation_speed = std::stof(v);
        }
        else if(auto v = arg_value(arg, "--gear0_width"); !v.empty())
        {
            cfg.gear0_width = std::stof(v);
        }
        else if(auto v = arg_value(arg, "--gear1_width"); !v.empty())
        {
            cfg.gear1_width = std::stof(v);
        }
        else if(auto v = arg_value(arg, "--gear2_width"); !v.empty())
        {
            cfg.gear2_width = std::stof(v);
        }
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

void draw_scene(
  const ml::mat4x4& proj,
  const ml::vec4& light_pos,
  const gear_object (&gears)[3],
  float gear_rotation,
  float view_z,
  float clip_orbit_amplitude)
{
    swr::BindUniform(0, proj);

    ml::mat4x4 view = ml::mat4x4::identity();
    view *= ml::matrices::translation(std::sin(gear_rotation * 0.37f) * clip_orbit_amplitude, 0.0f, view_z);
    swr::BindUniform(2, view * light_pos);

    view *= ml::matrices::rotation_x(ml::to_radians(20.0f));
    view *= ml::matrices::rotation_y(ml::to_radians(30.0f));

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

} /* anonymous namespace */

int main(int argc, char** argv)
{
    const bench_config cfg = parse_args(argc, argv);

    auto context = swr::CreateOffscreenContext(cfg.width, cfg.height, cfg.threads);
    if(!context || !swr::MakeContextCurrent(context))
    {
        die("failed to create/make-current offscreen context");
    }

    swr::SetClearColor(0, 0, 0, 1);
    swr::SetClearDepth(1.0f);
    swr::SetViewport(0, 0, static_cast<int>(cfg.width), static_cast<int>(cfg.height));
    swr::SetState(swr::state::cull_face, true);
    swr::SetState(swr::state::depth_test, true);

    gear_object gears[3];
    try
    {
        gears[0].make_gear(1.0, 4.0, cfg.gear0_width, 20, 0.7, {0.8f, 0.1f, 0.0f, 1.0f});
        gears[1].make_gear(0.5, 2.0, cfg.gear1_width, 10, 0.7, {0.0f, 0.8f, 0.2f, 1.0f});
        gears[2].make_gear(1.3, 2.0, cfg.gear2_width, 10, 0.7, {0.2f, 0.2f, 1.0f, 1.0f});
    }
    catch(const std::exception& ex)
    {
        swr::DestroyContext(context);
        die(std::format("failed to initialize gears: {}", ex.what()));
    }

    const ml::mat4x4 proj = ml::matrices::perspective_projection(
      static_cast<float>(cfg.width) / static_cast<float>(cfg.height),
      static_cast<float>(M_PI) / 8.0f,
      5.0f,
      60.0f);
    const ml::vec4 light_pos{5.0f, 5.0f, 10.0f, 0.0f};

    float gear_rotation = 0.0f;
    auto t0 = std::chrono::steady_clock::now();
    for(std::uint32_t frame = 0; frame < cfg.frames; ++frame)
    {
        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();

        gear_rotation += (1.0f / 60.0f) * cfg.gear_rotation_speed;
        if(gear_rotation >= 2.0f * static_cast<float>(M_PI))
        {
            gear_rotation -= 2.0f * static_cast<float>(M_PI);
        }

        draw_scene(
          proj,
          light_pos,
          gears,
          gear_rotation,
          cfg.view_z,
          cfg.clip_orbit_amplitude);

        swr::Present();
    }
    auto t1 = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(t1 - t0).count();
    const double fps = static_cast<double>(cfg.frames) / seconds;
    const double ms = 1000.0 / fps;

    std::cout << std::format(
      "frames: {} runtime: {:.2f}s fps: {:.2f} msec: {:.2f} size: {}x{} threads: {} benchmark: bench_gears_clip view_z: {:.2f} clip_orbit_amplitude: {:.2f} gear_widths: [{:.2f}, {:.2f}, {:.2f}]\n",
      cfg.frames,
      seconds,
      fps,
      ms,
      cfg.width,
      cfg.height,
      cfg.threads,
      cfg.view_z,
      cfg.clip_orbit_amplitude,
      cfg.gear0_width,
      cfg.gear1_width,
      cfg.gear2_width);

    gears[0].release();
    gears[1].release();
    gears[2].release();

    swr::DestroyContext(context);
    return 0;
}
