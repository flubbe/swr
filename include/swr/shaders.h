/**
 * swr - a software rasterizer
 *
 * public interface for shader support.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

/* C++ headers. */
#include <cassert>
#include <cstdint>
#include <span>
#include <type_traits>

/* boost headers. */
#include <boost/container/static_vector.hpp>

/*
 * we do not include swr.h here, since it should already be included anyways. in
 * particular, "ml/all.h" and "vector" are already included.
 */

/* user headers. */
#include "limits.h"

namespace swr
{

/**
 * uniform.
 *
 * from https://www.khronos.org/opengl/wiki/Uniform_(GLSL):
 *
 * All non-array/struct types will be assigned a single location.
 */
union uniform
{
    float f;
    int i;

    ml::vec4 v4;
    ml::mat4x4 m4;

    /** default constructor. does not initialize the data. */
    uniform()
    {
    }

    /** default copy/move constructors. */
    uniform(const uniform&) = default;
    uniform(uniform&&) = default;

    /** assignment. */
    uniform& operator=(const uniform&) = default;
    uniform& operator=(uniform&&) = default;

    /** float constructor. */
    uniform(float in_f)
    : f{in_f}
    {
    }

    /** integer constructor. */
    uniform(int in_i)
    : i{in_i}
    {
    }

    /** vector constructor. */
    uniform(ml::vec4 in_v4)
    : v4{in_v4}
    {
    }

    /** matrix constructor. */
    uniform(const ml::mat4x4& in_m4)
    : m4{in_m4}
    {
    }
};

/** Uniform bindings type. */
using uniform_bindings = boost::container::static_vector<
  swr::uniform,
  swr::limits::max::uniform_locations>;

/* sampler_2d is already defined by the assumed inclusion of swr.h */

/** Sampler bindings type. */
using sampler_bindings = boost::container::static_vector<
  swr::sampler_base*,
  swr::limits::max::texture_units>;

/** Cached color sampler bindings type. */
using sampler_2d_bindings = boost::container::static_vector<
  const swr::sampler_2d*,
  swr::limits::max::texture_units>;

/** Cached shadow sampler bindings type. */
using sampler_shadow_2d_bindings = boost::container::static_vector<
  const swr::sampler_shadow_2d*,
  swr::limits::max::texture_units>;

/** Bindings for a program. */
struct program_instance_bindings
{
    /** Uniforms. */
    std::span<const uniform> uniforms{};

    /** 2D samplers. */
    std::span<sampler_base* const> samplers_2d{};

    /** Default constructor. */
    program_instance_bindings() = default;

    /**
     * Set up bindings with uniforms only.
     *
     * @param uniforms The uniforms to use.
     */
    explicit program_instance_bindings(
      const uniform_bindings& uniforms)
    : uniforms{uniforms.data(), uniforms.size()}
    {
    }

    /**
     * Set up bindings with uniforms and texture samplers.
     *
     * @param uniforms The uniforms to use.
     * @param samplers_2d The samplers to use.
     */
    program_instance_bindings(
      const uniform_bindings& uniforms,
      const sampler_bindings& samplers_2d)
    : uniforms{uniforms.data(), uniforms.size()}
    , samplers_2d{samplers_2d.data(), samplers_2d.size()}
    {
    }
};

/**
 * Interpolation qualifier.
 * See https://www.khronos.org/opengl/wiki/Type_Qualifier_(GLSL)
 */
enum class interpolation_qualifier
{
    flat,           /** Constant, i.e. no interpolation. */
    no_perspective, /** Linear interpolation in window space. */
    smooth          /** Perspective interpolation. */
};

/** varyings. */
struct varying
{
    /** current value of the varying. */
    ml::vec4 value;

    /** approximation of the partial derivative with respect to x. */
    ml::vec4 dFdx;

    /** approximation of the partial derivative with respect to x. */
    ml::vec4 dFdy;

    /** constructors. */
    varying() = default;
    varying(const varying&) = default;
    varying(varying&&) = default;

    /** assignment. */
    varying& operator=(const varying&) = default;
    varying& operator=(varying&&) = default;

    /** initializing constructor. */
    varying(
      const ml::vec4& in_value,
      const ml::vec4& in_dFdx,
      const ml::vec4& in_dFdy)
    : value(in_value)
    , dFdx(in_dFdx)
    , dFdy(in_dFdy)
    {
    }

