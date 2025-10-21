/**
 * swr - a software rasterizer
 *
 * framework to quickly set up an application with a software rasterizer.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers */
#include <mutex>
#include <chrono>

/* boost */
#include <boost/lexical_cast.hpp>

namespace swr_app
{

/** SDL window with an associated SDL renderer and software renderer context. */
class renderwindow
{
protected:
    /** window title. */
    std::string title;

    /** dimensions. */
    int width{0}, height{0};

    /** software rasterizer context. */
    swr::context_handle context{nullptr};

    /** The main SDL window. */
    SDL_Window* sdl_window{nullptr};

    /** Main SDL renderer. */
    SDL_Renderer* sdl_renderer{nullptr};

    /** free the window and the renderer. */
    void free_resources();

public:
    /** constructor. */
    renderwindow(const std::string& in_title, int in_width = 320, int in_height = 240)
    : title(in_title)
    , width(in_width)
    , height(in_height)
    {
        if(in_width <= 0 || in_height <= 0)
        {
            throw std::runtime_error(std::format("invalid window dimensions: ({},{})", in_width, in_height));
        }
    }

    /** destructor. */
    virtual ~renderwindow()
    {
        free_resources();
    }

    /** override (and call) this to add custom initialization functions. */
    virtual bool create();

    /** free allocated resources. */
    virtual void destroy()
    {
        free_resources();
    }

    /** update the window. */
    virtual void update(float delta_time) = 0;

    /** context access. */
    swr::context_handle get_rasterizer_context() const
    {
        return context;
    }

    /** get the contents of the window as RGBA 32-bit values. */
    bool get_surface_buffer_rgba32(std::vector<uint32_t>& contents) const;

    /** get the contents of the window as an SDL surface. SDL_FreeSurface needs to be invoked by the caller. */
    SDL_Surface* get_surface()
    {
        if(!sdl_renderer)
        {
            throw std::runtime_error("sdl_renderer==0");
        }

        SDL_Surface* surface = SDL_CreateRGBSurface(0, width, height, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0);
        if(!surface)
        {
            throw std::runtime_error(std::format("SDL_CreateRGBSurface failed: {}", SDL_GetError()));
        }
        SDL_RenderReadPixels(sdl_renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch);
        return surface;
    }

    /* get window width */
    int get_surface_width() const
    {
        if(!sdl_window)
        {
            return 0;
        }

        auto surface = SDL_GetWindowSurface(sdl_window);
        return surface ? surface->w : 0;
    }

    /* get window height */
    int get_surface_height() const
    {
        if(!sdl_window)
        {
            return 0;
        }

        auto surface = SDL_GetWindowSurface(sdl_window);
        return surface ? surface->h : 0;
    }
};

/**
 * sdl application with associated renderwindow.
 *
 * throughout the lifetime of the application, only a single instance
 * of this class is allowed to exist at a time (reflecting the idea
 * that the program is represented by this class).
 */
class application
{
    /** the application singleton. */
    static application* global_app;

    /** global_app mutex. */
    static std::mutex global_app_mtx;

    /** command line arguments. */
    std::vector<std::string> cmd_args;

    /** timer. */
    std::chrono::steady_clock timer;

    /** run time of the application, in seconds. */
    float run_time{0};

    /** if non-negative, this is the maximal requested run time of the application, in seconds. */
    float max_run_time{-1.f};

protected:
    /** the renderer. */
    std::unique_ptr<renderwindow> window;

    /** An indicator showing whether we want to run or exit the program. */
    bool quit_program{false};

    /** process command line argument. */
    bool process_cmdline(int argc, char* argv[]);

public:
    /** instance initialization */
    static void initialize_instance(int argc, char* argv[]);

    /** instance shutdown. */
    static void shutdown_instance();

    /** check if the global instance already exists. */
    static bool has_instance()
    {
        const std::scoped_lock lock{global_app_mtx};
        return global_app != nullptr;
    }

    /** thread-safe singleton setter. */
    static void set_instance(application* new_app)
    {
        const std::scoped_lock lock{global_app_mtx};
        global_app = new_app;
    }

    /** singleton getter. */
    static application& get_instance()
    {
        const std::scoped_lock lock{global_app_mtx};

        if(!global_app)
        {
            throw std::runtime_error("application not initialized");
        }

        return *global_app;
    }

