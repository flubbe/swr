/**
 * swr - a software rasterizer
 * 
 * face culling.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/** face culling */
namespace swr
{

namespace impl
{

/** get the orientation of a triangle */
cull_face_direction get_face_orientation( front_face_orientation ffd, const ml::vec2 v1, const ml::vec2 v2, const ml::vec2 v3 );

/** check if a given face orientation should be rejected based on the cull mode. */
bool cull_reject(cull_face_direction mode, cull_face_direction test_direction);

} /* namespace impl */

} /* namespace swr */

