/**
 * swr - a software rasterizer
 *
 * general render context and SDL render context.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace swr
{

namespace impl
{

/*
 * shader support.
 */

/** program flags. */
namespace program_flags
{
enum
{
    none = 0,
    prelinked = 1,
    linked = 2
};
} /* namespace program_flags */

/** invalid vertex attribute index. */
enum class vertex_attribute_index
{
    invalid = -1
};

/** graphics program info. */
struct program_info
{
    /** varying count. has to match iqs.size(). */
    uint32_t varying_count{0};

    /** interpolation qualifiers for varyings. */
    boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings> iqs;

    /** flags. */
    uint32_t flags{program_flags::none};

    /** (pointer to) the graphics program/shader. */
    const program_base* shader{nullptr};

    /** shader size. */
    std::size_t program_size;

#ifndef SWR_ENABLE_MULTI_THREADING
    /** shader instance. */
    std::vector<std::byte> storage;
#endif

    /** default constructor. */
    program_info() = default;

    /** constructor. */
    program_info(const program_base* in_shader)
    : shader{in_shader}
    , program_size{in_shader->size()}
#ifndef SWR_ENABLE_MULTI_THREADING
    , storage{program_size}
#endif
    {
    }

    /** shader validation. */
    bool validate() const
    {
        return shader && (varying_count == iqs.size());
    }

    /*
     * accessors.
     */

    bool is_prelinked() const
    {
        return (flags & program_flags::prelinked) != 0;
    }

    bool is_linked() const
    {
        return (flags & program_flags::linked) != 0;
    }
};

/*
 * render contexts.
 */

/** convenience vertex shader instance container. */
class vertex_shader_instance_container
{
    const swr::program_base* shader;
    const std::size_t varying_count;

public:
    vertex_shader_instance_container(std::byte* storage, impl::program_info* shader_info, const boost::container::static_vector<swr::uniform, geom::limits::max::uniform_locations>& uniforms)
    : shader{shader_info->shader->create_vertex_shader_instance(storage, uniforms)}
    , varying_count{shader_info->varying_count}
    {
    }

    vertex_shader_instance_container(const vertex_shader_instance_container&) = delete;
    vertex_shader_instance_container(vertex_shader_instance_container&& other)
    : shader{other.shader}
    , varying_count{other.varying_count}
    {
        other.shader = nullptr;
    }

    ~vertex_shader_instance_container()
    {
        if(shader != nullptr)
        {
            shader->~program_base();
        }
    }

    vertex_shader_instance_container& operator=(const vertex_shader_instance_container&) = delete;
    vertex_shader_instance_container& operator=(vertex_shader_instance_container&& other) = delete;

    const swr::program_base* get() const
    {
        return shader;
    }

    std::size_t get_varying_count() const
    {
        return varying_count;
    }
};

/** a general render device context (not associated to any output device/window). */
class render_device_context
{
public:
    /*
     * frame buffers.
     */

    /** default frame buffer. */
    default_framebuffer framebuffer;

    /** frame buffer objects. */
    utils::slot_map<framebuffer_object> framebuffer_objects;

    /** depth renderbuffers. */
    utils::slot_map<attachment_depth> depth_attachments;

    /*
     * context states.
     */

    /** The current render states. These are copied on each draw call and stored in a draw list. */
    render_states states;

    /*
     * error handling.
     */

    /** last detected error. */
    error last_error;

    /*
     * buffers and lists.
     */

    /** list of render commands to be processed. points into objects. */
    std::list<render_object> render_object_list;

    /** index buffers. */
    utils::slot_map<std::vector<uint32_t>> index_buffers;

    /** vertex attribute buffers. */
    utils::slot_map<vertex_attribute_buffer> vertex_attribute_buffers;

    /** currently active vertex attribute buffers. stores indices into vertex_attribute_buffers. */
    boost::container::static_vector<int, geom::limits::max::attributes> active_vabs;

    /*
     * shaders.
     */

    /** the registered shaders, together with their program information. */
    utils::slot_map<program_info> programs;

#ifdef SWR_ENABLE_MULTI_THREADING
    /** storage for the shader instances. */
    std::vector<std::byte, utils::allocator<std::byte>> program_storage;

    /** render object with their associated program instances, to avoid reallocations. */
    std::vector<std::pair<swr::impl::render_object*, impl::vertex_shader_instance_container>> program_instances;
#endif /* SWR_ENDABLE_MULTI_THREADING */

    /** default shader. */
    std::unique_ptr<program_base> default_shader;

    /*
     * texture management.
     */

    /** texture storage. */
    utils::slot_map<std::unique_ptr<texture_2d>> texture_2d_storage;

    /** a default texture. this needs to be allocated in texture_2d_storage at index 0. */
    texture_2d* default_texture_2d{nullptr};

    /*
     * thread pool.
     */

#ifdef SWR_ENABLE_MULTI_THREADING
    /** thread pool type to use. */
    typedef concurrency_utils::deferred_thread_pool<concurrency_utils::mpmc_blocking_queue<std::function<void()>>> thread_pool_type;

