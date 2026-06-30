/**
 * swr - a software rasterizer
 *
 * output buffers for rendering.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <optional>
#include <variant>

namespace swr
{

namespace impl
{

/** output after fragment processing, before merging. contains color and depth values, along with write flags. */
struct fragment_output
{
    /*
     * flag values.
     */
    static const std::uint32_t fof_write_color = 1;   /** write color value. */
    static const std::uint32_t fof_write_depth = 2;   /** write depth value. */
    static const std::uint32_t fof_write_stencil = 4; /** write stencil value. */

    /** color produced by the fragment shader. */
    ml::vec4 color;

    /** write flags. */
    std::uint32_t write_flags{0};

    /** default constructor. */
    fragment_output() = default;
};

/** output after fragment processing, before merging. contains color and depth values, along with write masks for 2x2 blocks. */
struct fragment_output_block
{
    /** 2x2 block of colors produced by the fragment shader. */
    std::array<ml::vec4, 4> color;

    /*
     * masks.
     */

    /** whether the color values should be written to the color buffer. */
    std::uint8_t write_color = 0b1111;

    /** whether the stencil values should be written to the stencil buffer (currently unused). */
    std::uint8_t write_stencil = 0b0; /* currently unused */

    /** default constructor. */
    fragment_output_block() = default;

    /** initialize color mask. */
    fragment_output_block(std::uint8_t mask)
    {
        write_color = mask;
    }
};

/** framebuffer attachment info. */
template<typename T>
struct attachment_info
{
    using value_type = T;

    /** width of the attachment. Has to be aligned on rasterizer_block_size.  */
    int width{0};

    /** height of the attachment. Has to be aligned on rasterizer_block_size. */
    int height{0};

    /**
     * attachment pitch. the interpretation depends on the buffer type:
     * for std::uint32_t color buffers, this is the buffer width, in bytes.
     * for ml::vec4 textures, this is the difference between two lines, measured in sizeof(ml::vec4).
     */
    int pitch{0};

    /** pointer to the attachment's data. */
    T* data_ptr{nullptr};

    /** default constructor. */
    attachment_info() = default;

    /** reset the attachment, i.e., clear width, height, pitch and data_ptr. */
    void reset()
    {
        width = 0;
        height = 0;
        pitch = 0;
        data_ptr = nullptr;
    }

    /** set up all parameters. */
    void setup(
      int in_width,
      int in_height,
      int in_pitch,
      T* in_data_ptr)
    {
        width = in_width;
        height = in_height;
        pitch = in_pitch;
        data_ptr = in_data_ptr;
    }
};

/** A fixed-point depth buffer attachment. */
struct attachment_depth
{
    using value_type = ml::fixed_32_t;

    /** attachment info. */
    attachment_info<value_type> info;

    /** The depth buffer data. */
    utils::sse_aligned_vector<value_type> data;

    /** free resources. */
    void reset()
    {
        info.reset();

        data.clear();
        data.shrink_to_fit();
    }

    /** allocate the buffer. */
    void allocate(
      int in_width,
      int in_height)
    {
        assert(in_width > 0 && in_height > 0);
        data.resize(in_width * in_height);
        info.setup(
          in_width,
          in_height,
          in_width * sizeof(value_type),
          data.data());
    }
};

/** A 32-bit color buffer. */
struct attachment_color_buffer
{
    using value_type = std::uint32_t;

    /** attachment info. */
    attachment_info<value_type> info;

    /** pixel format converter. needs explicit initialization. */
    pixel_format_converter converter;

    /** reset buffer. */
    void reset()
    {
        info.reset();
        converter.set_pixel_format(
          pixel_format_descriptor::named_format(
            pixel_format::unsupported));
    }

    /** attach externally managed buffer. */
    void attach(
      int width,
      int height,
      int pitch,
      value_type* ptr)
    {
        info.setup(width, height, pitch, ptr);

        if(width <= 0
           || height <= 0
           || pitch <= 0
           || ptr == nullptr)
        {
            info.reset();
        }
    }

    /** detach external buffer. */
    void detach()
    {
        attach(0, 0, 0, nullptr);
    }

