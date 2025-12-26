/**
 * swr - a software rasterizer
 *
 * interpolators for quantities on lines and triangles.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021-Present.
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
    /** Linear or weighted step (with respect to window coordinates). */
    ml::tvec2<ml::vec4> step;

    /** Value at the start of a row. */
    ml::vec4 row_start;

    /** constructors. */
    varying_interpolator() = default;
    varying_interpolator(const varying_interpolator&) = default;
    varying_interpolator(varying_interpolator&&) = default;

    varying_interpolator(const varying& in_attrib, const ml::tvec2<ml::vec4>& in_step)
    : varying{in_attrib}
    , step{in_step}
    , row_start(in_attrib.value)
    {
    }

    /** assignment. */
    varying_interpolator& operator=(const varying_interpolator&) = default;

    /** Initialize at a specific point. */
    void set_value(const ml::vec4& v)
    {
        value = v;
        row_start = v;
    }

    /** store current value as row start. */
    void setup_block_processing()
    {
        row_start = value;
    }

    /** Step along the x-direction. */
    void advance_x()
    {
        value += step.x;
    }

    /** Advance multiple steps along the x-direction. */
    void advance_x(int i)
    {
        value += step.x * i;
    }

    /** Step along y-direction and reset x-direction. */
    void advance_y()
    {
        row_start += step.y;
        value = row_start;
    }

    /** Advance multiple steps along the y-direction. */
    void advance_y(int i)
    {
        row_start += step.y * i;
        value = row_start;
    }
};

/** basic interpolation data. */
template<typename T>
struct basic_interpolation_data
{
    /** interpolated depth value for the depth buffer */
    T depth_value;

    /** interpolated inverse viewport z value. */
    T one_over_viewport_z;

    /** varyings from the shader. */
    boost::container::static_vector<varying_interpolator, geom::limits::max::varyings> varyings;

    /** constructors. */
    basic_interpolation_data() = default;
    basic_interpolation_data(const basic_interpolation_data&) = default;
    basic_interpolation_data(basic_interpolation_data&&) = default;

    /** assignment. */
    basic_interpolation_data<T>& operator=(const basic_interpolation_data<T>&) = default;

    /** get the varyings' values. */
    void get_varyings(boost::container::static_vector<swr::varying, geom::limits::max::varyings>& out_varyings) const
    {
        out_varyings.clear();
        for(auto& it: varyings)
        {
            out_varyings.push_back(it);
        }
    }

    /**
     * @brief get interpolated data (varyings, depth values and viewport z values) for a 2x2 block.
     *
     * @param out_varyings Varyings for the block. Assumed to be empty.
     * @param out_depth Depth values for the block.
     * @param out_one_over_viewport_z Inverse of viewport z for the block.
     */
    void get_data_block(
      boost::container::static_vector<swr::varying, geom::limits::max::varyings> out_varyings[4],
      ml::vec4& out_depth,
      ml::vec4& out_one_over_viewport_z) const
    {
        /*
         * depth.
         */

        auto depth = depth_value;
        depth.setup_block_processing();

        // store value at (x,y)
        out_depth[0] = depth.value;

        // store value at (x+1,y)
        depth.advance_x();
        out_depth[1] = depth.value;

        // store value at (x,y+1)
        depth.advance_y();
        out_depth[2] = depth.value;

        // store value at (x+1,y+1)
        depth.advance_x();
        out_depth[3] = depth.value;

        /*
         * viewport z.
         */

        auto one_over_z = one_over_viewport_z;
        one_over_z.setup_block_processing();

        // store value at (x,y)
        out_one_over_viewport_z[0] = one_over_z.value;

        // store value at (x+1,y)
        one_over_z.advance_x();
        out_one_over_viewport_z[1] = one_over_z.value;

        // store value at (x,y+1)
        one_over_z.advance_y();
        out_one_over_viewport_z[2] = one_over_z.value;

        // store value at (x+1,y+1)
        one_over_z.advance_x();
        out_one_over_viewport_z[3] = one_over_z.value;

        /*
         * varyings.
         */

        for(auto it: varyings)
        {
            it.setup_block_processing();

            // store value at (x,y)
            out_varyings[0].push_back(it);

            // store value at (x+1,y)
            it.advance_x();
            out_varyings[1].push_back(it);

            // store value at (x,y+1)
            it.advance_y();
            out_varyings[2].push_back(it);

            // store value at (x+1,y+1)
            it.advance_x();
            out_varyings[3].push_back(it);
        }
    }
};

