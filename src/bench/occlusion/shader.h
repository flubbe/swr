/**
 * swr - a software rasterizer
 *
 * flat color shader and wireframe shader.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace shader
{

/*
 * helpers.
 */

static float saturate(float x)
{
    return std::clamp(x, 0.0f, 1.0f);
}

static float fract(float x)
{
    return x - std::floor(x);
}

static ml::vec3 palette(float t)
{
    t = saturate(t);

    // Deep blue -> cyan -> warm gold.
    return {
      0.15f + 0.85f * t,
      0.25f + 0.65f * std::sin(t * 3.14159265f),
      0.45f + 0.45f * (1.0f - t)};
}

/**
 * A shader that applies flat coloring.
 *
 * vertex shader input:
 *   attribute 0: position
 *   attribute 1: color
 *
 * varyings:
 *   location 0: color
 *
 * uniforms:
 *   location 0: projection matrix              [mat4x4]
 *   location 1: view matrix                    [mat4x4]
 *
 */
class color_flat : public swr::program<color_flat>
{
public:
    color_flat() = default;

    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, swr::limits::max::varyings>& iqs) const override
    {
        // set interpolation qualifiers for all varyings.
        iqs = {
          swr::interpolation_qualifier::smooth /* color */
        };
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
        ml::mat4x4 proj = uniforms[0].m4;
        ml::mat4x4 view = uniforms[1].m4;

        // transform vertex.
        gl_Position = proj * view * attribs[0];

        varyings[0] = attribs[1];
    }

    swr::fragment_shader_result fragment_shader(
      const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        const float x = gl_FragCoord.x * 0.0065f;
        const float y = gl_FragCoord.y * 0.0065f;
        const float z = saturate(gl_FragCoord.z);

        // Expands far-plane depth differences.
        const float depth = std::pow(1.0f - z, 0.25f);

        float f = x * 1.7f + y * 1.3f + depth * 4.0f;
        float amp = 0.5f;

        // Expensive but visually coherent procedural turbulence.
        for(std::size_t i = 0; i < 96; ++i)
        {
            const float fi = static_cast<float>(i);

            const float wx = x * (1.0f + fi * 0.017f);
            const float wy = y * (1.0f + fi * 0.013f);

            const float n =
              std::sin(wx * 6.1f + f * 1.37f + fi * 0.11f) * std::cos(wy * 5.3f - f * 1.91f - fi * 0.07f);

            f += amp * n;
            amp *= 0.985f;
        }

        // Marble-like bands, modulated by depth.
        const float marble =
          0.5f + 0.5f * std::sin(f * 2.5f + depth * 18.0f);

        // Repeating depth contours, useful for seeing depth behavior.
        const float band = fract(depth * 18.0f);
        const float contour = band < 0.035f ? 0.35f : 1.0f;

        ml::vec3 color = palette(0.25f + 0.75f * marble);

        // Depth tint: near brighter, far cooler/darker.
        const float lighting = 0.35f + 0.65f * depth;
        color *= lighting * contour;

        gl_FragColor = {color.x, color.y, color.z, 1.0f};

        return swr::accept;
    }
};

} /* namespace shader */
