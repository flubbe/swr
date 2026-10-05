/**
 * swr - a software rasterizer
 *
 * Frame buffer implementation.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "swr_internal.h"

namespace swr
{

namespace impl
{

/*
 * Command creation.
 */

void render_context::create_framebuffer_texture_command(
  std::uint32_t framebuffer_id,
  framebuffer_attachment attachment,
  std::uint32_t texture_id,
  std::uint32_t level)
{
    command_list.emplace_back(framebuffer_texture_command{
      .framebuffer_id = framebuffer_id,
      .attachment = attachment,
      .texture_id = texture_id,
      .level = level});
}

void render_context::create_framebuffer_renderbuffer_command(
  std::uint32_t framebuffer_id,
  std::uint32_t renderbuffer_id)
{
    command_list.emplace_back(framebuffer_renderbuffer_command{
      .framebuffer_id = framebuffer_id,
      .renderbuffer_id = renderbuffer_id});
}

/*
 * Command execution.
 */

void render_context::execute_command(
  const framebuffer_texture_command& cmd)
{
    const auto slot = framebuffer_id_to_slot(cmd.framebuffer_id);
    if(!framebuffer_objects.contains(slot))
    {
        last_error = error::invalid_value;
        return;
    }

    auto& fbo = framebuffer_objects[slot];
    if(cmd.texture_id == 0)
    {
        if(cmd.attachment == framebuffer_attachment::depth_attachment)
        {
            fbo.detach_depth();
        }
        else
        {
            fbo.detach_texture(cmd.attachment);
        }
        return;
    }

    if(!texture_2d_storage.contains(cmd.texture_id)
       || !texture_2d_storage[cmd.texture_id])
    {
        last_error = error::invalid_value;
        return;
    }

    auto* texture = texture_2d_storage[cmd.texture_id].get();
    if(cmd.attachment == framebuffer_attachment::depth_attachment)
    {
        if(texture->as_texture_depth_2d() == nullptr
           || cmd.level >= texture->mip_level_count())
        {
            last_error = error::invalid_value;
            return;
        }

        fbo.attach_depth_texture(texture, cmd.level);
        return;
    }

    if(texture->as_texture_color_2d() == nullptr
       || cmd.level >= texture->mip_level_count())
    {
        last_error = error::invalid_value;
        return;
    }

    fbo.attach_texture(cmd.attachment, texture, cmd.level);
}

void render_context::execute_command(
  const framebuffer_renderbuffer_command& cmd)
{
    const auto framebuffer_slot = framebuffer_id_to_slot(cmd.framebuffer_id);
    if(!framebuffer_objects.contains(framebuffer_slot)
       || !depth_attachments.contains(cmd.renderbuffer_id))
    {
        last_error = error::invalid_value;
        return;
    }

    framebuffer_objects[framebuffer_slot].attach_depth_renderbuffer(
      cmd.renderbuffer_id,
      &depth_attachments[cmd.renderbuffer_id]);
}

/*
 * Mask helper.
 */

[[nodiscard]]
static std::uint32_t to_uint32_mask(
  bool b)
{
    return b ? 0xffffffffu : 0u;
};

/*
 * texture_attachment_binding.
 */

bool texture_attachment_binding::is_valid() const
{
    if(tex_id == default_tex_id
       || tex == nullptr
       || info.data_ptr == nullptr)
    {
        return false;
    }

    const auto* color_texture = tex->as_texture_color_2d();
    if(color_texture == nullptr
       || level >= color_texture->data.data_ptrs.size())
    {
        return false;
    }

    if(!global_context->texture_2d_storage.contains(tex_id))
    {
        return false;
    }

    if(!global_context->texture_2d_storage[tex_id])
    {
        return false;
    }

    if(global_context->has_pending_deletion(resource_type::texture, tex_id))
    {
        return false;
    }

    return global_context->texture_2d_storage[tex_id].get() == tex
           && tex_id == tex->id
           && info.data_ptr == color_texture->data.data_ptrs[level];
}

bool depth_texture_attachment_binding::is_valid() const
{
    if(tex_id == default_tex_id
       || tex == nullptr
       || info.data_ptr == nullptr)
    {
        return false;
    }

    const auto* depth_texture = tex->as_texture_depth_2d();
    if(depth_texture == nullptr
       || level >= depth_texture->data.data_ptrs.size())
    {
        return false;
    }

    if(!global_context->texture_2d_storage.contains(tex_id))
    {
        return false;
    }

    if(!global_context->texture_2d_storage[tex_id])
    {
        return false;
    }

    return global_context->texture_2d_storage[tex_id].get() == tex
           && tex_id == tex->id
           && info.data_ptr == depth_texture->data.data_ptrs[level];
}

bool depth_renderbuffer_attachment_binding::is_valid() const
{
    if(attachment == nullptr
       || info.data_ptr == nullptr)
    {
        return false;
    }

    if(!global_context->depth_attachments.contains(attachment_id))
    {
        return false;
    }

    for(const auto& res: global_context->pending_resource_deletions)
    {
        if(res.type == resource_type::depth_attachment
           && res.id == attachment_id)
        {
            return false;
        }
    }

    return &global_context->depth_attachments[attachment_id] == attachment
           && info.data_ptr == attachment->info.data_ptr
           && info.width == attachment->info.width
           && info.height == attachment->info.height
           && info.stride == attachment->info.stride;
}

/*
 * Default framebuffer.
 */

void default_framebuffer::clear_color(
  std::uint32_t attachment,
  ml::vec4 clear_color)
{
    if(attachment == 0)
    {
        auto& info = color_buffer.info;
        utils::memset32(
          info.data_ptr,
          color_buffer.converter.to_pixel(clear_color),
          info.stride * info.height);
    }
}

