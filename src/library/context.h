/**
 * swr - a software rasterizer
 *
 * general render context and SDL render context.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <bit>
#include <functional>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "concurrency_utils/thread_pool.h"
#include "frame_arena.h"
#include "renderobject.h"

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

/*
 * render commands.
 */

/**
 * Payload descriptor for update buffer contents stored in a raw pool.
 *
 * @note We store `begin` and `count` (instead of e.g. `std::span`) here,
 *     since the referenced buffer might re-allocate.
 */
struct index_range
{
    /** Beginning index into a buffer pool. */
    std::size_t begin{0};

    /** Buffer element count. */
    std::size_t count{0};
};

/** Draw command for a render object. */
struct draw_command
{
    /** Vertex buffer mode. */
    vertex_buffer_mode mode;

    /** State snapshot. */
    std::size_t state_snapshot_index;

    /** Index buffer, as indices into the index buffer pool. */
    index_range indices;

    /** Index into the active vertex attribute buffers pool */
    index_range active_vab_indices;

    /** Attribute slot count. */
    std::size_t attribute_slot_count;

    /** Whether the attribute indices were remapped. */
    bool remapped;

    /** Attribute source indices, as indices into the index buffer pool. */
    index_range attribute_indices;
};

/** Resolved execution context for a draw command. */
struct draw_execution
{
    render_object* object{nullptr};
    std::size_t vertex_count;
    index_range attribute_index_range;
    std::size_t attribute_slot_count;
    index_range clipped_vertex_range;
    const render_states* states{nullptr};
    framebuffer_draw_target* draw_target{nullptr};
    bool discard{false};
};

/** Clear command type. */
enum class clear_kind
{
    color,
    depth
};

/** Clear command. */
struct clear_command
{
    clear_kind kind;
    std::size_t state_snapshot_index;
};

/** Buffer update type. */
enum class buffer_update_kind
{
    index,
    attribute
};

/** Update buffer command. */
struct update_buffer_command
{
    buffer_update_kind kind;
    std::uint32_t buffer_id;
    index_range range;
};

/** Update texture command. */
struct update_texture_command
{
    /* TODO Rect, Data, ... */
};

/** Render command type, including command data. */
using render_command = std::variant<
  clear_command,
  update_buffer_command,
  draw_command>;

/** The default framebuffer has id 0, so we skip it. */
constexpr std::uint32_t framebuffer_id_to_slot(
  std::uint32_t id)
{
    return id - 1;
}

/** The default framebuffer has id 0, so we skip it. */
constexpr std::uint32_t framebuffer_slot_to_id(std::uint32_t slot)
{
    return slot + 1;
};

/** Resource type, used for deferred deletion. */
enum class resource_type
{
    none,               /** No type. */
    index_buffer,       /** Index buffer. */
    attribute_buffer,   /** Attribute buffer. */
    texture,            /** Texture. */
    framebuffer_object, /** Framebuffer object. */
    depth_attachment    /** Depth attachment for a framebuffer object. */
};

/** Resource key for deferred deletions. */
struct resource_key
{
    /** Resource type. */
    resource_type type{resource_type::none};

    /** Resource id. */
    std::uint32_t id{0};

    friend bool operator==(
      const resource_key&,
      const resource_key&) = default;
};

/** a general render context (not associated to any output device/window). */
struct render_context
{
    /** Arena memory is cleaned up every N frames. */
    static constexpr std::size_t CleanupFrames = 120;

    /** the context type. */
    context_type type{context_type::generic};

    /*
     * frame buffers.
     */

    /** default frame buffer. */
    default_framebuffer framebuffer;

    /** Frame buffer objects. */
    utils::slot_map<framebuffer_object> framebuffer_objects;

    /** Depth renderbuffers. */
    utils::slot_map<attachment_depth> depth_attachments;

    /**
     * Check framebuffer completeness.
     *
     * @param id The framebuffer id.
     * @returns Returns `true` if the framebuffer is complete.
     */
    bool is_framebuffer_complete(
      std::uint32_t id);

