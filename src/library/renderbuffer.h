/**
 * swr - a software rasterizer
 *
 * Output buffers for rendering.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "ml/all.h"

#include "common/utils.h"
#include "pixelformat.h"
#include "textures.h"

namespace swr
{

namespace impl
{

/**
 * Flags controlling fragment output.
 *
 * @note Not an `enum class` for easier type conversion and bit manipulation.
 */
struct fragment_output_flags
{
    using flag_type = std::uint8_t;

    constexpr static flag_type none = 0;          /** No flag. */
    constexpr static flag_type write_color = 1;   /** Write color value. */
    constexpr static flag_type write_depth = 2;   /** Write depth value. */
    constexpr static flag_type write_stencil = 4; /** Write stencil value. */
};

/** Output after fragment processing, before merging. */
struct fragment_output
{
    /** Color produced by the fragment shader. */
    ml::vec4 color;

    /** Write flags. */
    fragment_output_flags::flag_type write_flags{
      fragment_output_flags::none};
};

/** Output after fragment processing, before merging, for 2x2 blocks. */
struct fragment_output_block
{
    /** 2x2 block of colors produced by the fragment shader. */
    std::array<ml::vec4, 4> color;

    /** Whether the color values should be written to the color buffer. */
    std::uint8_t write_color_mask = 0b1111;

    /**
     * Whether the stencil values should be written to the stencil buffer.
     *
     * @note Currently unused.
     */
    std::uint8_t write_stencil_mask = 0b0;
};

/** Framebuffer attachment info. */
template<typename T>
struct attachment_info
{
    /** Type of the buffer entries. */
    using value_type = T;

    /** Width of the attachment. Has to be aligned on `rasterizer_block_size`.  */
    std::size_t width{0};

    /** Height of the attachment. Has to be aligned on `rasterizer_block_size`. */
    std::size_t height{0};

    /**
     * Attachment row stride, measured in `value_type` elements.
     *
     * This may differ from `width` when rows contain padding or the attachment
     * is backed by a larger buffer.
     */
    std::size_t stride{0};

    /** Pointer to the attachment's data. */
    T* data_ptr{nullptr};
};

/** A fixed-point depth buffer attachment. */
struct attachment_depth
{
    using value_type = ml::fixed_32_t;

    /** Attachment info. */
    attachment_info<value_type> info;

    /** The depth buffer data. */
    utils::sse_aligned_vector<value_type> data;

    /** Free resources. */
    void reset()
    {
        info = {.width = 0, .height = 0, .stride = 0, .data_ptr = nullptr};

        data.clear();
        data.shrink_to_fit();
    }

    /**
     * Allocate the buffer.
     *
     * @param width Buffer width.
     * @param height Buffer height.
     */
    void allocate(
      std::size_t width,
      std::size_t height)
    {
        assert(width > 0 && height > 0);
        data.resize(width * height);
        info = {
          .width = width,
          .height = height,
          .stride = width * sizeof(value_type),
          .data_ptr = data.data()};
    }
};

/** Non-owning 32-bit color buffer binding. */
struct attachment_color_buffer
{
    using value_type = std::uint32_t;

    /** Attachment info. */
    attachment_info<value_type> info;

    /**
     * Pixel format converter.
     *
     * @note Needs explicit initialization by calling `reset()` or `converter.set_pixel_format(...)`.
     */
    pixel_format_converter converter;

    /** Reset buffer. */
    void reset()
    {
        info = {.width = 0, .height = 0, .stride = 0, .data_ptr = nullptr};
        converter.set_pixel_format(
          pixel_format_descriptor::named_format(
            pixel_format::unsupported));
    }

    /**
     * Attach externally managed buffer.
     *
     * @note If any of `width`, `height`, `pitch` is zero, or if `ptr == nullptr`,
     *     the attachment is reset.
     * @param width Buffer width.
     * @param height Buffer height.
     * @param pitch Buffer pitch.
     * @param ptr Pointer to the buffer data.
     */
    void attach(
      std::size_t width,
      std::size_t height,
      std::size_t pitch,
      value_type* ptr)
    {
        if(width == 0
           || height == 0
           || pitch == 0
           || ptr == nullptr)
        {
            detach();
        }
        else
        {
            info = {
              .width = width,
              .height = height,
              .stride = pitch,
              .data_ptr = ptr};
        }
    }