void default_framebuffer::clear_color(
  std::uint32_t attachment,
  ml::vec4 clear_color,
  const utils::rect& rect)
{
    if(attachment == 0)
    {
        auto clear_value = color_buffer.converter.to_pixel(clear_color);

        const int height = static_cast<int>(color_buffer.info.height);

        const std::size_t x_min = utils::clamp_to_size(
          rect.x_min,
          color_buffer.info.width);
        const std::size_t x_max = utils::clamp_to_size(
          rect.x_max,
          color_buffer.info.width);
        const std::size_t y_min = utils::clamp_to_size(
          height - rect.y_max,
          color_buffer.info.height);
        const std::size_t y_max = utils::clamp_to_size(
          height - rect.y_min,
          color_buffer.info.height);

        const auto row_bytes = (x_max - x_min) * sizeof(std::uint32_t);

        auto ptr = reinterpret_cast<std::uint8_t*>(color_buffer.info.data_ptr)
                   + y_min * color_buffer.info.stride
                   + x_min * sizeof(std::uint32_t);
        for(std::size_t y = y_min; y < y_max; ++y)
        {
            utils::memset32(
              ptr,
              clear_value,
              row_bytes);
            ptr += color_buffer.info.stride;
        }
    }
}

void default_framebuffer::clear_depth(
  ml::fixed_32_t clear_depth)
{
    auto& info = depth_buffer.info;
    if(info.data_ptr)
    {
        utils::memset32(
          reinterpret_cast<std::uint32_t*>(info.data_ptr),
          ml::unwrap(clear_depth),
          info.stride * info.height);
    }
}

void default_framebuffer::clear_depth(
  ml::fixed_32_t clear_depth,
  const utils::rect& rect)
{
    const int height = static_cast<int>(depth_buffer.info.height);

    const std::size_t x_min = utils::clamp_to_size(
      rect.x_min,
      depth_buffer.info.width);
    const std::size_t x_max = utils::clamp_to_size(
      rect.x_max,
      depth_buffer.info.width);
    const std::size_t y_min = utils::clamp_to_size(
      height - rect.y_max,
      depth_buffer.info.height);
    const std::size_t y_max = utils::clamp_to_size(
      height - rect.y_min,
      depth_buffer.info.height);

    const auto row_size = (x_max - x_min) * sizeof(ml::fixed_32_t);

    auto ptr = reinterpret_cast<std::uint8_t*>(depth_buffer.info.data_ptr)
               + y_min * depth_buffer.info.stride
               + x_min * sizeof(ml::fixed_32_t);
    for(std::size_t y = y_min; y < y_max; ++y)
    {
        utils::memset32(
          ptr,
          *reinterpret_cast<std::uint32_t*>(&clear_depth),
          row_size);
        ptr += depth_buffer.info.stride;
    }
}

void default_framebuffer::merge_color(
  std::uint32_t attachment,
  int x,
  int y,
  const fragment_output& frag,
  bool do_blend,
  blend_func blend_src,
  blend_func blend_dst)
{
    if(attachment != 0)
    {
        return;
    }

    if(frag.write_flags & fragment_output_flags::write_color)
    {
        // convert color to output format.
        std::uint32_t write_color = color_buffer.converter.to_pixel(
          ml::clamp_to_unit_interval(frag.color));

        // alpha blending.
        const int row_stride = color_buffer.info.stride / static_cast<int>(sizeof(attachment_color_buffer::value_type));
        std::uint32_t* color_buffer_ptr = color_buffer.info.data_ptr + y * row_stride + x;
        if(do_blend)
        {
            write_color = swr::output_merger::blend(
              color_buffer.converter,
              blend_src,
              blend_dst,
              write_color,
              *color_buffer_ptr);
        }

        // write color.
        *color_buffer_ptr = write_color;
    }
}

