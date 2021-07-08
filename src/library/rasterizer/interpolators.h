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
    const float reference_depth_value{1};
    const float reference_one_over_viewport_z{1};

    /** interpolated depth value for the depth buffer */
    T depth_value{};

    /** interpolated inverse viewport z value. */
    T one_over_viewport_z{};

    /** varyings from the shader. */
    boost::container::static_vector<varying_interpolator, geom::limits::max::varyings> varyings;

    /** default constructor. */
    basic_interpolation_data() = default;

    /** initialize constant data. */
    basic_interpolation_data(float ref_depth, float ref_one_over_viewport_z)
    : reference_depth_value(ref_depth)
    , reference_one_over_viewport_z(ref_one_over_viewport_z)
    {
    }

    /** default copy constructor. */
    basic_interpolation_data(const basic_interpolation_data&) = default;

    /** default move constructor. */
    basic_interpolation_data(basic_interpolation_data&&) = default;

    /** get the varyings' values. */
    template<int offs_x = 0, int offs_y = 0>
    void get_varyings(boost::container::static_vector<swr::varying, geom::limits::max::varyings>& out_varyings) const
    {
        out_varyings.resize(varyings.size());

        auto it = varyings.begin();
        auto out_it = out_varyings.begin();

        if(offs_x == 0 && offs_y == 0)
        {
            for(; it != varyings.end(); ++it, ++out_it)
            {
                *out_it = *it;
            }
        }
        else if(offs_x == 0)
        {
            for(; it != varyings.end(); ++it, ++out_it)
            {
                auto v = *it;
                v.setup_block_processing();
                v.advance_y(offs_y);
                *out_it = v;
            }
        }
        else if(offs_y == 0)
        {
            for(; it != varyings.end(); ++it, ++out_it)
            {
                auto v = *it;
                v.advance_x(offs_x);
                *out_it = v;
            }
        }
        else
        {
            for(; it != varyings.end(); ++it, ++out_it)
            {
                auto v = *it;
                v.setup_block_processing();
                v.advance_y(offs_y);
                v.advance_x(offs_x);
                *out_it = v;
            }
        }
    }

    /** get all varyings' values for a 2x2 block. */
    void get_varyings_block(boost::container::static_vector<swr::varying, geom::limits::max::varyings> out_varyings[4]) const
    {
        out_varyings[0].clear();
        out_varyings[1].clear();
        out_varyings[2].clear();
        out_varyings[3].clear();

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

    /** get the depth value. */
    template<int offs_x = 0, int offs_y = 0>
    void get_depth(float& out_depth) const
    {
        if(offs_x == 0 && offs_y == 0)
        {
            out_depth = depth_value.value;
        }
        else if(offs_x == 0)
        {
            auto depth = depth_value;
            depth.setup_block_processing();
            depth.advance_y(offs_y);
            out_depth = depth.value;
        }
        else if(offs_y == 0)
        {
            auto depth = depth_value;
            depth.advance_x(offs_x);
            out_depth = depth.value;
        }
        else
        {
            auto depth = depth_value;
            depth.setup_block_processing();
            depth.advance_y(offs_y);
            depth.advance_x(offs_x);
            out_depth = depth.value;
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

    /** get one over viewport z. */
    template<int offs_x = 0, int offs_y = 0>
    void get_one_over_viewport_z(float& out_one_over_viewport_z) const
    {
        if(offs_x == 0 && offs_y == 0)
        {
            out_one_over_viewport_z = one_over_viewport_z.value;
        }
        else if(offs_x == 0)
        {
            auto one_over_z = one_over_viewport_z;
            one_over_z.setup_block_processing();
            one_over_z.advance_y(offs_y);
            out_one_over_viewport_z = one_over_z.value;
        }
        else if(offs_y == 0)
        {
            auto one_over_z = one_over_viewport_z;
            one_over_z.advance_x(offs_x);
            out_one_over_viewport_z = one_over_z.value;
        }
        else
        {
            auto one_over_z = one_over_viewport_z;
            one_over_z.setup_block_processing();
            one_over_z.advance_y(offs_y);
            one_over_z.advance_x(offs_x);
            out_one_over_viewport_z = one_over_z.value;
        }
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

    /** assignment. */
    basic_interpolation_data<T>& operator=(const basic_interpolation_data<T>& other)
    {
        assert(reference_depth_value == other.reference_depth_value);
        assert(reference_one_over_viewport_z == other.reference_one_over_viewport_z);

        depth_value = other.depth_value;
        one_over_viewport_z = other.one_over_viewport_z;
        varyings = other.varyings;

        return *this;
    }
};

/** Interpolate vertex varyings along lines. */
struct line_interpolator : basic_interpolation_data<geom::linear_interpolator_1d<float>>
{
    /** 
     * Initialize the interpolator. 
     * !!fixme: why do we need to pass one_over_span_length explicitly?
     */
    line_interpolator(const geom::vertex& v1, const geom::vertex& v2, const geom::vertex& v_ref, const boost::container::static_vector<swr::interpolation_qualifier, geom::limits::max::varyings>& iqs, float one_over_span_length)
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
            if(iqs[i] == swr::interpolation_qualifier::smooth)
            {
                auto varying_v1 = v1.varyings[i];
                auto varying_v2 = v2.varyings[i];

                varying_v1 *= v1.coords.w;
                varying_v2 *= v2.coords.w;

                auto dir = varying_v2 - varying_v1;
                auto step = dir * one_over_span_length;

                varyings[i] = varying_interpolator(
                  swr::varying{varying_v1, ml::vec4::zero(), ml::vec4::zero(), swr::interpolation_qualifier::smooth},
                  {step, ml::vec4::zero()},
                  {dir, ml::vec4::zero()});
            }
            else if(iqs[i] == swr::interpolation_qualifier::flat)
            {
                varyings[i] = varying_interpolator{
                  {v_ref.varyings[i], ml::vec4::zero(), ml::vec4::zero(), swr::interpolation_qualifier::flat},
                  {ml::vec4::zero(), ml::vec4::zero()},
                  {ml::vec4::zero(), ml::vec4::zero()}};
            }
            else
            {
                //!!todo: unimplemented.
            }
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
    const float inv_area{1};

    /** the two triangle edge functions used for interpolation. */
    const geom::edge_function edge_v0v1, edge_v0v2;

    /** no default constructor (edge_v0v1 and edge_v0v2 need to be initialized). */
    triangle_interpolator() = delete;

    /** default copy constructor. */
    triangle_interpolator(const triangle_interpolator&) = default;

    /** default move constructor. */
    triangle_interpolator(triangle_interpolator&&) = default;

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
      const geom::vertex& v0, const geom::vertex& v1, const geom::vertex& v2, const geom::vertex& v_ref,
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

        assert(iqs.size() == v0.varyings.size());
        const auto varying_count = iqs.size();

        varyings.resize(varying_count);
        for(size_t i = 0; i < varying_count; ++i)
        {
            if(iqs[i] == swr::interpolation_qualifier::smooth)
            {
                auto varying_v0 = v0.varyings[i];
                auto varying_v1 = v1.varyings[i];
                auto varying_v2 = v2.varyings[i];

                varying_v0 *= v0.coords.w;
                varying_v1 *= v1.coords.w;
                varying_v2 *= v2.coords.w;

                auto diff_v0v1 = varying_v1 - varying_v0;
                auto diff_v0v2 = varying_v2 - varying_v0;

                auto step_x = diff_v0v1 * normalized_diff_v0v2.y - diff_v0v2 * normalized_diff_v0v1.y;
                auto step_y = -diff_v0v1 * normalized_diff_v0v2.x + diff_v0v2 * normalized_diff_v0v1.x;

                varyings[i] = varying_interpolator(
                  {varying_v0, step_x, step_y, swr::interpolation_qualifier::smooth},
                  {step_x, step_y},
                  {diff_v0v1, diff_v0v2});
            }
            else if(iqs[i] == swr::interpolation_qualifier::flat)
            {
                varyings[i] = varying_interpolator{
                  {v_ref.varyings[i], ml::vec4::zero(), ml::vec4::zero(), swr::interpolation_qualifier::flat},
                  {ml::vec4::zero(), ml::vec4::zero()},
                  {ml::vec4::zero(), ml::vec4::zero()}};
            }
            else
            {
                //!!todo: unimplemented.
            }
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

    /** assignment. */
    triangle_interpolator& operator=(const triangle_interpolator& other)
    {
        assert(inv_area == other.inv_area);
        assert(edge_v0v1.c == other.edge_v0v1.c);
        assert(edge_v0v1.v_diff == other.edge_v0v1.v_diff);
        assert(edge_v0v2.c == other.edge_v0v2.c);
        assert(edge_v0v2.v_diff == other.edge_v0v2.v_diff);

        static_cast<basic_interpolation_data<geom::linear_interpolator_2d<float>>>(*this) = basic_interpolation_data<geom::linear_interpolator_2d<float>>(other);

        return *this;
    }
};

} /* namespace rast */
