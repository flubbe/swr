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
class color : public swr::program
{
    const ml::vec4 light_pos{5.0f, 5.0f, 10.0f, 0.0f};
    ml::vec4 diffuse_color{1, 0, 0, 1};
    ml::vec4 ambient_color{1, 0, 0, 1};

public:
    color() = default;
    color(ml::vec4 in_color)
    : diffuse_color{in_color}
    , ambient_color{in_color}
    {
    }

    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set varying count and interpolation qualifiers.
        iqs.resize(2);
        iqs[0] = swr::interpolation_qualifier::smooth;
        iqs[1] = swr::interpolation_qualifier::smooth;
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      const boost::container::static_vector<ml::vec4, geom::limits::max::attributes>& attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] float* gl_ClipDistance,
      boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& varyings) const override
    {
        ml::mat4x4 proj = (*uniforms)[0].m4;
        ml::mat4x4 view = (*uniforms)[1].m4;

        // transform vertex.
        gl_Position = proj * (view * attribs[0]);

        varyings[0] = attribs[1];
        varyings[1] = light_pos - ml::vec4((view * attribs[0]).xyz(), 0);
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

        auto l = boost::algorithm::clamp(ml::dot(normal, direction.normalized()), 0.f, 1.f);

        // write color.
        gl_FragColor = ambient_color * 0.2f + diffuse_color * l;

        // accept fragment.
        return swr::accept;
    }
};

} /* namespace shader */