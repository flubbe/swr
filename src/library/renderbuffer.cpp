/**
 * swr - a software rasterizer
 * 
 * frame buffer buffer implementation.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{

namespace impl
{

bool texture_renderbuffer::has_valid_attachment() const
{
    if(tex_id == default_tex_id || tex == nullptr || data == nullptr)
    {
        return false;
    }

    if(tex_id >= global_context->texture_2d_storage.size())
    {
        return false;
    }

    if(!global_context->texture_2d_storage[tex_id])
    {
        return false;
    }

    return global_context->texture_2d_storage[tex_id].get() == tex && tex_id == tex->id && data == &tex->data;
}

} /* namespace impl */

} /* namespace swr */
