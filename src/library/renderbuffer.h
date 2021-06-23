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
    /** color produced by the fragment shader. */
    ml::vec4 color;

    /*
      * flags.
      */

    /** whether the color value should be written to the color buffer. */
    bool write_color{false};

    /** whether the stencil value should be written to the stencil buffer (currently unused). */
    bool write_stencil{false};

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
    bool write_color[4] = {false, false, false, false};

    /** whether the stencil values should be written to the stencil buffer (currently unused). */
    bool write_stencil[4] = {false, false, false, false}; /* currently unused */

    /** default constructor. */
    fragment_output_block() = default;
};

/** Render buffers. */
template<typename T>
struct renderbuffer
{
    using value_type = T;

    /** Width of the allocated color- and depth buffers. Has to be aligned on rasterizer_block_size.  */
    int width = 0;

    /** Height of the allocated color- and depth buffers. Has to be aligned on rasterizer_block_size. */
    int height = 0;

    /** Buffer width, in bytes. */
    int pitch = 0;

    /** Pointer to the buffer data. */
    T* data_ptr = nullptr;

    /** reset the renderbuffer, i.e., clear width, height, pitch and data_ptr. */
    void reset()
    {
        width = 0;
        height = 0;
        pitch = 0;
        data_ptr = nullptr;
    }

    /** Clear the buffer. */
    void clear(const T& v);

    /** get pointer to the buffer at coordinate (x,y). */
    T* at(int x, int y) const
    {
        return &data_ptr[y * width + x];
    }

    /** get four pointers to a 2x2 block with upper-left corner (x,y). */
    void at(int x, int y, T* ptr[4]) const
    {
        ptr[0] = &data_ptr[y * width + x];
        ptr[1] = ptr[0] + 1;
        ptr[2] = ptr[0] + width;
        ptr[3] = ptr[2] + 1;
    }
};

/* Generic buffer clearing function. */
template<typename T>
inline void renderbuffer<T>::clear(const T& v)
{
    if(data_ptr)
    {
        std::fill_n(data_ptr, pitch * height, v);
    }
}

/** A fixed-point depth buffer. */
struct depth_buffer : public renderbuffer<ml::fixed_32_t>
{
    /** The depth buffer data. */
    std::vector<ml::fixed_32_t> data;

    /** free resources. */
    void reset()
    {
        renderbuffer<ml::fixed_32_t>::reset();

        data.clear();
        data.shrink_to_fit();
    }

    /** allocate the buffer. */
    void allocate(int in_width, int in_height)
    {
        assert(in_width > 0 && in_height > 0);

        width = in_width;
        height = in_height;
        pitch = in_width * sizeof(ml::fixed_32_t);

        data_ptr = utils::align_vector(utils::alignment::sse, in_width * in_height, data);
    }
};

/* Specialized buffer clearing function. */
template<>
inline void renderbuffer<ml::fixed_32_t>::clear(const ml::fixed_32_t& v)
{
    if(data_ptr)
    {
        utils::memset32(reinterpret_cast<uint32_t*>(data_ptr), pitch * height, ml::unwrap(v));
    }
}

/** A 32-bit color buffer. */
struct color_buffer : public renderbuffer<uint32_t>
{
    /** pixel format converter. needs explicit initialization. */
    pixel_format_converter pf_conv;

    /** merge fragment. */
    void merge_fragment(int x, int y, const render_states& states, const fragment_output& in)
    {
        if(in.write_color)
        {
            // convert color to output format.
            uint32_t write_color = pf_conv.to_pixel(ml::clamp_to_unit_interval(in.color));

            // alpha blending.
            uint32_t* color_buffer_ptr = at(x, y);
            if(states.blending_enabled)
            {
                write_color = swr::output_merger::blend(pf_conv, states, *color_buffer_ptr, write_color);
            }

            // write color.
            *color_buffer_ptr = write_color;
        }
    }

    void merge_fragment_block(int x, int y, const render_states& states, const fragment_output_block& in)
    {
        // generate write mask.
        auto to_mask = [](bool mask) -> uint32_t
        { return ~(static_cast<uint32_t>(mask) - 1); };
        uint32_t color_write_mask[4] = {to_mask(in.write_color[0]), to_mask(in.write_color[1]), to_mask(in.write_color[2]), to_mask(in.write_color[3])};

        if(in.write_color[0] || in.write_color[1] || in.write_color[2] || in.write_color[3])
        {
            // convert color to output format.
            uint32_t write_color[4] = {
              pf_conv.to_pixel(ml::clamp_to_unit_interval(in.color[0])),
              pf_conv.to_pixel(ml::clamp_to_unit_interval(in.color[1])),
              pf_conv.to_pixel(ml::clamp_to_unit_interval(in.color[2])),
              pf_conv.to_pixel(ml::clamp_to_unit_interval(in.color[3]))};

            // alpha blending.
            uint32_t* color_buffer_ptr[4];
            at(x, y, color_buffer_ptr);

            if(states.blending_enabled)
            {
                write_color[0] = swr::output_merger::blend(pf_conv, states, *color_buffer_ptr[0], write_color[0]);
                write_color[1] = swr::output_merger::blend(pf_conv, states, *color_buffer_ptr[1], write_color[1]);
                write_color[2] = swr::output_merger::blend(pf_conv, states, *color_buffer_ptr[2], write_color[2]);
                write_color[3] = swr::output_merger::blend(pf_conv, states, *color_buffer_ptr[3], write_color[3]);
            }

            // write color.
            *(color_buffer_ptr[0]) = (*(color_buffer_ptr[0]) & ~color_write_mask[0]) | (write_color[0] & color_write_mask[0]);
            *(color_buffer_ptr[1]) = (*(color_buffer_ptr[1]) & ~color_write_mask[1]) | (write_color[1] & color_write_mask[1]);
            *(color_buffer_ptr[2]) = (*(color_buffer_ptr[2]) & ~color_write_mask[2]) | (write_color[2] & color_write_mask[2]);
            *(color_buffer_ptr[3]) = (*(color_buffer_ptr[3]) & ~color_write_mask[3]) | (write_color[3] & color_write_mask[3]);
        }
    }
};

