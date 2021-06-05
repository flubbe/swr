/**
 * swr - a software rasterizer
 * 
 * vertex shader and fragment shader support.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

#include "rasterizer/interpolators.h"

#include "output_merger.h"

namespace swr
{

namespace impl
{

void create_default_shader(render_device_context* context)
{
    assert(context);

    // create default shader.
    program* default_shader = new program();
    swr::impl::program_info pi(default_shader);

    // pre-link the shader and initialize varying count.
    default_shader->pre_link(pi.iqs);
    pi.varying_count = pi.iqs.size();
    pi.flags |= swr::impl::program_flags::prelinked;

    // the default shader needs to be at position 0.
    if(context->ShaderObjectHash.size() > 0)
    {
        throw std::runtime_error("unable to create default shader: memory already allocated.");
    }

    // Register shader.
    auto index = context->ShaderObjectHash.push(pi);
    if(index != 0)
    {
        throw std::runtime_error("unable to create default shader: wrong shader location.");
    }

    // activate the default shader.
    context->RenderStates.shader_info = &context->ShaderObjectHash[0];
}

} /* namespace impl */

/*
 * Public Interface
 */

uint32_t RegisterShader(program* in_shader)
{
    ASSERT_INTERNAL_CONTEXT;

    if(!in_shader)
    {
        return 0;
    }

    swr::impl::program_info pi(in_shader);

    // pre-link the shader and initialize varying count.
    //
    // it is allowed for the shader to be pre-linked multiple times, so we don't check
    // pi.is_prelinked().
    in_shader->pre_link(pi.iqs);
    pi.varying_count = pi.iqs.size();

    pi.flags |= swr::impl::program_flags::prelinked;

    // Register shader.
    return impl::global_context->ShaderObjectHash.push(pi);
}

void UnregisterShader(uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;

    // check for invalid values. 0 is the default shader, which should not be unregistered.
    if(id == 0)
    {
        impl::global_context->last_error = error::invalid_value;
        return;
    }

    if(id < impl::global_context->ShaderObjectHash.size())
    {
        impl::global_context->ShaderObjectHash.free(id);
    }
}

bool BindShader(uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;

    if(id < impl::global_context->ShaderObjectHash.size())
    {
        // Bind the shader.
        impl::global_context->RenderStates.shader_info = &impl::global_context->ShaderObjectHash[id];
        return true;
    }

    impl::global_context->last_error = error::invalid_value;
    return false;
}

/*
 * uniforms.
 */

void BindUniform(uint32_t id, int value)
{
    ASSERT_INTERNAL_CONTEXT;

    if(id < geom::limits::max::uniform_locations)
    {
        auto* context = impl::global_context;

        if(id >= context->RenderStates.uniforms.size())
        {
            context->RenderStates.uniforms.resize(id + 1);
        }

        context->RenderStates.uniforms[id].i = value;
    }
}

void BindUniform(uint32_t id, float value)
{
    ASSERT_INTERNAL_CONTEXT;

    if(id < geom::limits::max::uniform_locations)
    {
        auto* context = impl::global_context;

        if(id >= context->RenderStates.uniforms.size())
        {
            context->RenderStates.uniforms.resize(id + 1);
        }

        context->RenderStates.uniforms[id].f = value;
    }
}

void BindUniform(uint32_t id, ml::mat4x4 value)
{
    ASSERT_INTERNAL_CONTEXT;

    if(id < geom::limits::max::uniform_locations)
    {
        auto* context = impl::global_context;

        if(id >= context->RenderStates.uniforms.size())
        {
            context->RenderStates.uniforms.resize(id + 1);
        }

        context->RenderStates.uniforms[id].m4 = value;
    }
}

void BindUniform(uint32_t id, ml::vec4 value)
{
    ASSERT_INTERNAL_CONTEXT;

    if(id < geom::limits::max::uniform_locations)
    {
        auto* context = impl::global_context;

        if(id >= context->RenderStates.uniforms.size())
        {
            context->RenderStates.uniforms.resize(id + 1);
        }

        context->RenderStates.uniforms[id].v4 = value;
    }
}

} /* namespace swr */