void default_framebuffer::merge_color_block(
  std::uint32_t attachment,
  int x,
  int y,
  const fragment_output_block& frag,
  bool do_blend,
  blend_func blend_src,
  blend_func blend_dst)
{
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_merge = 0;
    utils::clock(stage_merge);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    if(attachment != 0)
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_merge);
        swr::impl::profile_merge_cycles.fetch_add(stage_merge, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
        return;
    }

    const int row_stride = color_buffer.info.stride / static_cast<int>(sizeof(attachment_color_buffer::value_type));
    const bool block_in_bounds = (x + 1 < color_buffer.info.width)
                                 && (y + 1 < color_buffer.info.height);

    if(frag.write_color_mask && block_in_bounds)
    {
        // convert color to output format.
        std::array<std::uint32_t, 4> write_color = {
          color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[0])),
          color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[1])),
          color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[2])),
          color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[3]))};

        // alpha blending and writeback on hot fully in-bounds path.
        const std::array<std::uint32_t*, 4> color_buffer_ptr = {
          color_buffer.info.data_ptr + y * row_stride + x,
          color_buffer.info.data_ptr + y * row_stride + (x + 1),
          color_buffer.info.data_ptr + (y + 1) * row_stride + x,
          color_buffer.info.data_ptr + (y + 1) * row_stride + (x + 1)};

        const std::array<std::uint32_t, 4> color_buffer_values = {
          *color_buffer_ptr[0],
          *color_buffer_ptr[1],
          *color_buffer_ptr[2],
          *color_buffer_ptr[3]};

        if(do_blend)
        {
            // note: when compiling with SSE/SIMD enabled, make sure that src/dest/out are aligned on 16-byte boundaries.
            swr::output_merger::blend_block(
              color_buffer.converter,
              blend_src,
              blend_dst,
              write_color,
              color_buffer_values,
              write_color);
        }

        const std::array<std::uint32_t, 4> color_write_mask = {
          to_uint32_mask((frag.write_color_mask & 0x8) != 0),
          to_uint32_mask((frag.write_color_mask & 0x4) != 0),
          to_uint32_mask((frag.write_color_mask & 0x2) != 0),
          to_uint32_mask((frag.write_color_mask & 0x1) != 0)};

        *(color_buffer_ptr[0]) = (color_buffer_values[0] & ~color_write_mask[0]) | (write_color[0] & color_write_mask[0]);
        *(color_buffer_ptr[1]) = (color_buffer_values[1] & ~color_write_mask[1]) | (write_color[1] & color_write_mask[1]);
        *(color_buffer_ptr[2]) = (color_buffer_values[2] & ~color_write_mask[2]) | (write_color[2] & color_write_mask[2]);
        *(color_buffer_ptr[3]) = (color_buffer_values[3] & ~color_write_mask[3]) | (write_color[3] & color_write_mask[3]);
    }
    else if(frag.write_color_mask)
    {
        const bool x0_valid = x < color_buffer.info.width;
        const bool x1_valid = (x + 1) < color_buffer.info.width;
        const bool y0_valid = y < color_buffer.info.height;
        const bool y1_valid = (y + 1) < color_buffer.info.height;

        const std::uint8_t valid_mask = static_cast<std::uint8_t>(
          (static_cast<std::uint8_t>(x0_valid && y0_valid) << 3)
          | (static_cast<std::uint8_t>(x1_valid && y0_valid) << 2)
          | (static_cast<std::uint8_t>(x0_valid && y1_valid) << 1)
          | static_cast<std::uint8_t>(x1_valid && y1_valid));
        const std::uint8_t write_mask = frag.write_color_mask & valid_mask;

        if(write_mask)
        {
            std::array<std::uint32_t, 4> write_color = {
              color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[0])),
              color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[1])),
              color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[2])),
              color_buffer.converter.to_pixel(ml::clamp_to_unit_interval(frag.color[3]))};

            const std::array<std::uint32_t*, 4> color_buffer_ptr = {
              color_buffer.info.data_ptr + y * row_stride + x,
              color_buffer.info.data_ptr + y * row_stride + (x + 1),
              color_buffer.info.data_ptr + (y + 1) * row_stride + x,
              color_buffer.info.data_ptr + (y + 1) * row_stride + (x + 1)};

            std::array<std::uint32_t, 4> color_buffer_values;

            if(write_mask & 0x8)
            {
                color_buffer_values[0] = *color_buffer_ptr[0];
            }
            if(write_mask & 0x4)
            {
                color_buffer_values[1] = *color_buffer_ptr[1];
            }
            if(write_mask & 0x2)
            {
                color_buffer_values[2] = *color_buffer_ptr[2];
            }
            if(write_mask & 0x1)
            {
                color_buffer_values[3] = *color_buffer_ptr[3];
            }

            if(do_blend)
            {
                swr::output_merger::blend_block(
                  color_buffer.converter,
                  blend_src,
                  blend_dst,
                  write_color,
                  color_buffer_values,
                  write_color);
            }

            if(write_mask & 0x8)
            {
                *color_buffer_ptr[0] = write_color[0];
            }
            if(write_mask & 0x4)
            {
                *color_buffer_ptr[1] = write_color[1];
            }
            if(write_mask & 0x2)
            {
                *color_buffer_ptr[2] = write_color[2];
            }
            if(write_mask & 0x1)
            {
                *color_buffer_ptr[3] = write_color[3];
            }
        }
    }
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_merge);
    swr::impl::profile_merge_cycles.fetch_add(stage_merge, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

void default_framebuffer::depth_compare_write(
  int x,
  int y,
  float depth_value,
  comparison_func depth_func,
  bool write_depth,
  bool& write_mask)
{
    // discard fragment if depth testing is always failing.
    if(depth_func == swr::comparison_func::fail)
    {
        write_mask = false;
        return;
    }

    write_mask = true;

    // if no depth buffer was created, accept.
    if(!depth_buffer.info.data_ptr)
    {
        return;
    }

    const int row_stride = depth_buffer.info.stride / static_cast<int>(sizeof(attachment_depth::value_type));

    // read and compare depth buffer.
    ml::fixed_32_t* const depth_buffer_ptr = depth_buffer.info.data_ptr + y * row_stride + x;
    const ml::fixed_32_t old_depth_value = *depth_buffer_ptr;
    const ml::fixed_32_t new_depth_value{depth_value};

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
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::not_equal)] =
      !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)] =
      depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less)]
      || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)] =
      !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater_equal)] =
      depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)]
      || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];

    // generate write mask for this fragment.
    write_mask &= depth_compare[static_cast<std::uint32_t>(depth_func)];

    // write depth value.
    const std::uint32_t depth_write_mask = to_uint32_mask(write_depth && write_mask);
    *depth_buffer_ptr = ml::wrap(
      (ml::unwrap(*depth_buffer_ptr) & ~depth_write_mask)
      | (ml::unwrap(new_depth_value) & depth_write_mask));
}

