/**
 * swr - a software rasterizer
 *
 * platform specific code (mostly logging).
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include "fmt/format.h"

namespace platform
{

/** generic text logging device */
class log_device
{
    /** singleton */
    static log_device* singleton;

protected:
    /** log with newline at end. */
    virtual void log_n(const std::string& message) = 0;

public:
    /** empty destructor */
    virtual ~log_device()
    {
    }

    void log(const char* format, fmt::format_args args)
    {
        const std::string msg = fmt::vformat(format, args);
        log_n(fmt::format("Log: {}", msg));
    }

    template<typename... Args>
    void logf(const char* format, const Args&... args)
    {
        log(format, fmt::make_format_args(args...));
    }

    /** set new singleton. note that this does not clean up memory. does not check if new_singleton is valid. */
    static void set(log_device* new_singleton);

    /** singleton interface getter. */
    static log_device& get();

    /** check if a singleton is available. */
    static bool is_initialized();

    /** free singleton memory allocated from new. */
    static void cleanup();
};

/** formatted log interface. */
template<typename... Args>
void logf(const char* format, const Args&... args)
{
    log_device::get().log(format, fmt::make_format_args(args...));
}

/** log empty line. */
inline void log_n()
{
    log_device::get().logf("");
}

/** Fall-back null log device. Does not log. */
class log_null : public log_device
{
protected:
    void log_n(const std::string&)
    {
    }
};

/** (early) platform initialization. */
void global_initialize(log_device* log = nullptr);

/** (late) platform shutdown. */
void global_shutdown();

/** enable log by setting a (non-null) log device. disable it by passing nullptr. */
void set_log(log_device* in_log);

/** log cpu info. */
void get_cpu_info();

} /* namespace platform */
