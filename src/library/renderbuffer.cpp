/**
 * swr - a software rasterizer
 *
 * frame buffer buffer implementation.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{

namespace impl
{

/*
 * helper lambdas.
 * FIXME these are duplicated in fragment.cpp
 */

static auto to_uint32_mask = [](bool b) -> std::uint32_t
{
    return ~(static_cast<std::uint32_t>(b) - 1);
};

static auto set_uniform_mask = [](bool mask[4], bool v)
{
    mask[0] = mask[1] = mask[2] = mask[3] = v;
};

static auto apply_mask = [](bool mask[4], const auto additional_mask[4])
{
    mask[0] &= static_cast<bool>(additional_mask[0]);
    mask[1] &= static_cast<bool>(additional_mask[1]);
    mask[2] &= static_cast<bool>(additional_mask[2]);
    mask[3] &= static_cast<bool>(additional_mask[3]);
};

/*
 * attachment_texture.
 */

bool attachment_texture::is_valid() const
{
    if(tex_id == default_tex_id || tex == nullptr || info.data_ptr == nullptr)
    {
        return false;
    }

    if(level >= tex->data.data_ptrs.size())
    {
        return false;
    }

    if(tex_id >= global_context->texture_2d_storage.size())
    {
        return false;
    }

    if(!global_context->texture_2d_storage[tex_id])
    {
        return false;
    }

    return global_context->texture_2d_storage[tex_id].get() == tex && tex_id == tex->id && info.data_ptr == tex->data.data_ptrs[level];
}

#if defined(SWR_USE_MORTON_CODES) && 0

template<>
void scissor_clear_buffer(ml::vec4 clear_value, attachment_info<ml::vec4>& info, const utils::rect& scissor_box)
{
    int x_min = std::min(std::max(0, scissor_box.x_min), info.width);
    int x_max = std::max(0, std::min(scissor_box.x_max, info.width));
    int y_min = std::min(std::max(0, scissor_box.y_max), info.height);
    int y_max = std::max(0, std::min(scissor_box.y_min, info.height));

    const auto row_size = x_max - x_min;
    const auto skip = info.pitch - row_size;

    auto ptr = info.data_ptr + y_min * info.pitch + x_min;
    for(int y = y_min; y < y_max; ++y)
    {
        for(int i = 0; i < row_size; ++i)
        {
            *ptr++ = clear_value;
        }

        ptr += skip;
    }
}

#elif 0

template<typename T>
static void scissor_clear_buffer_morton(T clear_value, attachment_info<T>& info, const utils::rect& scissor_box)
{
    int x_min = std::min(std::max(0, scissor_box.x_min), info.width);
    int x_max = std::max(0, std::min(scissor_box.x_max, info.width));
    int y_min = std::min(std::max(info.height - scissor_box.y_max, 0), info.height);
    int y_max = std::max(0, std::min(info.height - scissor_box.y_min, info.height));

    for(int y = y_min; y < y_max; ++y)
    {
        for(int x = x_min; x < x_max; ++x)
        {
            *(info.data_ptr + libmorton::morton2D_32_encode(x, y)) = clear_value;
        }
    }
}

#endif

/*
 * default framebuffer.
 */

void default_framebuffer::clear_color(uint32_t attachment, ml::vec4 clear_color)
{
    if(attachment == 0)
    {
        auto& info = color_buffer.info;
        utils::memset32(info.data_ptr, color_buffer.converter.to_pixel(clear_color), info.pitch * info.height);
    }
}

void default_framebuffer::clear_color(uint32_t attachment, ml::vec4 clear_color, const utils::rect& rect)
{
    if(attachment == 0)
    {
        auto clear_value = color_buffer.converter.to_pixel(clear_color);

        int x_min = std::min(std::max(0, rect.x_min), color_buffer.info.width);
        int x_max = std::max(0, std::min(rect.x_max, color_buffer.info.width));
        int y_min = std::min(std::max(color_buffer.info.height - rect.y_max, 0), color_buffer.info.height);
        int y_max = std::max(0, std::min(color_buffer.info.height - rect.y_min, color_buffer.info.height));

        const auto row_size = (x_max - x_min) * sizeof(uint32_t);

        auto ptr = reinterpret_cast<uint8_t*>(color_buffer.info.data_ptr) + y_min * color_buffer.info.pitch + x_min * sizeof(uint32_t);
        for(int y = y_min; y < y_max; ++y)
        {
            utils::memset32(ptr, *reinterpret_cast<uint32_t*>(&clear_value), row_size);
            ptr += color_buffer.info.pitch;
        }
    }
}

