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
    /** A input reference value (possibly weighted). */
    ml::vec4 input_value;

    /** Linear or weighted step (with respect to window coordinates). */
    ml::tvec2<ml::vec4> step;

    /** Value at the start of a row. */
    ml::vec4 row_start;

    /** constructors. */
    varying_interpolator() = default;
    varying_interpolator(const varying_interpolator&) = default;
    varying_interpolator(varying_interpolator&&) = default;

    /** assignment. */
    varying_interpolator& operator=(const varying_interpolator&) = default;

    varying_interpolator(const varying& in_attrib, const ml::tvec2<ml::vec4>& in_step)
    : varying(in_attrib)
    , input_value(in_attrib.value)
    , step(in_step)
    , row_start(in_attrib.value)
    {
    }

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
    T depth_value{};

    /** interpolated inverse viewport z value. */
    T one_over_viewport_z{};

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
        out_varyings.reserve(varyings.size());
        for(auto& it: varyings)
        {
            out_varyings.push_back(it);
        }
    }

    /** get all varyings' values for a 2x2 block. */
    void get_varyings_block(boost::container::static_vector<swr::varying, geom::limits::max::varyings> out_varyings[4]) const
    {
        out_varyings[0].clear();
        out_varyings[0].reserve(varyings.size());
        out_varyings[1].clear();
        out_varyings[1].reserve(varyings.size());
        out_varyings[2].clear();
        out_varyings[2].reserve(varyings.size());
        out_varyings[3].clear();
        out_varyings[3].reserve(varyings.size());

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

    /** get all depth values for a 2x2 block. */
    void get_depth_block(float out_depth[4]) const
    {
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
    }

    /** get all one over viewport z values for a 2x2 block. */
    void get_one_over_viewport_z_block(float out_one_over_viewport_z[4]) const
    {
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
    {
        // depth interpolation.
        float depth_diff = v2.coords.z - v1.coords.z;
        float depth_step = depth_diff * one_over_span_length;
        depth_value = geom::linear_interpolator_1d<float>{v1.coords.z, depth_step};
        depth_value.set_value(v1.coords.z);

        // viewport z interpolation
        float one_over_viewport_z_diff = v2.coords.w - v1.coords.w;
        float one_over_viewport_z_step = one_over_viewport_z_diff * one_over_span_length;
        one_over_viewport_z = geom::linear_interpolator_1d<float>{v1.coords.w, one_over_viewport_z_step};
        one_over_viewport_z.set_value(v1.coords.w);

        /*
         * all other vertex attributes.
         */

        // consistency check.
        assert(v1.varyings.size() == v2.varyings.size());
        assert(v1.varyings.size() == iqs.size());

        std::size_t varying_count = v1.varyings.size();

        // initialize varying interpolation.
        varyings.resize(varying_count);
        for(size_t i = 0; i < varying_count; ++i)
        {
            if(iqs[i] == swr::interpolation_qualifier::smooth)
            {
                ml::vec4 varying_v1 = v1.varyings[i];
                ml::vec4 varying_v2 = v2.varyings[i];

                varying_v1 *= v1.coords.w;
                varying_v2 *= v2.coords.w;

                ml::vec4 dir = varying_v2 - varying_v1;
                ml::vec4 step = dir * one_over_span_length;

                varyings[i] = varying_interpolator(
                  swr::varying{varying_v1, ml::vec4::zero(), ml::vec4::zero()},
                  {step, ml::vec4::zero()});
            }
            else if(iqs[i] == swr::interpolation_qualifier::flat)
            {
                varyings[i] = varying_interpolator{
                  {v_ref.varyings[i], ml::vec4::zero(), ml::vec4::zero()},
                  {ml::vec4::zero(), ml::vec4::zero()}};
            }
            else
            {
                //!!todo: unimplemented.
            }

            varyings[i].set_value(varyings[i].input_value);
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
     * \param v0 first triangle vertex in cw orienation (w.r.t. viewport coordinstes)
     * \param v1 second triangle vertex in cw orientation (w.r.t. viewport coordinates)
     * \param v2 third triangle vertex in cw orientation (w.r.t. viewport coordinates)
     * \param v_ref reference vertex for flat shading
     * \param iqs Interpolation qualifiers for the varyings.
     * \param one_over_area inverse area of the triangle
     */
    triangle_interpolator(
      const ml::vec2 screen_coords,
      const geom::vertex& v0, const geom::vertex& v1, const geom::vertex& v2, const geom::vertex& v_ref,
      const boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs,
      float one_over_area)
    {
        // the two triangle edge functions
        geom::edge_function edge_v0v1{v0.coords.xy(), v1.coords.xy()}, edge_v0v2{v0.coords.xy(), v2.coords.xy()};

        // set up vertex attribute interpolation
        ml::vec2 normalized_diff_v0v1 = edge_v0v1.v_diff * one_over_area;
        ml::vec2 normalized_diff_v0v2 = edge_v0v2.v_diff * one_over_area;

        // calculate floating-point normalized barycentric coordinates.
        float lambda2 = -edge_v0v1.evaluate(screen_coords) * one_over_area;
        float lambda0 = edge_v0v2.evaluate(screen_coords) * one_over_area;

        // depth value interpolation.
        float depth_diff_v0v1 = v1.coords.z - v0.coords.z;
        float depth_diff_v0v2 = v2.coords.z - v0.coords.z;
        ml::vec2 depth_steps =
          {
            depth_diff_v0v1 * normalized_diff_v0v2.y - depth_diff_v0v2 * normalized_diff_v0v1.y,
            -depth_diff_v0v1 * normalized_diff_v0v2.x + depth_diff_v0v2 * normalized_diff_v0v1.x,
          };
        depth_value = geom::linear_interpolator_2d<float>{
          v0.coords.z,
          ml::to_tvec2<float>(depth_steps)};
        depth_value.set_value(v0.coords.z + depth_diff_v0v1 * lambda0 + depth_diff_v0v2 * lambda2);

        // viewport z interpolation.
        float viewport_z_diff_v0v1 = v1.coords.w - v0.coords.w;
        float viewport_z_diff_v0v2 = v2.coords.w - v0.coords.w;
        ml::vec2 viewport_z_steps =
          {
            viewport_z_diff_v0v1 * normalized_diff_v0v2.y - viewport_z_diff_v0v2 * normalized_diff_v0v1.y,
            -viewport_z_diff_v0v1 * normalized_diff_v0v2.x + viewport_z_diff_v0v2 * normalized_diff_v0v1.x};
        one_over_viewport_z = geom::linear_interpolator_2d<float>{
          v0.coords.w,
          ml::to_tvec2<float>(viewport_z_steps)};
        one_over_viewport_z.set_value(v0.coords.w + viewport_z_diff_v0v1 * lambda0 + viewport_z_diff_v0v2 * lambda2);

        /*
         * all other vertex attributes.
         */

        // consistency check.
        assert(v0.varyings.size() == v1.varyings.size());
        assert(v1.varyings.size() == v2.varyings.size());

        assert(iqs.size() == v0.varyings.size());
        std::size_t varying_count = iqs.size();

        varyings.resize(varying_count);
        for(size_t i = 0; i < varying_count; ++i)
        {
            if(iqs[i] == swr::interpolation_qualifier::smooth)
            {
                ml::vec4 varying_v0 = v0.varyings[i];
                ml::vec4 varying_v1 = v1.varyings[i];
                ml::vec4 varying_v2 = v2.varyings[i];

                varying_v0 *= v0.coords.w;
                varying_v1 *= v1.coords.w;
                varying_v2 *= v2.coords.w;

                ml::vec4 diff_v0v1 = varying_v1 - varying_v0;
                ml::vec4 diff_v0v2 = varying_v2 - varying_v0;

                ml::vec4 step_x = diff_v0v1 * normalized_diff_v0v2.y - diff_v0v2 * normalized_diff_v0v1.y;
                ml::vec4 step_y = -diff_v0v1 * normalized_diff_v0v2.x + diff_v0v2 * normalized_diff_v0v1.x;

                varyings[i] = varying_interpolator(
                  {varying_v0, step_x, step_y},
                  {step_x, step_y});

                varyings[i].set_value(varyings[i].input_value + diff_v0v1 * lambda0 + diff_v0v2 * lambda2);
            }
            else if(iqs[i] == swr::interpolation_qualifier::flat)
            {
                varyings[i] = varying_interpolator{
                  {v_ref.varyings[i], ml::vec4::zero(), ml::vec4::zero()},
                  {ml::vec4::zero(), ml::vec4::zero()}};
            }
            else
            {
                //!!todo: unimplemented.
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
