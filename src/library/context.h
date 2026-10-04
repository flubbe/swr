/**
 * swr - a software rasterizer
 *
 * General render context and SDL render context.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <bit>
#include <functional>
#include <memory>
#include <span>
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
 * Shader support.
 */

using shader_storage_buffer = utils::aligned_byte_storage;

/** Program flags. */
enum class program_flags : std::uint32_t
{
    none = 0,                  /** No flags. */
    prelinked = 1 << 0,        /** Program is pre-linked. */
    linked = 1 << 1,           /** Program is linked. */
    has_flat_varyings = 1 << 2 /** Whether any varying is flat / doesn't need interpolation. */
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

/** Invalid vertex attribute index. */
enum class vertex_attribute_index
{
    invalid = -1
};

/** Graphics program info. */
struct program_info
{
    /** Varying count. Has to match `iqs.size()`. */
    std::uint32_t varying_count{0};

    /** Interpolation qualifiers for varyings. */
    boost::container::static_vector<
      swr::interpolation_qualifier,
      swr::limits::max::varyings>
      iqs;

    /** Flags. */
    program_flags flags{program_flags::none};

    /** Shader behavior metadata. */
    swr::program_metadata metadata{};

    /** The graphics program/shader. */
    const program_base* shader{nullptr};

    /** Shader size. */
    std::size_t program_size{0};

    /** Shader alignment. */
    std::size_t program_alignment{utils::alignment::sse};

#ifndef SWR_ENABLE_MULTI_THREADING
    /** Shader instance. */
    shader_storage_buffer storage;
#endif

    /** Default constructor. */
    program_info() = default;
    program_info(const program_info&) = default;
    program_info(program_info&&) = default;

    /** Constructor. */
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

    /** Assignments. */
    program_info& operator=(const program_info&) = default;
    program_info& operator=(program_info&&) = default;

    /** Shader validation. */
    bool validate() const
    {
        return shader
               && std::has_single_bit(program_alignment)
               && (varying_count == iqs.size());
    }

    /*
     * Accessors.
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
 * Render contexts.
 */

/**
 * Vertex shader instance container.
 *
 * Manages shader lifetime: Creates a shader instance on construction
 * in externally provided storage and takes care of destruction. Handles
 * move assignments.
 *
 * @note The external storage has to outlive the container instance.
 * @note Uniforms and samplers are non-owning and must outlive the container instance.
 */
class vertex_shader_instance_container
{
    /** Shader instance. */
    const swr::program_base* shader{nullptr};

    /** Varying count. */
    std::size_t varying_count{0};

public:
    /** Disallow construction without shader instance. */
    vertex_shader_instance_container() = delete;

    /**
     * Construct a vertex shader instance container.
     *
     * @param storage Storage to construct the shader in.
     * @param shader_info Shader info.
     * @param uniforms Shader uniform.
     * @param samplers_2d 2D samplers.
     * @note Uniforms and samplers are non-owning and must outlive the class instance.
     */
    vertex_shader_instance_container(
      std::byte* storage,
      impl::program_info* shader_info,
      const swr::uniform_bindings& uniforms,
      const swr::sampler_bindings& samplers_2d)
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

    /** Disallow copies. */
    vertex_shader_instance_container(
      const vertex_shader_instance_container&) = delete;

    /** Move the shader instance. */
    vertex_shader_instance_container(
      vertex_shader_instance_container&& other) noexcept
    : shader{other.shader}
    , varying_count{other.varying_count}
    {
        other.shader = nullptr;
    }

    /** Destructor. */
    ~vertex_shader_instance_container() noexcept
    {
        // Happens when container was moved from.
        if(shader != nullptr)
        {
            shader->~program_base();
        }
    }

    /** Disallow copies. */
    vertex_shader_instance_container& operator=(
      const vertex_shader_instance_container&) = delete;

    /** Move the shader instance. */
    vertex_shader_instance_container& operator=(
      vertex_shader_instance_container&& other) noexcept
    {
        shader = other.shader;
        varying_count = other.varying_count;

        other.shader = nullptr;

        return *this;
    }