    /** assign the varying's value to a vec4. */
    operator ml::vec4() const
    {
        return value;
    }
};

/**
 * GLSL-style convenience function to return the x-derivative of a varying.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/dFdx.xhtml
 */
inline ml::vec4 dFdx(const varying& v)
{
    return v.dFdx;
}

/**
 * GLSL-style convenience function to return the y-derivative of a varying.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/dFdx.xhtml
 */
inline ml::vec4 dFdy(const varying& v)
{
    return v.dFdy;
}

/**
 * GLSL-style sum of the absolute value of derivatives in x and y.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/fwidth.xhtml
 */
inline float fwidth(const varying& v)
{
    return v.dFdx.length() + v.dFdy.length();
}

/**
 * GLSL-style texture helper for coordinates `(u, v)`.
 *
 * This overload preserves interpolated derivatives and is the closest match to
 * implicit-LOD fragment-stage sampling in GLSL.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/texture.xhtml
 */
inline ml::vec4 texture(
  const sampler_2d& sampler,
  const varying& tex_coords)
{
    return sampler.sample_at(tex_coords);
}

/**
 * GLSL-style texture helper for coordinates `(u, v)`.
 *
 * This convenience overload does not have access to implicit derivatives, so
 * it samples with zero gradients.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/texture.xhtml
 */
inline ml::vec4 texture(
  const sampler_2d& sampler,
  const ml::vec2& tex_coords)
{
    return sampler.sample_at(
      {{tex_coords.x, tex_coords.y, 0.f, 0.f},
       ml::vec4::zero(),
       ml::vec4::zero()});
}

/**
 * GLSL-style texture helper for shadow-style coordinates `(u, v, reference)`.
 *
 * When the texture compare mode is `ref_to_texture`, this performs a depth
 * comparison sample. Otherwise it returns the raw sampled depth value.
 *
 * This overload does not have access to implicit derivatives, so
 * it samples with zero gradients.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/texture.xhtml
 */
inline float texture(
  const sampler_shadow_2d& sampler,
  const ml::vec3& tex_coords)
{
    return sampler.sample_compare_at(
      {{tex_coords.x, tex_coords.y, tex_coords.z, 0.f},
       ml::vec4::zero(),
       ml::vec4::zero()});
}

/**
 * GLSL-style shadow sample that preserves interpolated uv derivatives while
 * supplying the compare reference separately.
 *
 * This overload is the closest match to implicit-LOD fragment-stage shadow
 * sampling in GLSL.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/texture.xhtml
 */
inline float texture(
  const sampler_shadow_2d& sampler,
  const varying& tex_coords,
  float reference)
{
    return sampler.sample_compare_at(
      {{tex_coords.value.x, tex_coords.value.y, reference, 0.f},
       {tex_coords.dFdx.x, tex_coords.dFdx.y, 0.f, 0.f},
       {tex_coords.dFdy.x, tex_coords.dFdy.y, 0.f, 0.f}});
}

/**
 * GLSL-style depth texture lookup helper for a prepared varying.
 *
 * This overload preserves interpolated derivatives and is the closest match to
 * implicit-LOD fragment-stage sampling in GLSL.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/texture.xhtml
 */
inline float texture(
  const sampler_depth_2d& sampler,
  const varying& tex_coords)
{
    return sampler.sample_depth_at(tex_coords);
}

/**
 * GLSL-style depth texture lookup helper for plain uv coordinates.
 *
 * This overload does not have access to implicit derivatives, so
 * it projects the coordinates and then samples with zero gradients.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/texture.xhtml
 */
inline float texture(
  const sampler_depth_2d& sampler,
  const ml::vec2& tex_coords)
{
    return sampler.sample_depth_at(
      {{tex_coords.x, tex_coords.y, 0.f, 0.f},
       ml::vec4::zero(),
       ml::vec4::zero()});
}

namespace detail
{

/** Project homogeneous texture coordinates and propagate derivatives. */
inline varying project_texture_coords(
  const varying& tex_coords)
{
    assert(std::abs(tex_coords.value.w) > 1.0e-6f);

    const float inv_w = 1.0f / tex_coords.value.w;
    const float inv_w_squared = inv_w * inv_w;

    const auto project_derivative =
      [&tex_coords, inv_w_squared](const ml::vec4& derivative) -> ml::vec4
    {
        return {
          (derivative.x * tex_coords.value.w - tex_coords.value.x * derivative.w)
            * inv_w_squared,
          (derivative.y * tex_coords.value.w - tex_coords.value.y * derivative.w)
            * inv_w_squared,
          (derivative.z * tex_coords.value.w - tex_coords.value.z * derivative.w)
            * inv_w_squared,
          0.0f};
    };

    return {
      {tex_coords.value.x * inv_w,
       tex_coords.value.y * inv_w,
       tex_coords.value.z * inv_w,
       0.0f},
      project_derivative(tex_coords.dFdx),
      project_derivative(tex_coords.dFdy)};
}

} /* namespace detail */

/**
 * GLSL-style shadow compare sample with integer texel offset.
 *
 * Does not have access to implicit derivatives and therefore
 * uses zero gradients.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/textureOffset.xhtml
 */
inline float textureOffset(
  const sampler_shadow_2d& sampler,
  const ml::vec3& tex_coords,
  const ml::tvec2<int>& offset)
{
    const auto size = sampler.size(0);
    assert(size.x > 0);
    assert(size.y > 0);

    const ml::vec3 shifted_coords{
      tex_coords.x + static_cast<float>(offset.x) / static_cast<float>(size.x),
      tex_coords.y + static_cast<float>(offset.y) / static_cast<float>(size.y),
      tex_coords.z};
    return texture(sampler, shifted_coords);
}

/**
 * GLSL-style projected shadow compare sample (`vec4` projected to `vec3`).
 *
 * This overload does not have access to implicit derivatives, so
 * it projects the coordinates and then samples with zero gradients.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/textureProj.xhtml
 */
inline float textureProj(
  const sampler_shadow_2d& sampler,
  const ml::vec4& tex_coords)
{
    assert(std::abs(tex_coords.w) > 1.0e-6f);

    const float inv_w = 1.0f / tex_coords.w;
    const ml::vec3 projected_coords{
      tex_coords.x * inv_w,
      tex_coords.y * inv_w,
      tex_coords.z * inv_w};
    return texture(sampler, projected_coords);
}

/**
 * GLSL-style projected shadow compare sample for a prepared varying.
 *
 * This overload projects the coordinates and propagates derivatives, making it
 * the closest match to implicit-LOD fragment-stage projected sampling in GLSL.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/textureProj.xhtml
 */
inline float textureProj(
  const sampler_shadow_2d& sampler,
  const varying& tex_coords)
{
    const varying projected_coords = detail::project_texture_coords(tex_coords);
    return texture(sampler, projected_coords, projected_coords.value.z);
}

/**
 * GLSL-style projected shadow compare sample with integer texel offset.
 *
 * This overload does not have access to implicit derivatives, so
 * it projects the coordinates and then samples with zero gradients.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/textureProjOffset.xhtml
 */
inline float textureProjOffset(
  const sampler_shadow_2d& sampler,
  const ml::vec4& tex_coords,
  const ml::tvec2<int>& offset)
{
    assert(std::abs(tex_coords.w) > 1.0e-6f);

    const float inv_w = 1.0f / tex_coords.w;
    const ml::vec3 projected_coords{
      tex_coords.x * inv_w,
      tex_coords.y * inv_w,
      tex_coords.z * inv_w};
    return textureOffset(sampler, projected_coords, offset);
}

/**
 * GLSL-style projected shadow compare sample with offset for a varying.
 *
 * This overload projects the coordinates and propagates derivatives before
 * applying the integer texel offset.
 *
 * See https://registry.khronos.org/OpenGL-Refpages/gl4/html/textureProjOffset.xhtml
 */
inline float textureProjOffset(
  const sampler_shadow_2d& sampler,
  const varying& tex_coords,
  const ml::tvec2<int>& offset)
{
    const varying projected_coords = detail::project_texture_coords(tex_coords);
    const auto size = sampler.size(0);
    assert(size.x > 0);
    assert(size.y > 0);

    varying shifted_coords = projected_coords;
    shifted_coords.value.x +=
      static_cast<float>(offset.x) / static_cast<float>(size.x);
    shifted_coords.value.y +=
      static_cast<float>(offset.y) / static_cast<float>(size.y);
    return texture(sampler, shifted_coords, shifted_coords.value.z);
}

/** fragment shader results */
enum fragment_shader_result
{
    discard,
    accept
};

/** Shader behavior metadata. */
struct program_metadata
{
    /** True if the fragment shader may return `fragment_shader_result::discard`. */
    bool fragment_shader_may_discard{true};

