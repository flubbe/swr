/**
 * swr - a software rasterizer
 *
 * Shaders for the framebuffer cube demo.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace shader
{

/**
 * A shader that applies coloring.
 *
 * vertex shader input:
 *   attribute 0: vertex position
 *   attribute 1: vertex color
 *
 * varyings:
 *   location 0: color
 *
 * uniforms:
 *   location 0: projection matrix              [mat4x4]
 *   location 1: view matrix                    [mat4x4]
 */
class color final : public swr::program<color>
{
public:
    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    void pre_link(boost::container::static_vector<swr::interpolation_qualifier, swr::limits::max::varyings>& iqs) const override
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
        const ml::mat4x4 proj = uniforms[0].m4;
        const ml::mat4x4 view = uniforms[1].m4;

        gl_Position = proj * (view * attribs[0]);
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
        gl_FragColor = varyings[0];
        return swr::accept;
    }
};

/**
 * A shader that applies a texture.
 *
 * vertex shader input:
 *   attribute 0: vertex position
 *   attribute 1: texture coordinates
 *
 * varyings:
 *   location 0: texture coordinates
 *
 * uniforms:
 *   location 0: projection matrix              [mat4x4]
 *   location 1: view matrix                    [mat4x4]
 *
 * samplers:
 *   location 0: diffuse texture
 */
class texture final : public swr::program<texture>
{
public:
    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    void pre_link(boost::container::static_vector<swr::interpolation_qualifier, swr::limits::max::varyings>& iqs) const override
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
        const ml::mat4x4 proj = uniforms[0].m4;
        const ml::mat4x4 view = uniforms[1].m4;

        gl_Position = proj * (view * attribs[0]);
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
        gl_FragColor = sampler2D(0).sample_at(varyings[0]);
        return swr::accept;
    }
};

} /* namespace shader */