    /** Get the shader instance. */
    const swr::program_base* get() const noexcept
    {
        return shader;
    }

    /** Return the varying count for this shader. */
    std::size_t get_varying_count() const noexcept
    {
        return varying_count;
    }
};

/**
 * Fragment shader instance container.
 *
 * Manages shader storage and lifetime: Allocates storage, creates a
 * shader instance on construction and takes care of destruction and
 * storage deallocation. Handles move assignments.
 *
 * @note Uniforms and samplers are non-owning and must outlive the container instance.
 */
class fragment_shader_instance_container
{
    /** Shader instance storage. */
    shader_storage_buffer storage;

    /** Shader instance. */
    const swr::program_base* shader{nullptr};

public:
    /** Disallow construction without shader instance. */
    fragment_shader_instance_container() = delete;

    /**
     * Create a fragment shader instance container.
     *
     * Manages shader storage allocation and lifetime.
     *
     * @param shader_info Shader info.
     * @param uniforms Shader uniform.
     * @param samplers_2d 2D samplers.
     * @note Uniforms and samplers are non-owning and must outlive the class instance.
     */
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

    /** Disallow copies. */
    fragment_shader_instance_container(
      const fragment_shader_instance_container&) = delete;

    /** Move the shader instance. */
    fragment_shader_instance_container(
      fragment_shader_instance_container&& other) noexcept
    : storage{std::move(other.storage)}
    , shader{other.shader}
    {
        other.shader = nullptr;
    }

    /** Destructor. */
    ~fragment_shader_instance_container() noexcept
    {
        // Happens when container was moved from.
        if(shader != nullptr)
        {
            shader->~program_base();
        }
    }

    /** Disable copies. */
    fragment_shader_instance_container& operator=(
      const fragment_shader_instance_container&) = delete;

    /** Move the shader instance. */
    fragment_shader_instance_container& operator=(
      fragment_shader_instance_container&& other) noexcept
    {
        storage = std::move(other.storage);
        shader = other.shader;

        other.shader = nullptr;

        return *this;
    }

    /** Get the shader instance. */
    const swr::program_base* get() const noexcept
    {
        return shader;
    }
};

/** The context type. */
enum class context_type
{
    generic,  /** A generic, unspecified context. */
    sdl,      /** SDL context. */
    offscreen /** Offscreen context. */
};

/*
 * Render commands.
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

/** Resolved execution context for a draw command. */
struct draw_execution
{
    /** Render object. Pointer into `render_context::render_objects`. */
    render_object* object{nullptr};

    /** Vertex count. */
    std::size_t vertex_count;

    /** Attribute range in `render_context::vec4_data`. */
    index_range attribute_index_range;

    /** Attribute slot count. */
    std::size_t attribute_slot_count;

    /**
     * Clipped vertices range in `render_context::vec4_data`.
     *
     * FIXME Seems unused-ish?
     */
    index_range clipped_vertex_range;

    /**
     * Render states in `render_context::state_snapshots`.
     *
     * TODO `object->states` points to the same data.
     */
    const render_states* states{nullptr};

    /** Draw target */
    framebuffer_draw_target* draw_target{nullptr};
};

/** Clear command type. */
enum class clear_kind
{
    color, /** Clear the color buffer. */
    depth  /** Clear the depth buffer. */
};

/** Clear command. */
struct clear_command
{
    /** Command requires pipeline flush. */
    static constexpr bool requires_flush = true;

    /** Clear kind. */
    clear_kind kind;

    /** State snapshot index in `render_context::state_snapshots`. */
    std::size_t state_snapshot_index;
};

/** Buffer update type. */
enum class buffer_update_kind
{
    index,    /** Update index buffer. */
    attribute /** Update attribute buffer. */
};

/** Update buffer command. */
struct update_buffer_command
{
    /** Command requires pipeline flush. */
    static constexpr bool requires_flush = true;

    /** Which buffer to update. */
    buffer_update_kind kind;

    /** Buffer id. */
    std::uint32_t buffer_id;

