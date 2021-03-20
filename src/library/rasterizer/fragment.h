/**
 * swr - a software rasterizer
 * 
 * fragment processing helpers.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace rast
{

/**
 * Information on a fragment which is passed on to the fragment shader.
 */
struct fragment_info
{
    /** Fragment z coordinate (within [0,1]), which may be written or compared to the depth buffer. */
    float depth_value;

    /** whether this fragment comes from a front-facing triangle. */
    bool front_facing;

    /** Veryings. */
    boost::container::static_vector<swr::varying, geom::limits::max::varyings>* Varyings = nullptr;

    /** no default constructor. */
    fragment_info() = delete;

    /** constructor. */
    fragment_info(
      float depth,
      bool in_front_facing,
      boost::container::static_vector<swr::varying, geom::limits::max::varyings>* InVaryings)
    : depth_value(depth)
    , front_facing(in_front_facing)
    , Varyings(InVaryings)
    {
    }
};

} /* namespace rast */