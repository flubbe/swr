/**
 * swr - a software rasterizer
 * 
 * A shader that draws front-facing polygons green and back-facing polygons red.
 *
 * vertex shader input:
 *   attribute 0: vertex position
 * 
 * varyings:
 *   none.
 * 
 * uniforms:
 *   location 0: projection matrix              [mat4x4]
 *   location 1: view matrix                    [mat4x4]
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace shader
{

/** A shader that colors front-facing polygons green and back-facing polygons red. */
class front_face_test : public swr::program
{
  public:
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) override
    {
        // set varying count and interpolation qualifiers.
        iqs.resize(0);
    }

    void vertex_shader(
      int gl_VertexID,
      int gl_InstanceID,
      const boost::container::static_vector<ml::vec4, geom::limits::max::attributes>& attribs,
      ml::vec4& gl_Position,
      float& gl_PointSize,
      float* gl_ClipDistance,
      boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& varyings) override
    {
        const ml::mat4x4 proj = (*uniforms)[0].m4;
        const ml::mat4x4 view = (*uniforms)[1].m4;

        // transform vertex. this overwrites the vertex position.
        gl_Position = proj * view * attribs[0];
    }

    swr::fragment_shader_result fragment_shader(
      const ml::vec4& gl_FragCoord,
      bool gl_FrontFacing,
      const ml::vec2& gl_PointCoord,
      const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      float& gl_FragDepth,
      boost::container::static_vector<ml::vec4, swr::max_color_attachments>& color_attachments) override
    {
        if(!gl_FrontFacing)
        {
            color_attachments[0] = ml::vec4(1, 0, 0, 1);
        }
        else
        {
            color_attachments[0] = ml::vec4(0, 1, 0, 1);
        }

        // accept fragment.
        return swr::accept;
    }
};

} /* namespace shader */