    /** processing threads. */
    uint32_t thread_pool_size{0};

    /** worker threads. */
    thread_pool_type thread_pool;
#else
    /** no thread pool type. */
    typedef std::nullptr_t thread_pool_type;
#endif /* SWR_ENABLE_MULTI_THREADING */

    /*
     * rasterization.
     */

    /** rasterizes points, lines and triangles. */
    std::unique_ptr<rast::rasterizer> rasterizer;

    /*
     * statistics and benchmarking.
     */
#ifdef SWR_ENABLE_STATS
    /** statistics collected during fragment processing. */
    stats::fragment_data stats_frag;

    /** rasterizer info and collected data. */
    stats::rasterizer_data stats_rast;
#endif

    /*
     * render_device_context implementation.
     */

    /** default constructor. */
    render_device_context() = default;

    /** virtual destructor. */
    virtual ~render_device_context()
    {
        shutdown();
    }

    /*
     * render object management.
     */

    /**
     * Create render object for vertex_count vertices.
     * Needs render context to copy the active render states (and buffers) over.
     *
     * @param mode Specifies how the contents of the subset of the vertex buffer should be interpretted.
     * @param count Number of elements to use from `index_buffer`.
     * @param index_buffer The index buffer to use.
     * @returns Returns a pointer to the render object on success and `nullptr` on failure.
     */
    render_object* create_render_object(vertex_buffer_mode mode, size_t count);

    /**
     * Create render object from (indexed) vertex buffer.
     * Needs render context to copy the active render states (and buffers) over.
     *
     * @param mode Specifies how the contents of the subset of the vertex buffer should be interpretted.
     * @param count Number of elements to use from `index_buffer`.
     * @param index_buffer The index buffer to use.
     * @returns Returns a pointer to the render object on success and `nullptr` on failure.
     */
    render_object* create_indexed_render_object(vertex_buffer_mode mode, std::size_t count, const std::vector<uint32_t>& index_buffer);

    /*
     * buffer management.
     */

    /** clear the color buffer while respecting active render states. */
    void clear_color_buffer();

    /** clear the depth buffer while respecting active render states. */
    void clear_depth_buffer();

    /*
     * primitive assembly.
     */

    /**
     * Assemble the base primitives from a given vertex buffer. The base primitives are stored in the rasterizer.
     * Face culling takes place at this stage.
     *
     * Reference: https://www.khronos.org/opengl/wiki/Primitive_Assembly
     */
    void assemble_primitives(const render_states* states, vertex_buffer_mode mode, vertex_buffer& vb);

    /*
     * render_device_context interface.
     */

    /** free all resources. */
    virtual void shutdown();

    /** Lock color buffer for writing. On success, ensures ColorBuffer.data_ptr to be valid. */
    virtual bool lock()
    {
        return false;
    }

    /** unlock the color buffer. */
    virtual void unlock()
    {
    }

    /** copy the default color buffer to some target. */
    virtual void copy_default_color_buffer()
    {
    }
};

/** a render device context for an SDL window. */
class sdl_render_context : public render_device_context
{
protected:
    /** context dimensions: the buffer may be a bit larger, but we only want to copy the correct rectangle. */
    SDL_FRect sdl_viewport_dimensions;

    /** color buffer. */
    SDL_Texture* sdl_color_buffer{nullptr};

    /** SDL renderer. */
    SDL_Renderer* sdl_renderer{nullptr};

    /** associated SDL window. */
    SDL_Window* sdl_window{nullptr};

    /** return the window's pixel format, converted to swr::pixel_format. if out_sdl_pixel_format is non-null, the SDL pixel format will be written into it. */
    swr::pixel_format get_window_pixel_format(SDL_PixelFormat* out_sdl_pixel_format = nullptr) const;

public:
    /** default constructor. */
    sdl_render_context([[maybe_unused]] uint32_t thread_hint)
    {
#ifdef SWR_ENABLE_MULTI_THREADING
        if(thread_hint > 0)
        {
            thread_pool_size = thread_hint;
        }
#endif
    }

    /** destructor. */
    ~sdl_render_context()
    {
        shutdown();
    }

    /*
     * render_device_context interface.
     */

    void shutdown() override;
    bool lock() override;
    void unlock() override;
    void copy_default_color_buffer() override;

    /*
     * sdl_render_context interface.
     */

    /** initialize the context with the supplied SDL data and create the buffers. */
    void initialize(SDL_Window* window, SDL_Renderer* renderer, int width, int height);

    /** (re-)create depth- and color buffers using the given width and height. */
    void update_buffers(int width, int height);
};

/*
 * global render contexts.
 */

/** the (thread-)global rendering context. */
extern thread_local render_device_context* global_context;

/** assert validity of render context in debug builds. */
#define ASSERT_INTERNAL_CONTEXT assert(impl::global_context)

/*
 * texture helpers.
 */

/** create a default texture. */
void create_default_texture(render_device_context* context);

/*
 * shader helpers.
 */

/** create a default shader in the supplied context which outputs empty fragments. */
void create_default_shader(render_device_context* context);

} /* namespace impl */

} /* namespace swr */