void default_framebuffer::clear_depth(ml::fixed_32_t clear_depth)
{
    auto& info = depth_buffer.info;
    if(info.data_ptr)
    {
        utils::memset32(reinterpret_cast<uint32_t*>(info.data_ptr), ml::unwrap(clear_depth), info.pitch * info.height);
    }
}

void default_framebuffer::clear_depth(ml::fixed_32_t clear_depth, const utils::rect& rect)
{
    int x_min = std::min(std::max(0, rect.x_min), depth_buffer.info.width);
    int x_max = std::max(0, std::min(rect.x_max, depth_buffer.info.width));
    int y_min = std::min(std::max(depth_buffer.info.height - rect.y_max, 0), depth_buffer.info.height);
    int y_max = std::max(0, std::min(depth_buffer.info.height - rect.y_min, depth_buffer.info.height));

    const auto row_size = (x_max - x_min) * sizeof(ml::fixed_32_t);

    auto ptr = reinterpret_cast<uint8_t*>(depth_buffer.info.data_ptr) + y_min * depth_buffer.info.pitch + x_min * sizeof(ml::fixed_32_t);
    for(int y = y_min; y < y_max; ++y)
    {
        utils::memset32(ptr, *reinterpret_cast<uint32_t*>(&clear_depth), row_size);
        ptr += depth_buffer.info.pitch;
    }
}

void default_framebuffer::merge_color(uint32_t attachment, int x, int y, const fragment_output& frag, bool do_blend, blend_func blend_src, blend_func blend_dst)
{
    if(attachment != 0)
    {
        return;
    }

    if(frag.write_flags & fragment_output::fof_write_color)
    {
        // convert color to output format.
        uint32_t write_color = color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color));

        // alpha blending.
        uint32_t* color_buffer_ptr = color_buffer.info.data_ptr + y * color_buffer.info.width + x;
        if(do_blend)
        {
            write_color = swr::output_merger::blend(color_buffer.converter, blend_src, blend_dst, write_color, *color_buffer_ptr);
        }

        // write color.
        *color_buffer_ptr = write_color;
    }
}

void default_framebuffer::merge_color_block(uint32_t attachment, int x, int y, const fragment_output_block& frag, bool do_blend, blend_func blend_src, blend_func blend_dst)
{
    if(attachment != 0)
    {
        return;
    }

    // generate write mask.
    uint32_t color_write_mask[4] = {to_uint32_mask(frag.write_color[0]), to_uint32_mask(frag.write_color[1]), to_uint32_mask(frag.write_color[2]), to_uint32_mask(frag.write_color[3])};

    // block coordinates
    const ml::tvec2<int> coords[4] = {{x, y}, {x + 1, y}, {x, y + 1}, {x + 1, y + 1}};

    if(frag.write_color[0] || frag.write_color[1] || frag.write_color[2] || frag.write_color[3])
    {
        // convert color to output format.
        DECLARE_ALIGNED_ARRAY4(uint32_t, write_color) = {
          color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[0])),
          color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[1])),
          color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[2])),
          color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[3]))};

        // alpha blending.
        uint32_t* color_buffer_ptr[4] = {
          color_buffer.info.data_ptr + coords[0].y * color_buffer.info.width + coords[0].x,
          color_buffer.info.data_ptr + coords[1].y * color_buffer.info.width + coords[1].x,
          color_buffer.info.data_ptr + coords[2].y * color_buffer.info.width + coords[2].x,
          color_buffer.info.data_ptr + coords[3].y * color_buffer.info.width + coords[3].x};

        DECLARE_ALIGNED_ARRAY4(uint32_t, color_buffer_values) = {
          *color_buffer_ptr[0], *color_buffer_ptr[1], *color_buffer_ptr[2], *color_buffer_ptr[3]};

        if(do_blend)
        {
            // note: when compiling with SSE/SIMD enabled, make sure that src/dest/out are aligned on 16-byte boundaries.
            swr::output_merger::blend_block(color_buffer.converter, blend_src, blend_dst, write_color, color_buffer_values, write_color);
        }

        // write color.
        *(color_buffer_ptr[0]) = (color_buffer_values[0] & ~color_write_mask[0]) | (write_color[0] & color_write_mask[0]);
        *(color_buffer_ptr[1]) = (color_buffer_values[1] & ~color_write_mask[1]) | (write_color[1] & color_write_mask[1]);
        *(color_buffer_ptr[2]) = (color_buffer_values[2] & ~color_write_mask[2]) | (write_color[2] & color_write_mask[2]);
        *(color_buffer_ptr[3]) = (color_buffer_values[3] & ~color_write_mask[3]) | (write_color[3] & color_write_mask[3]);
    }
}

