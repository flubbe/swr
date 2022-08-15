/**
 * swr - a software rasterizer
 *
 * miscellaneous functions that do not fit elsewhere: library versions, error handling.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

/* versions. */
constexpr int lib_version_major{0};
constexpr int lib_version_minor{3};
constexpr int lib_version_patch{2};

namespace swr
{

/*
 * versioning.
 */

void GetVersion(int& major, int& minor, int& patch)
{
    major = lib_version_major;
    minor = lib_version_minor;
    patch = lib_version_patch;
}

/*
 * error handling.
 */

error GetLastError()
{
    ASSERT_INTERNAL_CONTEXT;
    error ret = impl::global_context->last_error;
    impl::global_context->last_error = error::none;
    return ret;
}

} /* namespace swr */