    /** validate. */
    bool is_valid() const
    {
        return info.data_ptr && info.pitch > 0;
    }
};

/** Non-owning color texture binding. */
struct texture_attachment_binding
{
    using value_type = ml::vec4;

    /** attachment info. */
    attachment_info<value_type> info;

    /** attached texture id. */
    std::uint32_t tex_id{default_tex_id};

    /** attached texture pointer. */
    texture_2d* tex{nullptr};

    /** the mipmap level we are writing to. */
    std::uint32_t level{0};

    /** release binding state. */
    void reset()
    {
        detach();
        info.reset();
    }

    /** bind texture. */
    void attach(
      texture_2d* in_tex,
      std::uint32_t in_level = 0)
    {
        tex_id = default_tex_id;
        tex = nullptr;
        level = 0;

        info.reset();

        auto* color_texture = in_tex ? in_tex->as_texture_color_2d() : nullptr;
        if(color_texture
           && in_level < color_texture->data.data_ptrs.size())
        {
            tex_id = in_tex->id;
            tex = in_tex;
            level = in_level;

            const auto pitch = in_tex->mip_pitch(in_level);
            info.setup(
              in_tex->width >> in_level,
              in_tex->height >> in_level,
              static_cast<int>(pitch),
              color_texture->data.data_ptrs[in_level]);
        }
    }

    /** detach texture. same as `attach(nullptr)`. */
    void detach()
    {
        attach(nullptr);
    }

    /** check if a non-default texture was bound, and if it is still valid. */
    bool is_valid() const;
};

/** Non-owning depth texture binding. */
struct depth_texture_attachment_binding
{
    using value_type = ml::fixed_32_t;

    /** attachment info. */
    attachment_info<value_type> info;

    /** attached texture id. */
    std::uint32_t tex_id{default_tex_id};

    /** attached texture pointer. */
    texture_2d* tex{nullptr};

    /** the mipmap level we are writing to. */
    std::uint32_t level{0};

    /** release binding state. */
    void reset()
    {
        detach();
        info.reset();
    }

    /** bind texture. */
    void attach(
      texture_2d* in_tex,
      std::uint32_t in_level = 0)
    {
        tex_id = default_tex_id;
        tex = nullptr;
        level = 0;

        info.reset();

        auto* depth_texture = in_tex ? in_tex->as_texture_depth_2d() : nullptr;
        if(depth_texture
           && in_level < depth_texture->data.data_ptrs.size())
        {
            tex_id = in_tex->id;
            tex = in_tex;
            level = in_level;

            const auto pitch = in_tex->mip_pitch(in_level);

            info.setup(
              in_tex->width >> in_level,
              in_tex->height >> in_level,
              static_cast<int>(pitch * sizeof(value_type)),
              depth_texture->data.data_ptrs[in_level]);
        }
    }

    /** detach texture. same as attach(nullptr) */
    void detach()
    {
        attach(nullptr);
    }

    /** check if a non-default texture was bound, and if it is still valid. */
    bool is_valid() const;
};

/** Non-owning depth renderbuffer binding. */
struct depth_renderbuffer_attachment_binding
{
    using value_type = ml::fixed_32_t;

    /** attachment info. */
    attachment_info<value_type> info;

    /** attached depth renderbuffer id. */
    std::uint32_t attachment_id{0};

    /** attached depth renderbuffer pointer. */
    attachment_depth* attachment{nullptr};

    /** release binding state. */
    void reset()
    {
        detach();
        info.reset();
    }