void default_framebuffer::depth_compare_write_block(
  int x,
  int y,
  const std::array<float, 4>& depth_value,
  comparison_func depth_func,
  bool write_depth,
  std::uint8_t& write_mask)
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

    const int row_stride = depth_buffer.info.stride / static_cast<int>(sizeof(attachment_depth::value_type));
    const bool block_in_bounds = (x + 1 < depth_buffer.info.width)
                                 && (y + 1 < depth_buffer.info.height);

    // read and compare depth buffer.
    const std::array<ml::fixed_32_t*, 4> depth_buffer_ptr = {
      depth_buffer.info.data_ptr + y * row_stride + x,
      depth_buffer.info.data_ptr + y * row_stride + (x + 1),
      depth_buffer.info.data_ptr + (y + 1) * row_stride + x,
      depth_buffer.info.data_ptr + (y + 1) * row_stride + (x + 1)};

    std::uint8_t active_mask = write_mask;
    if(!block_in_bounds)
    {
        const bool x0_valid = x < depth_buffer.info.width;
        const bool x1_valid = (x + 1) < depth_buffer.info.width;
        const bool y0_valid = y < depth_buffer.info.height;
        const bool y1_valid = (y + 1) < depth_buffer.info.height;
        const std::uint8_t valid_mask = static_cast<std::uint8_t>(
          (static_cast<std::uint8_t>(x0_valid && y0_valid) << 3)
          | (static_cast<std::uint8_t>(x1_valid && y0_valid) << 2)
          | (static_cast<std::uint8_t>(x0_valid && y1_valid) << 1)
          | static_cast<std::uint8_t>(x1_valid && y1_valid));
        active_mask &= valid_mask;
        if(!active_mask)
        {
            write_mask = 0;
            return;
        }
    }

    std::array<ml::fixed_32_t, 4> old_depth_value = {};
    if(block_in_bounds)
    {
        old_depth_value[0] = *depth_buffer_ptr[0];
        old_depth_value[1] = *depth_buffer_ptr[1];
        old_depth_value[2] = *depth_buffer_ptr[2];
        old_depth_value[3] = *depth_buffer_ptr[3];
    }
    else
    {
        if(active_mask & 0x8)
        {
            old_depth_value[0] = *depth_buffer_ptr[0];
        }
        if(active_mask & 0x4)
        {
            old_depth_value[1] = *depth_buffer_ptr[1];
        }
        if(active_mask & 0x2)
        {
            old_depth_value[2] = *depth_buffer_ptr[2];
        }
        if(active_mask & 0x1)
        {
            old_depth_value[3] = *depth_buffer_ptr[3];
        }
    }
    const std::array<ml::fixed_32_t, 4> new_depth_value = {
      depth_value[0],
      depth_value[1],
      depth_value[2],
      depth_value[3]};

    // basic comparisons for depth test.
    std::array<std::array<bool, 4>, 8> depth_compare = {{
      {true, true, true, true},     /* pass */
      {false, false, false, false}, /* fail */
      {new_depth_value[0] == old_depth_value[0],
       new_depth_value[1] == old_depth_value[1],
       new_depth_value[2] == old_depth_value[2],
       new_depth_value[3] == old_depth_value[3]}, /* equal */
      {false, false, false, false},               /* not_equal */
      {new_depth_value[0] < old_depth_value[0],
       new_depth_value[1] < old_depth_value[1],
       new_depth_value[2] < old_depth_value[2],
       new_depth_value[3] < old_depth_value[3]}, /* less */
      {false, false, false, false},              /* less_equal */
      {false, false, false, false},              /* greater */
      {false, false, false, false}               /* greater_equal */
    }};

    using block_t = decltype(depth_compare)::value_type;
    constexpr std::size_t block_size = std::tuple_size<block_t>::value;

    // compound comparisons for depth test.
    for(std::size_t k = 0; k < block_size; ++k)
    {
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::not_equal)][k] =
          !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)][k] =
          depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less)][k]
          || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)][k] =
          !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater_equal)][k] =
          depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)][k]
          || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
    }

    const std::array<bool, 4> depth_mask = {
      depth_compare[static_cast<std::uint32_t>(depth_func)][0],
      depth_compare[static_cast<std::uint32_t>(depth_func)][1],
      depth_compare[static_cast<std::uint32_t>(depth_func)][2],
      depth_compare[static_cast<std::uint32_t>(depth_func)][3]};

    write_mask &= (depth_mask[0] << 3) | (depth_mask[1] << 2) | (depth_mask[2] << 1) | depth_mask[3];
    write_mask &= active_mask;

    // write depth.
    const std::array<std::uint32_t, 4> depth_write_mask = {
      to_uint32_mask((write_mask & 0x8) != 0 && write_depth),
      to_uint32_mask((write_mask & 0x4) != 0 && write_depth),
      to_uint32_mask((write_mask & 0x2) != 0 && write_depth),
      to_uint32_mask((write_mask & 0x1) != 0 && write_depth)};

    if(block_in_bounds)
    {
        *(depth_buffer_ptr[0]) = ml::wrap(
          (ml::unwrap(*(depth_buffer_ptr[0])) & ~depth_write_mask[0])
          | (ml::unwrap(new_depth_value[0]) & depth_write_mask[0]));
        *(depth_buffer_ptr[1]) = ml::wrap(
          (ml::unwrap(*(depth_buffer_ptr[1])) & ~depth_write_mask[1])
          | (ml::unwrap(new_depth_value[1]) & depth_write_mask[1]));
        *(depth_buffer_ptr[2]) = ml::wrap(
          (ml::unwrap(*(depth_buffer_ptr[2])) & ~depth_write_mask[2])
          | (ml::unwrap(new_depth_value[2]) & depth_write_mask[2]));
        *(depth_buffer_ptr[3]) = ml::wrap(
          (ml::unwrap(*(depth_buffer_ptr[3])) & ~depth_write_mask[3])
          | (ml::unwrap(new_depth_value[3]) & depth_write_mask[3]));
    }
    else
    {
        if(depth_write_mask[0])
        {
            *(depth_buffer_ptr[0]) = new_depth_value[0];
        }
        if(depth_write_mask[1])
        {
            *(depth_buffer_ptr[1]) = new_depth_value[1];
        }
        if(depth_write_mask[2])
        {
            *(depth_buffer_ptr[2]) = new_depth_value[2];
        }
        if(depth_write_mask[3])
        {
            *(depth_buffer_ptr[3]) = new_depth_value[3];
        }
    }
}

/*
 * framebuffer_object
 */

void framebuffer_object::clear_color(
  std::uint32_t attachment,
  ml::vec4 clear_color)
{
    if(attachment < color_bindings.size()
       && color_bindings[attachment])
    {
        // this also clears mipmaps, if present
        auto& info = color_bindings[attachment]->info;
#ifdef SWR_USE_SIMD
        utils::memset128(info.data_ptr, *reinterpret_cast<__m128i*>(&clear_color.data), info.stride * info.height * sizeof(__m128));
#else  /* SWR_USE_SIMD */
        std::fill_n(info.data_ptr, info.stride * info.height, clear_color);
#endif /* SWR_USE_SIMD */
    }
}