    /**
     * Resolve an id to a framebuffer.
     *
     * @param id The id the resolve.
     * @returns Returns a draw target.
     * @throws Throws `std::invalid_argument` if the id doesn't resolve to a draw target.
     */
    [[nodiscard]]
    framebuffer_draw_target* resolve_draw_target(
      std::uint32_t id);

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
     * command stream and per-frame storage.
     */

    /**
     * Per-frame command stream containing ordered render commands.
     * Storage is retained between frames; only the live slots [0, command_list.size()) are valid during a frame.
     * Each entry contains all the data needed to execute that command.
     */
    frame_arena<CleanupFrames, render_command> command_list;

    /** Pending resource deletions. */
    frame_arena<CleanupFrames, resource_key> pending_resource_deletions;

    /** Check whether a resource has a pending delete. */
    bool has_pending_deletion(
      resource_type type,
      std::uint32_t id) const;

    /** Process pending deletions list. */
    void process_pending_deletions();

    /**
     * Per-frame arena of render objects.  Storage is retained between frames.
     * Indexed by draw_command::render_object_index when processing draw commands.
     * On reset() inner vector buffers (coords, varyings, etc.) keep their capacity.
     */
    frame_arena<CleanupFrames, render_object> render_objects;

    /**
     * Per-frame pool of index-buffer payload elements referenced by draw commands.
     * Storage is retained between frames so payload elements can be reused without per-frame heap churn.
     */
    frame_arena<CleanupFrames, std::uint32_t> index_buffer_pool;

    frame_arena<CleanupFrames, std::pair<int, int>> active_vab_indices_pool;

    /**
     * Per-frame resolved draw execution contexts, one per draw command.
     * These are built at the start of Present() and consumed by the ST/MT pipeline.
     */
    frame_arena<CleanupFrames, draw_execution> resolved_draws;

    /**
     * Per-frame render state snapshots, one per draw call.
     *
     * Indexed by `draw_command::state_snapshot_index` when resolving draw
     * commands; the corresponding snapshot is referenced by
     * `render_object::states` during execution.
     *
     * Capacity is pre-reserved at the end of `Present()` to avoid mid-frame
     * reallocation.
     */
    frame_arena<CleanupFrames, render_states> state_snapshots;

    /**
     * Per-frame vertex data pool backing coords, attribs, and varyings for all
     * render objects. SSE-aligned; storage is retained between frames so capacity
     * converges to the scene high-water mark after a few frames.
     * Must be reset() after render_objects.reset() at the end of Present().
     */
    frame_arena<
      CleanupFrames,
      ml::vec4,
      utils::aligned_default_init_allocator<
        ml::vec4,
        utils::alignment::sse>>
      vec4_data;

    /**
     * Capture the current render states as a frame snapshot and return its index.
     * Reuses an existing slot if available (capacity retained from previous frame).
     */
    std::size_t capture_state();

    /**
     * Capture the current attribute buffers for indexed draw calls.
     *
     * @note Populates the vertex attributes as a sparse array.
     * @param index_count Index count.
     * @param active_vab_indices Range (inside `active_vab_indices_pool`) of
     *     active vertex array buffer indices.
     * @param attribute_indices Range (inside `index_buffer_pool`) of vertex attribute
     *     indices.
     * @returns Returns the index range of captured attributes inside `vec4_data`.
     */
    index_range capture_attribute_buffers(
      std::size_t index_count,
      const index_range& active_vab_indices,
      std::size_t attribute_slot_count,
      const index_range& attribute_indices)
    {
        const auto attrib_count = active_vab_indices.count;

        const std::size_t attrib_range_start = vec4_data.size();
        const std::size_t attrib_range_size = index_count * attribute_slot_count;

        const auto remapped_indices = std::span{
          &index_buffer_pool[attribute_indices.begin],
          attribute_indices.count};

        auto attribs = vec4_data.allocate_range(index_count * attribute_slot_count);
        for(std::size_t i = 0; i < index_count; ++i)
        {
            auto vertex_attribs = attribs.subspan(i * attribute_slot_count);
            for(std::size_t j = 0; j < attrib_count; ++j)
            {
                const auto [slot, buffer_id] = active_vab_indices_pool[active_vab_indices.begin + j];
                vertex_attribs[slot] = vertex_attribute_buffers[buffer_id].data[remapped_indices[i]];
            }
        }

        return {attrib_range_start, attrib_range_size};
    }