    /** Index range in `render_context::vec4_data` or `render_context::index_buffer_pool`. */
    index_range range;
};

/** Texture update kind. */
enum class texture_update_kind
{
    create, /** Create a new texture. */
    update  /** Update an existing texture. */
};

/** Update texture command. */
struct update_texture_command
{
    /** Command requires pipeline flush. */
    static constexpr bool requires_flush = true;

    /** Texture update kind. */
    texture_update_kind kind;

    /** Texture id. */
    std::uint32_t texture_id;

    /** Mipmap level. */
    std::uint32_t level;

    /** X offset for updates. */
    std::size_t offset_x;

    /** Y offset for updates. */
    std::size_t offset_y;

    /** Texture width. */
    std::size_t width;

    /** Texture height. */
    std::size_t height;

    /** Pixel format. */
    pixel_format format;

    /** Texture data (possibly empty). */
    std::vector<std::uint8_t> data;
};

/** Depth texture comparison parameter update. */
struct texture_compare_command
{
    /** Command requires pipeline flush. */
    static constexpr bool requires_flush = true;

    /** Texture id. */
    std::uint32_t texture_id;

    std::variant<
      texture_compare_mode,
      comparison_func>
      mode_or_function;

    /** Whether to update the mode or the function. */
    bool update_mode;

    /** Texture compare mode. */
    texture_compare_mode mode;

    /** Comparison function. */
    comparison_func function;
};

/** Update a framebuffer texture attachment. */
struct framebuffer_texture_command
{
    /** Command requires pipeline flush. */
    static constexpr bool requires_flush = true;

    /** Framebuffer id. */
    std::uint32_t framebuffer_id;

    /** Attachment type. */
    framebuffer_attachment attachment;

    /** Texture id. */
    std::uint32_t texture_id;

    /** Mipmap level. */
    std::uint32_t level;
};

/** Update a framebuffer depth renderbuffer attachment. */
struct framebuffer_renderbuffer_command
{
    /** Command requires pipeline flush. */
    static constexpr bool requires_flush = true;

    /** Framebuffer id. */
    std::uint32_t framebuffer_id;

    /** Renderbuffer id. */
    std::uint32_t renderbuffer_id;
};

/** Draw command for a render object. */
struct draw_command
{
    /** Command doesn't require flush. */
    static constexpr bool requires_flush = false;

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

/** Render command type, including command data. */
using render_command = std::variant<
  clear_command,
  update_buffer_command,
  update_texture_command,
  texture_compare_command,
  framebuffer_texture_command,
  framebuffer_renderbuffer_command,
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

/** A general render context (not associated to any output device/window). */
struct render_context
{
    /** Arena memory is cleaned up every N frames. */
    static constexpr std::size_t CleanupFrames = 120;

    /** Context type. */
    context_type type{context_type::generic};

    /*
     * Frame buffers.
     */

    /** Default frame buffer. */
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
     * Context states.
     */

    /** The current render states. These are copied on each draw call and stored in a draw list. */
    render_states states;

    /*
     * Error handling.
     */

    /** Last detected error. */
    error last_error{error::none};

    /*
     * Command stream and per-frame storage.
     */

    /**
     * Per-frame command stream containing ordered render commands.
     * Storage is retained between frames; only the live slots [0, command_list.size()) are valid during a frame.
     * Each entry contains all the data needed to execute that command.
     */
    frame_arena<CleanupFrames, render_command> command_list;

    /*
     * Command dispatch.
     */

    void execute_command(
      const impl::clear_command& cmd);

    void execute_command(
      const impl::draw_command& cmd);

    void execute_command(
      const impl::update_buffer_command& cmd);

    void execute_command(
      const impl::update_texture_command& cmd);

    void execute_command(
      const impl::texture_compare_command& cmd);

    void execute_command(
      const impl::framebuffer_texture_command& cmd);

    void execute_command(
      const impl::framebuffer_renderbuffer_command& cmd);

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
     * References by `draw_command::render_object` when processing draw commands.
     * On `reset()` inner vector buffers (attribs, vertex flags, clipped vertices)
     * keep their capacity.
     *
     * @note Needs to reserve memory before storing render objects, since these references
     *     have to be stable within a frame.
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

