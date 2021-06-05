/**
 * swr - a software rasterizer
 * 
 * color and mesh shader.
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
class color : public swr::program
{
public:
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set varying count and interpolation qualifiers.
        iqs.resize(1);
        iqs[0] = swr::interpolation_qualifier::smooth;
    }

    void vertex_shader(
      int gl_VertexID,
      int gl_InstanceID,
      const boost::container::static_vector<ml::vec4, geom::limits::max::attributes>& attribs,
      ml::vec4& gl_Position,
      float& gl_PointSize,
      float* gl_ClipDistance,
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
      const ml::vec4& gl_FragCoord,
      bool gl_FrontFacing,
      const ml::vec2& gl_PointCoord,
      const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      float& gl_FragDepth,
      boost::container::static_vector<ml::vec4, swr::max_color_attachments>& color_attachments) const override
    {
        // get color.
        const ml::vec4 color = varyings[0];

        // write color.
        color_attachments[0] = color;

        // accept fragment.
        return swr::accept;
    }
};

/** 
 * A shader that applies coloring to meshes.
 *
 * vertex shader input:
 *   attribute 0: position
 *   attribute 1: normal [unused]
 *   attribute 2: tangent [unused]
 *   attribute 3: bitangent [unused]
 *   attribute 4: color
 *   attribute 5: texture coordinate [unused]
 * 
 * varyings:
 *   location 0: color
 * 
 * uniforms:
 *   location 0: projection matrix              [mat4x4]
 *   location 1: view matrix                    [mat4x4]
 */
class mesh_color : public swr::program
{
public:
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set varying count and interpolation qualifiers.
        iqs.resize(6);
        iqs[0] = swr::interpolation_qualifier::smooth;
        iqs[1] = swr::interpolation_qualifier::smooth;
        iqs[2] = swr::interpolation_qualifier::smooth;
        iqs[3] = swr::interpolation_qualifier::smooth;
        iqs[4] = swr::interpolation_qualifier::smooth;
        iqs[5] = swr::interpolation_qualifier::smooth;
    }

    void vertex_shader(
      int gl_VertexID,
      int gl_InstanceID,
      const boost::container::static_vector<ml::vec4, geom::limits::max::attributes>& attribs,
      ml::vec4& gl_Position,
      float& gl_PointSize,
      float* gl_ClipDistance,
      boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& varyings) const override
    {
        ml::mat4x4 proj = (*uniforms)[0].m4;
        ml::mat4x4 view = (*uniforms)[1].m4;

        // transform vertex.
        gl_Position = proj * (view * attribs[0]);

        // pass color to fragment shader.
        varyings[0] = attribs[4];
    }

    swr::fragment_shader_result fragment_shader(
      const ml::vec4& gl_FragCoord,
      bool gl_FrontFacing,
      const ml::vec2& gl_PointCoord,
      const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      float& gl_FragDepth,
      boost::container::static_vector<ml::vec4, swr::max_color_attachments>& color_attachments) const override
    {
        // get color.
        const ml::vec4 color = varyings[0];

        // write color.
        color_attachments[0] = color;

        // accept fragment.
        return swr::accept;
    }
};

} /* namespace shader */