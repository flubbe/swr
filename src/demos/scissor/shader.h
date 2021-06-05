/**
 * swr - a software rasterizer
 * 
 * Phong lighting shader.
 *
 * vertex shader input:
 *   attribute 0: vertex position
 *   attribute 1: vertex normal
 *   attribute 2: texture coordinates
 * 
 * varyings:
 *   location 0: texture coordinates
 *   location 1: vertex position in camera space
 *   location 2: normal w.r.t. camera space
 *   location 3: eye direction in camera space
 *   location 4: light direction in camera space
 * 
 * uniforms:
 *   location 0: projection matrix              [mat4x4]
 *   location 1: view matrix                    [mat4x4]
 *   location 2: light position in camera space [vec4]
 * 
 * samplers:
 *   location 0: diffuse texture
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace shader
{

class phong : public swr::program
{
    const ml::vec4 light_color{1, 1, 1, 1};
    const ml::vec4 light_specular_color{0.7, 0.7, 0.7, 1};
    const float light_power{10.0f};
    const float shininess{25.f};

    const float ambient_diffuse_factor{0.1f};

public:
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const override
    {
        // set varying count and interpolation qualifiers.
        iqs.resize(5);
        iqs[0] = swr::interpolation_qualifier::smooth;
        iqs[1] = swr::interpolation_qualifier::smooth;
        iqs[2] = swr::interpolation_qualifier::smooth;
        iqs[3] = swr::interpolation_qualifier::smooth;
        iqs[4] = swr::interpolation_qualifier::smooth;
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
        const ml::mat4x4 proj = (*uniforms)[0].m4;
        const ml::mat4x4 view = (*uniforms)[1].m4;

        const ml::vec3 light_position_cameraspace = (*uniforms)[2].v4.xyz();

        // Position of the vertex, in camera space.
        const ml::vec3 position_cameraspace = (view * attribs[0]).xyz();

        // vector pointing from vertex towards light.
        const ml::vec3 light_direction_cameraspace = light_position_cameraspace - position_cameraspace;

        const ml::vec4 normal_modelspace = attribs[1]; /* vertex normal */
        const ml::vec3 normal_cameraspace = (view * normal_modelspace).xyz();

        // pass texture coordinates to fragment shader.
        varyings[0] = attribs[2]; /* texture coordinates */
        varyings[1] = ml::vec4(position_cameraspace, 0);
        varyings[2] = ml::vec4(normal_cameraspace, 0);
        varyings[3] = ml::vec4(-position_cameraspace, 0); /* eye direction: vector from vertex pointing towards camera. */
        varyings[4] = ml::vec4(light_direction_cameraspace, 0);

        // transform vertex. this overwrites the vertex position.
        gl_Position = proj * view * attribs[0];
    }

    swr::fragment_shader_result fragment_shader(
      const ml::vec4& gl_FragCoord,
      bool gl_FrontFacing,
      const ml::vec2& gl_PointCoord,
      const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      float& gl_FragDepth,
      boost::container::static_vector<ml::vec4, swr::max_color_attachments>& color_attachments) const override
    {
        const ml::vec4 tex_coords = varyings[0];
        const ml::vec4 position = varyings[1];
        const ml::vec4 normal = varyings[2];
        const ml::vec4 eye_direction = varyings[3];
        const ml::vec4 light_direction = varyings[4];

        ml::vec4 light_position = (*uniforms)[2].v4;

        // sample diffuse texture.
        ml::vec4 material_diffuse_color = samplers[0]->sample_at({tex_coords.x, tex_coords.y});

        // distance to light.
        float distance_squared = (light_position - position).xyz().length_squared();
        float falloff = light_power / distance_squared;

        // normal of the computed fragment, in camera space.
        ml::vec3 n = normal.xyz().normalized();
        // Direction of the light (from the fragment to the light)
        ml::vec3 l = light_direction.xyz().normalized();

        float lambertian = boost::algorithm::clamp(n * l, 0.f, 1.f);

        // calculate diffuse color.
        ml::vec4 diffuse_color = light_color * material_diffuse_color * lambertian;

        // calculate ambient color.
        ml::vec4 ambient_color = material_diffuse_color * ambient_diffuse_factor;

        // specular color.
        float specular = 0.0f;

        if(lambertian > 0.0f)
        {
            auto reflect_dir = -(l - n * 2.f * (l * n)); /* reflect vector w.r.t. normal */
            auto specular_angle = reflect_dir * eye_direction.xyz().normalized();
            specular = std::pow(boost::algorithm::clamp(specular_angle, 0.f, 1.f), shininess / 4.f);
        }

        color_attachments[0] = ambient_color + (diffuse_color + light_specular_color * specular) * falloff;

        // accept fragment.
        return swr::accept;
    }
};

} /* namespace shader */