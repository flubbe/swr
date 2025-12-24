/**
 * swr - a software rasterizer
 *
 * color shader with directional lighting.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace shader
{

/**
 * A shader that applies coloring and directional lighting.
 *
 * vertex shader input:
 *   attribute 0: vertex position
 *   attribute 1: vertex normal
 *
 * varyings:
 *   location 0: normal
 *   location 1: light direction in camera space
 *
 * uniforms:
 *   location 0: projection matrix              [mat4x4]
 *   location 1: view matrix                    [mat4x4]
 *
 */
class color_flat : public swr::program<color_flat>
{
    ml::vec4 diffuse_color{1, 0, 0, 1};
    ml::vec4 ambient_color{1, 0, 0, 1};

public:
    color_flat() = default;
    color_flat(ml::vec4 in_color)
    : diffuse_color{in_color}
    , ambient_color{in_color}
    {
    }

    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set interpolation qualifiers for all varyings.
        iqs = {
          swr::interpolation_qualifier::flat};
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      const ml::vec4* attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] float* gl_ClipDistance,
      ml::vec4* varyings) const override
    {
        ml::mat4x4 proj = (*uniforms)[0].m4;
        ml::mat4x4 view = (*uniforms)[1].m4;
        ml::vec4 light_dir = (*uniforms)[2].v4;

        // transform vertex.
        gl_Position = proj * view * attribs[0];

        auto n = ml::vec4((view * attribs[1]).xyz(), 0).normalized(); /* normal in camera space */
        auto d = ml::vec4(light_dir.xyz(), 0).normalized();           /* light direction. */
        auto l = std::clamp(ml::dot(n, d), 0.f, 1.f);

        varyings[0] = ambient_color * 0.2f + diffuse_color * l;
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        // write color.
        gl_FragColor = varyings[0];

        // accept fragment.
        return swr::accept;
    }
};

class color_smooth : public swr::program<color_smooth>
{
    ml::vec4 diffuse_color{1, 0, 0, 1};
    ml::vec4 ambient_color{1, 0, 0, 1};

public:
    color_smooth() = default;
    color_smooth(ml::vec4 in_color)
    : diffuse_color{in_color}
    , ambient_color{in_color}
    {
    }

    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set interpolation qualifiers for all varyings.
        iqs = {
          swr::interpolation_qualifier::smooth, /* normal */
          swr::interpolation_qualifier::flat,   /* light direction */
        };
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      const ml::vec4* attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] float* gl_ClipDistance,
      ml::vec4* varyings) const override
    {
        ml::mat4x4 proj = (*uniforms)[0].m4;
        ml::mat4x4 view = (*uniforms)[1].m4;
        ml::vec4 light_dir = (*uniforms)[2].v4;

        // transform vertex.
        gl_Position = proj * view * attribs[0];

        varyings[0] = ml::vec4((view * attribs[1]).xyz(), 0);    /* normal in camera space */
        varyings[1] = ml::vec4(light_dir.xyz().normalized(), 0); /* light direction. */
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        const ml::vec4 normal = varyings[0];
        const ml::vec4 direction = varyings[1];

        const ml::vec3 n = normal.xyz().normalized();
        const ml::vec3 d = direction.xyz();

        auto l = std::clamp(ml::dot(n, d), 0.f, 1.f);

        // write color.
        gl_FragColor = ambient_color * 0.2f + diffuse_color * l;

        // accept fragment.
        return swr::accept;
    }
};

} /* namespace shader */