    /** quit application. */
    static void quit()
    {
        const std::scoped_lock lock{global_app_mtx};
        if(global_app)
        {
            global_app->quit_program = true;
        }
    }

public:
    /** constructor. */
    application()
    {
        if(has_instance())
        {
            throw std::runtime_error("multiple applications");
        }

        // initialize the singleton
        set_instance(this);
    }

    /** destructor. */
    virtual ~application()
    {
        set_instance(nullptr);
    }

    /** override this e.g. to create a render window. */
    virtual void initialize()
    {
        // process command line arguments.
        max_run_time = get_argument<float>("--run_time", -1.0f);
    }

    /** override this e,g, for global resource de-allocation. */
    virtual void shutdown()
    {
    }

    /** event loop. this only renders frames until a quit condition is met. does not process input. */
    virtual void event_loop()
    {
        std::chrono::steady_clock::time_point msec_reference_time = std::chrono::steady_clock::now();

        while(!quit_program && window)
        {
            auto update_time = std::chrono::steady_clock::now();
            float delta_time = std::chrono::duration<float>(update_time - msec_reference_time).count();
            msec_reference_time = update_time;

            run_time += delta_time;

            window->update(delta_time);

            // check if we reached the maximal runtime.
            if(!quit_program)
            {
                quit_program = (max_run_time >= 0 && run_time >= max_run_time);
            }
        }
    }

    /*
     * command line arguments.
     */

    /** return the value of a parameter-value pair of the form 'name=value'. if there are multiply instances of 'name=', returns the last value. */
    template<typename T>
    const T get_argument(const std::string& name, const T& default_value) const
    {
        std::string opt = std::format("{}=", name);
        std::vector<std::string>::const_reverse_iterator it = std::find_if(cmd_args.rbegin(), cmd_args.rend(),
                                                                           [opt](const std::string& p)
                                                                           { return p.substr(0, opt.length()) == opt; });

        if(it == cmd_args.rend())
        {
            // argument not found
            return default_value;
        }

        std::string val = it->substr(opt.length(), std::string::npos);
        if(val.length() == 0)
        {
            // no value was supplied.
            return default_value;
        }

        try
        {
            return boost::lexical_cast<T>(val);
        }
        catch(const boost::bad_lexical_cast&)
        {
            // lexical cast failed. fall through to return default value.
        }

        return default_value;
    }

    /** return the values of all parameter-value pairs of the form 'name=value'. */
    template<typename T>
    std::size_t get_arguments(const std::string& name, std::vector<T>& values) const
    {
        values.clear();

        std::string opt = std::format("{}=", name);
        std::vector<std::string>::const_iterator it = cmd_args.begin();
        while(true)
        {
            it = std::find_if(it, cmd_args.end(),
                              [opt](const std::string& p)
                              { return p.substr(0, opt.length()) == opt; });

            if(it == cmd_args.end())
            {
                break;
            }

            std::string val = it->substr(opt.length(), std::string::npos);
            if(val.length() == 0)
            {
                // no value was supplied.
                return false;
            }

            try
            {
                // convert type and add to vector.
                values.push_back(boost::lexical_cast<T>(val));
            }
            catch(const boost::bad_lexical_cast&)
            {
                // lexical cast failed. we just ignore it here and continue.
            }

            ++it;
        }

        return values.size();
    }

    /** get application window. */
    renderwindow* get_window() const
    {
        return window.get();
    }

    /** get application run time, in seconds. */
    float get_run_time() const
    {
        return run_time;
    }
};

/*
 * interface code for boost test framework.
 */
#ifdef BOOST_TEST_MAIN

/**
 * minimal application initialization and shutdown code for boost's global fixture.
 * note that this needs to be integrated into the boost test framework by
 *
 *  using fixture = swr_app::boost_global_fixture;
 *  BOOST_GLOBAL_FIXTURE(fixture)
 *
 * this circumvents a substitution error of macros when used with nested names/namespaces.
 */
struct boost_global_fixture
{
    /** constructor. */
    boost_global_fixture()
    {
        swr_app::application::initialize_instance();
        swr_app::application::get_instance().initialize();
    }

    /** destructor. */
    ~boost_global_fixture()
    {
        swr_app::application::get_instance().shutdown();
        swr_app::application::shutdown_instance();
    }
};

#endif

} /* namespace swr_app */
