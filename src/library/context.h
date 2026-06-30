/**
 * swr - a software rasterizer
 *
 * general render context and SDL render context.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include "concurrency_utils/thread_pool.h"

namespace swr
{

namespace impl
{

/*
 * shader support.
 */

using shader_storage_buffer = utils::aligned_byte_storage;

/** program flags. */
enum class program_flags : std::uint32_t
{
    none = 0,
    prelinked = 1 << 0,
    linked = 1 << 1,
    has_flat_varyings = 1 << 2
};

constexpr program_flags operator&(
  program_flags a,
  program_flags b)
{
    return static_cast<program_flags>(
      static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

constexpr program_flags operator|(
  program_flags a,
  program_flags b)
{
    return static_cast<program_flags>(
      static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr program_flags& operator|=(
  program_flags& a,
  program_flags b)
{
    a = a | b;
    return a;
}

/** invalid vertex attribute index. */
enum class vertex_attribute_index
{
    invalid = -1
};

/** graphics program info. */
struct program_info
{
    /** varying count. has to match iqs.size(). */
    std::uint32_t varying_count{0};

    /** interpolation qualifiers for varyings. */
    boost::container::static_vector<
      swr::interpolation_qualifier,
      swr::limits::max::varyings>
      iqs;

    /** flags. */
    program_flags flags{program_flags::none};

    /** Shader behavior metadata. */
    swr::program_metadata metadata{};

    /** (pointer to) the graphics program/shader. */
    const program_base* shader{nullptr};

    /** shader size. */
    std::size_t program_size{0};

    /** shader alignment. */
    std::size_t program_alignment{utils::alignment::sse};

#ifndef SWR_ENABLE_MULTI_THREADING
    /** shader instance. */
    shader_storage_buffer storage;
#endif

    /** default constructor. */
    program_info() = default;

    /** constructor. */
    program_info(const program_base* in_shader)
    : metadata{in_shader->get_metadata()}
    , shader{in_shader}
    , program_size{in_shader->size()}
    , program_alignment{in_shader->alignment()}
#ifndef SWR_ENABLE_MULTI_THREADING
    , storage{program_size, program_alignment}
#endif
    {
    }

    /** shader validation. */
    bool validate() const
    {
        return shader
               && std::has_single_bit(program_alignment)
               && (varying_count == iqs.size());
    }

    /*
     * accessors.
     */

    bool is_prelinked() const
    {
        return (flags & program_flags::prelinked) != program_flags::none;
    }

    bool is_linked() const
    {
        return (flags & program_flags::linked) != program_flags::none;
    }

    bool uses_flat_varyings() const
    {
        return (flags & program_flags::has_flat_varyings) != program_flags::none;
    }
};

/*
 * render contexts.
 */

/** convenience vertex shader instance container. */
class vertex_shader_instance_container
{
    const swr::program_base* shader{nullptr};
    std::size_t varying_count{0};

public:
    vertex_shader_instance_container(
      std::byte* storage,
      impl::program_info* shader_info,
      const swr::uniform_bindings& uniforms,
      const swr::sampler_bindings& samplers_2d = {})
    {
        assert(shader_info);
        assert(shader_info->shader);
        assert(std::has_single_bit(shader_info->program_alignment));
        assert(
          reinterpret_cast<std::uintptr_t>(storage)
            % shader_info->program_alignment
          == 0);
        varying_count = shader_info->varying_count;
        shader = shader_info->shader->create_instance(
          storage,
          swr::program_instance_bindings{uniforms, samplers_2d});
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

/** convenience fragment shader instance container. */
class fragment_shader_instance_container
{
    shader_storage_buffer storage;
    const swr::program_base* shader{nullptr};

public:
    fragment_shader_instance_container(
      const impl::program_info* shader_info,
      const swr::uniform_bindings& uniforms,
      const swr::sampler_bindings& samplers_2d)
    {
        assert(shader_info);
        assert(shader_info->shader);
        assert(std::has_single_bit(shader_info->program_alignment));

        storage.allocate(
          shader_info->program_size,
          shader_info->program_alignment);

        assert(
          reinterpret_cast<std::uintptr_t>(storage.data())
            % shader_info->program_alignment
          == 0);

        shader = shader_info->shader->create_instance(
          storage.data(),
          swr::program_instance_bindings{uniforms, samplers_2d});
    }

    fragment_shader_instance_container(const fragment_shader_instance_container&) = delete;
    fragment_shader_instance_container(fragment_shader_instance_container&& other) noexcept
    : storage{std::move(other.storage)}
    , shader{other.shader}
    {
        other.shader = nullptr;
    }

    ~fragment_shader_instance_container()
    {
        if(shader != nullptr)
        {
            shader->~program_base();
        }
    }

    fragment_shader_instance_container& operator=(const fragment_shader_instance_container&) = delete;
    fragment_shader_instance_container& operator=(fragment_shader_instance_container&& other) = delete;

    const swr::program_base* get() const
    {
        return shader;
    }
};

/** the context type. */
enum class context_type
{
    generic,  /** generic context. */
    sdl,      /** SDL context. */
    offscreen /** offscreen context. */
};

/** a general render context (not associated to any output device/window). */
struct render_context
{
    /** the context type. */
    context_type type{context_type::generic};

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
    error last_error{error::none};

    /*
     * buffers and lists.
     */

    /** list of render commands to be processed. points into objects. */
    std::list<render_object> render_object_list;

    /** index buffers. */
    utils::slot_map<
      std::vector<
        std::uint32_t>>
      index_buffers;

    /** vertex attribute buffers. */
    utils::slot_map<vertex_attribute_buffer> vertex_attribute_buffers;

    /** currently active vertex attribute buffers. stores indices into vertex_attribute_buffers. */
    boost::container::static_vector<
      int,
      swr::limits::max::attributes>
      active_vabs;

    /*
     * shaders.
     */

    /** the registered shaders, together with their program information. */
    utils::slot_map<program_info> programs;

#ifdef SWR_ENABLE_MULTI_THREADING
    /** storage for the shader instances. */
    shader_storage_buffer program_storage;

    /** render object with their associated program instances, to avoid reallocations. */
    std::vector<
      std::pair<
        swr::impl::render_object*,
        impl::vertex_shader_instance_container>>
      program_instances;
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
    typedef concurrency_utils::deferred_thread_pool<
      concurrency_utils::mpmc_blocking_queue<
        std::function<void()>>>
      thread_pool_type;

    /** processing threads. */
    std::uint32_t thread_pool_size{0};

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

    /** create the rasterizer from the internal state. */
    void create_rasterizer();

    /*
     * render_device_context implementation.
     */

    /** default constructor. */
    render_context() = default;

    /** virtual destructor. */
    virtual ~render_context()
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
    render_object* create_render_object(
      vertex_buffer_mode mode,
      std::size_t count);

    /**
     * Create render object from (indexed) vertex buffer.
     * Needs render context to copy the active render states (and buffers) over.
     *
     * @param mode Specifies how the contents of the subset of the vertex buffer should be interpretted.
     * @param count Number of elements to use from `index_buffer`.
     * @param index_buffer The index buffer to use.
     * @returns Returns a pointer to the render object on success and `nullptr` on failure.
     */
    render_object* create_indexed_render_object(
      vertex_buffer_mode mode,
      std::size_t count,
      const std::vector<std::uint32_t>& index_buffer);

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
    void assemble_primitives(
      const render_states* states,
      vertex_buffer_mode mode,
      vertex_buffer& vb);

    /**
     * Assemble primitives from an indexed viewport-space vertex buffer.
     *
     * This is used by the no-clipping fast path, where vertices can stay compact
     * and primitives are described by render_object::indices.
     */
    void assemble_indexed_primitives(
      const render_states* states,
      vertex_buffer_mode mode,
      vertex_buffer& vb,
      std::span<const std::uint32_t> indices);

    /**
     * Assemble primitives from original post-shader vertex storage.
     *
     * This is used by the no-clipping fast path. Coordinates have already been
     * transformed in-place in render_object::coords; vertices are materialized
     * only when stable rasterizer pointers are needed.
     */
    void assemble_original_indexed_primitives(
      const render_states* states,
      vertex_buffer_mode mode,
      render_object& obj);

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

/** a render context for an SDL window. */
class sdl_render_context final : public render_context
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
    swr::pixel_format get_window_pixel_format(
      SDL_PixelFormat* out_sdl_pixel_format = nullptr) const;

public:
    sdl_render_context(
      [[maybe_unused]] std::uint32_t thread_hint)
    {
        type = context_type::sdl;

#ifdef SWR_ENABLE_MULTI_THREADING
        if(thread_hint > 0)
        {
            thread_pool_size = thread_hint;
        }
#endif
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
    void initialize(
      SDL_Window* window,
      SDL_Renderer* renderer,
      int width,
      int height);

    /** (re-)create depth- and color buffers using the given width and height. */
    void update_buffers(int width, int height);
};

/** a offscreen render context. */
class offscreen_render_context final : public render_context
{
    int width = 0;
    int height = 0;

    /** RGBA buffer. */
    std::vector<std::uint32_t> rgba_buffer;

    /** Whether the buffer is locked. */
    bool locked{false};

public:
    offscreen_render_context(
      [[maybe_unused]] std::uint32_t thread_hint)
    {
        type = context_type::offscreen;

#ifdef SWR_ENABLE_MULTI_THREADING
        if(thread_hint > 0)
        {
            thread_pool_size = thread_hint;
        }
#endif
    }

    /*
     * render_device_context interface.
     */

    void shutdown() override;
    bool lock() override;
    void unlock() override;

    /*
     * offscreen_render_context interface.
     */

    /** initialize the context with the supplied SDL data and create the buffers. */
    void initialize(
      int width,
      int height);

    /** (re-)create depth- and color buffers using the given width and height. */
    bool update_buffers(
      int width,
      int height);
};

/*
 * global render contexts.
 */

/** the (thread-)global rendering context. */
extern thread_local render_context* global_context;

/** assert validity of render context in debug builds. */
#define ASSERT_INTERNAL_CONTEXT assert(impl::global_context)

/*
 * texture helpers.
 */

/** create a default texture. */
void create_default_texture(render_context* context);

/*
 * shader helpers.
 */

/** create a default shader in the supplied context which outputs empty fragments. */
void create_default_shader(render_context* context);

} /* namespace impl */

} /* namespace swr */