void framebuffer_object::clear_color(
  std::uint32_t attachment,
  ml::vec4 clear_color,
  const utils::rect& rect)
{
    if(attachment < color_bindings.size()
       && color_bindings[attachment])
    {
        auto& info = color_bindings[attachment]->info;

        const int height = static_cast<int>(info.height);

        const std::size_t x_min = utils::clamp_to_size(
          rect.x_min,
          info.width);
        const std::size_t x_max = utils::clamp_to_size(
          rect.x_max,
          info.width);
        const std::size_t y_min = utils::clamp_to_size(
          height - rect.y_max,
          info.height);
        const std::size_t y_max = utils::clamp_to_size(
          height - rect.y_min,
          info.height);

#ifdef SWR_USE_MORTON_CODES
        for(std::size_t x = x_min; x < x_max; ++x)
        {
            for(std::size_t y = y_min; y < y_max; ++y)
            {
                *(info.data_ptr + libmorton::morton2D_32_encode(x, y)) = clear_color;
            }
        }
#else
        const auto row_size = x_max - x_min;

#    ifdef SWR_USE_SIMD
        auto ptr = info.data_ptr + y_min * info.stride + x_min;
        for(int y = y_min; y < y_max; ++y)
        {
            utils::memset128(ptr, *reinterpret_cast<__m128i*>(&clear_color.data), row_size * sizeof(__m128));
            ptr += info.stride;
        }
#    else  /* SWR_USE_SIMD */
        const auto skip = info.stride - row_size;
        auto ptr = info.data_ptr + y_min * info.stride + x_min;
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

void framebuffer_object::clear_depth(
  ml::fixed_32_t clear_depth)
{
    if(const auto* info = get_depth_attachment_info();
       info && info->data_ptr)
    {
        utils::memset32(
          reinterpret_cast<std::uint32_t*>(info->data_ptr),
          ml::unwrap(clear_depth),
          info->stride * info->height);
    }
}

void framebuffer_object::clear_depth(
  ml::fixed_32_t clear_depth,
  const utils::rect& rect)
{
    if(const auto* info = get_depth_attachment_info();
       info && info->data_ptr)
    {
        const int height = static_cast<int>(info->height);

        const std::size_t x_min = utils::clamp_to_size(
          rect.x_min,
          info->width);
        const std::size_t x_max = utils::clamp_to_size(
          rect.x_max,
          info->width);
        const std::size_t y_min = utils::clamp_to_size(
          height - rect.y_max,
          info->height);
        const std::size_t y_max = utils::clamp_to_size(
          height - rect.y_min,
          info->height);

#ifdef SWR_USE_MORTON_CODES
        for(std::size_t x = x_min; x < x_max; ++x)
        {
            for(std::size_t y = y_min; y < y_max; ++y)
            {
                *(info->data_ptr + libmorton::morton2D_32_encode(x, y)) = clear_depth;
            }
        }
#else
        const auto row_size = (x_max - x_min) * sizeof(ml::fixed_32_t);

        auto ptr = reinterpret_cast<std::uint8_t*>(info->data_ptr) + y_min * info->stride + x_min * sizeof(ml::fixed_32_t);
        for(int y = y_min; y < y_max; ++y)
        {
            utils::memset32(ptr, *reinterpret_cast<std::uint32_t*>(&clear_depth), row_size);
            ptr += info->stride;
        }
#endif /* SWR_USE_MORTON_CODES */
    }
}

void framebuffer_object::merge_color(
  std::uint32_t attachment,
  int x,
  int y,
  const fragment_output& frag,
  bool do_blend,
  blend_func blend_src,
  blend_func blend_dst)
{
    if(attachment > color_bindings.size()
       || !color_bindings[attachment])
    {
        return;
    }

    if(frag.write_flags & fragment_output_flags::write_color)
    {
        ml::vec4 write_color{ml::clamp_to_unit_interval(frag.color)};

        ml::vec4* data_ptr = color_bindings[attachment]->info.data_ptr;

        // alpha blending.
#ifdef SWR_USE_MORTON_CODES
        ml::vec4* color_buffer_ptr = data_ptr + libmorton::morton2D_32_encode(x, y);
#else
        int stride = color_bindings[attachment]->info.stride;
        ml::vec4* color_buffer_ptr = data_ptr + y * stride + x;
#endif
        if(do_blend)
        {
            write_color = swr::output_merger::blend(blend_src, blend_dst, write_color, *color_buffer_ptr);
        }

        // write color.
        *color_buffer_ptr = write_color;
    }
}

void framebuffer_object::merge_color_block(
  std::uint32_t attachment,
  int x,
  int y,
  const fragment_output_block& frag,
  bool do_blend,
  blend_func blend_src,
  blend_func blend_dst)
{
#ifdef SWR_ENABLE_PIPELINE_PROFILING
    std::uint64_t stage_merge = 0;
    utils::clock(stage_merge);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
    if(attachment > color_bindings.size() || !color_bindings[attachment])
    {
#ifdef SWR_ENABLE_PIPELINE_PROFILING
        utils::unclock(stage_merge);
        swr::impl::profile_merge_cycles.fetch_add(stage_merge, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
        return;
    }

    if(frag.write_color_mask)
    {
        // convert color to output format.
        std::array<ml::vec4, 4> write_color = {
          ml::clamp_to_unit_interval(frag.color[0]),
          ml::clamp_to_unit_interval(frag.color[1]),
          ml::clamp_to_unit_interval(frag.color[2]),
          ml::clamp_to_unit_interval(frag.color[3])};

        ml::vec4* data_ptr = color_bindings[attachment]->info.data_ptr;

        // block coordinates
        const std::array<ml::tvec2<int>, 4> coords =
          {{{x, y},
            {x + 1, y},
            {x, y + 1},
            {x + 1, y + 1}}};

        // alpha blending.
#ifdef SWR_USE_MORTON_CODES
        std::array<ml::vec4*, 4> color_buffer_ptrs = {
          data_ptr + libmorton::morton2D_32_encode(coords[0].x, coords[0].y),
          data_ptr + libmorton::morton2D_32_encode(coords[1].x, coords[1].y),
          data_ptr + libmorton::morton2D_32_encode(coords[2].x, coords[2].y),
          data_ptr + libmorton::morton2D_32_encode(coords[3].x, coords[3].y)};
#else
        int stride = color_bindings[attachment]->info.stride;
        std::array<ml::vec4*, 4> color_buffer_ptrs = {
          data_ptr + coords[0].y * stride + coords[0].x,
          data_ptr + coords[1].y * stride + coords[1].x,
          data_ptr + coords[2].y * stride + coords[2].x,
          data_ptr + coords[3].y * stride + coords[3].x};
#endif

        // check bounds for partial blocks
        const int width = color_bindings[attachment]->info.width;
        const int height = color_bindings[attachment]->info.height;
        const bool block_in_bounds = (x + 1 < width) && (y + 1 < height);

        if(block_in_bounds)
        {
            const std::array<ml::vec4, 4> color_buffer_values = {
              *color_buffer_ptrs[0],
              *color_buffer_ptrs[1],
              *color_buffer_ptrs[2],
              *color_buffer_ptrs[3]};

            if(do_blend)
            {
                swr::output_merger::blend_block(
                  blend_src,
                  blend_dst,
                  write_color,
                  color_buffer_values,
                  write_color);
            }

            // write color for fully in-bounds block
#define CONDITIONAL_WRITE(condition, write_target, write_source) \
    if(condition)                                                \
    {                                                            \
        write_target = write_source;                             \
    }

            CONDITIONAL_WRITE(((frag.write_color_mask & 0x8) >> 3), *(color_buffer_ptrs[0]), write_color[0]);
            CONDITIONAL_WRITE(((frag.write_color_mask & 0x4) >> 2), *(color_buffer_ptrs[1]), write_color[1]);
            CONDITIONAL_WRITE(((frag.write_color_mask & 0x2) >> 1), *(color_buffer_ptrs[2]), write_color[2]);
            CONDITIONAL_WRITE((frag.write_color_mask & 0x1), *(color_buffer_ptrs[3]), write_color[3]);

#undef CONDITIONAL_WRITE
        }
        else
        {
            const bool x0_valid = x < width;
            const bool x1_valid = (x + 1) < width;
            const bool y0_valid = y < height;
            const bool y1_valid = (y + 1) < height;
            const std::uint8_t valid_mask = static_cast<std::uint8_t>(
              (static_cast<std::uint8_t>(x0_valid && y0_valid) << 3)
              | (static_cast<std::uint8_t>(x1_valid && y0_valid) << 2)
              | (static_cast<std::uint8_t>(x0_valid && y1_valid) << 1)
              | static_cast<std::uint8_t>(x1_valid && y1_valid));

            std::uint8_t write_mask = static_cast<std::uint8_t>(frag.write_color_mask) & valid_mask;
            if(write_mask)
            {
                std::array<ml::vec4, 4> color_buffer_values;    // default-init

                if(write_mask & 0x8)
                {
                    color_buffer_values[0] = *color_buffer_ptrs[0];
                }
                if(write_mask & 0x4)
                {
                    color_buffer_values[1] = *color_buffer_ptrs[1];
                }
                if(write_mask & 0x2)
                {
                    color_buffer_values[2] = *color_buffer_ptrs[2];
                }
                if(write_mask & 0x1)
                {
                    color_buffer_values[3] = *color_buffer_ptrs[3];
                }

                if(do_blend)
                {
                    swr::output_merger::blend_block(
                      blend_src,
                      blend_dst,
                      write_color,
                      color_buffer_values,
                      write_color);
                }

                if(write_mask & 0x8)
                {
                    *(color_buffer_ptrs[0]) = write_color[0];
                }
                if(write_mask & 0x4)
                {
                    *(color_buffer_ptrs[1]) = write_color[1];
                }
                if(write_mask & 0x2)
                {
                    *(color_buffer_ptrs[2]) = write_color[2];
                }
                if(write_mask & 0x1)
                {
                    *(color_buffer_ptrs[3]) = write_color[3];
                }
            }
        }
    }

#ifdef SWR_ENABLE_PIPELINE_PROFILING
    utils::unclock(stage_merge);
    swr::impl::profile_merge_cycles.fetch_add(stage_merge, std::memory_order_relaxed);
#endif /* SWR_ENABLE_PIPELINE_PROFILING */
}

// FIXME this is almost exactly the same as default_framebuffer::depth_compare_write.
void framebuffer_object::depth_compare_write(
  int x,
  int y,
  float depth_value,
  comparison_func depth_func,
  bool write_depth,
  bool& write_mask)
{
    // discard fragment if depth testing is always failing.
    if(depth_func == swr::comparison_func::fail)
    {
        write_mask = false;
        return;
    }

    write_mask = true;

    // if no depth buffer was created, accept.
    const auto* depth_info = get_depth_attachment_info();
    if(!depth_info || !depth_info->data_ptr)
    {
        return;
    }

    // read and compare depth buffer.
#ifdef SWR_USE_MORTON_CODES
    ml::fixed_32_t* depth_buffer_ptr = depth_info->data_ptr + libmorton::morton2D_32_encode(x, y);
#else
    const int row_stride = depth_info->stride / static_cast<int>(sizeof(attachment_depth::value_type));
    ml::fixed_32_t* depth_buffer_ptr = depth_info->data_ptr + y * row_stride + x;
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
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::not_equal)] =
      !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)] =
      depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less)]
      || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)] =
      !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)];
    depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater_equal)] =
      depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)]
      || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)];

    // generate write mask for this fragment.
    write_mask &= depth_compare[static_cast<std::uint32_t>(depth_func)];

    // write depth value.
    std::uint32_t depth_write_mask = to_uint32_mask(write_depth && write_mask);
    *depth_buffer_ptr = ml::wrap((ml::unwrap(*depth_buffer_ptr) & ~depth_write_mask) | (ml::unwrap(new_depth_value) & depth_write_mask));
}

