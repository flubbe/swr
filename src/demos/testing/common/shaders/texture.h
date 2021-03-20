/**
 * swr - a software rasterizer
 * 
 * A shader that applies the diffuse texture.
 *
 * vertex shader input:
 *   attribute 0: vertex position
 *   attribute 2: texture coordinates
 * 
 * varyings:
 *   location 0: texture coordinates
 * 
 * uniforms:
 *   location 0: projection matrix              [mat4x4]
 *   location 1: view matrix                    [mat4x4]
 *   location 2: texture id                     [int]
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace shader
{

/** A shader that applies the diffuse texture. */
class texture : public swr::program
{
  public:
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) override
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
      boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& varyings) override
    {
        ml::mat4x4 proj = (*uniforms)[0].m4;
        ml::mat4x4 view = (*uniforms)[1].m4;

        // transform vertex.
        gl_Position = proj * (view * attribs[swr::default_index::position]);

        // pass texture coordinates to fragment shader.
        varyings[0] = attribs[swr::default_index::tex_coord];
    }

    swr::fragment_shader_result fragment_shader(
      const ml::vec4& gl_FragCoord,
      bool gl_FrontFacing,
      const ml::vec2& gl_PointCoord,
      const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      float& gl_FragDepth,
      boost::container::static_vector<ml::vec4, swr::max_color_attachments>& color_attachments) override
    {
        // texture coordinates.
        const ml::vec4 tex_coords = varyings[0];

        // get texture from uniform.
        uint32_t tex_id = (*uniforms)[2].i;
        swr::sampler_2d* sampler = swr::GetSampler2d(tex_id);
        ml::vec4 color = sampler->sample_at(tex_coords.xy());

        // write fragment color.
        color_attachments[0] = color;

        // accept fragment.
        return swr::accept;
    }
};

} /* namespace shader */