void default_framebuffer::depth_compare_write(int x, int y, float depth_value, comparison_func depth_func, bool write_depth, bool& write_mask)
{
    // discard fragment if depth testing is always failing.
    if(depth_func == swr::comparison_func::fail)
    {
        write_mask = false;
        return;
    }

    write_mask = write_depth;

    // if no depth buffer was created, accept.
    if(!depth_buffer.info.data_ptr)
    {
        return;
    }

    // read and compare depth buffer.
    ml::fixed_32_t* depth_buffer_ptr = depth_buffer.info.data_ptr + y * depth_buffer.info.width + x;
    ml::fixed_32_t old_depth_value = *depth_buffer_ptr;
    ml::fixed_32_t new_depth_value{depth_value};

    // basic comparisons for depth test.
    bool depth_compare[] = {
      true,                               /* pass */
      false,                              /* fail */
      new_depth_value == old_depth_value, /* equal */
      false,                              /* not_equal */
      new_depth_value < old_depth_value,  /* less */
      false,                              /* less_equal */
      false,                              /* greater */
      false                               /* greater_equal */
    };

    // compound comparisons for depth test.
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::not_equal)] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less)] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater_equal)] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];

    // generate write mask for this fragment.
    write_mask &= depth_compare[static_cast<std::uint32_t>(depth_func)];

    // write depth value.
    uint32_t depth_write_mask = to_uint32_mask(write_depth && write_mask);
    *depth_buffer_ptr = ml::wrap((ml::unwrap(*depth_buffer_ptr) & ~depth_write_mask) | (ml::unwrap(new_depth_value) & depth_write_mask));
}

void default_framebuffer::depth_compare_write_block(int x, int y, float depth_value[4], comparison_func depth_func, bool write_depth, uint8_t& write_mask)
{
    // discard fragment if depth testing is always failing.
    if(depth_func == swr::comparison_func::fail)
    {
        write_mask = 0;
        return;
    }

    // if no depth buffer was created, accept.
    if(!depth_buffer.info.data_ptr)
    {
        // the write mask is initialized with "accept all".
        return;
    }

    // block coordinates
    const ml::tvec2<int> coords[4] = {{x, y}, {x + 1, y}, {x, y + 1}, {x + 1, y + 1}};

    // read and compare depth buffer.
    ml::fixed_32_t* depth_buffer_ptr[4] = {
      depth_buffer.info.data_ptr + coords[0].y * depth_buffer.info.width + coords[0].x,
      depth_buffer.info.data_ptr + coords[1].y * depth_buffer.info.width + coords[1].x,
      depth_buffer.info.data_ptr + coords[2].y * depth_buffer.info.width + coords[2].x,
      depth_buffer.info.data_ptr + coords[3].y * depth_buffer.info.width + coords[3].x};

    ml::fixed_32_t old_depth_value[4] = {*depth_buffer_ptr[0], *depth_buffer_ptr[1], *depth_buffer_ptr[2], *depth_buffer_ptr[3]};
    ml::fixed_32_t new_depth_value[4] = {depth_value[0], depth_value[1], depth_value[2], depth_value[3]};

    // basic comparisons for depth test.
    bool depth_compare[][4] = {
      {true, true, true, true},                                                                                                                                                 /* pass */
      {false, false, false, false},                                                                                                                                             /* fail */
      {new_depth_value[0] == old_depth_value[0], new_depth_value[1] == old_depth_value[1], new_depth_value[2] == old_depth_value[2], new_depth_value[3] == old_depth_value[3]}, /* equal */
      {false, false, false, false},                                                                                                                                             /* not_equal */
      {new_depth_value[0] < old_depth_value[0], new_depth_value[1] < old_depth_value[1], new_depth_value[2] < old_depth_value[2], new_depth_value[3] < old_depth_value[3]},     /* less */
      {false, false, false, false},                                                                                                                                             /* less -<equal */
      {false, false, false, false},                                                                                                                                             /* greater */
      {false, false, false, false}                                                                                                                                              /* greater_equal */
    };

    // compound comparisons for depth test.
    for(int k = 0; k < 4; ++k)
    {
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::not_equal)][k] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)][k] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less)][k] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)][k] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater_equal)][k] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)][k] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
    }

    bool depth_mask[4] = {
      depth_compare[static_cast<std::uint32_t>(depth_func)][0],
      depth_compare[static_cast<std::uint32_t>(depth_func)][1],
      depth_compare[static_cast<std::uint32_t>(depth_func)][2],
      depth_compare[static_cast<std::uint32_t>(depth_func)][3]};

    write_mask &= (depth_mask[0] << 3) | (depth_mask[1] << 2) | (depth_mask[2] << 1) | depth_mask[3];

    // write depth.
    uint32_t depth_write_mask[4] = {
      to_uint32_mask((write_mask & 0x8) != 0 && write_depth),
      to_uint32_mask((write_mask & 0x4) != 0 && write_depth),
      to_uint32_mask((write_mask & 0x2) != 0 && write_depth),
      to_uint32_mask((write_mask & 0x1) != 0 && write_depth)};

    *(depth_buffer_ptr[0]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[0])) & ~depth_write_mask[0]) | (ml::unwrap(new_depth_value[0]) & depth_write_mask[0]));
    *(depth_buffer_ptr[1]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[1])) & ~depth_write_mask[1]) | (ml::unwrap(new_depth_value[1]) & depth_write_mask[1]));
    *(depth_buffer_ptr[2]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[2])) & ~depth_write_mask[2]) | (ml::unwrap(new_depth_value[2]) & depth_write_mask[2]));
    *(depth_buffer_ptr[3]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[3])) & ~depth_write_mask[3]) | (ml::unwrap(new_depth_value[3]) & depth_write_mask[3]));
}

