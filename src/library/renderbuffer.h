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

/** Render buffers. */
template<typename T>
struct render_buffer
{
    // this is here because of the pixel format conversion.
    static_assert(sizeof(T) == sizeof(uint32_t), "Type size does not match size needed for pixel format conversion");

    /** Width of the allocated color- and depth buffers. Has to be aligned on rasterizer_block_size.  */
    int width = 0;

    /** Height of the allocated color- and depth buffers. Has to be aligned on rasterizer_block_size. */
    int height = 0;

    /** Buffer width, in bytes. */
    int pitch = 0;

    /** Pointer to the buffer data. */
    T* data_ptr = nullptr;

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
inline void render_buffer<T>::clear(const T& v)
{
    if(data_ptr)
    {
        std::fill_n(data_ptr, pitch * height, v);
    }
}

/** A fixed-point depth buffer. */
struct depth_buffer : public render_buffer<ml::fixed_32_t>
{
    /** The depth buffer data. */
    std::vector<ml::fixed_32_t> data;

    /** allocate the buffer. */
    void allocate(int in_width, int in_height)
    {
        assert(in_width > 0 && in_height > 0);

        width = in_width;
        pitch = in_width * sizeof(ml::fixed_32_t);
        height = in_height;

        data_ptr = utils::align_vector(utils::alignment::sse, in_width * in_height, data);
    }
};

/* Specialized buffer clearing function. */
template<>
inline void render_buffer<ml::fixed_32_t>::clear(const ml::fixed_32_t& v)
{
    if(data_ptr)
    {
        utils::memset32(reinterpret_cast<uint32_t*>(data_ptr), pitch * height, ml::unwrap(v));
    }
}

/** A 32-bit color buffer. */
struct color_buffer : public render_buffer<uint32_t>
{
    /** pixel format converter. needs explicit initialization. */
    pixel_format_converter pf_conv;
};

/* Specialized buffer clearing function. */
template<>
inline void render_buffer<uint32_t>::clear(const uint32_t& v)
{
    if(data_ptr)
    {
        utils::memset32(static_cast<uint32_t*>(data_ptr), pitch * height, v);
    }
}

} /* namespace impl */

} /* namespace swr */