    /** True if the fragment shader may modify `gl_FragDepth`. */
    bool fragment_shader_may_write_depth{true};
};

/** A complete graphics program, consisting of vertex- and fragment shader. */
class program_base
{
protected:
    std::span<const uniform> uniforms{};
    sampler_2d_bindings sampler_2d_views{};
    sampler_shadow_2d_bindings sampler_shadow_2d_views{};

    const sampler_2d& sampler2D(std::size_t index) const
    {
        assert(index < sampler_2d_views.size());
        return *sampler_2d_views[index];
    }

    const sampler_shadow_2d& sampler2DShadow(std::size_t index) const
    {
        assert(index < sampler_shadow_2d_views.size());
        return *sampler_shadow_2d_views[index];
    }

public:
    program_base() = default;
    program_base(const program_base&) = default;
    program_base(program_base&&) = default;

    program_base& operator=(const program_base&) = default;
    program_base& operator=(program_base&&) = default;

    virtual ~program_base() = default;

    /** return the size (in bytes) of the program. */
    virtual std::size_t size() const = 0;

    /** return the alignment required for the program. */
    virtual std::size_t alignment() const = 0;

    /**
     * create a new shader instance from this program.
     *
     * @param mem The memory to store the program object in.
     * @param bindings The render state bindings for this program instance.
     * @returns A program instance for execution.
     */
    virtual program_base* create_instance(
      void* mem,
      const program_instance_bindings& bindings) const = 0;