/*
 * framebuffer_object
 */

void framebuffer_object::clear_color(uint32_t attachment, ml::vec4 clear_color)
{
    if(attachment < color_attachments.size() && color_attachments[attachment])
    {
        // this also clears mipmaps, if present
        auto& info = color_attachments[attachment]->info;
#ifdef SWR_USE_SIMD
        utils::memset128(info.data_ptr, *reinterpret_cast<__m128i*>(&clear_color.data), info.pitch * info.height * sizeof(__m128));
#else  /* SWR_USE_SIMD */
        std::fill_n(info.data_ptr, info.pitch * info.height, clear_color);
#endif /* SWR_USE_SIMD */
    }
}

void framebuffer_object::clear_color(uint32_t attachment, ml::vec4 clear_color, const utils::rect& rect)
{
    if(attachment < color_attachments.size() && color_attachments[attachment])
    {
#ifdef SWR_USE_MORTON_CODES
        auto& info = color_attachments[attachment]->info;

        int x_min = std::min(std::max(0, rect.x_min), info.width);
        int x_max = std::max(0, std::min(rect.x_max, info.width));
        int y_min = std::min(std::max(0, rect.y_min), info.height);
        int y_max = std::max(0, std::min(rect.y_max, info.height));

        for(int x = x_min; x < x_max; ++x)
        {
            for(int y = y_min; y < y_max; ++y)
            {
                *(info.data_ptr + libmorton::morton2D_32_encode(x, y)) = clear_color;
            }
        }
#else
        auto& info = color_attachments[attachment]->info;

        int x_min = std::min(std::max(0, rect.x_min), info.width);
        int x_max = std::max(0, std::min(rect.x_max, info.width));
        int y_min = std::min(std::max(0, rect.y_min), info.height);
        int y_max = std::max(0, std::min(rect.y_max, info.height));

        const auto row_size = x_max - x_min;

#    ifdef SWR_USE_SIMD
        auto ptr = info.data_ptr + y_min * info.pitch + x_min;
        for(int y = y_min; y < y_max; ++y)
        {
            utils::memset128(ptr, *reinterpret_cast<__m128i*>(&clear_color.data), row_size * sizeof(__m128));
            ptr += info.pitch;
        }
#    else  /* SWR_USE_SIMD */
        const auto skip = info.pitch - row_size;
        auto ptr = info.data_ptr + y_min * info.pitch + x_min;
        for(int y = y_min; y < y_max; ++y)
        {
            for(int i = 0; i < row_size; ++i)
            {
                *ptr++ = clear_color;
            }

            ptr += skip;
        }
#    endif /* SWR_USE_SIMD */
#endif     /* SWR_USE_MORTON_CODES */
    }
}

