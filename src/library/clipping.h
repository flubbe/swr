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
	point_list,      /* a list of points */
	line_list,       /* a list of lines */
	triangle_list    /* a list of triangles */
};

/**
 * Clip a vertex buffer/index buffer pair against the view frustum. the index buffer/vertex buffer pair is assumed
 * to contain a triangle list, i.e., if i is divisible by 3, then in_ib[i], in_ib[i+1] and in_ib[i+2] need to
 * be indices into in_vb forming a triangle.
 */
void clip_triangle_buffer( const vertex_buffer& in_vb, const index_buffer& in_ib, clip_output output_type, vertex_buffer& out_vb );

} /* namespace impl */

} /* namespace swr */
