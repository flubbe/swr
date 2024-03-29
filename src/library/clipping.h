/**
 * swr - a software rasterizer
 *
 * Clipping in homogeneous clip space.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace swr
{

namespace impl
{

/** the desired output of the triangle clipping function. */
enum clip_output
{
    point_list,   /* a list of points */
    line_list,    /* a list of lines */
    triangle_list /* a list of triangles */
};

/**
 * Clip a vertex buffer/index buffer pair against the view frustum. the index buffer/vertex buffer pair is assumed
 * to contain a line list, i.e., if i is divisible by 2, then in_ib[i] and in_ib[i+1] need to be indices into in_vb
 * forming a line.
 */
void clip_line_buffer(render_object& obj, clip_output output_type);

/**
 * Clip a vertex buffer/index buffer pair against the view frustum. the index buffer/vertex buffer pair is assumed
 * to contain a triangle list, i.e., if i is divisible by 3, then in_ib[i], in_ib[i+1] and in_ib[i+2] need to
 * be indices into in_vb forming a triangle.
 */
void clip_triangle_buffer(render_object& obj, clip_output output_type);

} /* namespace impl */

} /* namespace swr */