    /** Detach external buffer. */
    void detach()
    {
        info = {.width = 0, .height = 0, .stride = 0, .data_ptr = nullptr};
    }

    /** Validate. */
    bool is_valid() const
    {
        return info.width > 0
               && info.height > 0
               && info.stride > 0
               && info.data_ptr != nullptr;
    }
};

/** Non-owning color texture binding. */
struct texture_attachment_binding
{
    using value_type = ml::vec4;

    /** Attachment info. */
    attachment_info<value_type> info;

    /** Attached texture id. */
    std::uint32_t tex_id{default_tex_id};

    /** Attached texture pointer. */
    texture_2d* tex{nullptr};

    /** Mipmap level we are writing to. */
    std::uint32_t level{0};

    /** Release binding state. */
    void reset()
    {
        detach();
        info = {.width = 0, .height = 0, .stride = 0, .data_ptr = nullptr};
    }

    /**
     * Bind texture.
     *
     * @param in_tex The texture to bind to.
     * @param in_level The mipmap lavel to bind to.
     */
    [[nodiscard]]
    bool attach(
      texture_2d* in_tex,
      std::uint32_t in_level)
    {
        assert(in_tex != nullptr);

        auto* color_texture = in_tex->as_texture_color_2d();
        if(color_texture == nullptr
           || in_level >= color_texture->data.data_ptrs.size())
        {
            return false;
        }

        tex_id = in_tex->id;
        tex = in_tex;
        level = in_level;

        // FIXME Dimensions should not be re-calculated here.
        info = {
          .width = std::max(1uz, static_cast<std::size_t>(in_tex->width >> in_level)),
          .height = std::max(1uz, static_cast<std::size_t>(in_tex->height >> in_level)),
          .stride = in_tex->mip_pitch(in_level),
          .data_ptr = color_texture->data.data_ptrs[in_level]};

        return true;
    }

    /** Detach external buffer. */
    void detach()
    {
        tex_id = default_tex_id;
        tex = nullptr;
        level = 0;

        info = {.width = 0, .height = 0, .stride = 0, .data_ptr = nullptr};
    }

    /** Check if a non-default texture was bound, and if it is still valid. */
    bool is_valid() const;
};

/** Non-owning depth texture binding. */
struct depth_texture_attachment_binding
{
    using value_type = ml::fixed_32_t;

    /** Attachment info. */
    attachment_info<value_type> info;

    /** Attached texture id. */
    std::uint32_t tex_id{default_tex_id};

    /** Attached texture pointer. */
    texture_2d* tex{nullptr};

    /** Mipmap level we are writing to. */
    std::uint32_t level{0};

    /** Release binding state. */
    void reset()
    {
        detach();
        info = {.width = 0, .height = 0, .stride = 0, .data_ptr = nullptr};
    }

    /**
     * Bind texture.
     *
     * @param in_tex The texture to bind to.
     * @param in_level The mipmap lavel to bind to.
     */
    [[nodiscard]]
    bool attach(
      texture_2d* in_tex,
      std::uint32_t in_level)
    {
        assert(in_tex != nullptr);

        auto* depth_texture = in_tex->as_texture_depth_2d();
        if(depth_texture == nullptr
           || in_level >= depth_texture->data.data_ptrs.size())
        {
            return false;
        }

        tex_id = in_tex->id;
        tex = in_tex;
        level = in_level;

        // FIXME Dimensions should not be re-calculated here.
        info = {
          .width = std::max(1uz, static_cast<std::size_t>(in_tex->width >> in_level)),
          .height = std::max(1uz, static_cast<std::size_t>(in_tex->height >> in_level)),
          .stride = in_tex->mip_pitch(in_level) * sizeof(value_type),
          .data_ptr = depth_texture->data.data_ptrs[in_level]};

        return true;
    }

    /** Detach external buffer. */
    void detach()
    {
        tex_id = default_tex_id;
        tex = nullptr;
        level = 0;

        info = {.width = 0, .height = 0, .stride = 0, .data_ptr = nullptr};
    }

    /** Check if a non-default texture was bound, and if it is still valid. */
    bool is_valid() const;
};

