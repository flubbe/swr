/**
 * swr - a software rasterizer
 *
 * shaders for the depth-texture gears demo.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "demos/gears/shader.h"

namespace shader
{

/** Fullscreen quad shader used to visualize the rendered color and depth textures. */
class depth_texture_display final : public swr::program<depth_texture_display>
{
public:
    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    void pre_link(
      boost::container::static_vector<
        swr::interpolation_qualifier,
        swr::limits::max::varyings>& iqs) const override
    {
        iqs = {swr::interpolation_qualifier::smooth};
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      std::span<const ml::vec4> attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      std::span<ml::vec4> varyings) const override
    {
        gl_Position = attribs[0];
        varyings[0] = attribs[1];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        const ml::vec2 uv = varyings[0].value.xy();
        const float near_plane = uniforms[0].v4.x;
        const float far_plane = uniforms[0].v4.y;

        auto linearize_depth = [near_plane, far_plane](float depth) -> float
        {
            const float z_ndc = depth * 2.0f - 1.0f;
            const float linear =
              (2.0f * near_plane * far_plane)
              / (far_plane + near_plane - z_ndc * (far_plane - near_plane));
            return std::clamp(
              (linear - near_plane) / (far_plane - near_plane),
              0.0f,
              1.0f);
        };

        if(uv.x < 0.5f)
        {
            const ml::vec2 remapped_uv = {uv.x * 2.0f, uv.y};
            gl_FragColor = swr::texture(sampler2D(0), remapped_uv);
        }
        else
        {
            const ml::vec2 remapped_uv = {(uv.x - 0.5f) * 2.0f, uv.y};
            const float depth = swr::texture(sampler2D(1), remapped_uv).x;
            const float linear_depth = linearize_depth(depth);
            const float value = 1.0f - std::sqrt(linear_depth);
            gl_FragColor = {value, value, value, 1.0f};
        }

        return swr::accept;
    }
};

} /* namespace shader */