void framebuffer_object::clear_depth(ml::fixed_32_t clear_depth)
{
    if(depth_attachment)
    {
        auto& info = depth_attachment->info;
        utils::memset32(reinterpret_cast<uint32_t*>(info.data_ptr), ml::unwrap(clear_depth), info.pitch * info.height);
    }
}

void framebuffer_object::clear_depth(ml::fixed_32_t clear_depth, const utils::rect& rect)
{
    if(depth_attachment)
    {
#ifdef SWR_USE_MORTON_CODES
        auto& info = depth_attachment->info;

        int x_min = std::min(std::max(0, rect.x_min), info.width);
        int x_max = std::max(0, std::min(rect.x_max, info.width));
        int y_min = std::min(std::max(rect.y_min, 0), info.height);
        int y_max = std::max(0, std::min(rect.y_max, info.height));

        for(int x = x_min; x < x_max; ++x)
        {
            for(int y = y_min; y < y_max; ++y)
            {
                *(info.data_ptr + libmorton::morton2D_32_encode(x, y)) = clear_depth;
            }
        }
#else
        auto& info = depth_attachment->info;

        int x_min = std::min(std::max(0, rect.x_min), info.width);
        int x_max = std::max(0, std::min(rect.x_max, info.width));
        int y_min = std::min(std::max(rect.y_min, 0), info.height);
        int y_max = std::max(0, std::min(rect.y_max, info.height));

        const auto row_size = (x_max - x_min) * sizeof(ml::fixed_32_t);

        auto ptr = reinterpret_cast<uint8_t*>(info.data_ptr) + y_min * info.pitch + x_min * sizeof(ml::fixed_32_t);
        for(int y = y_min; y < y_max; ++y)
        {
            utils::memset32(ptr, *reinterpret_cast<uint32_t*>(&clear_depth), row_size);
            ptr += info.pitch;
        }
#endif /* SWR_USE_MORTON_CODES */
    }
}

void framebuffer_object::merge_color(uint32_t attachment, int x, int y, const fragment_output& frag, bool do_blend, blend_func blend_src, blend_func blend_dst)
{
    if(attachment > color_attachments.size() || !color_attachments[attachment])
    {
        return;
    }

    if(frag.write_flags & fragment_output::fof_write_color)
    {
        ml::vec4 write_color{ml::clamp_to_unit_interval(frag.color)};

        ml::vec4* data_ptr = color_attachments[attachment]->info.data_ptr;

        // alpha blending.
#ifdef SWR_USE_MORTON_CODES
        ml::vec4* color_buffer_ptr = data_ptr + libmorton::morton2D_32_encode(x, y);
#else
        int pitch = color_attachments[attachment]->info.pitch;
        ml::vec4* color_buffer_ptr = data_ptr + y * pitch + x;
#endif
        if(do_blend)
        {
            write_color = swr::output_merger::blend(blend_src, blend_dst, write_color, *color_buffer_ptr);
        }

        // write color.
        *color_buffer_ptr = write_color;
    }
}

