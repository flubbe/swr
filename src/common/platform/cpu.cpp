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

#if defined(__x86_64__) || defined(_M_X64)
#    include "cpuinfo_x86.h"
#elif defined(__aarch64__)
#    include "cpuinfo_aarch64.h"
#else
#    error "Unsupported architecture for cpu_features"
#endif

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

#if defined(__x86_64__) || defined(_M_X64)
    const cpu_features::X86Info info = cpu_features::GetX86Info();
    log_device::get().logf("arch:     x86");
    log_device::get().logf("brand:    {:s}", info.brand_string);
    log_device::get().logf("family:   {:#x}", info.family);
    log_device::get().logf("model:    {:#x}", info.model);
    log_device::get().logf("stepping: {:#x}", info.stepping);
    log_device::get().logf("uarch:    {:s}", GetX86MicroarchitectureName(cpu_features::GetX86Microarchitecture(&info)));
#elif defined(__aarch64__)
    const cpu_features::Aarch64Info info = cpu_features::GetAarch64Info();
    log_device::get().logf("arch:        aarch64");
    log_device::get().logf("implementer: {}", info.implementer);
    log_device::get().logf("part:        {}", info.part);
    log_device::get().logf("revision:    {}", info.revision);
    log_device::get().logf("variant:     {}", info.variant);
#else
#    error "Unsupported architecture for cpu_features"
#endif
}

} /* namespace platform */