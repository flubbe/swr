/**
 * swr - a software rasterizer
 *
 * a simple particle emitter.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace particles
{

/** particle parameters. */
struct parameters
{
    /** current position. */
    ml::vec4 position;

    /** current velocity. */
    ml::vec4 velocity;

    /** rotation axis. */
    ml::vec4 rotation_axis;

    /** current rotation offset. */
    float rotation_offset{0};

    /** current rotation speed. */
    float rotation_speed{0};

    /** scale factor. */
    float scale{1};

    /** if this is non-negative, the particle is allowed to respawn. */
    float respawn_time{0};

    /** only active particles are updated. */
    bool is_active{true};

    /** default constructor. */
    parameters() = default;

    /** constructor for constructing a delayed particle. */
    parameters(float delay_time)
    : respawn_time{-delay_time}
    , is_active{false}
    {
    }
};

/** a simple particle system. */
class particle_system
{
    /** acting forces. */
    ml::vec4 gravity{0, 0, -9.81, 0};

    /** spawn point for the particles. */
    ml::vec3 spawn_point;

    /** only particles in this radius are considered active. */
    float activity_radius{6};

    /** scale of the particles this system produces. */
    float scale{0.2f};

    /** the minimal velocity. */
    float min_velocity{8};

    /** velocity variation. */
    float var_velocity{1};

    /** particle list. */
    std::vector<parameters> particles;

    /** particle generator. */
    parameters generate()
    {
        parameters new_particle;

        new_particle.position = spawn_point;

        /*
         * for now, the emission direction is hard-coded. we emit randomly within a small cone upwards.
         */

        float phi = static_cast<float>(std::rand() & 0xff) / 255.f * 2 * static_cast<float>(M_PI);
        float theta = static_cast<float>(std::rand() & 0xff) / 255.f * static_cast<float>(M_PI_2);
        new_particle.rotation_axis = ml::vec4{std::sin(phi) * std::sin(theta), std::cos(phi) * std::sin(theta), std::cos(theta)};
        new_particle.rotation_speed = static_cast<float>(std::rand() & 0xff) / 255.f * 5;

        new_particle.respawn_time = 0;

        float velocity = min_velocity + static_cast<float>(std::rand() & 0xff) / 255.f * var_velocity;
        phi = static_cast<float>(std::rand() & 0xff) / 255.f * 2 * static_cast<float>(M_PI);
        theta = (static_cast<float>(std::rand() & 0xff) / 255.f - 1.f) * static_cast<float>(M_PI_4) / 2;

        new_particle.velocity = ml::vec4{std::sin(phi) * std::sin(theta), std::cos(phi) * std::sin(theta), std::cos(theta)} * velocity;

        new_particle.scale = scale;

        return new_particle;
    }

    /** generate an inactive particle with a time delay. */
    parameters delay_generate(float delay_time)
    {
        return {delay_time};
    }

public:
    /** default constructor. */
    particle_system() = default;

    /** initialize the system's parameters. */
    particle_system(ml::vec3 in_spawn_point, float in_activity_radius, float in_scale, float in_min_velocity, float in_velocity_variation, ml::vec4 in_gravity = {0, 0, -9.81f, 0})
    : gravity{in_gravity}
    , spawn_point{in_spawn_point}
    , activity_radius{in_activity_radius}
    , scale{in_scale}
    , min_velocity{in_min_velocity}
    , var_velocity{in_velocity_variation}
    {
    }

    /** update all particles. */
    void update(float delta_time)
    {
        float respawn_time = -0.1;

        for(auto& it: particles)
        {
            if(!it.is_active)
            {
                it.respawn_time += delta_time;
                if(it.respawn_time > 0)
                {
                    it = generate();
                }
                else
                {
                    continue;
                }
            }

            // apply gravity.
            it.velocity += gravity * delta_time;

            // update position.
            it.position += it.velocity * delta_time;

            // update rotation.
            it.rotation_offset += it.rotation_speed * delta_time;
            if(it.rotation_offset > 2 * static_cast<float>(M_PI))
            {
                it.rotation_offset -= 2 * static_cast<float>(M_PI);
            }
            else if(it.rotation_offset < 0)
            {
                it.rotation_offset += 2 * static_cast<float>(M_PI);
            }

            // if the particle is outside some radius, consider it inactive.
            if((it.position - spawn_point).length_squared() > activity_radius * activity_radius)
            {
                it.is_active = false;
                it.respawn_time = respawn_time;

                respawn_time -= 0.1f;
            }
        }
    }

    /** add a new particle. */
    void add()
    {
        particles.emplace_back(generate());
    }

    /** delay-add a particle. */
    void delay_add(float delay_time)
    {
        particles.emplace_back(delay_time);
    }

    /** delay-add multiple particles. if 'diff' is negative, all particle will spawn immediately. */
    void delay_add(float diff, std::size_t count)
    {
        float delay_time = 0;
        for(std::size_t i = 0; i < count; ++i)
        {
            particles.emplace_back(delay_time);
            delay_time += diff;
        }
    }

    /** get current particles (including inactive ones). */
    std::size_t get_particle_count() const
    {
        return particles.size();
    }

    /** get currently active particles. */
    std::size_t get_active_particle_count() const
    {
        std::size_t count{0};

        for(auto it: particles)
        {
            count += static_cast<std::size_t>(it.is_active);
        }

        return count;
    }

    /** access. */
    const std::vector<parameters>& get_particles() const
    {
        return particles;
    }
};

} /* namespace particles */