void framebuffer_object::merge_color_block(uint32_t attachment, int x, int y, const fragment_output_block& frag, bool do_blend, blend_func blend_src, blend_func blend_dst)
{
    if(attachment > color_attachments.size() || !color_attachments[attachment])
    {
        return;
    }

    if(frag.write_color[0] || frag.write_color[1] || frag.write_color[2] || frag.write_color[3])
    {
        // convert color to output format.
        ml::vec4 write_color[4] = {
          ml::clamp_to_unit_interval(frag.color[0]),
          ml::clamp_to_unit_interval(frag.color[1]),
          ml::clamp_to_unit_interval(frag.color[2]),
          ml::clamp_to_unit_interval(frag.color[3])};

        ml::vec4* data_ptr = color_attachments[attachment]->info.data_ptr;

        // block coordinates
        const ml::tvec2<int> coords[4] = {{x, y}, {x + 1, y}, {x, y + 1}, {x + 1, y + 1}};

        // alpha blending.
#ifdef SWR_USE_MORTON_CODES
        ml::vec4* color_buffer_ptrs[4] = {
          data_ptr + libmorton::morton2D_32_encode(coords[0].x, coords[0].y),
          data_ptr + libmorton::morton2D_32_encode(coords[1].x, coords[1].y),
          data_ptr + libmorton::morton2D_32_encode(coords[2].x, coords[2].y),
          data_ptr + libmorton::morton2D_32_encode(coords[3].x, coords[3].y)};
#else
        int pitch = color_attachments[attachment]->info.pitch;
        ml::vec4* color_buffer_ptrs[4] = {
          data_ptr + coords[0].y * pitch + coords[0].x,
          data_ptr + coords[1].y * pitch + coords[1].x,
          data_ptr + coords[2].y * pitch + coords[2].x,
          data_ptr + coords[3].y * pitch + coords[3].x};
#endif

        ml::vec4 color_buffer_values[4] = {
          *color_buffer_ptrs[0], *color_buffer_ptrs[1], *color_buffer_ptrs[2], *color_buffer_ptrs[3]};

        if(do_blend)
        {
            swr::output_merger::blend_block(blend_src, blend_dst, write_color, color_buffer_values, write_color);
        }

        // write color.
#define CONDITIONAL_WRITE(condition, write_target, write_source) \
    if(condition)                                                \
    {                                                            \
        write_target = write_source;                             \
    }

        CONDITIONAL_WRITE(frag.write_color[0], *(color_buffer_ptrs[0]), write_color[0]);
        CONDITIONAL_WRITE(frag.write_color[1], *(color_buffer_ptrs[1]), write_color[1]);
        CONDITIONAL_WRITE(frag.write_color[2], *(color_buffer_ptrs[2]), write_color[2]);
        CONDITIONAL_WRITE(frag.write_color[3], *(color_buffer_ptrs[3]), write_color[3]);

#undef CONDITIONAL_WRITE
    }
}

// FIXME this is almost exactly the same as default_framebuffer::depth_compare_write.
void framebuffer_object::depth_compare_write(int x, int y, float depth_value, comparison_func depth_func, bool write_depth, bool& write_mask)
{
    // discard fragment if depth testing is always failing.
    if(depth_func == swr::comparison_func::fail)
    {
        write_mask = false;
        return;
    }

    write_mask = true;

    // if no depth buffer was created, accept.
    if(!depth_attachment || !depth_attachment->info.data_ptr)
    {
        return;
    }

    // read and compare depth buffer.
#ifdef SWR_USE_MORTON_CODES
    ml::fixed_32_t* depth_buffer_ptr = depth_attachment->info.data_ptr + libmorton::morton2D_32_encode(x, y);
#else
    ml::fixed_32_t* depth_buffer_ptr = depth_attachment->info.data_ptr + y * depth_attachment->info.width + x;
#endif
    ml::fixed_32_t old_depth_value = *depth_buffer_ptr;
    ml::fixed_32_t new_depth_value{depth_value};

    // basic comparisons for depth test.
    bool depth_compare[] = {
      true,                               /* pass */
      false,                              /* fail */
      new_depth_value == old_depth_value, /* equal */
      false,                              /* not_equal */
      new_depth_value < old_depth_value,  /* less */
      false,                              /* less_equal */
      false,                              /* greater */
      false                               /* greater_equal */
    };

    // compound comparisons for depth test.
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::not_equal)] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less)] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater_equal)] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];

    // generate write mask for this fragment.
    write_mask &= depth_compare[static_cast<std::uint32_t>(depth_func)];

    // write depth value.
    uint32_t depth_write_mask = to_uint32_mask(write_depth && write_mask);
    *depth_buffer_ptr = ml::wrap((ml::unwrap(*depth_buffer_ptr) & ~depth_write_mask) | (ml::unwrap(new_depth_value) & depth_write_mask));
}