    /** bind renderbuffer. */
    void attach(
      std::uint32_t in_attachment_id,
      attachment_depth* in_attachment)
    {
        attachment_id = 0;
        attachment = nullptr;

        info.reset();

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

/** framebuffer properties. */
struct framebuffer_properties
{
    /** (effective) width of the framebuffer target. */
    int width{0};

    /** (effective) height of the framebuffer target. */
    int height{0};

    /** reset dimensions. */
    void reset(int in_width = 0, int in_height = 0)
    {
        width = in_width;
        height = in_height;
    }
};

/** framebuffer draw target. */
struct framebuffer_draw_target
{
    /** the target's properties. */
    framebuffer_properties properties;

    /** virtual destructor. */
    virtual ~framebuffer_draw_target() = default;

    /** clear a color attachment. fails silently if the attachment is not available. */
    virtual void clear_color(
      std::uint32_t attachment,
      ml::vec4 clear_color) = 0;

    /** clear part of a color attachment. fails silently if the attachment is not available or if the supplied rectangle was invalid. */
    virtual void clear_color(
      std::uint32_t attachment,
      ml::vec4 clear_color,
      const utils::rect& rect) = 0;

    /** clear the depth attachment. fails silently if the attachment is not available. */
    virtual void clear_depth(
      ml::fixed_32_t clear_depth) = 0;

    /** clear the depth attachment. fails silently if the attachment is not available of if the supplied rectangle was invalid. */
    virtual void clear_depth(
      ml::fixed_32_t clear_depth,
      const utils::rect& rect) = 0;

    /** merge a color value while respecting blend modes, if requested. silently fails for invalid attachments. */
    virtual void merge_color(
      std::uint32_t attachment,
      int x,
      int y,
      const fragment_output& frag,
      bool do_blend,
      blend_func src,
      blend_func dst) = 0;

    /** merge a 2x2 block of color values while respecting blend modes, if requested. silently fails for invalid attachments. */
    virtual void merge_color_block(
      std::uint32_t attachment,
      int x,
      int y,
      const fragment_output_block& frag,
      bool do_blend,
      blend_func src,
      blend_func dst) = 0;

    /**
     * if a depth buffer is available, perform a depth comparison and (also depending on write_mask) possibly write a new value to the depth buffer.
     * if the depth test failed, write_mask is set to false, and true otherwise. sets write_mask to true if no depth buffer was available.
     */
    virtual void depth_compare_write(
      int x,
      int y,
      float depth_value,
      comparison_func depth_func,
      bool write_depth,
      bool& write_mask) = 0;

    /**
     * if a depth buffer is available, perform a depth comparison and (also depending on write_mask) possibly write new values to the depth buffer.
     * if a depth test failed, correpsonding entry in write_mask is set to false, and true otherwise. sets all write_mask entries
     * to true if no depth buffer was available.
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
struct default_framebuffer final : public framebuffer_draw_target
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
        properties.reset();
        color_buffer.reset();
        depth_buffer.reset();
    }

    /** set up the default framebuffer. */
    void setup(
      int width,
      int height,
      int pitch,
      pixel_format pixel_format,
      std::uint32_t* data)
    {
        reset();
        color_buffer.attach(width, height, pitch, data);
        color_buffer.converter.set_pixel_format(
          pixel_format_descriptor::named_format(
            pixel_format));
        depth_buffer.allocate(width, height);
        properties.reset(width, height);
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

/** framebuffer objects. */
class framebuffer_object final : public framebuffer_draw_target
{
    using depth_binding_variant = std::variant<
      std::monostate,
      depth_renderbuffer_attachment_binding,
      depth_texture_attachment_binding>;

    /** id of this object. */
    std::uint32_t id{0};

    /** color attachments. */
    std::array<
      std::optional<texture_attachment_binding>,
      swr::limits::max::color_attachments>
      color_bindings;

    /** current color attachment count. */
    std::uint32_t color_attachment_count{0};

    /** depth attachment binding. */
    depth_binding_variant depth_binding;

    /** Cached active depth attachment info for hot paths. */
    const attachment_info<ml::fixed_32_t>* active_depth_attachment_info{nullptr};

    /** check whether a depth attachment is currently bound. */
    bool has_depth_binding() const
    {
        return !std::holds_alternative<std::monostate>(depth_binding);
    }

    /** refresh cached pointers that hot paths rely on. */
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

    /** return the active depth attachment info. */
    const attachment_info<ml::fixed_32_t>* get_depth_attachment_info() const
    {
        return active_depth_attachment_info;
    }

    /** check if the active depth binding is valid. */
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

    /** calculate effective width and height. */
    void calculate_effective_dimensions()
    {
        int width = -1;
        int height = -1;

        if(color_attachment_count)
        {
            for(const auto& it: color_bindings)
            {
                if(it)
                {
                    width = (width < 0)
                              ? it->info.width
                              : std::min(width, it->info.width);
                    height = (height < 0)
                               ? it->info.height
                               : std::min(height, it->info.height);
                }
            }
        }

        const auto* depth_info = get_depth_attachment_info();
        int depth_width = (depth_info == nullptr)
                            ? -1
                            : depth_info->width;
        int depth_height = (depth_info == nullptr)
                             ? -1
                             : depth_info->height;

        // set the effective width and height. we handle all cases except both widths/heights from above being negative.
        width = (width > 0 && depth_width > 0)
                  ? std::min(width, depth_width)
                  : std::max(width, depth_width);
        height = (height > 0 && depth_height > 0)
                   ? std::min(height, depth_height)
                   : std::max(height, depth_height);

        // if the widths/heights from above were negative, then the respective effective size is zero.
        properties.reset(
          std::max(width, 0),
          std::max(height, 0));
    }

public:
    /** default constructor. */
    framebuffer_object() = default;

    /** disallow copying. */
    framebuffer_object(const framebuffer_object&) = delete;

    /** move constructor. */
    framebuffer_object(framebuffer_object&& other)
    : id{other.id}
    , color_bindings{std::move(other.color_bindings)}
    , color_attachment_count{other.color_attachment_count}
    , depth_binding{std::move(other.depth_binding)}
    {
        refresh_attachment_caches();
    }

    /** virtual destructor. */
    virtual ~framebuffer_object() = default;

    /** disallow copying. */
    framebuffer_object& operator=(const framebuffer_object&) = delete;

    /** move object. */
    framebuffer_object& operator=(framebuffer_object&& other)
    {
        if(this != &other)
        {
            id = other.id;
            color_bindings = std::move(other.color_bindings);
            color_attachment_count = other.color_attachment_count;
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

    /** reset. */
    void reset(int in_id = 0)
    {
        for(auto& it: color_bindings)
        {
            it.reset();
        }
        color_attachment_count = 0;

        depth_binding.emplace<std::monostate>();
        refresh_attachment_caches();

        // set/reset id.
        id = in_id;
    }

    /** attach at texture. */
    void attach_texture(
      framebuffer_attachment attachment,
      texture_2d* tex, int level)
    {
        auto index = static_cast<int>(attachment);
        if(index >= 0
           && index < swr::limits::max::color_attachments)
        {
            if(!color_bindings[index])
            {
                color_bindings[index].emplace();
                color_bindings[index]->attach(tex, level);

                ++color_attachment_count;
            }
            else
            {
                color_bindings[index]->attach(tex, level);
            }

            calculate_effective_dimensions();
        }
    }

    /** detach a texture. */
    void detach_texture(framebuffer_attachment attachment)
    {
        auto index = static_cast<std::size_t>(attachment);
        if(index < swr::limits::max::color_attachments
           && color_bindings[index])
        {
            color_bindings[index]->detach();
            color_bindings[index].reset();
            --color_attachment_count;

            calculate_effective_dimensions();
        }
    }

    /** bind a depth renderbuffer. */
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

    /** bind a depth texture. */
    void attach_depth_texture(
      texture_2d* texture,
      int level)
    {
        depth_binding.emplace<depth_texture_attachment_binding>();
        std::get<depth_texture_attachment_binding>(depth_binding)
          .attach(texture, level);

        refresh_attachment_caches();
        calculate_effective_dimensions();
    }

    /** detach the current depth binding. */
    void detach_depth()
    {
        depth_binding.emplace<std::monostate>();

        refresh_attachment_caches();
        calculate_effective_dimensions();
    }

    /** check completeness. */
    bool is_complete() const
    {
        const bool has_color_attachment = color_attachment_count != 0;
        const bool has_depth_attachment = has_depth_binding();
        if(!has_color_attachment
           && !has_depth_attachment)
        {
            return false;
        }

        // attachment completeness.
        for(auto& it: color_bindings)
        {
            if(it
               && (it->info.width == 0
                   || it->info.height == 0
                   || !it->is_valid()))
            {
                return false;
            }
        }

        if(has_depth_attachment
           && !has_valid_depth_binding())
        {
            return false;
        }

        return true;
    }
};

} /* namespace impl */

} /* namespace swr */
