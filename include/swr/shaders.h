/**
 * swr - a software rasterizer
 * 
 * public interface for shader support.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

/*
 * dependencies.
 * 
 * we do not include swr.h here, since it should already be included anyways. in
 * particular, "ml/all.h" and "vector" are already included.
 */

/* boost headers. */
#include <boost/container/static_vector.hpp>

/* user headers. */
#include "geometry/limits.h"

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
    int i;
    float f;

    ml::vec4 v4;
    ml::mat4x4 m4;

    /** default constructor. sets f to zero. */
    uniform()
    : f(0)
    {
    }

    /** integer constructor. */
    uniform(int in_i)
    : i(in_i)
    {
    }

    /** float constructor. */
    uniform(float in_f)
    : f(in_f)
    {
    }

    /** vector constructor. */
    uniform(ml::vec4 in_v4)
    : v4(in_v4)
    {
    }

    /** matrix constructor. */
    uniform(const ml::mat4x4& in_m4)
    : m4(in_m4)
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

    /** interpolation qualifier. */
    interpolation_qualifier iq{interpolation_qualifier::smooth};

    /** default constructor. */
    varying() = default;

    /** initializing constructor. */
    varying(const ml::vec4& in_value, const ml::vec4& in_dFdx, const ml::vec4& in_dFdy, interpolation_qualifier in_iq)
    : value(in_value)
    , dFdx(in_dFdx)
    , dFdy(in_dFdy)
    , iq(in_iq)
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

/** maximum number of color attachments. */
//!!fixme: this is related more to the framebuffer, which is not implemented.
constexpr int max_color_attachments = 8;

/** fragment shader results */
enum fragment_shader_result
{
    discard,
    accept
};

/**
 * A complete graphics program, consisting of vertex- and fragment shader.
 */
class program
{
protected:
    const boost::container::static_vector<swr::uniform, geom::limits::max::uniform_locations>* uniforms{nullptr};
    boost::container::static_vector<struct sampler_2d*, geom::limits::max::texture_units> samplers;

public:
    virtual ~program()
    {
    }

    void update_uniforms(const boost::container::static_vector<swr::uniform, geom::limits::max::uniform_locations>* in_uniforms)
    {
        uniforms = in_uniforms;
    }

    void update_samplers(const boost::container::static_vector<struct sampler_2d*, geom::limits::max::texture_units>* in_samplers)
    {
        // we copy the samplers (instead of using the pointer).
        samplers.resize(in_samplers->size());
        for(std::size_t i = 0; i < in_samplers->size(); ++i)
        {
            samplers[i] = (*in_samplers)[i];
        }
    }

    /** pre-link the program. */
    virtual void pre_link(boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs) const
    {
        // from https://www.khronos.org/opengl/wiki/Fragment_Shader:
        //
        // "The user-defined inputs received by this fragment shader will be interpolated according to the interpolation qualifiers
        //  declared on the input variables declared by this fragment shader. The fragment shader's input variables must be declared
        //  in accord with the interface matching rules between shader stages. Specifically, between this stage and the last Vertex
        //  Processing shader stage in the program or pipeline object."
        //
        // That is, interpolation qualifiers should be set here.

        iqs.clear();

        // !!todo: Also see https://www.khronos.org/opengl/wiki/Shader_Compilation for pre-linking setup.
    }

    /**
     * Vertex shader entry point.
     */
    virtual void vertex_shader(
      [[maybe_unused]] int gl_VertexID,
      [[maybe_unused]] int gl_InstanceID,
      [[maybe_unused]] const boost::container::static_vector<ml::vec4, geom::limits::max::attributes>& attribs,
      [[maybe_unused]] ml::vec4& gl_Position,
      [[maybe_unused]] float& gl_PointSize,
      [[maybe_unused]] float* gl_ClipDistance,
      [[maybe_unused]] boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& varyings) const
    {
    }

    /**
     * Fragment shader entry point.
     */
    virtual fragment_shader_result fragment_shader(
      [[maybe_unused]] const ml::vec4& gl_FragCoord,
      [[maybe_unused]] bool gl_FrontFacing,
      [[maybe_unused]] const ml::vec2& gl_PointCoord,
      [[maybe_unused]] const boost::container::static_vector<swr::varying, geom::limits::max::varyings>& varyings,
      [[maybe_unused]] float& gl_FragDepth,
      [[maybe_unused]] ml::vec4& gl_FragColor) const
    {
        return accept;
    }
};

/*
 * Interface.
 */

/**
 * Register a new shader.
 * \param InShader Pointer to the shader.
 * \return On success, this returns the (positive) Id of the shader. If an error occured, the return value is 0.
 */
uint32_t RegisterShader(program* InShader);

/**
 * Removes a shader from the graphics pipeline.
 *
 * \param Id The (positive) Id of the shader. If 0 is passed, the function sets last_error to invalid_value.
 */
void UnregisterShader(uint32_t Id);

/**
 * Bind a shader.
 *
 * \param Id The Id of the shader. If zero is passed, an empty shader is selected.
 * \return Returns false if the Id was invalid.
 */
bool BindShader(uint32_t Id);

}    // namespace swr
