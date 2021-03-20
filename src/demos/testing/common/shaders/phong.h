/**
 * swr - a software rasterizer
 * 
 * Two shaders that apply texturing and phong (resp. blinn-phong) shading.
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
 *   location 3: diffuse texture id             [int]
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace shader
{

/** Base class for phong- and blinn-phong shader. */
class phong_base : public swr::program
{
    const ml::vec4 light_color{1, 1, 1, 1};
    const ml::vec4 light_specular_color{0.5, 0.5, 0.5, 1};
    const float light_power{10.0f};
    const float shininess{16.f};

    const float ambient_diffuse_factor{0.2f};

  public:
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) override
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
      boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& varyings) override
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
      boost::container::static_vector<ml::vec4, swr::max_color_attachments>& color_attachments) override
    {
        const ml::vec4 tex_coords = varyings[0];
        const ml::vec4 position = varyings[1];
        const ml::vec4 normal = varyings[2];
        const ml::vec4 eye_direction = varyings[3];
        const ml::vec4 light_direction = varyings[4];

        ml::vec4 light_position = (*uniforms)[2].v4;

        // material properties.
        uint32_t diffuse_tex_id = (*uniforms)[3].i;
        swr::sampler_2d* diffuse_sampler = swr::GetSampler2d(diffuse_tex_id);
        ml::vec4 material_diffuse_color = diffuse_sampler->sample_at({tex_coords.x, tex_coords.y});

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
            specular = calculate_specular(eye_direction.xyz().normalized(), l, n, shininess);
        }

        color_attachments[0] = ambient_color + (diffuse_color + light_specular_color * specular) * falloff;

        // accept fragment.
        return swr::accept;
    }

    virtual float calculate_specular(const ml::vec3 eye_direction, const ml::vec3 light_direction, const ml::vec3 normal, float shininess) = 0;
};

/** Phong shader. */
class phong : public phong_base
{
  public:
    virtual float calculate_specular(const ml::vec3 eye_direction, const ml::vec3 light_direction, const ml::vec3 normal, float shininess) override
    {
        auto reflect_dir = -(light_direction - normal * 2.f * (light_direction * normal)); /* reflect vector w.r.t. normal */
        auto specular_angle = reflect_dir * eye_direction;
        return std::pow(boost::algorithm::clamp(specular_angle, 0.f, 1.f), shininess / 4.f); /* exponent is different for phong shading */
    }
};

/** Blinn-Phong shader. */
class blinn_phong : public phong_base
{
  public:
    virtual float calculate_specular(const ml::vec3 eye_direction, const ml::vec3 light_direction, const ml::vec3 normal, float shininess) override
    {
        auto half_dir = (eye_direction + light_direction).normalized();
        auto specular_angle = normal * half_dir;
        return std::pow(boost::algorithm::clamp(specular_angle, 0.f, 1.f), shininess);
    }
};

} /* namespace shader */