/* Specialized buffer clearing function. */
template<>
inline void renderbuffer<uint32_t>::clear(const uint32_t& v)
{
    if(data_ptr)
    {
        utils::memset32(static_cast<uint32_t*>(data_ptr), pitch * height, v);
    }
}

/** render to texture. */
struct texture_renderbuffer : public renderbuffer<ml::vec4>
{
    /** attached texture id. */
    uint32_t tex_id{default_tex_id};

    /** attached texture pointer. */
    texture_2d* tex{nullptr};

    /** texture data. */
    texture_storage<ml::vec4>* data{nullptr};

    /** free resources. */
    void reset()
    {
        detach();
        renderbuffer<ml::vec4>::reset();
    }

    /** attach texture. */
    void attach(texture_2d* in_tex)
    {
        tex_id = default_tex_id;
        tex = nullptr;
        data = nullptr;

        if(in_tex)
        {
            tex_id = in_tex->id;
            tex = in_tex;
            data = &in_tex->data;
        }
    }

    /** detach texture. same as attach(nullptr) */
    void detach()
    {
        attach(nullptr);
    }

    /** check if a non-default texture was attached, and if it is still valid. */
    bool has_valid_attachment() const;

    /** allocate the buffer. */
    void allocate(int in_width, int in_height)
    {
        assert(in_width > 0 && in_height > 0);

        // textures only support power-of-two dimensions, so we round in_width and in_height to the next power of two.
        // we also store the original values as width/height, and the rounded width in "pitch" for use in access functions.

        width = in_width;
        height = in_height;
        pitch = utils::round_to_next_power_of_two(in_width) * sizeof(value_type);

        data->allocate(utils::round_to_next_power_of_two(in_width), utils::round_to_next_power_of_two(in_height), false);
        data_ptr = data->data_ptrs[0];
    }
};

/** default framebuffer. */
struct default_framebuffer
{
    /** default color buffer. */
    color_buffer color_attachment;

    /** default depth attachment. */
    depth_buffer depth_attachment;

    //!!todo: add stencil attachment.

    /** default constructor. */
    default_framebuffer() = default;

    /** reset to default state. */
    void reset()
    {
        color_attachment.reset();
        depth_attachment.reset();
    }

    /** update color attachment. */
    void attach_color(int in_pitch, color_buffer::value_type* in_data_ptr)
    {
        color_attachment.pitch = in_pitch;
        color_attachment.data_ptr = in_data_ptr;
    }

    /** detach. same as attach_color(0,nullptr) */
    void detach_color()
    {
        attach_color(0, nullptr);
    }

    /** update the color attachment's format. */
    void set_color_pixel_format(swr::pixel_format name)
    {
        color_attachment.pf_conv.set_pixel_format(pixel_format_descriptor::named_format(name));
    }

    /** allocate depth buffer. */
    void allocate_depth(int width, int height)
    {
        depth_attachment.allocate(width, height);
    }

    /** free depth buffer. */
    void reset_depth()
    {
        depth_attachment.reset();
    }

    /** check if the color attachment currently is attached to the externally supplied memory. */
    bool is_color_attached() const
    {
        return color_attachment.pitch != 0 && color_attachment.data_ptr != nullptr;
    }

    /** weakly check if the color attachment currently is attached to the externally supplied memory, i.e., only check data pointer. */
    bool is_color_weakly_attached() const
    {
        return color_attachment.data_ptr != nullptr;
    }

    /** get the default framebuffer's width. this matches the color attachment's width. */
    int get_width() const
    {
        return color_attachment.width;
    }

    /** get the default framebuffer's height. this matches the color attachment's height. */
    int get_height() const
    {
        return color_attachment.height;
    }
};

/** maximum number of color attachments. !!fixme: this should probably be put somewhere else? */
constexpr int max_color_attachments = 8;

/** framebuffer objects. */
struct framebuffer_object
{
    /** color attachments. */
    std::array<std::unique_ptr<texture_renderbuffer>, max_color_attachments> color_attachments;

    /** depth attachment. */
    std::unique_ptr<depth_buffer> depth_attachment;

    //!!todo: add stencil attachment.

    /** default constructor. */
    framebuffer_object() = default;

    /** reset. */
    void reset()
    {
        for(auto& it : color_attachments)
        {
            // note: this deletes the object managed by the unique_ptr.
            it.reset();
        }

        // note: this deletes the object managed by the unique_ptr.
        depth_attachment.reset();
    }

    /** check completeness. */
    bool is_complete() const
    {
        // attachment completeness.
        for(auto& it: color_attachments)
        {
            if(it && (it->width == 0 || it->height == 0 || !it->has_valid_attachment()))
            {
                return false;
            }
        }

        if(depth_attachment && (depth_attachment->width == 0 || depth_attachment->height == 0))
        {
            return false;
        }

        //!!todo.
        assert(0 && "implement framebuffer_object::is_complete");

        return false;
    }
};

} /* namespace impl */

} /* namespace swr */