// FIXME this is almost exactly the same as default_framebuffer::depth_compare_write_block.
void framebuffer_object::depth_compare_write_block(
  int x,
  int y,
  const std::array<float, 4>& depth_value,
  comparison_func depth_func,
  bool write_depth,
  std::uint8_t& write_mask)
{
    // discard fragment if depth testing is always failing.
    if(depth_func == swr::comparison_func::fail)
    {
        write_mask = 0;
        return;
    }

    // if no depth buffer was created, accept.
    const auto* depth_info = get_depth_attachment_info();
    if(!depth_info || !depth_info->data_ptr)
    {
        // the write mask is initialized with "accept all".
        return;
    }

    // block coordinates
    const std::array<ml::tvec2<int>, 4> coords =
      {{{x, y},
        {x + 1, y},
        {x, y + 1},
        {x + 1, y + 1}}};

    // read and compare depth buffer.
#ifdef SWR_USE_MORTON_CODES
    std::array<ml::fixed_32_t*, 4> depth_buffer_ptr = {
      depth_info->data_ptr + libmorton::morton2D_32_encode(coords[0].x, coords[0].y),
      depth_info->data_ptr + libmorton::morton2D_32_encode(coords[1].x, coords[1].y),
      depth_info->data_ptr + libmorton::morton2D_32_encode(coords[2].x, coords[2].y),
      depth_info->data_ptr + libmorton::morton2D_32_encode(coords[3].x, coords[3].y)};
#else
    const int row_stride = depth_info->stride / static_cast<int>(sizeof(attachment_depth::value_type));
    std::array<ml::fixed_32_t*, 4> depth_buffer_ptr = {
      depth_info->data_ptr + coords[0].y * row_stride + coords[0].x,
      depth_info->data_ptr + coords[1].y * row_stride + coords[1].x,
      depth_info->data_ptr + coords[2].y * row_stride + coords[2].x,
      depth_info->data_ptr + coords[3].y * row_stride + coords[3].x};
#endif

    const int width = depth_info->width;
    const int height = depth_info->height;
    const bool block_in_bounds = (x + 1 < width) && (y + 1 < height);

    std::array<ml::fixed_32_t, 4> old_depth_value = {};
    if(block_in_bounds)
    {
        old_depth_value[0] = *depth_buffer_ptr[0];
        old_depth_value[1] = *depth_buffer_ptr[1];
        old_depth_value[2] = *depth_buffer_ptr[2];
        old_depth_value[3] = *depth_buffer_ptr[3];
    }
    else
    {
        const bool x0_valid = x < width;
        const bool x1_valid = (x + 1) < width;
        const bool y0_valid = y < height;
        const bool y1_valid = (y + 1) < height;
        const std::uint8_t valid_mask = static_cast<std::uint8_t>(
          (static_cast<std::uint8_t>(x0_valid && y0_valid) << 3)
          | (static_cast<std::uint8_t>(x1_valid && y0_valid) << 2)
          | (static_cast<std::uint8_t>(x0_valid && y1_valid) << 1)
          | static_cast<std::uint8_t>(x1_valid && y1_valid));

        std::uint8_t active_mask = write_mask & valid_mask;
        if(!active_mask)
        {
            write_mask = 0;
            return;
        }

        if(active_mask & 0x8)
        {
            old_depth_value[0] = *depth_buffer_ptr[0];
        }
        if(active_mask & 0x4)
        {
            old_depth_value[1] = *depth_buffer_ptr[1];
        }
        if(active_mask & 0x2)
        {
            old_depth_value[2] = *depth_buffer_ptr[2];
        }
        if(active_mask & 0x1)
        {
            old_depth_value[3] = *depth_buffer_ptr[3];
        }
        // ensure write_mask reflects only active pixels
        write_mask &= active_mask;
    }

    const std::array<ml::fixed_32_t, 4> new_depth_value = {
      depth_value[0],
      depth_value[1],
      depth_value[2],
      depth_value[3]};

    // basic comparisons for depth test.
    std::array<std::array<bool, 4>, 8> depth_compare = {{
      {true, true, true, true},     /* pass */
      {false, false, false, false}, /* fail */
      {new_depth_value[0] == old_depth_value[0],
       new_depth_value[1] == old_depth_value[1],
       new_depth_value[2] == old_depth_value[2],
       new_depth_value[3] == old_depth_value[3]}, /* equal */
      {false, false, false, false},               /* not_equal */
      {new_depth_value[0] < old_depth_value[0],
       new_depth_value[1] < old_depth_value[1],
       new_depth_value[2] < old_depth_value[2],
       new_depth_value[3] < old_depth_value[3]}, /* less */
      {false, false, false, false},              /* less_equal */
      {false, false, false, false},              /* greater */
      {false, false, false, false}               /* greater_equal */
    }};

    using block_t = decltype(depth_compare)::value_type;
    constexpr std::size_t block_size = std::tuple_size<block_t>::value;

    // compound comparisons for depth test.
    for(std::size_t k = 0; k < block_size; ++k)
    {
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::not_equal)][k] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)][k] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less)][k] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)][k] = !depth_compare[static_cast<std::uint32_t>(swr::comparison_func::less_equal)][k];
        depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater_equal)][k] = depth_compare[static_cast<std::uint32_t>(swr::comparison_func::greater)][k] || depth_compare[static_cast<std::uint32_t>(swr::comparison_func::equal)][k];
    }

    const std::array<bool, 4> depth_mask = {
      depth_compare[static_cast<std::uint32_t>(depth_func)][0],
      depth_compare[static_cast<std::uint32_t>(depth_func)][1],
      depth_compare[static_cast<std::uint32_t>(depth_func)][2],
      depth_compare[static_cast<std::uint32_t>(depth_func)][3]};

    write_mask &= (depth_mask[0] << 3) | (depth_mask[1] << 2) | (depth_mask[2] << 1) | depth_mask[3];

    // write depth.
    const std::array<std::uint32_t, 4> depth_write_mask = {
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
 * Framebuffer object interface.
 */

std::uint32_t CreateFramebufferObject()
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    auto slot = context->framebuffer_objects.insert();
    return impl::framebuffer_slot_to_id(slot);
}

