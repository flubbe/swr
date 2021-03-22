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

void create_default_shader(render_device_context* Context)
{
    assert(Context);

    // create default shader.
    program* NewShader = new program();
    NewShader->bind(Context);

    swr::impl::program_info pi(NewShader);

    // pre-link the shader and initialize varying count.
    NewShader->pre_link(pi.iqs);
    pi.varying_count = pi.iqs.size();
    pi.flags |= swr::impl::program_flags::prelinked;

    // the default shader needs to be at position 0.
    if(Context->ShaderObjectHash.size() > 0)
    {
        throw std::runtime_error("unable to create default shader: memory already allocated.");
    }

    // Register shader.
    auto index = Context->ShaderObjectHash.push(pi);
    if(index != 0)
    {
        throw std::runtime_error("unable to create default shader: wrong shader location.");
    }

    // activate the default shader.
    Context->RenderStates.shader_info = &Context->ShaderObjectHash[0];
}

} /* namespace impl */

/*
 * shader/context registration.
 */

bool program::bind(context_handle InContext)
{
    if(InContext == nullptr && Context)
    {
        // If InContext is invalid, we mark the shader as unregistered by
        // invalidating the stored context.
        Context = nullptr;
    }
    else if(InContext && Context == nullptr)
    {
        // Mark the shader as registered.
        Context = InContext;
    }
    else
    {
        // re-registering a shader to a different context is not possible.
        return false;
    }

    return true;
}

swr::sampler_2d* program::get_sampler_2d(uint32_t id)
{
    impl::render_device_context* ctx = static_cast<impl::render_device_context*>(Context);

    if(id == 0) /* impl::default_texture_id */
    {
        return ctx->DefaultTexture2d->sampler;
    }

    if(id < ctx->Texture2dHash.size())
    {
        impl::texture_2d* tex = ctx->Texture2dHash[id];
        return (tex ? tex->sampler : nullptr);
    }

    return nullptr;
}

/*
 * Public Interface
 */

uint32_t RegisterShader(program* InShader)
{
    ASSERT_INTERNAL_CONTEXT;

    if(!InShader)
    {
        return 0;
    }

    // Bind context to shader. If the shader was already registered to the context,
    // the binding will fail.
    if(!InShader->bind(impl::global_context))
    {
        return 0;
    }

    swr::impl::program_info pi(InShader);

    // pre-link the shader and initialize varying count.
    //
    // it is allowed for the shader to be pre-linked multiple times, so we don't check
    // pi.is_prelinked().
    InShader->pre_link(pi.iqs);
    pi.varying_count = pi.iqs.size();

    pi.flags |= swr::impl::program_flags::prelinked;

    // Register shader.
    return impl::global_context->ShaderObjectHash.push(pi);
}

void UnregisterShader(uint32_t Id)
{
    ASSERT_INTERNAL_CONTEXT;

    // check for invalid values. 0 is the default shader, which should not be unregistered.
    if(Id == 0)
    {
        impl::global_context->last_error = error::invalid_value;
        return;
    }

    if(Id < impl::global_context->ShaderObjectHash.size())
    {
        impl::global_context->ShaderObjectHash.free(Id);
    }
}

bool BindShader(uint32_t Id)
{
    ASSERT_INTERNAL_CONTEXT;

    if(Id < impl::global_context->ShaderObjectHash.size())
    {
        // Bind the shader.
        impl::global_context->RenderStates.shader_info = &impl::global_context->ShaderObjectHash[Id];
        return true;
    }

    impl::global_context->last_error = error::invalid_value;
    return false;
}

/*
 * uniforms.
 */

void BindUniform(uint32_t UniformId, int Value)
{
    ASSERT_INTERNAL_CONTEXT;

    if(UniformId < geom::limits::max::uniform_locations)
    {
        auto* Context = impl::global_context;

        if(UniformId >= Context->RenderStates.uniforms.size())
        {
            Context->RenderStates.uniforms.resize(UniformId + 1);
        }

        Context->RenderStates.uniforms[UniformId].i = Value;
    }
}

void BindUniform(uint32_t UniformId, float Value)
{
    ASSERT_INTERNAL_CONTEXT;

    if(UniformId < geom::limits::max::uniform_locations)
    {
        auto* Context = impl::global_context;

        if(UniformId >= Context->RenderStates.uniforms.size())
        {
            Context->RenderStates.uniforms.resize(UniformId + 1);
        }

        Context->RenderStates.uniforms[UniformId].f = Value;
    }
}

void BindUniform(uint32_t UniformId, ml::mat4x4 Value)
{
    ASSERT_INTERNAL_CONTEXT;

    if(UniformId < geom::limits::max::uniform_locations)
    {
        auto* Context = impl::global_context;

        if(UniformId >= Context->RenderStates.uniforms.size())
        {
            Context->RenderStates.uniforms.resize(UniformId + 1);
        }

        Context->RenderStates.uniforms[UniformId].m4 = Value;
    }
}

void BindUniform(uint32_t UniformId, ml::vec4 Value)
{
    ASSERT_INTERNAL_CONTEXT;

    if(UniformId < geom::limits::max::uniform_locations)
    {
        auto* Context = impl::global_context;

        if(UniformId >= Context->RenderStates.uniforms.size())
        {
            Context->RenderStates.uniforms.resize(UniformId + 1);
        }

        Context->RenderStates.uniforms[UniformId].v4 = Value;
    }
}

} /* namespace swr */
