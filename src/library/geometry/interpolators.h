/**
 * swr - a software rasterizer
 * 
 * linear interpolators in 1d and 2d.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace geom
{

/**
 * linear interpolator for data with one interpolation direction.
 */
template<typename T>
struct linear_interpolator_1d
{
    /** Current (possibly weighted) value. */
    T value;

    /** Unit step size. */
    T step;

    /** Difference along the edge */
    T diff;

    /** default constructor. */
    linear_interpolator_1d() = default;

    /** initializing constructor. */
    linear_interpolator_1d(const T& in_value, const T& in_step, const T& in_diff)
    : value(in_value)
    , step(in_step)
    , diff(in_diff)
    {
    }

    void set_value_from_reference(const T& reference_value, float lambda)
    {
        value = reference_value + diff * lambda;
    }

    void set_value(const T& in_value)
    {
        value = in_value;
    }

    void advance()
    {
        value += step;
    }

    void advance(int i)
    {
        value += step.x * i;
    }
};

/**
 * linear interpolator for data with two interpolation directions.
 * 
 * the advance_y method is geared towards the data
 * living on an object with a left vertical edge.
 */
template<typename T>
struct linear_interpolator_2d
{
    /** Current (possibly weighted) value. */
    T value;

    /** Unit step size. */
    ml::tvec2<T> step;

    /** Differences along two linearly independent vectors. */
    ml::tvec2<T> diffs;

    /** Value at the start of a row. */
    T row_start;

    /** Constructors. */
    linear_interpolator_2d() = default;

    linear_interpolator_2d(const T& in_value, const ml::tvec2<T>& in_step, const ml::tvec2<T>& in_diffs)
    : value{in_value}
    , step{in_step}
    , diffs{in_diffs}
    , row_start{in_value}
    {
    }

    void set_value_from_reference(const T& reference_value, float lambda1, float lambda2)
    {
        row_start = reference_value + diffs.x * lambda1 + diffs.y * lambda2;
        value = row_start;
    }

    /** blocks are processed in rows, so we need to store the value of the interpolated value at the start of each row in order to use advance_y, which jumps to the next row. */
    void setup_block_processing()
    {
        row_start = value;
    }

    void set_value(const T& in_value)
    {
        value = in_value;
        row_start = in_value;
    }

    /** step in x direction. */
    void advance_x()
    {
        value += step.x;
    }

    /** step multiple times in x direction. */
    void advance_x(int i)
    {
        value += step.x * i;
    }

    /** advance in y direction and reset x. */
    void advance_y()
    {
        row_start += step.y;
        value = row_start;
    }

    /** advance multiple times in y direction and reset x. */
    void advance_y(int i)
    {
        row_start += step.y * i;
        value = row_start;
    }

    /** 
     * step in y direction. 
     * 
     * NOTE: this method does not affect row_start. If the interpolator is to be used to process blocks,
     *       setup_block_processing needs to be called before using advance_y.
     */
    void step_y(int i)
    {
        value += step.y * i;
    }
};

} /* namespace geom */