/**
 * swr - a software rasterizer
 *
 * output buffers for rendering.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

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
    static const uint32_t fof_write_color = 1;   /** write color value. */
    static const uint32_t fof_write_depth = 2;   /** write depth value. */
    static const uint32_t fof_write_stencil = 4; /** write stencil value. */

    /** color produced by the fragment shader. */
    ml::vec4 color;

    /** write flags. */
    uint32_t write_flags{0};

    /** default constructor. */
    fragment_output() = default;
};

/** output after fragment processing, before merging. contains color and depth values, along with write masks for 2x2 blocks. */
struct fragment_output_block
{
    /** 2x2 block of colors produced by the fragment shader. */
    ml::vec4 color[4];

    /*
     * masks.
     */

    /** whether the color values should be written to the color buffer. */
    bool write_color[4] = {true, true, true, true};

    /** whether the stencil values should be written to the stencil buffer (currently unused). */
    bool write_stencil[4] = {false, false, false, false}; /* currently unused */

    /** default constructor. */
    fragment_output_block() = default;

    /** initialize color mask. */
    fragment_output_block(bool mask0, bool mask1, bool mask2, bool mask3)
    {
        write_color[0] = mask0;
        write_color[1] = mask1;
        write_color[2] = mask2;
        write_color[3] = mask3;
    }
};

/** framebuffer attachment info. */
template<typename T>
struct attachment_info
{
    /** width of the attachment. Has to be aligned on rasterizer_block_size.  */
    int width{0};

    /** height of the attachment. Has to be aligned on rasterizer_block_size. */
    int height{0};

    /**
     * attachment pitch. the interpretation depends on the buffer type:
     * for uint32_t color buffers, this is the buffer width, in bytes.
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
    void setup(int in_width, int in_height, int in_pitch, T* in_data_ptr)
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
    /** attachment info. */
    attachment_info<ml::fixed_32_t> info;

    /** The depth buffer data. */
    std::vector<ml::fixed_32_t> data;

    /** free resources. */
    void reset()
    {
        info.reset();

        data.clear();
        data.shrink_to_fit();
    }

    /** allocate the buffer. */
    void allocate(int in_width, int in_height)
    {
        assert(in_width > 0 && in_height > 0);
        info.setup(in_width, in_height, in_width * sizeof(ml::fixed_32_t), utils::align_vector(utils::alignment::sse, in_width * in_height, data));
    }
};

/** A 32-bit color buffer. */
struct attachment_color_buffer
{
    /** attachment info. */
    attachment_info<std::uint32_t> info;

    /** pixel format converter. needs explicit initialization. */
    pixel_format_converter converter;

    /** reset buffer. */
    void reset()
    {
        info.reset();
        converter.set_pixel_format(pixel_format_descriptor::named_format(pixel_format::unsupported));
    }

