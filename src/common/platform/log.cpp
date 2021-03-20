/**
 * swr - a software rasterizer
 * 
 * logging.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <iostream>
#include <string>
#include <mutex>
#include <cassert>

#include "platform.h"

namespace platform
{

/** log_device singleton */
static log_null null_log;
log_device* log_device::singleton{&null_log};

/*
 * singleton interface 
 */

void log_device::set(log_device* new_singleton)
{
    if(new_singleton)
    {
        singleton = new_singleton;
    }
    else
    {
        singleton = &null_log;
    }
}

log_device& log_device::get()
{
    assert(is_initialized());
    return *singleton;
}

void log_device::cleanup()
{
    singleton = &null_log;
}

} /* namespace platform */