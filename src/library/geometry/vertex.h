/**
 * swr - a software rasterizer
 *
 * vertex definition.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

namespace geom
{

/** possible flags associated to a vertex. */
enum vertex_flags : std::uint32_t
{
    vf_none = 0,           /** no vertex flags set. */
    vf_line_strip_end = 1, /** this is the last vertex in a line strip. */
    vf_clip_discard = 2,   /** this vertex does not lie inside the view volume. */
    vf_interpolated = 4    /** this vertex was generated by interpolation. */
};

/** for compatibility: default positions of color, normal and texture coordinates inside the vertex attributes. */
enum default_index
{
    di_color = 0,
    di_tex_coord = 1,
    di_normal = 2,
    di_max = 3
};

/** Vertex format. */
struct vertex
{
    /**
     * coordinates at different stages of the pipeline.
     *
     * depending on the pipeline stage, these may contain any of:
     *  *) the vertex position relative
     *  *) the homogeneous clip coordinates
     *  *) the viewport coordinates
     */
    ml::vec4 coords;

    /** vertex attributes. */
    boost::container::static_vector<ml::vec4, limits::max::attributes> attribs;

    /** varyings. these are the vertex shader outputs. */
    boost::container::static_vector<ml::vec4, limits::max::varyings> varyings;

    /** vertex flags. */
    uint32_t flags{vf_none};

    /* default constructor. */
    vertex() = default;

    /** constructor initializing the vertex coordinates. */
    vertex(ml::vec4 in_coords)
    : coords(in_coords)
    {
    }
};

/**
 * Linear interpolate vertex data in clipping stage. This occurs after the vertex shader has been called,
 * so that we also interpolate vertex shader outputs (i.e., varyings).
 *
 * Interpolated data:
 *  *) clip coordinates
 *  *) varyings
 */
inline const vertex lerp(float t, const vertex& v1, const vertex& v2)
{
    vertex r;

    // interpolate coordinates.
    r.coords = ml::lerp(t, v1.coords, v2.coords);

    // interpolate varyings
    const auto varying_count = v1.varyings.size();
    r.varyings.resize(varying_count);
    for(size_t i = 0; i < varying_count; ++i)
    {
        // Depending on the interpolation type, Value stores either a the value of the attribute
        // itself or a weighted value, so that the equation does the correct interpolation.
        r.varyings[i] = ml::lerp(t, v1.varyings[i], v2.varyings[i]);
    }

    // mark interpolated vertex.
    r.flags |= vf_interpolated;

    return r;
}

} /* namespace geom */