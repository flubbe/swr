/**
 * swr - a software rasterizer
 * 
 * interpolators for quantities on lines and triangles.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace rast
{

/**
 * interpolator for varyings with one or two interpolation directions.
 * 
 * the advance_y method is geared towards the data living on an object with a left vertical edge.
 */
struct varying_interpolator : public swr::varying
{
    /** A input reference value (possibly weighted). */
    ml::vec4 input_value;

    /** Linear or weighted step (with respect to window coordinates). */
    ml::tvec2<ml::vec4> step;

    /** Differences along two linearly independent vectors. */
    ml::tvec2<ml::vec4> diffs;

    /** Value at the start of a row. */
    ml::vec4 row_start;

    /** Constructors. */
    varying_interpolator() = default;

    varying_interpolator(const varying& in_attrib, const ml::tvec2<ml::vec4>& in_step, const ml::tvec2<ml::vec4>& in_diffs)
    : varying(in_attrib)
    , input_value(in_attrib.value)
    , step(in_step)
    , diffs(in_diffs)
    , row_start(in_attrib.value)
    {
    }

    /** Initialize at a specific point. */
    void set_value_from_reference(float x, float y = 0)
    {
        value = input_value + diffs.x * x + diffs.y * y;
        row_start = value;
    }

    /** Step along the x-direction. */
    void advance_x()
    {
        value += step.x;
    }

    /** Step along y-direction and reset x-direction. */
    void advance_y()
    {
        row_start += step.y;
        value = row_start;
    }
};

/** basic interpolation data. */
template<typename T>
struct basic_interpolation_data
{
    const float reference_depth_value;
    const float reference_one_over_viewport_z;

    /** interpolated depth value for the depth buffer */
    T depth_value{};

    /** interpolated inverse viewport z value. */
    T one_over_viewport_z{};

    /** varyings from the shader. */
    boost::container::static_vector<varying_interpolator, geom::limits::max::varyings> varyings;

    /** initialize constant data. */
    basic_interpolation_data(float ref_depth, float ref_one_over_viewport_z)
    : reference_depth_value(ref_depth)
    , reference_one_over_viewport_z(ref_one_over_viewport_z)
    {
    }
};

/** Interpolate vertex varyings along lines. */
struct line_interpolator : basic_interpolation_data<geom::linear_interpolator_1d<float>>
{
    /** 
     * Initialize the interpolator. 
     * !!fixme: why do we need to pass one_over_span_length explicitly?
     */
    line_interpolator(const geom::vertex& v1, const geom::vertex& v2, const boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs, float one_over_span_length)
    : basic_interpolation_data(v1.coords.z, v1.coords.w)
    {
        // depth interpolation.
        auto depth_diff = v2.coords.z - v1.coords.z;
        auto depth_step = depth_diff * one_over_span_length;
        depth_value = geom::linear_interpolator_1d<float>{v1.coords.z, depth_step, depth_diff};

        // viewport z interpolation
        auto one_over_viewport_z_diff = v2.coords.w - v1.coords.w;
        auto one_over_viewport_z_step = one_over_viewport_z_diff * one_over_span_length;
        one_over_viewport_z = geom::linear_interpolator_1d<float>{v1.coords.w, one_over_viewport_z_step, one_over_viewport_z_diff};

        /*
         * all other vertex attributes.
         */

        // consistency check.
        assert(v1.varyings.size() == v2.varyings.size());
        assert(v1.varyings.size() == iqs.size());

        const auto varying_count = v1.varyings.size();

        // initialize varying interpolation.
        varyings.resize(varying_count);
        for(size_t i = 0; i < varying_count; ++i)
        {
            auto varying_v1 = v1.varyings[i];
            auto varying_v2 = v2.varyings[i];

            if(iqs[i] == swr::interpolation_qualifier::smooth)
            {
                varying_v1 *= v1.coords.w;
                varying_v2 *= v2.coords.w;
            }

            auto dir = varying_v2 - varying_v1;
            auto step = dir * one_over_span_length;

            varyings[i] = varying_interpolator(
              {varying_v1, ml::vec4::zero(), ml::vec4::zero(), iqs[i]},
              {step, ml::vec4::zero()},
              {dir, ml::vec4::zero()});
        }
    }