// FIXME this is almost exactly the same as default_framebuffer::depth_compare_write_block.
void framebuffer_object::depth_compare_write_block(int x, int y, float depth_value[4], comparison_func depth_func, bool write_depth, uint8_t& write_mask)
{
    // discard fragment if depth testing is always failing.
    if(depth_func == swr::comparison_func::fail)
    {
        write_mask = 0;
        return;
    }

    // if no depth buffer was created, accept.
    if(!depth_attachment || !depth_attachment->info.data_ptr)
    {
        // the write mask is initialized with "accept all".
        return;
    }

    // block coordinates
    const ml::tvec2<int> coords[4] = {{x, y}, {x + 1, y}, {x, y + 1}, {x + 1, y + 1}};

    // read and compare depth buffer.
#ifdef SWR_USE_MORTON_CODES
    ml::fixed_32_t* depth_buffer_ptr[4] = {
      depth_attachment->info.data_ptr + libmorton::morton2D_32_encode(coords[0].x, coords[0].y),
      depth_attachment->info.data_ptr + libmorton::morton2D_32_encode(coords[1].x, coords[1].y),
      depth_attachment->info.data_ptr + libmorton::morton2D_32_encode(coords[2].x, coords[2].y),
      depth_attachment->info.data_ptr + libmorton::morton2D_32_encode(coords[3].x, coords[3].y)};
#else
    ml::fixed_32_t* depth_buffer_ptr[4] = {
      depth_attachment->info.data_ptr + coords[0].y * depth_attachment->info.width + coords[0].x,
      depth_attachment->info.data_ptr + coords[1].y * depth_attachment->info.width + coords[1].x,
      depth_attachment->info.data_ptr + coords[2].y * depth_attachment->info.width + coords[2].x,
      depth_attachment->info.data_ptr + coords[3].y * depth_attachment->info.width + coords[3].x};
#endif

    ml::fixed_32_t old_depth_value[4] = {*depth_buffer_ptr[0], *depth_buffer_ptr[1], *depth_buffer_ptr[2], *depth_buffer_ptr[3]};
    ml::fixed_32_t new_depth_value[4] = {depth_value[0], depth_value[1], depth_value[2], depth_value[3]};

    // basic comparisons for depth test.
    bool depth_compare[][4] = {
      {true, true, true, true},                                                                                                                                                 /* pass */
      {false, false, false, false},                                                                                                                                             /* fail */
      {new_depth_value[0] == old_depth_value[0], new_depth_value[1] == old_depth_value[1], new_depth_value[2] == old_depth_value[2], new_depth_value[3] == old_depth_value[3]}, /* equal */
      {false, false, false, false},                                                                                                                                             /* not_equal */
      {new_depth_value[0] < old_depth_value[0], new_depth_value[1] < old_depth_value[1], new_depth_value[2] < old_depth_value[2], new_depth_value[3] < old_depth_value[3]},     /* less */
      {false, false, false, false},                                                                                                                                             /* less -<equal */
      {false, false, false, false},                                                                                                                                             /* greater */
      {false, false, false, false}                                                                                                                                              /* greater_equal */
    };

    // compound comparisons for depth test.
    for(int k = 0; k < 4; ++k)
    {
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::not_equal)][k] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)][k] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less)][k] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)][k] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater_equal)][k] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)][k] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
    }

    bool depth_mask[4] = {
      depth_compare[static_cast<std::uint32_t>(depth_func)][0], depth_compare[static_cast<std::uint32_t>(depth_func)][1], depth_compare[static_cast<std::uint32_t>(depth_func)][2], depth_compare[static_cast<std::uint32_t>(depth_func)][3]};

    write_mask &= (depth_mask[0] << 3) | (depth_mask[1] << 2) | (depth_mask[2] << 1) | depth_mask[3];

    // write depth.
    uint32_t depth_write_mask[4] = {
      to_uint32_mask((write_mask & 0x8) != 0 && write_depth),
      to_uint32_mask((write_mask & 0x4) != 0 && write_depth),
      to_uint32_mask((write_mask & 0x2) != 0 && write_depth),
      to_uint32_mask((write_mask & 0x1) != 0 && write_depth)};

    *(depth_buffer_ptr[0]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[0])) & ~depth_write_mask[0]) | (ml::unwrap(new_depth_value[0]) & depth_write_mask[0]));
    *(depth_buffer_ptr[1]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[1])) & ~depth_write_mask[1]) | (ml::unwrap(new_depth_value[1]) & depth_write_mask[1]));
    *(depth_buffer_ptr[2]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[2])) & ~depth_write_mask[2]) | (ml::unwrap(new_depth_value[2]) & depth_write_mask[2]));
    *(depth_buffer_ptr[3]) = ml::wrap((ml::unwrap(*(depth_buffer_ptr[3])) & ~depth_write_mask[3]) | (ml::unwrap(new_depth_value[3]) & depth_write_mask[3]));
}

} /* namespace impl */

/*
 * framebuffer object interface.
 */

/** the default framebuffer has id 0. this constant is mostly here to make the code below more readable. */
constexpr uint32_t default_framebuffer_id = 0;

/** two more functions for handling the default_framebuffer_id case. */
static auto id_to_slot = [](std::uint32_t id) -> std::uint32_t
{ return id - 1; };
static auto slot_to_id = [](std::uint32_t slot) -> std::uint32_t
{ return slot + 1; };

