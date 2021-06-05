/**
 * swr - a software rasterizer
 * 
 * entry point for framework to quickly set up an application with a software rasterizer.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* platform code. */
#include "../common/platform/platform.h"

/* software rasterizer. */
#include "swr/swr.h"

/* application framework */
#include "framework.h"

/** Program entry point. */
#if defined(_WIN32) || defined(_WIN64)
int _tmain(int argc, _TCHAR* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    try
    {
        swr_app::application::initialize_instance(argc, argv);

        swr_app::application::get_instance().initialize();
        swr_app::application::get_instance().event_loop();
        swr_app::application::get_instance().shutdown();

        swr_app::application::shutdown_instance();
    }
    catch(const std::exception& e)
    {
        platform::logf("{}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
