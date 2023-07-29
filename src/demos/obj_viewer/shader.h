/**
 * swr - a software rasterizer
 *
 * flat color shader and wireframe shader.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2022
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace shader
{

/**
 * A shader that applies flat coloring.
 *
 * vertex shader input:
 *   attribute 0: position
 *   attribute 1: normal [unused]
 *   attribute 2: color
 *   attribute 3: texture [unused]
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

    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set interpolation qualifiers for all varyings.
        iqs = {
          swr::interpolation_qualifier::smooth /* color */
        };
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
        gl_Position = proj * view * attribs[0];

        varyings[0] = attribs[2];
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

/**
 * A shader for displaying a wireframe model.
 *
 * vertex shader input:
 *   attribute 0: vertex position
 *
 * varyings:
 *   -
 *
 * uniforms:
 *   location 0: projection matrix              [mat4x4]
 *   location 1: view matrix                    [mat4x4]
 *
 */
class wireframe : public swr::program<wireframe>
{
    const ml::vec4 diffuse_color{0, 0, 0.4f, 1.0f};

public:
    wireframe() = default;

    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set interpolation qualifiers for all varyings.
        iqs = {};
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      const boost::container::static_vector<ml::vec4, geom::limits::max::attributes>& attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] float* gl_ClipDistance,
      [[maybe_unused]] boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& varyings) const override
    {
        ml::mat4x4 proj = (*uniforms)[0].m4;
        ml::mat4x4 view = (*uniforms)[1].m4;

        // transform vertex.
        gl_Position = proj * view * attribs[0];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        // write color.
        gl_FragColor = diffuse_color;

        // accept fragment.
        return swr::accept;
    }
};

} /* namespace shader */
