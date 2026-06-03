/**
 * swr - a software rasterizer
 *
 * public interface for shader support.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
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

/** 2D sampler bindings type. */
using sampler_2d_bindings = boost::container::static_vector<
  swr::sampler_2d*,
  swr::limits::max::texture_units>;

/** Bindings for a program. */
struct program_instance_bindings
{
    /** Uniforms. */
    std::span<const uniform> uniforms{};

    /** 2D samplers. */
    std::span<sampler_2d* const> samplers_2d{};

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
      const sampler_2d_bindings& samplers_2d)
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

    /** assignment to vectors. does not reset the other values. */
    varying& operator=(ml::vec4 v)
    {
        value = v;
        return *this;
    }

    /** assign the varying's value to a vec4. */
    operator ml::vec4() const
    {
        return value;
    }
};

/** convenience function to return x-derivative of a varying. */
inline ml::vec4 dFdx(const varying& v)
{
    return v.dFdx;
}

/** convenience function to return y-derivative of a varying. */
inline ml::vec4 dFdy(const varying& v)
{
    return v.dFdy;
}

/** return the sum of the absolute value of derivatives in x and y. */
inline float fwidth(const varying& v)
{
    return v.dFdx.length() + v.dFdy.length();
}

/** fragment shader results */
enum fragment_shader_result
{
    discard,
    accept
};

/**
 * Optional shader behavior contract supplied by programs.
 *
 * A program must explicitly opt in before the rasterizer may run fragment
 * depth tests before the fragment shader.
 */
struct program_metadata
{
    /** True if the fragment shader may return fragment_shader_result::discard. */
    bool fragment_shader_may_discard{true};

    /** True if the fragment shader may modify gl_FragDepth. */
    bool fragment_shader_may_write_depth{true};
};

/** A complete graphics program, consisting of vertex- and fragment shader. */
class program_base
{
protected:
    std::span<const uniform> uniforms{};
    std::span<sampler_2d* const> samplers{};

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

    /**
     * Return optional shader behavior metadata.
     *
     * Existing shaders inherit the conservative default. Override this in a
     * shader to opt into optimizations that depend on fragment shader behavior.
     */
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

    /**
     * Vertex shader entry point.
     */
    virtual void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      [[maybe_unused]] std::span<const ml::vec4> attribs,
      [[maybe_unused]] ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] std::span<float> gl_ClipDistance,
      [[maybe_unused]] std::span<ml::vec4> varyings) const = 0;

    /**
     * Fragment shader entry point.
     */
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
    new_program->samplers = bindings.samplers_2d;
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
