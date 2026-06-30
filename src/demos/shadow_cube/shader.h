namespace shader
{

class depth_pass final : public swr::program<depth_pass>
{
public:
    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    void pre_link(
      boost::container::static_vector<
        swr::interpolation_qualifier,
        swr::limits::max::varyings>& iqs) const override
    {
        iqs = {};
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      std::span<const ml::vec4> attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      [[maybe_unused]] std::span<ml::vec4> varyings) const override
    {
        const ml::mat4x4 light_mvp = uniforms[0].m4;
        gl_Position = light_mvp * attribs[0];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      [[maybe_unused]] ml::vec4& gl_FragColor) const override
    {
        return swr::accept;
    }
};

class scene_pass final : public swr::program<scene_pass>
{
public:
    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    void pre_link(
      boost::container::static_vector<
        swr::interpolation_qualifier,
        swr::limits::max::varyings>& iqs) const override
    {
        iqs = {
          swr::interpolation_qualifier::smooth,
          swr::interpolation_qualifier::smooth,
          swr::interpolation_qualifier::smooth,
          swr::interpolation_qualifier::smooth};
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      std::span<const ml::vec4> attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      std::span<ml::vec4> varyings) const override
    {
        const ml::mat4x4 camera_mvp = uniforms[0].m4;
        const ml::mat4x4 model = uniforms[1].m4;
        const ml::mat4x4 shadow_bias_light_mvp = uniforms[2].m4;

        const ml::vec4 world_pos = model * attribs[0];
        const ml::vec4 normal = model * ml::vec4{attribs[2].xyz(), 0.0f};

        gl_Position = camera_mvp * attribs[0];

        varyings[0] = attribs[1];
        varyings[1] = ml::vec4{normal.xyz().normalized(), 0.0f};
        varyings[2] = shadow_bias_light_mvp * attribs[0];
        varyings[3] = world_pos;
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        const ml::vec3 light_dir = uniforms[3].v4.xyz();
        const ml::vec3 normal = varyings[1].value.xyz();    // non-normalizing should be okay for cube
        const ml::vec4& base_color = varyings[0].value;
        const ml::vec4& shadow_pos = varyings[2].value;

        const float diffuse =
          std::max(0.0f, ml::dot(normal, -light_dir));

        if(diffuse <= 0.0f)
        {
            gl_FragColor = base_color * ml::vec4{0.2f, 0.2f, 0.2f, 1.0f};
            return swr::accept;
        }

        float shadow = 1.0f;
        if(std::abs(shadow_pos.w) > 1.0e-8f)
        {
            const bool use_pcf = uniforms[4].f >= 0.5f;
            const ml::vec3 world_pos = varyings[3].value.xyz();

            const ml::vec3 projected_shadow_pos = shadow_pos.xyz() / shadow_pos.w;
            if(projected_shadow_pos.x >= 0.0f && projected_shadow_pos.x <= 1.0f
               && projected_shadow_pos.y >= 0.0f && projected_shadow_pos.y <= 1.0f
               && projected_shadow_pos.z >= 0.0f && projected_shadow_pos.z <= 1.0f)
            {
                const float texel_size = uniforms[6].f;
                const float kernel_radius = use_pcf ? 1.5f : 0.5f;
                const float sin_theta =
                  std::sqrt(std::max(0.0f, 1.0f - diffuse * diffuse));
                const float tan_theta = sin_theta / std::max(diffuse, 0.12f);
                const float depth_bias = std::min(
                  0.02f,
                  texel_size * (1.25f + 2.5f * kernel_radius + 4.0f * tan_theta));
                const float normal_offset =
                  std::min(0.06f, texel_size * (6.0f + 6.0f * kernel_radius));

                const ml::vec3 offset_world_pos =
                  world_pos + normal * normal_offset;
                const ml::mat4x4& shadow_bias_light_vp = uniforms[5].m4;
                swr::varying shadow_sample_pos = varyings[2];
                shadow_sample_pos.value =
                  shadow_bias_light_vp * ml::vec4{offset_world_pos, 1.0f};
                shadow_sample_pos.value.z -= depth_bias * shadow_sample_pos.value.w;

                const auto& shadow_sampler = sampler2DShadow(0);

                float lit = 0.0f;
                if(use_pcf)
                {
                    auto sample = [&shadow_sampler, &shadow_sample_pos](int x, int y) -> float
                    {
                        return swr::textureProjOffset(
                          shadow_sampler,
                          shadow_sample_pos,
                          {x, y});
                    };

                    float pcf_sum = 0.0f;
                    pcf_sum += sample(-1, -1);
                    pcf_sum += sample(0, -1);
                    pcf_sum += sample(1, -1);
                    pcf_sum += sample(-1, 0);
                    pcf_sum += sample(0, 0);
                    pcf_sum += sample(1, 0);
                    pcf_sum += sample(-1, 1);
                    pcf_sum += sample(0, 1);
                    pcf_sum += sample(1, 1);

                    lit = pcf_sum / 9.0f;
                }
                else
                {
                    lit = swr::textureProj(shadow_sampler, shadow_sample_pos);
                }

                shadow = 0.35f + 0.65f * lit;
            }
        }

        const float lighting = 0.2f + 0.8f * diffuse * shadow;
        gl_FragColor =
          base_color * ml::vec4{lighting, lighting, lighting, 1.0f};
        return swr::accept;
    }
};

class debug_depth final : public swr::program<debug_depth>
{
public:
    swr::program_metadata get_metadata() const override
    {
        return {
          .fragment_shader_may_discard = false,
          .fragment_shader_may_write_depth = false};
    }

    void pre_link(
      boost::container::static_vector<
        swr::interpolation_qualifier,
        swr::limits::max::varyings>& iqs) const override
    {
        iqs = {swr::interpolation_qualifier::smooth};
    }

    void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      std::span<const ml::vec4> attribs,
      ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      std::span<ml::vec4> varyings) const override
    {
        gl_Position = attribs[0];
        varyings[0] = attribs[1];
    }

    swr::fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      ml::vec4& gl_FragColor) const override
    {
        const float depth = sampler2D(0).sample_at(varyings[0]).x;
        const float mapped =
          std::pow(std::clamp(1.0f - depth, 0.0f, 1.0f), 0.35f);
        gl_FragColor = {mapped, mapped, mapped, 1.0f};
        return swr::accept;
    }
};

} /* namespace shader */
