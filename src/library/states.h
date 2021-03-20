/**
 * swr - a software rasterizer
 * 
 * render pipeline state management.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <boost/container/static_vector.hpp>

namespace swr
{

namespace impl
{

/** States that are set on a per-primitive basis. */
struct render_states
{
    /** viewport transform. */
    int x{0}, y{0};
    unsigned int width{0}, height{0};
    float z_near{0}, z_far{1};

    /** scissor test. */
    bool scissor_test_enabled{false};
    utils::rect scissor_box;

    /* depth test. */
    bool depth_test_enabled{true};
    bool write_depth{true};
    comparison_func depth_func{comparison_func::less};

    /* culling. */
    bool culling_enabled{false};
    front_face_orientation front_face{front_face_orientation::ccw};
    cull_face_direction cull_mode{cull_face_direction::back};

    polygon_mode poly_mode{polygon_mode::fill};

    /* blending */
    bool blending_enabled{false};
    blend_func blend_src{blend_func::one};
    blend_func blend_dst{blend_func::zero};

    /* textures */
    struct texture_2d* tex_2d{nullptr};

    /* shaders */
    struct program_info* shader_info{nullptr};
    boost::container::static_vector<swr::uniform, geom::limits::max::uniform_locations> uniforms;
};

} /* namespace impl */

} /* namespace swr */
