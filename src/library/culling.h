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
inline cull_face_direction get_face_orientation(front_face_orientation ffo, const ml::vec2 v1, const ml::vec2 v2, const ml::vec2 v3)
{
    const auto area_sign = (v2 - v1).area_sign(v3 - v1);
    if((ffo == front_face_orientation::cw && area_sign >= 0) || (ffo == front_face_orientation::ccw && area_sign <= 0))
    {
        return cull_face_direction::front;
    }

    return cull_face_direction::back;
}

/** check if a given face orientation should be rejected based on the cull mode. */
inline bool cull_reject(cull_face_direction mode, cull_face_direction test_direction)
{
    return (mode == cull_face_direction::front_and_back) || (mode == test_direction);
}

} /* namespace impl */

} /* namespace swr */