    /** Index buffers. */
    utils::slot_map<
      std::vector<
        std::uint32_t>>
      index_buffers;

    /** Vertex attribute buffers. */
    utils::slot_map<vertex_attribute_buffer> vertex_attribute_buffers;

    /** Currently active vertex attribute buffers. stores indices into vertex_attribute_buffers. */
    boost::container::static_vector<
      int,
      swr::limits::max::attributes>
      active_vabs;

    /*
     * Shaders.
     */

    /** The registered shaders, together with their program information. */
    utils::slot_map<program_info> programs;

#ifdef SWR_ENABLE_MULTI_THREADING
    /** Storage for the shader instances. */
    shader_storage_buffer program_storage;

    /** Resolved draw with their associated program instances, to avoid reallocations. */
    std::vector<
      std::pair<
        swr::impl::draw_execution*,
        impl::vertex_shader_instance_container>>
      program_instances;
#endif /* SWR_ENDABLE_MULTI_THREADING */

    /** Default shader. */
    std::unique_ptr<program_base> default_shader;

    /*
     * Texture management.
     */

    /** Texture storage. */
    utils::slot_map<std::unique_ptr<texture_2d>> texture_2d_storage;

    /** A default texture. This needs to be allocated in texture_2d_storage at index 0. */
    texture_2d* default_texture_2d{nullptr};

    /*
     * Thread pool.
     */

#ifdef SWR_ENABLE_MULTI_THREADING
    /** Thread pool type to use. */
    typedef concurrency_utils::deferred_thread_pool<
      concurrency_utils::mpmc_blocking_queue<
        std::function<void()>>>
      thread_pool_type;

    /** Processing threads. */
    std::uint32_t thread_pool_size{0};

    /** Worker threads. */
    thread_pool_type thread_pool;
#else
    /** No thread pool type. */
    typedef std::nullptr_t thread_pool_type;
#endif /* SWR_ENABLE_MULTI_THREADING */

    /*
     * Rasterization.
     */

    /** Rasterizes points, lines and triangles. */
    std::unique_ptr<rast::rasterizer> rasterizer;

    /** Create the rasterizer from the internal state. */
    void create_rasterizer();

    /*
     * Render context implementation.
     */

    /** Default constructor. */
    render_context() = default;

    /* Disable copies and moves. */
    render_context(const render_context&) = delete;
    render_context(render_context&&) = delete;

    /** virtual destructor. */
    virtual ~render_context()
    {
        shutdown();
    }

    /* Disable copies and moves. */
    render_context& operator=(const render_context&) = delete;
    render_context& operator=(render_context&&) = delete;

    /*
     * Render object management.
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
      std::span<const std::uint32_t> index_buffer);

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

    /**
     * Insert an update command for a texture.
     *
     * @param mode Create or update the texture.
     * @param texture_id Id of the texture to be updated.
     * @param level ;ipmap level.
     * @param offset_x x-offset
     * @param offset_y y-offset
     * @param width Width of the data
     * @param height Height of the data
     * @param format Pixel format of the data
     * @param data Image data
     */
    void create_texture_update_command(
      texture_update_kind mode,
      std::uint32_t texture_id,
      std::uint32_t level,
      std::size_t offset_x,
      std::size_t offset_y,
      std::size_t width,
      std::size_t height,
      pixel_format format,
      std::span<const std::uint8_t> data);

    /** Insert a framebuffer texture attachment command. */
    void create_framebuffer_texture_command(
      std::uint32_t framebuffer_id,
      framebuffer_attachment attachment,
      std::uint32_t texture_id,
      std::uint32_t level);

    /** Insert a framebuffer depth renderbuffer attachment command. */
    void create_framebuffer_renderbuffer_command(
      std::uint32_t framebuffer_id,
      std::uint32_t renderbuffer_id);

    /*
     * Buffer management.
     */

    /**
     * Create a clear command for a buffer.
     *
     * @param kind The clear operation kind.
     */
    void create_clear_command(clear_kind kind);

    /*
     * Primitive assembly.
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
     * Render context interface.
     */

    /** Free all resources. */
    virtual void shutdown();

