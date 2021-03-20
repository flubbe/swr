/**
 * swr - a software rasterizer
 * 
 * general render context and SDL render context.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
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
    program* shader{nullptr};

    /** default constructor. */
    program_info() = default;

    /** constructor. */
    program_info(program* in_shader)
    : shader(in_shader)
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

/** a general render device context (not associated to any output device/window). */
class render_device_context
{
public:
    /*
     * context states.
     */

    /** clear color for color buffer, stored in the internal pixel format. */
    uint32_t ClearColor{0};

    /** clear value for depth buffer. */
    ml::fixed_32_t ClearDepth{1};

    /** Color buffer. */
    color_buffer ColorBuffer;

    /** Depth buffer. */
    depth_buffer DepthBuffer;

    /** The current render states. These are copied on each draw call and stored in a draw list. */
    render_states RenderStates;

    /** texture magnification filter. */
    texture_filter TextureFilterMag{texture_filter::nearest};

    /** texture minification filter. */
    texture_filter TextureFilterMin{texture_filter::nearest};

    /** texture wrapping mode (s-direction). */
    wrap_mode TextureWrapS = wrap_mode::repeat;

    /** texture wrapping mode (t-direction). */
    wrap_mode TextureWrapT = wrap_mode::repeat;

    /*
     * error handling.
     */

    /** last detected error. */
    error last_error;

    /*
     * debug states. 
     */

    /** if this is set, a grayscale depth buffer representation is copied to the color buffer at the end of Present. */
    bool CopyDepthToColor{false};

    /*
     * buffers and lists.
     */

    /** list of objects which may be sent to the rasterizer. */
    std::list<render_object> objects; /* note on container: iterators have to stay valid after insertions */

    /** list of object which are sent to the rasterizer. */
    std::list<render_object*> DrawList;

    /** vertex buffers. */
    utils::slot_map<vertex_buffer> vertex_buffers;

    /** index buffers. */
    utils::slot_map<index_buffer> index_buffers;

    /** vertex attribute buffers. */
    utils::slot_map<vertex_attribute_buffer> vertex_attribute_buffers;

    /** currently active vertex attribute buffers. stores indices into vertex_attribute_buffers. */
    boost::container::static_vector<int, geom::limits::max::attributes> active_vabs;

    /*
     * shaders.
     */

    /** the registered shaders, together with their program information. */
    utils::slot_map<program_info> ShaderObjectHash;

    /*
     * immediate mode support.
     */

    /** whether we are currently drawing primitives, i.e., if BeginPrimitives had been called. */
    bool im_declaring_primitives{false};

    /** the mode BeginPrimitives was called with. */
    vertex_buffer_mode im_mode{vertex_buffer_mode::points};

    /** the texture coordinate set by SetTexCoord. */
    ml::vec4 im_tex_coord;

    /** vertex normal (currently unused). */
    ml::vec4 im_normal;

    /** vertex color, set by SetColor. */
    ml::vec4 im_color{1.0f, 1.0f, 1.0f, 1.0f};

    /** temporary immediate mode vertex buffer. vertices in this buffer are drawn in the stored order. */
    std::vector<ml::vec4> im_vertex_buf;

    /** temporary immediate mode color buffer. */
    std::vector<ml::vec4> im_color_buf;

    /** temporary immediate mode texture coordinate buffer. */
    std::vector<ml::vec4> im_tex_coord_buf;

    /** temporary immediate mode normal buffer. */
    std::vector<ml::vec4> im_normal_buf;

    /*
     * texture management.
     */

    /** texture storage. */
    utils::slot_map<texture_2d*> Texture2dHash;

    /** a default texture. */
    texture_2d* DefaultTexture2d{nullptr};

    /*
     * rasterization.
     */

    /** rasterizes points, lines and triangles. */
    std::unique_ptr<rast::rasterizer> Rasterizer;

    /*
     * statistics and benchmarking.
     */

    /** statistics collected during fragment processing. */
    stats::fragment_data stats_frag;

    /*
     * render_device_context implementation.
     */

    /** default constructor. */
    render_device_context() = default;

    /** virtual destructor. */
    virtual ~render_device_context()
    {
        Shutdown();
    }

    /*
     * render object management.
     */

    /**
     * Create render object for vertex_count vertices.
     * Needs render context to copy the active render states (and buffers) over.
     *
     * Returns nullptr on failure.
     */
    render_object* CreateRenderObject(size_t vertex_count, vertex_buffer_mode mode);

    /**
     * Create render object from (indexed) vertex buffer.
     * Needs render context to copy the active render states (and buffers) over.
     *
     * Returns nullptr on failure.
     */
    render_object* CreateIndexedRenderObject(const index_buffer& ib, vertex_buffer_mode mode);

    /**
     * Release all render objects.
     *
     * This is usually done after rasterization.
     */
    void ReleaseRenderObjects();

    /*
     * buffer management.
     */

    /** allocate depth buffer matching the color buffer's dimensions. */
    void AllocateDepthBuffer();

    /** clear the color buffer while respecting active render states. */
    void ClearColorBuffer();

    /** clear the depth buffer while respecting active render states. */
    void ClearDepthBuffer();

    /** set the current clear color. */
    void SetClearColor(float r, float g, float b, float a)
    {
        ClearColor = ColorBuffer.pf_conv.to_pixel({r, g, b, a});
    }

    /** set the current clear depth. */
    void SetClearDepth(float z)
    {
        ClearDepth = boost::algorithm::clamp(z, 0.f, 1.f);
    }

    /*
     * primitive assembly.
     */

    /**
     * Assemble the base primitives from a given vertex buffer. The base primitives are stored in the rasterizer.
     * Face culling takes place at this stage. 
     *
     * Reference: https://www.khronos.org/opengl/wiki/Primitive_Assembly
     */
    void AssemblePrimitives(const render_states* States, vertex_buffer_mode Mode, const vertex_buffer& Buffer);

    /*
     * render_device_context interface.
     */

    /** free all resources. */
    virtual void Shutdown();

    /** Lock color buffer for writing. On success, ensures ColorBuffer.data_ptr to be valid. */
    virtual bool Lock()
    {
        return false;
    }

    /** unlock the color buffer. */
    virtual void Unlock()
    {
    }

    /** copy the default color buffer to some target. */
    virtual void CopyDefaultColorBuffer()
    {
    }

    /** debugging: copy the depth buffer into the frame buffer. */
    virtual void DisplayDepthBuffer()
    {
    }
};

/** a render device context for an SDL window. */
class sdl_render_context : public render_device_context
{
protected:
    /** context dimensions: the buffer may be a bit larger, but we only want to copy the correct rectangle. */
    SDL_Rect ContextDimensions;

    /** color buffer. */
    SDL_Texture* Buffer{nullptr};

    /** SDL renderer. */
    SDL_Renderer* Renderer{nullptr};

    /** associated SDL window. */
    SDL_Window* Window{nullptr};

public:
    /** default constructor. */
    sdl_render_context() = default;

    /** destructor. */
    ~sdl_render_context()
    {
        Shutdown();
    }

    /*
     * render_device_context interface.
     */

    void Shutdown() override;
    bool Lock() override;
    void Unlock() override;
    void CopyDefaultColorBuffer() override;

    /*
     * sdl_render_context interface.
     */

    /** initialize the context with the supplied SDL data and create the buffers. */
    void Initialize(SDL_Window* InWindow, SDL_Renderer* InRenderer, int InWidth, int InHeight);

    /** (re-)create depth- and color buffers. */
    void UpdateBuffers();

    /* copy the depth buffer into the frame buffer. */
    void DisplayDepthBuffer() override;
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