void ReleaseFramebufferObject(
  std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(id == impl::default_framebuffer_id)
    {
        // do not release default framebuffer.
        return;
    }

    auto slot = impl::framebuffer_id_to_slot(id);

    if(!context->framebuffer_objects.contains(slot))
    {
        // Ignore unknown ids.
        return;
    }

    // check if we are bound to a target and reset the target if necessary.
    if(context->states.draw_target == id)
    {
        context->states.draw_target = impl::default_framebuffer_id;
    }

    // Mark buffer for deletion.
    // Duplications are resolved when processing deletions.
    context->pending_resource_deletions.push_back(
      {.type = impl::resource_type::framebuffer_object,
       .id = id});
}

void BindFramebufferObject(
  framebuffer_target target,
  std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(id == impl::default_framebuffer_id)
    {
        context->states.draw_target = impl::default_framebuffer_id;
        return;
    }

    // check that the id is valid.
    auto slot = impl::framebuffer_id_to_slot(id);
    if(!context->framebuffer_objects.contains(slot))
    {
        context->last_error = error::invalid_value;
        return;
    }

    if(target == framebuffer_target::draw
       || target == framebuffer_target::draw_read)
    {
        context->states.draw_target = id;
    }
    else if(target == framebuffer_target::read
            || target == framebuffer_target::draw_read)
    {
        /* unimplemented. */
        context->last_error = error::unimplemented;
    }
}