    /** Lock color buffer for writing. On success, ensures `framebuffer.color_buffer.info.data_ptr` to be valid. */
    virtual bool lock()
    {
        return false;
    }

    /** Unlock the color buffer. */
    virtual void unlock()
    {
    }

    /** Copy the default color buffer to some target. */
    virtual void copy_default_color_buffer()
    {
    }
};

/** A render context for an SDL window. */
class sdl_render_context final
: public render_context
{
protected:
    /** Context dimensions: the buffer may be a bit larger, but we only want to copy the correct rectangle. */
    SDL_FRect sdl_viewport_dimensions;

    /** Color buffer. */
    SDL_Texture* sdl_color_buffer{nullptr};

    /** SDL renderer. */
    SDL_Renderer* sdl_renderer{nullptr};

    /** Associated SDL window. */
    SDL_Window* sdl_window{nullptr};

    /**
     * Return the window's pixel format, converted to swr::pixel_format.
     * If out_sdl_pixel_format is non-null, the SDL pixel format will be written into it.
     */
    swr::pixel_format get_window_pixel_format(
      SDL_PixelFormat* out_sdl_pixel_format = nullptr) const;

public:
    /**
     * Create an SDL render context.
     *
     * @param thread_hint Thread count hint for the thread pool in multi-threaded builds.
     */
    sdl_render_context(
      [[maybe_unused]] std::uint32_t thread_hint = 0)
    {
        type = context_type::sdl;

#ifdef SWR_ENABLE_MULTI_THREADING
        if(thread_hint > 0)
        {
            thread_pool_size = thread_hint;
        }
#endif
    }

    /* Disable copies and moves. */
    sdl_render_context(const sdl_render_context&) = delete;
    sdl_render_context(sdl_render_context&&) = delete;

    sdl_render_context& operator=(const sdl_render_context&) = delete;
    sdl_render_context& operator=(sdl_render_context&&) = delete;

    /*
     * Render context interface.
     */

    void shutdown() override;
    bool lock() override;
    void unlock() override;
    void copy_default_color_buffer() override;

    /*
     * SDL render context interface.
     */

    /**
     * Initialize the context with the supplied SDL data and create the render buffers.
     *
     * @param window SDL window to create the context for.
     * @param renderer An SDL renderer.
     * @param width Render buffer width.
     * @param height Render buffer height.
     */
    void initialize(
      SDL_Window* window,
      SDL_Renderer* renderer,
      int width,
      int height);

    /** (Re-)create depth- and color buffers using the given width and height. */
    void update_buffers(
      int width,
      int height);
};

/** A offscreen render context. */
class offscreen_render_context final
: public render_context
{
    /** Render buffer width. */
    int width = 0;

    /** Render buffer height. */
    int height = 0;

    /** RGBA buffer. */
    std::vector<std::uint32_t> rgba_buffer;

    /** Whether the buffer is locked. */
    bool locked{false};

public:
    /**
     * Create an offscreen render context.
     *
     * @param thread_hint Thread count hint for the thread pool in multi-threaded builds.
     */
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
     * Render context interface.
     */

    void shutdown() override;
    bool lock() override;
    void unlock() override;

    /*
     * Offscreen render context interface.
     */

    /**
     * Initialize the context and create the render buffers.
     *
     * @param width Render buffer width.
     * @param height Render buffer height.
     */
    void initialize(
      int width,
      int height);

    /** (Re-)create depth- and color buffers using the given width and height. */
    bool update_buffers(
      int width,
      int height);
};

/*
 * Global render contexts.
 */

/** The (thread-)global rendering context. */
extern thread_local render_context* global_context;

/** Assert validity of render context in debug builds. */
#define ASSERT_INTERNAL_CONTEXT assert(impl::global_context)

/*
 * Texture helpers.
 */

/**
 * Create a default texture.
 *
 * @param context The render context for the texture.
 */
void create_default_texture(
  render_context* context);

/*
 * Shader helpers.
 */

/**
 * Create a default shader in the supplied context which outputs empty fragments.
 *
 * @param context The render context for the shader.
 */
void create_default_shader(
  render_context* context);

} /* namespace impl */

} /* namespace swr */