    /** return shader behavior metadata. */
    [[nodiscard]]
    virtual program_metadata get_metadata() const
    {
        return {};
    }

    /**
     * pre-link the program.
     *
     * from https://www.khronos.org/opengl/wiki/Fragment_Shader:
     *
     *   "The user-defined inputs received by this fragment shader will be interpolated according to the interpolation qualifiers
     *    declared on the input variables declared by this fragment shader. The fragment shader's input variables must be declared
     *    in accord with the interface matching rules between shader stages. Specifically, between this stage and the last Vertex
     *    Processing shader stage in the program or pipeline object."
     *
     * That is, interpolation qualifiers should be set here.
     *
     * TODO Also see https://www.khronos.org/opengl/wiki/Shader_Compilation for pre-linking setup.
     */
    virtual void pre_link(
      boost::container::static_vector<
        swr::interpolation_qualifier,
        swr::limits::max::varyings>&
        iqs) const = 0;

    /** Vertex shader entry point. */
    virtual void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      [[maybe_unused]] std::span<const ml::vec4> attribs,
      [[maybe_unused]] ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      [[maybe_unused]] std::span<ml::vec4> varyings) const = 0;

    /** Fragment shader entry point. */
    virtual fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] std::span<const swr::varying> varyings,
      [[maybe_unused]] float& gl_FragDepth,
      [[maybe_unused]] ml::vec4& gl_FragColor) const = 0;
};

/**
 * A helper class providing instantiation methods to simplify program creation.
 *
 * @tparam T A type derived from `program`.
 */
template<typename T>
class program : public program_base
{
public:
    program()
    : program_base{}
    {
        static_assert(
          std::is_base_of_v<program<T>, T>,
          "Invalid program base.");
    }
    program(const program&) = default;
    program(program&&) = default;

    program& operator=(const program&) = default;
    program& operator=(program&&) = default;

    ~program() override = default;

    std::size_t size() const override;
    std::size_t alignment() const override;
    program_base* create_instance(
      void* mem,
      const program_instance_bindings& bindings) const override;
};

template<typename T>
std::size_t program<T>::size() const
{
    return sizeof(T);
}

template<typename T>
std::size_t program<T>::alignment() const
{
    return alignof(T);
}

template<typename T>
program_base* program<T>::create_instance(
  void* mem,
  const program_instance_bindings& bindings) const
{
    assert(reinterpret_cast<std::uintptr_t>(mem) % alignof(T) == 0);
    auto* new_program = new(mem) T{static_cast<const T&>(*this)};

    new_program->uniforms = bindings.uniforms;
    new_program->sampler_2d_views.clear();
    new_program->sampler_shadow_2d_views.clear();
    for(auto* sampler: bindings.samplers_2d)
    {
        new_program->sampler_2d_views.push_back(sampler->as_sampler_2d());
        new_program->sampler_shadow_2d_views.push_back(
          sampler->as_sampler_shadow_2d());
    }

    return static_cast<program_base*>(new_program);
}

/*
 * Interface.
 */

/**
 * Register a new shader.
 * @param InShader Pointer to the shader.
 * @returns On success, this returns the (positive) Id of the shader. If an error occured, the return value is 0.
 */
std::uint32_t RegisterShader(const program_base* InShader);

/**
 * Removes a shader from the graphics pipeline.
 *
 * @param Id The (positive) Id of the shader. If 0 is passed, the function sets last_error to invalid_value.
 */
void UnregisterShader(std::uint32_t Id);

/**
 * Bind a shader.
 *
 * @param Id The Id of the shader. If zero is passed, an empty shader is selected.
 * @returns Returns false if the Id was invalid.
 */
bool BindShader(std::uint32_t Id);

}    // namespace swr