    /**
     * Capture the current attribute buffers for regular draw calls.
     *
     * @note Populates the vertex attributes as a sparse array.
     * @param index_count Index count.
     * @param active_vab_indices Range (inside `active_vab_indices_pool`) of
     *     active vertex array buffer indices.
     * @returns Returns the index range of captured attributes inside `vec4_data`.
     */
    index_range capture_attribute_buffers(
      std::size_t index_count,
      const index_range& active_vab_indices,
      std::size_t attribute_slot_count)
    {
        const auto attrib_count = active_vab_indices.count;

        const std::size_t attrib_range_start = vec4_data.size();
        const std::size_t attrib_range_size = index_count * attribute_slot_count;

        auto attribs = vec4_data.allocate_range(attrib_range_size);
        for(std::size_t i = 0; i < index_count; ++i)
        {
            auto vertex_attribs = attribs.subspan(i * attribute_slot_count);
            for(std::size_t j = 0; j < attrib_count; ++j)
            {
                const auto [slot, buffer_id] = active_vab_indices_pool[active_vab_indices.begin + j];
                vertex_attribs[slot] = vertex_attribute_buffers[buffer_id].data[i];
            }
        }

        return {attrib_range_start, attrib_range_size};
    }

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

    /** resolved draw with their associated program instances, to avoid reallocations. */
    std::vector<
      std::pair<
        swr::impl::draw_execution*,
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
     * Create and queue a non-indexed draw command.
     *
     * Allocates a per-frame render object, captures the current render state,
     * and copies the active vertex attribute data at submission time. The queued
     * command references this render object during `Present()`.
     *
     * @param mode Specifies how the submitted vertices are assembled into primitives.
     * @param count Number of sequential vertices to submit.
     */
    void create_draw_command(
      vertex_buffer_mode mode,
      std::size_t count);

    /**
     * Insert a draw command for an indexed render object.
     *
     * Allocates a per-frame render object, captures the current render state,
     * and copies the active vertex attribute data at submission time. The queued
     * command references this render object during `Present()`.
     *
     * @param mode Specifies how the contents of the subset of the vertex buffer should be interpretted.
     * @param count Number of elements to use from `index_buffer`.
     * @param index_buffer The index buffer to use.
     */
    void create_indexed_draw_command(
      vertex_buffer_mode mode,
      std::size_t count,
      const std::vector<std::uint32_t>& index_buffer);

    /**
     * Insert an update command for a buffer.
     *
     * @param kind The buffer kind.
     * @param id The buffer id.
     * @param range Range in the buffer storage pool.
     */
    void create_update_buffer_command(
      buffer_update_kind kind,
      std::uint32_t id,
      index_range range);

    /*
     * buffer management.
     */

    /**
     * Create a clear command for a buffer.
     *
     * @param kind The clear operation kind.
     */
    void create_clear_command(clear_kind kind);

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
      framebuffer_draw_target& draw_target,
      const render_states* states,
      vertex_buffer_mode mode,
      vertex_buffer& vb);

    /**
     * Assemble primitives from original post-shader vertex storage.
     *
     * This is used by the no-clipping fast path. Coordinates have already been
     * transformed in-place in render_object::coords; vertices are materialized
     * only when stable rasterizer pointers are needed.
     */
    void assemble_original_indexed_primitives(
      framebuffer_draw_target& draw_target,
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