    line_interpolator(const line_interpolator& v1, const line_interpolator& v2, float one_over_span_length)
    : basic_interpolation_data(v1.reference_depth_value, v1.reference_one_over_viewport_z)
    {
        // raster z interpolation.
        auto depth_diff = v2.depth_value.value - v1.depth_value.value;
        auto depth_step = depth_diff * one_over_span_length;
        depth_value = geom::linear_interpolator_1d<float>{v1.depth_value.value, depth_step, depth_diff};

        // viewport z interpolation
        auto one_over_viewport_z_diff = v2.one_over_viewport_z.value - v1.one_over_viewport_z.value;
        auto one_over_viewport_z_step = one_over_viewport_z_diff * one_over_span_length;
        one_over_viewport_z = geom::linear_interpolator_1d<float>{v1.one_over_viewport_z.value, one_over_viewport_z_step, one_over_viewport_z_diff};

        // all other vertex attributes.
        assert(v1.varyings.size() == v2.varyings.size());
        const auto varying_count = v1.varyings.size();

        varyings.resize(varying_count);
        for(size_t i = 0; i < varying_count; ++i)
        {
            auto dir = v2.varyings[i].value - v1.varyings[i].value;
            auto step = dir * one_over_span_length;

            varyings[i] = varying_interpolator(
              v1.varyings[i],
              {step, ml::vec4::zero()},
              {dir, ml::vec4::zero()});
        }
    }

    void setup(float lambda)
    {
        depth_value.set_value_from_reference(reference_depth_value, lambda);
        one_over_viewport_z.set_value_from_reference(reference_one_over_viewport_z, lambda);

        for(auto& it: varyings)
        {
            it.set_value_from_reference(lambda);
        }
    }

    /** Increment values along the parameter direction. */
    void advance()
    {
        depth_value.advance();
        one_over_viewport_z.advance();

        for(auto& it: varyings)
        {
            it.advance_x();
        }
    }
};

/**
 * Interpolate vertex varyings on triangles using (normalized) barycentric coordinates.
 * These coordinates are given with respect to the edges used during their initialization 
 * in the constructor.
 * 
 * NOTE: The validity of the parameters is not checked!
 */
struct triangle_interpolator : basic_interpolation_data<geom::linear_interpolator_2d<float>>
{
    /** inverse triangle area, needed for normalization. */
    const float inv_area;

    /** the two triangle edge functions used for interpolation. */
    const geom::edge_function edge_v0v1, edge_v0v2;

