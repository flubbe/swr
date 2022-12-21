/**
 * swr - a software rasterizer
 *
 * color shader and texture shader.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
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
class color : public swr::program<color>
{
public:
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set varying count and interpolation qualifiers.
        iqs.resize(1);
        iqs[0] = swr::interpolation_qualifier::smooth;
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

        // pass color to fragment shader.
        varyings[0] = attribs[1];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        // get color.
        const ml::vec4 color = varyings[0];

        // write color.
        gl_FragColor = {color.xyz(), 0.5f};

        // accept fragment.
        return swr::accept;
    }
};

/**
 * A shader that applies the diffuse texture.
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
class texture : public swr::program<texture>
{
public:
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set varying count and interpolation qualifiers.
        iqs.resize(1);
        iqs[0] = swr::interpolation_qualifier::smooth;
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

        // pass texture coordinates to fragment shader.
        varyings[0] = attribs[1];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        // texture coordinates.
        const swr::varying& tex_coords = varyings[0];

        // sample texture.
        ml::vec4 color = samplers[0]->sample_at(tex_coords);

        // write fragment color.
        gl_FragColor = color;

        // accept fragment.
        return swr::accept;
    }
};

} /* namespace shader */