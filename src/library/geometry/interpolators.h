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

    /** default constructor. */
    linear_interpolator_1d() = default;

    /** initializing constructor. */
    linear_interpolator_1d(const T& in_value, const T& in_step)
    : value(in_value)
    , step(in_step)
    {
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

    /** Value at the start of a row. */
    T row_start;

    /** Constructors. */
    linear_interpolator_2d() = default;

    linear_interpolator_2d(const T& in_value, const ml::tvec2<T>& in_step)
    : value{in_value}
    , step{in_step}
    , row_start{in_value}
    {
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