void FramebufferTexture(
  std::uint32_t id,
  framebuffer_attachment attachment,
  std::uint32_t attachment_id,
  std::uint32_t level)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(id == impl::default_framebuffer_id)
    {
        // textures cannot be bound to the default framebuffer.
        context->last_error = error::invalid_value;
        return;
    }

    auto slot = impl::framebuffer_id_to_slot(id);
    if(!context->framebuffer_objects.contains(slot)
       || context->has_pending_deletion(impl::resource_type::framebuffer_object, id))
    {
        context->last_error = error::invalid_value;
        return;
    }

    const auto numeric_attachment = std::to_underlying(attachment);
    const bool is_color_attachment =
      numeric_attachment >= std::to_underlying(framebuffer_attachment::color_attachment_0)
      && numeric_attachment <= std::to_underlying(framebuffer_attachment::color_attachment_7);
    const bool is_depth_attachment = attachment == framebuffer_attachment::depth_attachment;
    if(!is_color_attachment && !is_depth_attachment)
    {
        context->last_error = error::invalid_value;
        return;
    }

    if(attachment_id == 0)
    {
        context->framebuffer_objects[slot].detach_logical_texture(attachment);
    }
    else
    {
        if(!context->texture_2d_storage.contains(attachment_id)
           || context->has_pending_deletion(impl::resource_type::texture, attachment_id))
        {
            context->last_error = error::invalid_value;
            return;
        }

        auto* texture = context->texture_2d_storage[attachment_id].get();
        if((texture->logical_format == pixel_format::depth32f) != is_depth_attachment
           || level >= texture->logical_mip_level_count())
        {
            context->last_error = error::invalid_value;
            return;
        }

        context->framebuffer_objects[slot].attach_logical_texture(
          attachment,
          attachment_id,
          level,
          texture->logical_width >> level,
          texture->logical_height >> level);
    }

    context->create_framebuffer_texture_command(id, attachment, attachment_id, level);
}

std::uint32_t CreateDepthRenderbuffer(
  std::uint32_t width,
  std::uint32_t height)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    auto slot = context->depth_attachments.insert();
    context->depth_attachments[slot].allocate(width, height);

    return slot;
}

void ReleaseDepthRenderbuffer(
  std::uint32_t id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    // Ignore unknown ids.
    if(context->depth_attachments.contains(id))
    {
        for(std::size_t slot = 0; slot < context->framebuffer_objects.slot_count(); ++slot)
        {
            if(context->framebuffer_objects.contains(slot))
            {
                context->framebuffer_objects[slot].detach_logical_depth_renderbuffer_resource(id);
            }
        }

        // Duplications are resolved when processing deletions.
        context->pending_resource_deletions.push_back(
          {.type = impl::resource_type::depth_attachment,
           .id = id});
    }
}

void FramebufferRenderbuffer(
  std::uint32_t id,
  framebuffer_attachment attachment,
  std::uint32_t attachment_id)
{
    ASSERT_INTERNAL_CONTEXT;
    impl::render_context* context = impl::global_context;

    if(id == impl::default_framebuffer_id)
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

    if(!context->depth_attachments.contains(attachment_id)
       || context->has_pending_deletion(impl::resource_type::depth_attachment, attachment_id))
    {
        context->last_error = error::invalid_value;
        return;
    }

    auto slot = impl::framebuffer_id_to_slot(id);
    if(!context->framebuffer_objects.contains(slot))
    {
        context->last_error = error::invalid_value;
        return;
    }

    context->framebuffer_objects[slot].set_logical_depth_renderbuffer_attachment(
      attachment_id,
      context->depth_attachments[attachment_id].info.width,
      context->depth_attachments[attachment_id].info.height);
    context->create_framebuffer_renderbuffer_command(id, attachment_id);
}

} /* namespace swr */