uint32_t CreateFramebufferObject()
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    // set up a new framebuffer.
    auto slot = context->framebuffer_objects.push({});
    auto* new_fbo = &context->framebuffer_objects[slot];
    new_fbo->reset(slot);

    return slot_to_id(slot);
}

void ReleaseFramebufferObject(uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(id == default_framebuffer_id)
    {
        // do not release default framebuffer.
        return;
    }

    auto slot = id_to_slot(id);
    if(slot < context->framebuffer_objects.size() && !context->framebuffer_objects.is_free(slot))
    {
        // check if we are bound to a target and reset the target if necessary.
        if(context->states.draw_target == &context->framebuffer_objects[slot])
        {
            context->states.draw_target = &context->framebuffer;
        }

        // release framebuffer object.
        context->framebuffer_objects[slot].reset();
        context->framebuffer_objects.free(slot);
    }
}

void BindFramebufferObject(framebuffer_target target, uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(id == default_framebuffer_id)
    {
        // bind the default framebuffer.
        if(target == framebuffer_target::draw || target == framebuffer_target::draw_read)
        {
            context->states.draw_target = &context->framebuffer;
        }

        if(target == framebuffer_target::read || target == framebuffer_target::draw_read)
        {
            /* unimplemented. */
        }

        return;
    }

    // check that the id is valid.
    auto slot = id_to_slot(id);
    if(slot >= context->framebuffer_objects.size() || context->framebuffer_objects.is_free(slot))
    {
        context->last_error = error::invalid_operation;
        return;
    }

    if(target == framebuffer_target::draw || target == framebuffer_target::draw_read)
    {
        context->states.draw_target = &context->framebuffer_objects[slot];
    }

    if(target == framebuffer_target::read || target == framebuffer_target::draw_read)
    {
        /* unimplemented. */
    }
}

void FramebufferTexture(uint32_t id, framebuffer_attachment attachment, uint32_t attachment_id, uint32_t level)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(id == default_framebuffer_id)
    {
        // textures cannot be bound to the default framebuffer.
        context->last_error = error::invalid_value;
        return;
    }

    int numeric_attachment = static_cast<int>(attachment);
    if(numeric_attachment >= static_cast<int>(framebuffer_attachment::color_attachment_0) && numeric_attachment <= static_cast<int>(framebuffer_attachment::color_attachment_7))
    {
        // use texture as color buffer.

        // get framebuffer object.
        auto slot = id_to_slot(id);
        if(slot >= context->framebuffer_objects.size() || context->framebuffer_objects.is_free(slot))
        {
            context->last_error = error::invalid_value;
            return;
        }
        auto fbo = &context->framebuffer_objects[slot];

        // get texture.
        auto tex_id = attachment_id;
        if(tex_id >= context->texture_2d_storage.size() || context->texture_2d_storage.is_free(tex_id))
        {
            context->last_error = error::invalid_value;
            return;
        }

        // associate texture to fbo.
        fbo->attach_texture(attachment, context->texture_2d_storage[tex_id].get(), level);
    }
    else
    {
        // unknown attachment.
        context->last_error = error::invalid_value;
    }
}

uint32_t CreateDepthRenderbuffer(uint32_t width, uint32_t height)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    auto slot = context->depth_attachments.push({});
    context->depth_attachments[slot].allocate(width, height);

    return slot;
}

void ReleaseDepthRenderbuffer(uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(id < context->depth_attachments.size() && !context->depth_attachments.is_free(id))
    {
        context->depth_attachments.free(id);
    }
}

void FramebufferRenderbuffer(uint32_t id, framebuffer_attachment attachment, uint32_t attachment_id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_device_context* context = impl::global_context;

    if(id == default_framebuffer_id)
    {
        // don't operate on the default framebuffer.
        context->last_error = error::invalid_value;
        return;
    }

    if(attachment != framebuffer_attachment::depth_attachment)
    {
        // currently only depth attachments are supported.
        context->last_error = error::invalid_value;
        return;
    }

    if(attachment_id >= context->depth_attachments.size() || context->depth_attachments.is_free(attachment_id))
    {
        context->last_error = error::invalid_value;
        return;
    }

    auto slot = id_to_slot(id);
    if(slot >= context->framebuffer_objects.size() || context->framebuffer_objects.is_free(slot))
    {
        context->last_error = error::invalid_value;
        return;
    }

    auto& fbo = context->framebuffer_objects[slot];
    fbo.attach_depth(&context->depth_attachments[attachment_id]);
}

} /* namespace swr */
