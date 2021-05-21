/**
 * swr - a software rasterizer
 * 
 * statistics and benchmarking helpers.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{
namespace stats
{

void get_fragment_data(fragment_data& data)
{
    ASSERT_INTERNAL_CONTEXT;
    data = impl::global_context->stats_frag;
}

void get_rasterizer_data(rasterizer_data& data)
{
    ASSERT_INTERNAL_CONTEXT;
    data = impl::global_context->stats_rast;
}

}    // namespace stats
}    // namespace swr