/** Non-owning depth renderbuffer binding. */
struct depth_renderbuffer_attachment_binding
{
    using value_type = ml::fixed_32_t;

    /** Attachment info. */
    attachment_info<value_type> info;

    /** Attached depth renderbuffer id. */
    std::uint32_t attachment_id{0};

    /** Attached depth renderbuffer pointer. */
    attachment_depth* attachment{nullptr};

    /** Release binding state. */
    void reset()
    {
        detach();
        info = {.width = 0, .height = 0, .stride = 0, .data_ptr = nullptr};
    }

    /**
     * Bind renderbuffer.
     *
     * @param in_attachment_id Attachment id.
     * @param in_attachment Depth attachment.
     */
    void attach(
      std::uint32_t in_attachment_id,
      attachment_depth* in_attachment)
    {
        attachment_id = 0;
        attachment = nullptr;

        info = {.width = 0, .height = 0, .stride = 0, .data_ptr = nullptr};

        if(in_attachment != nullptr)
        {
            attachment_id = in_attachment_id;
            attachment = in_attachment;
            info = in_attachment->info;
        }
    }

    /** detach renderbuffer. same as attach(0, nullptr) */
    void detach()
    {
        attach(0, nullptr);
    }

    /** check if a depth renderbuffer was bound, and if it is still valid. */
    bool is_valid() const;
};

/** Framebuffer dimensions. */
struct framebuffer_dimensions
{
    /** Width of the framebuffer target. */
    std::size_t width{0};

    /** Height of the framebuffer target. */
    std::size_t height{0};
};

/** framebuffer draw target. */
struct framebuffer_draw_target
{
    /** The target's dimensions. */
    framebuffer_dimensions dimensions;

    /** Virtual destructor. */
    virtual ~framebuffer_draw_target() = default;

    /** Clear a color attachment. Fails silently if the attachment is not available. */
    virtual void clear_color(
      std::uint32_t attachment,
      ml::vec4 clear_color) = 0;

    /**
     * Clear part of a color attachment. Fails silently if the attachment is not
     * available or if the supplied rectangle was invalid.
     */
    virtual void clear_color(
      std::uint32_t attachment,
      ml::vec4 clear_color,
      const utils::rect& rect) = 0;

    /** Clear the depth attachment. Fails silently if the attachment is not available. */
    virtual void clear_depth(
      ml::fixed_32_t clear_depth) = 0;

    /**
     * Clear the depth attachment. Fails silently if the attachment is not available
     * of if the supplied rectangle was invalid.
     */
    virtual void clear_depth(
      ml::fixed_32_t clear_depth,
      const utils::rect& rect) = 0;

    /**
     * Merge a color value while respecting blend modes, if requested.
     * Silently fails for invalid attachments.
     */
    virtual void merge_color(
      std::uint32_t attachment,
      int x,
      int y,
      const fragment_output& frag,
      bool do_blend,
      blend_func src,
      blend_func dst) = 0;

    /**
     * Merge a 2x2 block of color values while respecting blend modes, if requested.
     * Silently fails for invalid attachments.
     */
    virtual void merge_color_block(
      std::uint32_t attachment,
      int x,
      int y,
      const fragment_output_block& frag,
      bool do_blend,
      blend_func src,
      blend_func dst) = 0;

    /**
     * If a depth buffer is available, perform a depth comparison and (also depending
     * on write_mask) possibly write a new value to the depth buffer.
     *
     * If the depth test failed, write_mask is set to false, and true otherwise.
     *
     * Sets write_mask to true if no depth buffer was available.
     */
    virtual void depth_compare_write(
      int x,
      int y,
      float depth_value,
      comparison_func depth_func,
      bool write_depth,
      bool& write_mask) = 0;

    /**
     * If a depth buffer is available, perform a depth comparison and (also
     * depending on write_mask) possibly write new values to the depth buffer.
     *
     * If a depth test failed, correpsonding entry in write_mask is set to false,
     * and true otherwise.
     *
     * Sets all write_mask entries to true if no depth buffer was available.
     */
    virtual void depth_compare_write_block(
      int x,
      int y,
      const std::array<float, 4>& depth_value,
      comparison_func depth_func,
      bool write_depth,
      std::uint8_t& write_mask) = 0;
};

