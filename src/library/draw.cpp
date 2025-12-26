/**
 * swr - a software rasterizer
 *
 * buffer drawing functions.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{

/*
 * drawing.
 */

void DrawElements(vertex_buffer_mode mode, std::size_t vertex_count)
{
    ASSERT_INTERNAL_CONTEXT;

    // add draw command to command list.
    impl::global_context->create_render_object(mode, vertex_count);
}

void DrawIndexedElements(vertex_buffer_mode mode, std::size_t count, const std::vector<std::uint32_t> index_buffer)
{
    ASSERT_INTERNAL_CONTEXT;

    // add draw command to the command list.
    impl::global_context->create_indexed_render_object(mode, count, index_buffer);
}

} /* namespace swr */