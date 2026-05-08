/**
 * swr - a software rasterizer
 *
 * Clipping in homogeneous clip space.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

#include <cstddef>

#include "buffers.h"

namespace swr::impl
{

/*
 * Forward declarations.
 */

class render_object; /* renderobject.h */

/** the desired output of the triangle clipping function. */
enum clip_output
{
    point_list,   /* a list of points */
    line_list,    /* a list of lines */
    triangle_list /* a list of triangles */
};

/**
 * Clip a render object containing lines against the view frustum.
 *
 * @param obj The render object. Used for input and output.
 * @param output_type Output type of the clipping function.
 */
void clip_line_buffer(
  render_object& obj,
  clip_output output_type);

/**
 * Clip parts of a render object containing lines against the view frustum.
 *
 * @param obj The render object.
 * @param output_type Output type of the clipping function.
 * @param index_begin Start index of the buffer part.
 * @param index_end End index of the buffer part.
 * @param out_vertices Clipped output vertices.
 */
void clip_line_buffer_range(
  const render_object& obj,
  clip_output output_type,
  std::size_t index_begin,
  std::size_t index_end,
  vertex_buffer& out_vertices);

/**
 * Clip a render object containing triangles against the view frustum.
 *
 * @param obj The render object. Used for input and output.
 * @param output_type Output type of the clipping function.
 */
void clip_triangle_buffer(
  render_object& obj,
  clip_output output_type);

/**
 * Clip part of a render object containing triangles against the view frustum.
 *
 * @param obj The render object.
 * @param output_type Output type of the clipping function.
 * @param index_begin Start index of the buffer part.
 * @param index_end End index of the buffer part.
 * @param out_vertices Clipped output vertices.
 */
void clip_triangle_buffer_range(
  const render_object& obj,
  clip_output output_type,
  std::size_t index_begin,
  std::size_t index_end,
  vertex_buffer& out_vertices);

} /* namespace swr::impl */