/** default framebuffer. */
struct default_framebuffer final
: public framebuffer_draw_target
{
    /** default color buffer. */
    attachment_color_buffer color_buffer;

    /** default depth attachment. */
    attachment_depth depth_buffer;

    // TODO add stencil attachment.

    /** default constructor. */
    default_framebuffer() = default;

    /** virtual destructor. */
    virtual ~default_framebuffer() = default;

    /*
     * framebuffer_draw_target interface.
     */

    virtual void clear_color(
      std::uint32_t attachment,
      ml::vec4 clear_color) override;
    virtual void clear_color(
      std::uint32_t attachment,
      ml::vec4 clear_color,
      const utils::rect& rect) override;
    virtual void clear_depth(
      ml::fixed_32_t clear_depth) override;
    virtual void clear_depth(
      ml::fixed_32_t clear_depth,
      const utils::rect& rect) override;
    virtual void merge_color(
      std::uint32_t attachment,
      int x,
      int y,
      const fragment_output& frag,
      bool do_blend,
      blend_func src,
      blend_func dst) override;
    virtual void merge_color_block(
      std::uint32_t attachment,
      int x,
      int y,
      const fragment_output_block& frag,
      bool do_blend,
      blend_func src,
      blend_func dst) override;
    virtual void depth_compare_write(
      int x,
      int y,
      float depth_value,
      comparison_func depth_func,
      bool write_depth,
      bool& write_mask) override;
    virtual void depth_compare_write_block(
      int x,
      int y,
      const std::array<float, 4>& depth_value,
      comparison_func depth_func,
      bool write_depth,
      std::uint8_t& write_mask) override;

    /*
     * default_framebuffer interface.
     */

    /** reset to default state. */
    void reset()
    {
        dimensions = {0, 0};
        color_buffer.reset();
        depth_buffer.reset();
    }

    /** set up the default framebuffer. */
    void setup(
      std::size_t width,
      std::size_t height,
      std::size_t pitch,
      pixel_format pixel_format,
      std::uint32_t* data)
    {
        reset();
        color_buffer.attach(width, height, pitch, data);
        color_buffer.converter.set_pixel_format(
          pixel_format_descriptor::named_format(
            pixel_format));
        depth_buffer.allocate(width, height);
        dimensions = {width, height};
    }

    /** update the color attachment's format. */
    void set_color_pixel_format(pixel_format name)
    {
        color_buffer.converter.set_pixel_format(
          pixel_format_descriptor::named_format(
            name));
    }

    /** check if the color attachment currently is attached to the externally supplied memory. */
    bool is_color_attached() const
    {
        return color_buffer.is_valid();
    }

    /** weakly check if the color attachment currently is attached to the externally supplied memory, i.e., only check data pointer. */
    bool is_color_weakly_attached() const
    {
        return color_buffer.info.data_ptr != nullptr;
    }
};

/** A logical texture attachment to make immediate state changes visible to the API. */
struct logical_texture_attachment
{
    /** Framebuffer dimensions. */
    std::optional<framebuffer_dimensions> dimensions;

    /** Mipmap level. */
    std::uint32_t level = 0;

    /** Texture id. */
    std::uint32_t texture_id = 0;
};

