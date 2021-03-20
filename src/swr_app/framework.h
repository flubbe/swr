/**
 * swr - a software rasterizer
 * 
 * framework to quickly set up an application with a software rasterizer.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/*
 * dependencies.
 */
#include <mutex>

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
            throw std::runtime_error(fmt::format("invalid window dimensions: ({},{})", in_width, in_height));
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
    virtual void update() = 0;

    /** context access. */
    auto get_rasterizer_context() const
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
            throw std::runtime_error(fmt::format("SDL_CreateRGBSurface failed: {}", SDL_GetError()));
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

protected:
    /** the renderer. */
    renderwindow* window{nullptr};

    /** An indicator showing whether we want to run or exit the program. */
    bool quit_program{false};

public:
    /** instance initialization */
    static void initialize_instance();

    /** instance shutdown. */
    static void shutdown_instance();

    /** check if the global instance already exists. */
    static bool has_instance()
    {
        std::lock_guard<std::mutex> lock(global_app_mtx);
        return global_app != nullptr;
    }

    /** thread-safe singleton setter. */
    static void set_instance(application* new_app)
    {
        std::lock_guard<std::mutex> lock(global_app_mtx);
        global_app = new_app;
    }

    /** singleton getter. */
    static application& get_instance()
    {
        std::lock_guard<std::mutex> lock(global_app_mtx);

        if(!global_app)
        {
            throw std::runtime_error("application not initialized");
        }

        return *global_app;
    }

    /** quit application. */
    static void quit()
    {
        std::lock_guard<std::mutex> lock(global_app_mtx);
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
    }

    /** override this e,g, for global resource de-allocation. */
    virtual void shutdown()
    {
    }

    /** event loop. this only renders frames until a quit condition is met. does not process input. */
    virtual void event_loop()
    {
        while(!quit_program && window)
        {
            window->update();
        }
    }

    /** get application window. */
    renderwindow* get_window() const
    {
        return window;
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
 *  using fixture = test::boost_global_fixture;
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