    /** attach externally managed buffer. */
    void attach(int width, int height, int pitch, std::uint32_t* ptr)
    {
        info.setup(width, height, pitch, ptr);

        if(width <= 0 || height <= 0 || pitch <= 0 || !ptr)
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

/** texture attachment. */
struct attachment_texture
{
    /** attachment info. */
    attachment_info<ml::vec4> info;

    /** attached texture id. */
    uint32_t tex_id{default_tex_id};

    /** attached texture pointer. */
    texture_2d* tex{nullptr};

    /** the mipmap level we are writing to. */
    uint32_t level{0};

    /** free resources. */
    void reset()
    {
        detach();
        info.reset();
    }

    /** attach texture. */
    void attach(texture_2d* in_tex, std::uint32_t in_level = 0)
    {
        tex_id = default_tex_id;
        tex = nullptr;
        level = 0;

        info.reset();

        if(in_tex && in_level < in_tex->data.data_ptrs.size())
        {
            tex_id = in_tex->id;
            tex = in_tex;
            level = in_level;

            // if we have mipmaps for this texture, the pitch is 1.5*in_tex->width.
            auto pitch = in_tex->width;
            if(in_tex->data.data_ptrs.size() > 1)
            {
                pitch += in_tex->width >> 1;
            }

            info.setup(in_tex->width >> in_level, in_tex->height >> in_level, pitch, in_tex->data.data_ptrs[in_level]);
        }
    }

    /** detach texture. same as attach(nullptr) */
    void detach()
    {
        attach(nullptr);
    }

    /** check if a non-default texture was attached, and if it is still valid. */
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
    virtual void clear_color(uint32_t attachment, ml::vec4 clear_color) = 0;

    /** clear part of a color attachment. fails silently if the attachment is not available or if the supplied rectangle was invalid. */
    virtual void clear_color(uint32_t attachment, ml::vec4 clear_color, const utils::rect& rect) = 0;

    /** clear the depth attachment. fails silently if the attachment is not available. */
    virtual void clear_depth(ml::fixed_32_t clear_depth) = 0;

    /** clear the depth attachment. fails silently if the attachment is not available of if the supplied rectangle was invalid. */
    virtual void clear_depth(ml::fixed_32_t clear_depth, const utils::rect& rect) = 0;

    /** merge a color value while respecting blend modes, if requested. silently fails for invalid attachments. */
    virtual void merge_color(uint32_t attachment, int x, int y, const fragment_output& frag, bool do_blend, blend_func src, blend_func dst) = 0;

    /** merge a 2x2 block of color values while respecting blend modes, if requested. silently fails for invalid attachments. */
    virtual void merge_color_block(uint32_t attachment, int x, int y, const fragment_output_block& frag, bool do_blend, blend_func src, blend_func dst) = 0;

    /**
     * if a depth buffer is available, perform a depth comparison and (also depending on write_mask) possibly write a new value to the depth buffer.
     * if the depth test failed, write_mask is set to false, and true otherwise. sets write_mask to true if no depth buffer was available.
     */
    virtual void depth_compare_write(int x, int y, float depth_value, comparison_func depth_func, bool write_depth, bool& write_mask) = 0;

    /**
     * if a depth buffer is available, perform a depth comparison and (also depending on write_mask) possibly write new values to the depth buffer.
     * if a depth test failed, correpsonding entry in write_mask is set to false, and true otherwise. sets all write_mask entries
     * to true if no depth buffer was available.
     */
    virtual void depth_compare_write_block(int x, int y, float depth_value[4], comparison_func depth_func, bool write_depth, bool write_mask[4]) = 0;
};

/** default framebuffer. */
struct default_framebuffer : public framebuffer_draw_target
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

    virtual void clear_color(uint32_t attachment, ml::vec4 clear_color) override;
    virtual void clear_color(uint32_t attachment, ml::vec4 clear_color, const utils::rect& rect) override;
    virtual void clear_depth(ml::fixed_32_t clear_depth) override;
    virtual void clear_depth(ml::fixed_32_t clear_depth, const utils::rect& rect) override;
    virtual void merge_color(uint32_t attachment, int x, int y, const fragment_output& frag, bool do_blend, blend_func src, blend_func dst) override;
    virtual void merge_color_block(uint32_t attachment, int x, int y, const fragment_output_block& frag, bool do_blend, blend_func src, blend_func dst) override;
    virtual void depth_compare_write(int x, int y, float depth_value, comparison_func depth_func, bool write_depth, bool& write_mask) override;
    virtual void depth_compare_write_block(int x, int y, float depth_value[4], comparison_func depth_func, bool write_depth, bool write_mask[4]) override;

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
    void setup(int width, int height, int pitch, pixel_format pixel_format, std::uint32_t* data)
    {
        reset();
        color_buffer.attach(width, height, pitch, data);
        color_buffer.converter.set_pixel_format(pixel_format_descriptor::named_format(pixel_format));
        depth_buffer.allocate(width, height);
        properties.reset(width, height);
    }

    /** update the color attachment's format. */
    void set_color_pixel_format(pixel_format name)
    {
        color_buffer.converter.set_pixel_format(pixel_format_descriptor::named_format(name));
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

/** maximum number of color attachments. FIXME this should probably be put somewhere else? */
// this must be compatible with the values in framebuffer_attachment.
constexpr int max_color_attachments = 8;

/** framebuffer objects. */
class framebuffer_object : public framebuffer_draw_target
{
    /** id of this object. */
    uint32_t id{0};

    /** color attachments. */
    std::array<std::unique_ptr<attachment_texture>, max_color_attachments> color_attachments;

    /** current color attachment count. */
    uint32_t color_attachment_count{0};

    /** depth attachment. */
    attachment_depth* depth_attachment{nullptr};

    // TODO add stencil attachment.

    /** calculate effective width and height. */
    void calculate_effective_dimensions()
    {
        int width = -1;
        int height = -1;

        if(color_attachment_count)
        {
            for(const auto& it: color_attachments)
            {
                if(it)
                {
                    width = (width < 0) ? it->info.width : std::min(width, it->info.width);
                    height = (height < 0) ? it->info.height : std::min(height, it->info.height);
                }
            }
        }

        int depth_width = (depth_attachment == nullptr) ? -1 : depth_attachment->info.width;
        int depth_height = (depth_attachment == nullptr) ? -1 : depth_attachment->info.height;

        // set the effective width and height. we handle all cases except both widths/heights from above being negative.
        width = (width > 0 && depth_width > 0) ? std::min(width, depth_width) : std::max(width, depth_width);
        height = (height > 0 && depth_height > 0) ? std::min(height, depth_height) : std::max(height, depth_height);

        // if the widths/heights from above were negative, then the respective effective size is zero.
        properties.reset(std::max(width, 0), std::max(height, 0));
    }

public:
    /** default constructor. */
    framebuffer_object() = default;

    /** disallow copying. */
    framebuffer_object(const framebuffer_object&) = delete;

    /** default move constructor. */
    framebuffer_object(framebuffer_object&&) = default;

    /** virtual destructor. */
    virtual ~framebuffer_object() = default;

    /** disallow copying. */
    framebuffer_object& operator=(const framebuffer_object&) = delete;

    /** move object. */
    framebuffer_object& operator=(framebuffer_object&& other) = default;

    /*
     * framebuffer_draw_target interface.
     */

    virtual void clear_color(uint32_t attachment, ml::vec4 clear_color) override;
    virtual void clear_color(uint32_t attachment, ml::vec4 clear_color, const utils::rect& rect) override;
    virtual void clear_depth(ml::fixed_32_t clear_depth) override;
    virtual void clear_depth(ml::fixed_32_t clear_depth, const utils::rect& rect) override;
    virtual void merge_color(uint32_t attachment, int x, int y, const fragment_output& frag, bool do_blend, blend_func src, blend_func dst) override;
    virtual void merge_color_block(uint32_t attachment, int x, int y, const fragment_output_block& frag, bool do_blend, blend_func src, blend_func dst) override;
    virtual void depth_compare_write(int x, int y, float depth_value, comparison_func depth_func, bool write_depth, bool& write_mask) override;
    virtual void depth_compare_write_block(int x, int y, float depth_value[4], comparison_func depth_func, bool write_depth, bool write_mask[4]) override;

    /*
     * framebuffer_object interface.
     */

    /** reset. */
    void reset(int in_id = 0)
    {
        for(auto& it: color_attachments)
        {
            // note: this deletes the object managed by the unique_ptr.
            it.reset();
        }
        color_attachment_count = 0;

        // the depth attachment is not managed by framebuffer_object.
        depth_attachment = nullptr;

        // set/reset id.
        id = in_id;
    }

    /** attach at texture. */
    void attach_texture(framebuffer_attachment attachment, texture_2d* tex, int level)
    {
        auto index = static_cast<int>(attachment);
        if(index >= 0 && index < max_color_attachments)
        {
            if(!color_attachments[index])
            {
                color_attachments[index] = std::make_unique<attachment_texture>();
                color_attachments[index]->attach(tex, level);

                ++color_attachment_count;

                if(!depth_attachment)
                {
                    // if this is the first attachment, we need to set the effective width and height.
                    properties.reset(color_attachments[index]->info.width, color_attachments[index]->info.height);
                }
                else
                {
                    // update effective dimensions.
                    properties.reset(std::min(depth_attachment->info.width, color_attachments[index]->info.width), std::min(depth_attachment->info.height, color_attachments[index]->info.height));
                }
            }
            else
            {
                color_attachments[index]->attach(tex, level);

                // update effective dimensions.
                int old_width = properties.width;
                int old_height = properties.height;
                properties.reset(std::min(old_width, color_attachments[index]->info.width), std::min(old_height, color_attachments[index]->info.height));
            }
        }
    }

    /** detach a texture. */
    void detach_texture(framebuffer_attachment attachment)
    {
        auto index = static_cast<std::size_t>(attachment);
        if(index < max_color_attachments && color_attachments[index])
        {
            color_attachments[index]->detach();
            color_attachments[index].reset();
            --color_attachment_count;

            calculate_effective_dimensions();
        }
    }

    /** attach a depth buffer. */
    void attach_depth(attachment_depth* attachment)
    {
        depth_attachment = attachment;

        // if there were no color attachments, set effective width and height. otherwise, update it.
        if(!std::count_if(color_attachments.begin(), color_attachments.end(), [](const auto& c) -> bool
                          { return static_cast<bool>(c); }))
        {
            properties.reset(depth_attachment->info.width, depth_attachment->info.height);
        }
        else
        {
            // update effective dimensions.
            properties.reset(std::min(properties.width, depth_attachment->info.width), std::min(properties.height, depth_attachment->info.height));
        }
    }

    /** detach a depth buffer. */
    void detach_depth()
    {
        attach_depth(nullptr);
        calculate_effective_dimensions();
    }

    /** check completeness. */
    bool is_complete() const
    {
        if(color_attachments.size() == 0)
        {
            return false;
        }

        // attachment completeness.
        for(auto& it: color_attachments)
        {
            if(it && (it->info.width == 0 || it->info.height == 0 || !it->is_valid()))
            {
                return false;
            }
        }

        if(depth_attachment && (depth_attachment->info.width == 0 || depth_attachment->info.height == 0))
        {
            return false;
        }

        return false;
    }
};

} /* namespace impl */

} /* namespace swr */