/** A framebuffer object. */
class framebuffer_object final
: public framebuffer_draw_target
{
    friend class render_context;

    /** Color attachments. */
    std::array<
      std::optional<texture_attachment_binding>,
      swr::limits::max::color_attachments>
      color_bindings;

    /** Current color attachment count. */
    std::uint32_t color_attachment_count{0};

    /*
     * Logical state, reflecting API changes immediately.
     */

    /** Logical color attachment state, visible immediately. */
    std::array<
      logical_texture_attachment,
      swr::limits::max::color_attachments>
      logical_color_attachments;

    /** Logical depth attachment state, visible immediately. */
    logical_texture_attachment logical_depth_attachment;

    /** Logical depth renderbuffer id, visible immediately. */
    std::uint32_t logical_depth_renderbuffer_id{0};

    /** Logical framebuffer dimensions, visible immediately. */
    framebuffer_dimensions logical_dimensions;

    /** Depth attachments. */
    std::variant<
      std::monostate,
      depth_renderbuffer_attachment_binding,
      depth_texture_attachment_binding>
      depth_binding;

    /** Cached active depth attachment info for hot paths. */
    const attachment_info<
      ml::fixed_32_t>*
      active_depth_attachment_info{nullptr};

    /** Check whether a depth attachment is currently bound. */
    bool has_depth_binding() const
    {
        return !std::holds_alternative<std::monostate>(depth_binding);
    }

    /** Refresh cached pointers that hot paths rely on. */
    void refresh_attachment_caches()
    {
        active_depth_attachment_info = std::visit(
          [](const auto& binding) -> const attachment_info<ml::fixed_32_t>*
          {
              using binding_type = std::decay_t<decltype(binding)>;
              if constexpr(std::is_same_v<binding_type, std::monostate>)
              {
                  return nullptr;
              }
              else
              {
                  return &binding.info;
              }
          },
          depth_binding);
    }

    /** Return the active depth attachment info. */
    const attachment_info<ml::fixed_32_t>* get_depth_attachment_info() const
    {
        return active_depth_attachment_info;
    }

    /** Check if the active depth binding is valid. */
    bool has_valid_depth_binding() const
    {
        return std::visit(
          [](const auto& binding) -> bool
          {
              using binding_type = std::decay_t<decltype(binding)>;
              if constexpr(std::is_same_v<binding_type, std::monostate>)
              {
                  return false;
              }
              else
              {
                  return binding.info.width != 0
                         && binding.info.height != 0
                         && binding.is_valid();
              }
          },
          depth_binding);
    }

    // TODO add stencil attachment.

    /** Calculate minimum bounding box across all active color & depth attachments. */
    template<
      typename ColorContainer,
      typename GetColorDim,
      typename GetDepthDim>
    static framebuffer_dimensions compute_min_dimensions(
      const ColorContainer& color_attachments,
      std::size_t active_color_count,
      GetColorDim&& get_color_dim,
      GetDepthDim&& get_depth_dim)
    {
        std::optional<std::size_t> width = std::nullopt;
        std::optional<std::size_t> height = std::nullopt;

        if(active_color_count > 0)
        {
            for(const auto& attachment: color_attachments)
            {
                if(auto dim = get_color_dim(attachment))
                {
                    width = !width.has_value()
                              ? dim->width
                              : std::min(width.value(), dim->width);
                    height = !height.has_value()
                               ? dim->height
                               : std::min(height.value(), dim->height);
                }
            }
        }

        if(auto depth_dim = get_depth_dim())
        {
            width = !width.has_value()
                      ? depth_dim->width
                      : std::min(width.value(), depth_dim->width);
            height = !height.has_value()
                       ? depth_dim->height
                       : std::min(height.value(), depth_dim->height);
        }

        return {
          width.value_or(0),
          height.value_or(0)};
    }

    /** Full recalculation of physical dimensions (called on detach / reset). */
    void calculate_effective_dimensions()
    {
        dimensions = compute_min_dimensions(
          color_bindings,
          color_attachment_count,
          [](const auto& binding) -> std::optional<framebuffer_dimensions>
          {
              if(binding)
              {
                  return framebuffer_dimensions{
                    binding->info.width,
                    binding->info.height};
              }
              return std::nullopt;
          },
          [this]() -> std::optional<framebuffer_dimensions>
          {
              const auto* depth = get_depth_attachment_info();
              if(depth && depth->width > 0 && depth->height > 0)
              {
                  return framebuffer_dimensions{
                    depth->width,
                    depth->height};
              }
              return std::nullopt;
          });
    }

    /** Recalculate dimensions visible to immediate API state changes. */
    void calculate_logical_dimensions()
    {
        logical_dimensions = compute_min_dimensions(
          logical_color_attachments,
          logical_color_attachments.size(),
          [](const auto& attachment) -> std::optional<framebuffer_dimensions>
          {
              if(attachment.texture_id != 0)
              {
                  return attachment.dimensions;
              }
              return std::nullopt;
          },
          [this]() -> std::optional<framebuffer_dimensions>
          {
              if(logical_depth_attachment.texture_id != 0
                 || logical_depth_renderbuffer_id != 0)
              {
                  return logical_depth_attachment.dimensions;
              }
              return std::nullopt;
          });
    }

    /** $O(1)$ incremental update when attaching a new target. */
    void update_effective_dimensions_incremental(
      std::size_t new_width,
      std::size_t new_height)
    {
        if(new_width == 0
           || new_height == 0)
        {
            return;
        }

        if(dimensions.width == 0
           && dimensions.height == 0)
        {
            dimensions = {new_width, new_height};
        }
        else
        {
            dimensions.width = std::min(dimensions.width, new_width);
            dimensions.height = std::min(dimensions.height, new_height);
        }
    }

public:
    /** Default constructor. */
    framebuffer_object() = default;

    /** Disallow copying. */
    framebuffer_object(
      const framebuffer_object&) = delete;

    /** Move constructor. */
    framebuffer_object(
      framebuffer_object&& other)
    : color_bindings{std::move(other.color_bindings)}
    , color_attachment_count{other.color_attachment_count}
    , logical_color_attachments{std::move(other.logical_color_attachments)}
    , logical_depth_attachment{std::move(other.logical_depth_attachment)}
    , logical_depth_renderbuffer_id{other.logical_depth_renderbuffer_id}
    , logical_dimensions{other.logical_dimensions}
    , depth_binding{std::move(other.depth_binding)}
    {
        refresh_attachment_caches();
    }

    /** Virtual destructor. */
    virtual ~framebuffer_object() = default;

    /** Disallow copying. */
    framebuffer_object& operator=(
      const framebuffer_object&) = delete;

    /** Move assignment. */
    framebuffer_object& operator=(
      framebuffer_object&& other)
    {
        if(this != &other)
        {
            color_bindings = std::move(other.color_bindings);
            color_attachment_count = other.color_attachment_count;
            logical_color_attachments = std::move(other.logical_color_attachments);
            logical_depth_attachment = std::move(other.logical_depth_attachment);
            logical_depth_renderbuffer_id = other.logical_depth_renderbuffer_id;
            logical_dimensions = other.logical_dimensions;
            depth_binding = std::move(other.depth_binding);
            refresh_attachment_caches();
        }
        return *this;
    }

    /*
     * framebuffer_draw_target interface.
     */

    virtual void clear_color(
      std::uint32_t attachment,
      ml::vec4 clear_color) override;
    virtual void clear_color(
      std::uint32_t attachment,
      ml::vec4 clear_color,
      const utils::rect& rect) override;
    virtual void clear_depth(ml::fixed_32_t clear_depth) override;
    virtual void clear_depth(
      ml::fixed_32_t clear_depth,
      const utils::rect& rect) override;
    virtual void merge_color(
      std::uint32_t attachment,
      int x,
      int y,
      const fragment_output& frag,
      bool do_blend,
      blend_func src,
      blend_func dst) override;
    virtual void merge_color_block(
      std::uint32_t attachment,
      int x,
      int y,
      const fragment_output_block& frag,
      bool do_blend,
      blend_func src,
      blend_func dst) override;
    virtual void depth_compare_write(
      int x,
      int y,
      float depth_value,
      comparison_func depth_func,
      bool write_depth,
      bool& write_mask) override;
    virtual void depth_compare_write_block(
      int x,
      int y,
      const std::array<float, 4>& depth_value,
      comparison_func depth_func,
      bool write_depth,
      std::uint8_t& write_mask) override;

    /*
     * framebuffer_object interface.
     */

    /** Reset. */
    void reset()
    {
        for(auto& it: color_bindings)
        {
            it.reset();
        }

        color_attachment_count = 0;
        for(auto& attachment: logical_color_attachments)
        {
            attachment.dimensions = {0, 0};
            attachment.level = 0;
            attachment.texture_id = 0;
        }

        logical_depth_attachment.dimensions = {0, 0};
        logical_depth_attachment.texture_id = 0;
        logical_depth_attachment.level = 0;
        logical_depth_renderbuffer_id = 0;

        logical_dimensions = {0, 0};

        depth_binding.emplace<std::monostate>();
        refresh_attachment_caches();
    }

    /**
     * Set the logical texture attachment.
     *
     * @note Changes to logical state are visible immediately.
     * @param attachment Attachment point.
     * @param texture_id Id of the texture to attach.
     * @param level Mipmap level of the texture to attach.
     * @param width Width of the texture at the mipmap level.
     * @param height Height of the texture at the mipmap level.
     */
    void attach_logical_texture(
      framebuffer_attachment attachment,
      std::uint32_t texture_id,
      std::uint32_t level,
      std::size_t width,
      std::size_t height)
    {
        if(attachment == framebuffer_attachment::depth_attachment)
        {
            logical_depth_attachment.dimensions = {width, height};
            logical_depth_attachment.texture_id = texture_id;
            logical_depth_attachment.level = level;
            logical_depth_renderbuffer_id = 0;
        }
        else
        {
            const auto index = static_cast<std::size_t>(attachment);
            if(index >= logical_color_attachments.size())
            {
                return;
            }
            logical_color_attachments[index].dimensions = {width, height};
            logical_color_attachments[index].texture_id = texture_id;
            logical_color_attachments[index].level = level;
        }
        calculate_logical_dimensions();
    }

    /** Remove logical attachment dimensions immediately. */
    void detach_logical_texture(
      framebuffer_attachment attachment)
    {
        if(attachment == framebuffer_attachment::depth_attachment)
        {
            logical_depth_attachment.dimensions = {0, 0};
            logical_depth_attachment.texture_id = 0;
            logical_depth_attachment.level = 0;
            logical_depth_renderbuffer_id = 0;
        }
        else
        {
            const auto index = static_cast<std::size_t>(attachment);
            if(index >= logical_color_attachments.size())
            {
                return;
            }
            logical_color_attachments[index].dimensions = {0, 0};
            logical_color_attachments[index].texture_id = 0;
            logical_color_attachments[index].level = 0;
        }
        calculate_logical_dimensions();
    }

    /** Remove all logical references to a texture immediately. */
    void detach_logical_texture_resource(std::uint32_t texture_id)
    {
        for(std::size_t index = 0; index < logical_color_attachments.size(); ++index)
        {
            if(logical_color_attachments[index].texture_id == texture_id)
            {
                logical_color_attachments[index].texture_id = 0;
                logical_color_attachments[index].level = 0;
                logical_color_attachments[index].dimensions = {0, 0};
            }
        }
        if(logical_depth_attachment.texture_id == texture_id)
        {
            logical_depth_attachment.texture_id = 0;
            logical_depth_attachment.level = 0;
            logical_depth_attachment.dimensions = {0, 0};
        }
        calculate_logical_dimensions();
    }

    /** Update logical attachment dimensions after a texture image is redefined. */
    void update_logical_texture_dimensions(
      std::uint32_t texture_id,
      std::size_t width,
      std::size_t height)
    {
        for(std::size_t index = 0; index < logical_color_attachments.size(); ++index)
        {
            if(logical_color_attachments[index].texture_id == texture_id)
            {
                const auto level = logical_color_attachments[index].level;
                logical_color_attachments[index].dimensions = {
                  width >> level,
                  height >> level};
            }
        }

        if(logical_depth_attachment.texture_id == texture_id)
        {
            logical_depth_attachment.dimensions = {
              width >> logical_depth_attachment.level,
              height >> logical_depth_attachment.level};
        }
        calculate_logical_dimensions();
    }

    /** Set immediate logical dimensions for a depth renderbuffer attachment. */
    void set_logical_depth_renderbuffer_attachment(
      std::uint32_t renderbuffer_id,
      std::size_t width,
      std::size_t height)
    {
        logical_depth_attachment.dimensions = {width, height};
        logical_depth_attachment.texture_id = 0;
        logical_depth_attachment.level = 0;
        logical_depth_renderbuffer_id = renderbuffer_id;
        calculate_logical_dimensions();
    }

    /** Remove a deleted depth renderbuffer from the immediate logical state. */
    void detach_logical_depth_renderbuffer_resource(
      std::uint32_t renderbuffer_id)
    {
        if(logical_depth_renderbuffer_id == renderbuffer_id)
        {
            logical_depth_attachment.dimensions = {0, 0};
            logical_depth_renderbuffer_id = 0;
            calculate_logical_dimensions();
        }
    }

    /** Refresh physical attachment pointers after a texture image update. */
    void refresh_texture_attachments(
      std::uint32_t texture_id,
      texture_2d* texture)
    {
        for(auto& binding: color_bindings)
        {
            if(binding && binding->tex_id == texture_id)
            {
                binding->attach(texture, binding->level);
            }
        }

        if(auto* depth_texture = std::get_if<depth_texture_attachment_binding>(&depth_binding);
           depth_texture && depth_texture->tex_id == texture_id)
        {
            depth_texture->attach(texture, depth_texture->level);
            refresh_attachment_caches();
        }
        calculate_effective_dimensions();
    }

    /** Detach texture references before their storage is destroyed. */
    void detach_texture_resource(
      std::uint32_t texture_id)
    {
        for(auto& binding: color_bindings)
        {
            if(binding && binding->tex_id == texture_id)
            {
                binding.reset();
                --color_attachment_count;
            }
        }
        if(auto* depth_texture = std::get_if<depth_texture_attachment_binding>(&depth_binding);
           depth_texture && depth_texture->tex_id == texture_id)
        {
            depth_binding.emplace<std::monostate>();
            refresh_attachment_caches();
        }
        detach_logical_texture_resource(texture_id);
        calculate_effective_dimensions();
    }

    /** Detach renderbuffer references before its storage is destroyed. */
    void detach_depth_renderbuffer_resource(
      std::uint32_t renderbuffer_id)
    {
        if(auto* depth_renderbuffer = std::get_if<depth_renderbuffer_attachment_binding>(&depth_binding);
           depth_renderbuffer && depth_renderbuffer->attachment_id == renderbuffer_id)
        {
            depth_binding.emplace<std::monostate>();
            refresh_attachment_caches();
            calculate_effective_dimensions();
        }
        detach_logical_depth_renderbuffer_resource(renderbuffer_id);
    }

    /** Height used by immediate API state conversion. */
    int get_logical_height() const
    {
        return logical_dimensions.height;
    }

    /** Attach at texture. */
    void attach_texture(
      framebuffer_attachment attachment,
      texture_2d* tex,
      std::uint32_t level)
    {
        const auto index = static_cast<std::size_t>(attachment);
        if(index < color_bindings.size())
        {
            const bool was_empty = !color_bindings[index];
            const int old_width = was_empty ? 0 : color_bindings[index]->info.width;
            const int old_height = was_empty ? 0 : color_bindings[index]->info.height;
            if(was_empty)
            {
                color_bindings[index].emplace();
                ++color_attachment_count;
            }

            color_bindings[index]->attach(tex, level);

            const auto width = color_bindings[index]->info.width;
            const auto height = color_bindings[index]->info.height;
            if(width <= 0 || height <= 0
               || (!was_empty
                   && (width > old_width || height > old_height)))
            {
                calculate_effective_dimensions();
            }
            else
            {
                update_effective_dimensions_incremental(width, height);
            }
        }
    }

    /** Detach a texture. */
    void detach_texture(framebuffer_attachment attachment)
    {
        auto index = static_cast<std::size_t>(attachment);
        if(index < color_bindings.size() && color_bindings[index])
        {
            color_bindings[index]->detach();
            color_bindings[index].reset();
            --color_attachment_count;

            calculate_effective_dimensions();
        }
    }

    /**
     * Attach a depth renderbuffer.
     *
     * TODO Documentation.
     */
    void attach_depth_renderbuffer(
      std::uint32_t attachment_id,
      attachment_depth* attachment)
    {
        depth_binding.emplace<depth_renderbuffer_attachment_binding>();
        std::get<depth_renderbuffer_attachment_binding>(depth_binding)
          .attach(attachment_id, attachment);

        refresh_attachment_caches();
        calculate_effective_dimensions();
    }

    /**
     * Attach a depth texture.
     *
     * TODO Documentation.
     */
    void attach_depth_texture(
      texture_2d* texture,
      std::uint32_t level)
    {
        depth_binding.emplace<depth_texture_attachment_binding>();
        std::get<depth_texture_attachment_binding>(depth_binding)
          .attach(texture, level);

        refresh_attachment_caches();
        calculate_effective_dimensions();
    }

    /** Detach the current depth binding. */
    void detach_depth()
    {
        depth_binding.emplace<std::monostate>();

        refresh_attachment_caches();
        calculate_effective_dimensions();
    }
};

} /* namespace impl */

} /* namespace swr */
