/**
 * swr - a software rasterizer
 * 
 * get/log CPU information using Google's cpu_features library.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* cpu info */
#include "cpu_features_macros.h"
#include "cpuinfo_x86.h"

#include "platform.h"

namespace platform
{

/** 
 * get (and log) cpu info 
 */
void get_cpu_info()
{
    log_device::get().logf("");
    log_device::get().logf("CPU info:");

    char brand_string[49];
    const cpu_features::X86Info info = cpu_features::GetX86Info();
    cpu_features::FillX86BrandString(brand_string);
    log_device::get().logf( "arch:     x86");
    log_device::get().logf( "brand:    {:s}", brand_string );
    log_device::get().logf( "family:   {:#x}", info.family );
    log_device::get().logf( "model:    {:#x}", info.model );
    log_device::get().logf( "stepping: {:#x}", info.stepping );
    log_device::get().logf( "uarch:    {:s}", GetX86MicroarchitectureName(cpu_features::GetX86Microarchitecture(&info)) );
}

} /* namespace platform */