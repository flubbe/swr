/**
 * swr - a software rasterizer
 *
 * Texture and color shader for bitmap font test.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace shader
{

/*
 * An immediate-mode shader that applies the diffuse texture.
 *
 * vertex shader input:
 *   attribute 0: vertex position
 *   attribute 1: color
 *   attribute 2: texture coordinates
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
class im_texture : public swr::program<im_texture>
{
public:
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set interpolation qualifiers for all varyings.
        iqs = {swr::interpolation_qualifier::smooth};
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

        // transform vertex.
        gl_Position = proj * (view * attribs[swr::default_index::position]);

        // pass texture coordinates to fragment shader.
        varyings[0] = attribs[swr::default_index::tex_coord];
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
        // set interpolation qualifiers for all varyings.
        iqs = {swr::interpolation_qualifier::smooth};
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
        gl_FragColor = color;

        // accept fragment.
        return swr::accept;
    }
};

} /* namespace shader */