/** Interpolate vertex varyings along lines. */
struct line_interpolator : basic_interpolation_data<geom::linear_interpolator_1d<float>>
{
    /** constructors */
    line_interpolator() = default;
    line_interpolator(const line_interpolator&) = default;
    line_interpolator(line_interpolator&&) = default;

    /** assignment. */
    line_interpolator& operator=(const line_interpolator&) = default;

    /**
     * Initialize the interpolator.
     * FIXME why do we need to pass one_over_span_length explicitly?
     */
    line_interpolator(const geom::vertex& v1, const geom::vertex& v2, const geom::vertex& v_ref, const boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs, float one_over_span_length)
    : basic_interpolation_data{}
    {
        // depth interpolation.
        float depth_diff = v2.coords.z - v1.coords.z;
        float depth_step = depth_diff * one_over_span_length;
        depth_value = geom::linear_interpolator_1d<float>{v1.coords.z, depth_step};

        // viewport z interpolation
        float one_over_viewport_z_diff = v2.coords.w - v1.coords.w;
        float one_over_viewport_z_step = one_over_viewport_z_diff * one_over_span_length;
        one_over_viewport_z = geom::linear_interpolator_1d<float>{v1.coords.w, one_over_viewport_z_step};

        /*
         * all other vertex attributes.
         */

        // consistency check.
        assert(v1.varyings.size() == v2.varyings.size());
        assert(v1.varyings.size() == iqs.size());

        std::size_t varying_count = v1.varyings.size();

        // initialize varying interpolation.
        varyings.reserve(varying_count);
        for(std::size_t i = 0; i < varying_count; ++i)
        {
            if(iqs[i] == swr::interpolation_qualifier::smooth)
            {
                ml::vec4 varying_v1 = v1.varyings[i];
                ml::vec4 varying_v2 = v2.varyings[i];

                varying_v1 *= v1.coords.w;
                varying_v2 *= v2.coords.w;

                ml::vec4 dir = varying_v2 - varying_v1;
                ml::vec4 step = dir * one_over_span_length;

                varyings.emplace_back(varying_interpolator{
                  swr::varying{varying_v1, ml::vec4::zero(), ml::vec4::zero()},
                  {step, ml::vec4::zero()}});
            }
            else if(iqs[i] == swr::interpolation_qualifier::flat)
            {
                varyings.emplace_back(varying_interpolator{
                  {v_ref.varyings[i], ml::vec4::zero(), ml::vec4::zero()},
                  {ml::vec4::zero(), ml::vec4::zero()}});
            }
            else
            {
                // TODO unimplemented.
            }
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
    /** constructors. */
    triangle_interpolator() = default;
    triangle_interpolator(const triangle_interpolator&) = default;
    triangle_interpolator(triangle_interpolator&&) = default;

    /** assignment. */
    triangle_interpolator& operator=(const triangle_interpolator&) = default;

    /**
     * Initialize the interpolator along the x-direction and along the y-direction with respect to the triangle edges.
     *
     * TODO split vertex info in (coords, varyings) in order to have a modifyable copy of the coords.
     *      v_ref is only needed for its varyings.
     *      split into vX and vX_varying arguments for each vertex?
     *
     * \param v0 first triangle vertex in cw orienation (w.r.t. viewport coordinstes)
     * \param v1 second triangle vertex in cw orientation (w.r.t. viewport coordinates)
     * \param v2 third triangle vertex in cw orientation (w.r.t. viewport coordinates)
     * \param v_ref reference vertex for flat shading
     * \param iqs Interpolation qualifiers for the varyings.
     * \param one_over_area inverse area of the triangle
     */
    triangle_interpolator(
      const ml::vec2 screen_coords,
      const ml::vec4& v0_coords, const ml::vec4& v1_coords, const ml::vec4& v2_coords,
      const boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& v0_varyings,
      const boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& v1_varyings,
      const boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& v2_varyings,
      const boost::container::static_vector<ml::vec4, geom::limits::max::varyings>& vref_varyings,
      const boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs,
      float one_over_area)
    : basic_interpolation_data{}
    {
        // the two triangle edge functions
        geom::edge_function edge_v0v1{v0_coords.xy(), v1_coords.xy()}, edge_v0v2{v0_coords.xy(), v2_coords.xy()};

        // set up vertex attribute interpolation
        ml::vec2 normalized_diff_v0v1 = edge_v0v1.v_diff * one_over_area;
        ml::vec2 normalized_diff_v0v2 = edge_v0v2.v_diff * one_over_area;

        // calculate floating-point normalized barycentric coordinates.
        float lambda2 = -edge_v0v1.evaluate(screen_coords) * one_over_area;
        float lambda0 = edge_v0v2.evaluate(screen_coords) * one_over_area;

        // depth value interpolation.
        float depth_diff_v0v1 = v1_coords.z - v0_coords.z;
        float depth_diff_v0v2 = v2_coords.z - v0_coords.z;
        ml::vec2 depth_steps =
          {
            depth_diff_v0v1 * normalized_diff_v0v2.y - depth_diff_v0v2 * normalized_diff_v0v1.y,
            -depth_diff_v0v1 * normalized_diff_v0v2.x + depth_diff_v0v2 * normalized_diff_v0v1.x,
          };
        depth_value = geom::linear_interpolator_2d<float>{
          v0_coords.z,
          ml::to_tvec2<float>(depth_steps)};
        depth_value.set_value(v0_coords.z + depth_diff_v0v1 * lambda0 + depth_diff_v0v2 * lambda2);

        // viewport z interpolation.
        float viewport_z_diff_v0v1 = v1_coords.w - v0_coords.w;
        float viewport_z_diff_v0v2 = v2_coords.w - v0_coords.w;
        ml::vec2 viewport_z_steps =
          {
            viewport_z_diff_v0v1 * normalized_diff_v0v2.y - viewport_z_diff_v0v2 * normalized_diff_v0v1.y,
            -viewport_z_diff_v0v1 * normalized_diff_v0v2.x + viewport_z_diff_v0v2 * normalized_diff_v0v1.x};
        one_over_viewport_z = geom::linear_interpolator_2d<float>{
          v0_coords.w,
          ml::to_tvec2<float>(viewport_z_steps)};
        one_over_viewport_z.set_value(v0_coords.w + viewport_z_diff_v0v1 * lambda0 + viewport_z_diff_v0v2 * lambda2);

        /*
         * all other vertex attributes.
         */

        // consistency check.
        assert(v0_varyings.size() == v1_varyings.size());
        assert(v1_varyings.size() == v2_varyings.size());

        assert(iqs.size() == v0_varyings.size());
        std::size_t varying_count = iqs.size();

        varyings.reserve(varying_count);
        for(std::size_t i = 0; i < varying_count; ++i)
        {
            if(iqs[i] == swr::interpolation_qualifier::smooth)
            {
                ml::vec4 varying_v0 = v0_varyings[i];
                ml::vec4 varying_v1 = v1_varyings[i];
                ml::vec4 varying_v2 = v2_varyings[i];

                varying_v0 *= v0_coords.w;
                varying_v1 *= v1_coords.w;
                varying_v2 *= v2_coords.w;

                ml::vec4 diff_v0v1 = varying_v1 - varying_v0;
                ml::vec4 diff_v0v2 = varying_v2 - varying_v0;

                ml::vec4 step_x = diff_v0v1 * normalized_diff_v0v2.y - diff_v0v2 * normalized_diff_v0v1.y;
                ml::vec4 step_y = -diff_v0v1 * normalized_diff_v0v2.x + diff_v0v2 * normalized_diff_v0v1.x;

                varyings.emplace_back(varying_interpolator{
                  {varying_v0, step_x, step_y},
                  {step_x, step_y}});

                varyings.back().set_value(varying_v0 + diff_v0v1 * lambda0 + diff_v0v2 * lambda2);
            }
            else if(iqs[i] == swr::interpolation_qualifier::flat)
            {
                varyings.emplace_back(varying_interpolator{
                  {vref_varyings[i], ml::vec4::zero(), ml::vec4::zero()},
                  {ml::vec4::zero(), ml::vec4::zero()}});
            }
            else
            {
                // TODO unimplemented.
            }
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

    /** advance multiple steps in x direction. */
    void advance_x(int i)
    {
        depth_value.advance_x(i);
        one_over_viewport_z.advance_x(i);

        for(auto& it: varyings)
        {
            it.advance_x(i);
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

    /** Advance multiple steps in y direction. resets the x direction. */
    void advance_y(int i)
    {
        depth_value.advance_y(i);
        one_over_viewport_z.advance_y(i);

        for(auto& it: varyings)
        {
            it.advance_y(i);
        }
    }

    /** set row start to current value. */
    void setup_block_processing()
    {
        depth_value.setup_block_processing();
        one_over_viewport_z.setup_block_processing();

        for(auto& it: varyings)
        {
            it.setup_block_processing();
        }
    }
};

} /* namespace rast */
