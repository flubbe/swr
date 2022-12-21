/**
 * swr - a software rasterizer
 *
 * Shaders for the motion blur demo.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace shader
{

/**
 * Normal mapping shader.
 *
 * vertex shader input:
 *   attribute 0: vertex position
 *   attribute 1: vertex normal
 *   attribute 2: vertex tangents
 *   attribute 3: vertex bitangents
 *   attribute 4: texture coordinates
 *
 * varyings:
 *   location 0: texture coordinates
 *   location 1: vertex position in camera space
 *   location 2: normal w.r.t. camera space
 *   location 3: tangent w.r.t. camera space
 *   location 4: bitangent w.r.t. camera space
 *   location 5: eye direction in camera space
 *   location 6: light direction in camera space
 *
 * uniforms:
 *   location 0: projection matrix              [mat4x4]
 *   location 1: view matrix                    [mat4x4]
 *   location 2: light position in camera space [vec4]
 *
 * samplers:
 *   location 0: diffuse texture
 *   location 1: normal map
 */
class normal_mapping : public swr::program<normal_mapping>
{
    const ml::vec4 light_color{0.7, 1, 1, 1};
    const ml::vec4 light_specular_color{0.25, 0.5, 0.75, 1};
    const float light_power{5.0f};
    const float shininess{5.f};

    const float ambient_diffuse_factor{0.5f};

public:
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set varying count and interpolation qualifiers.
        iqs.resize(7);
        iqs[0] = swr::interpolation_qualifier::smooth;
        iqs[1] = swr::interpolation_qualifier::smooth;
        iqs[2] = swr::interpolation_qualifier::smooth;
        iqs[3] = swr::interpolation_qualifier::smooth;
        iqs[4] = swr::interpolation_qualifier::smooth;
        iqs[5] = swr::interpolation_qualifier::smooth;
        iqs[6] = swr::interpolation_qualifier::smooth;
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
        const ml::mat4x4 proj = (*uniforms)[0].m4;
        const ml::mat4x4 view = (*uniforms)[1].m4;

        const ml::vec3 light_position_cameraspace = (*uniforms)[2].v4.xyz();

        // Position of the vertex, in camera space.
        const ml::vec3 position_cameraspace = (view * attribs[0]).xyz();

        // vector pointing from vertex towards light.
        const ml::vec3 light_direction_cameraspace = light_position_cameraspace - position_cameraspace;

        const ml::vec4 normal_modelspace = attribs[1]; /* vertex normal */
        const ml::vec3 normal_cameraspace = (view * normal_modelspace).xyz();

        const ml::vec4 tangent_modelspace = attribs[2]; /* vertex tangent */
        const ml::vec3 tangent_cameraspace = (view * tangent_modelspace).xyz();

        const ml::vec4 bitangent_modelspace = attribs[3]; /* vertex bitangent */
        const ml::vec3 bitangent_cameraspace = (view * bitangent_modelspace).xyz();

        // pass texture coordinates to fragment shader.
        varyings[0] = attribs[4]; /* texture coordinates */
        varyings[1] = ml::vec4(position_cameraspace, 0);
        varyings[2] = ml::vec4(normal_cameraspace, 0);
        varyings[3] = ml::vec4(tangent_cameraspace, 0);
        varyings[4] = ml::vec4(bitangent_cameraspace, 0);
        varyings[5] = ml::vec4(-position_cameraspace, 0); /* eye direction: vector from vertex pointing towards camera. */
        varyings[6] = ml::vec4(light_direction_cameraspace, 0);

        // transform vertex. this overwrites the vertex position.
        gl_Position = proj * view * attribs[0];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        const swr::varying& tex_coords = varyings[0];
        const ml::vec4 position = varyings[1];
        const ml::vec4 normal = varyings[2];
        const ml::vec4 tangent = varyings[3];
        const ml::vec4 bitangent = varyings[4];
        const ml::vec4 eye_direction = varyings[5];
        const ml::vec4 light_direction = varyings[6];

        const ml::vec4 light_position = (*uniforms)[2].v4;

        // distance to light.
        float distance_squared = (light_position - position).xyz().length_squared();
        float falloff = light_power / distance_squared;

        // sample normal map.
        const ml::vec3 material_normal = (samplers[1]->sample_at(tex_coords) * 2 - 1).xyz();

        // normal of the computed fragment, in camera space.
        auto tbn = ml::mat4x4{tangent, bitangent, normal, ml::vec4::zero()}.transposed();
        const ml::vec3 n = (tbn * ml::vec4{material_normal, 0.f}).xyz().normalized();
        // Direction of the light (from the fragment to the light)
        const ml::vec3 l = light_direction.xyz().normalized();

        float lambertian = boost::algorithm::clamp(ml::dot(n, l), 0.f, 1.f);

        // sample diffuse texture.
        auto material_diffuse_color = samplers[0]->sample_at(tex_coords);

        // calculate diffuse color.
        ml::vec4 diffuse_color = light_color * material_diffuse_color * lambertian;

        // calculate ambient color.
        ml::vec4 ambient_color = material_diffuse_color * ambient_diffuse_factor;

        // specular color.
        float specular = 0.0f;

        if(lambertian > 0.0f)
        {
            auto reflect_dir = -(l - n * 2.f * ml::dot(l, n)); /* reflect vector w.r.t. normal */
            auto specular_angle = ml::dot(reflect_dir, eye_direction.xyz().normalized());
            specular = std::pow(boost::algorithm::clamp(specular_angle, 0.f, 1.f), shininess / 4.f);
        }

        gl_FragColor = ambient_color + (diffuse_color + light_specular_color * specular) * falloff;

        // accept fragment.
        return swr::accept;
    }
};

/**
 * An immediate-mode blending shader. Blends some percentage of a texture into the framebuffer.
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
 *
 * samplers:
 *   location 0: diffuse texture
 */
class im_blend : public swr::program<im_blend>
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
        gl_FragColor = {color.xyz(), 0.16f};

        // accept fragment.
        return swr::accept;
    }
};

} /* namespace shader */