    /**
     * Initialize the interpolator along the x-direction and along the y-direction with respect to the triangle edges.
     * 
     * \param v0 first triangle vertex in cw orienation (w.r.t. viewport coordinstes)
     * \param v1 second triangle vertex in cw orientation (w.r.t. viewport coordinates)
     * \param v2 third triangle vertex in cw orientation (w.r.t. viewport coordinates)
     * \param iqs Interpolation qualifiers for the varyings.
     * \param one_over_area inverse area of the triangle
     */
    triangle_interpolator(
      const geom::vertex& v0, const geom::vertex& v1, const geom::vertex& v2,
      const boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs,
      float one_over_area)
    : basic_interpolation_data(v0.coords.z, v0.coords.w)
    , inv_area(one_over_area)
    , edge_v0v1{v0.coords.xy(), v1.coords.xy()}
    , edge_v0v2{v0.coords.xy(), v2.coords.xy()}
    {
        // set up vertex attribute interpolation
        const ml::vec2 normalized_diff_v0v1 = edge_v0v1.v_diff * inv_area;
        const ml::vec2 normalized_diff_v0v2 = edge_v0v2.v_diff * inv_area;

        // depth value interpolation.
        const auto depth_diff_v0v1 = v1.coords.z - v0.coords.z;
        const auto depth_diff_v0v2 = v2.coords.z - v0.coords.z;
        const ml::vec2 depth_steps =
          {
            depth_diff_v0v1 * normalized_diff_v0v2.y - depth_diff_v0v2 * normalized_diff_v0v1.y,
            -depth_diff_v0v1 * normalized_diff_v0v2.x + depth_diff_v0v2 * normalized_diff_v0v1.x,
          };
        depth_value = geom::linear_interpolator_2d<float>{
          v0.coords.z,
          depth_steps,
          {depth_diff_v0v1, depth_diff_v0v2}};

        // viewport z interpolation.
        const auto viewport_z_diff_v0v1 = v1.coords.w - v0.coords.w;
        const auto viewport_z_diff_v0v2 = v2.coords.w - v0.coords.w;
        const ml::vec2 viewport_z_steps =
          {
            viewport_z_diff_v0v1 * normalized_diff_v0v2.y - viewport_z_diff_v0v2 * normalized_diff_v0v1.y,
            -viewport_z_diff_v0v1 * normalized_diff_v0v2.x + viewport_z_diff_v0v2 * normalized_diff_v0v1.x};
        one_over_viewport_z = geom::linear_interpolator_2d<float>{
          v0.coords.w,
          viewport_z_steps,
          {viewport_z_diff_v0v1, viewport_z_diff_v0v2}};

        /*
         * all other vertex attributes.
         */

        // consistency check.
        assert(v0.varyings.size() == v1.varyings.size());
        assert(v1.varyings.size() == v2.varyings.size());

        assert(iqs.size() == v0.coords.size());
        const auto varying_count = iqs.size();

        varyings.resize(varying_count);
        for(size_t i = 0; i < varying_count; ++i)
        {
            auto varying_v0 = v0.varyings[i];
            auto varying_v1 = v1.varyings[i];
            auto varying_v2 = v2.varyings[i];

            if(iqs[i] == swr::interpolation_qualifier::smooth)
            {
                varying_v0 *= v0.coords.w;
                varying_v1 *= v1.coords.w;
                varying_v2 *= v2.coords.w;
            }

            auto diff_v0v1 = varying_v1 - varying_v0;
            auto diff_v0v2 = varying_v2 - varying_v0;

            auto step_x = diff_v0v1 * normalized_diff_v0v2.y - diff_v0v2 * normalized_diff_v0v1.y;
            auto step_y = -diff_v0v1 * normalized_diff_v0v2.x + diff_v0v2 * normalized_diff_v0v1.x;

            varyings[i] = varying_interpolator(
              {varying_v0, step_x, step_y, iqs[i]},
              {step_x, step_y},
              {diff_v0v1, diff_v0v2});
        }
    }

    /** Set up non-constant attributes. */
    void setup_from_screen_coords(const ml::vec2 screen_coords)
    {
        // calculate floating-point normalized barycentric coordinates.
        const auto lambda2 = -edge_v0v1.evaluate(screen_coords) * inv_area;
        const auto lambda0 = edge_v0v2.evaluate(screen_coords) * inv_area;

        // set up attributes.
        depth_value.set_value_from_reference(reference_depth_value, lambda0, lambda2);
        one_over_viewport_z.set_value_from_reference(reference_one_over_viewport_z, lambda0, lambda2);

        for(auto& it: varyings)
        {
            it.set_value_from_reference(lambda0, lambda2);
        }
    }

    /** Increment values in x direction. */
    void advance_x()
    {
        depth_value.advance_x();
        one_over_viewport_z.advance_x();

        for(auto& it: varyings)
        {
            it.advance_x();
        }
    }

    /** Increment values in y direction. resets the x direction. */
    void advance_y()
    {
        depth_value.advance_y();
        one_over_viewport_z.advance_y();

        for(auto& it: varyings)
        {
            it.advance_y();
        }
    }
};

} /* namespace rast */
