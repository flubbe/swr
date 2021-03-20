/**
 * swr - a software rasterizer
 * 
 * floating-point and fixed-point edge functions.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace geom
{

/**
 * Given a point p and an oriented line, the edge function of the line determines 
 * on which side (w.r.t. the orientation) p is.
 *
 * A value of 0 means the point is on the line.
 * 
 * A value > 0 means the point is on the "right side" of the line, i.e. the triangle given by (p, v1, v2) has
 * the opposite orientation the triangle ( (0,0), (1,0), (0,1) ).
 * 
 * A value < 0 means the point is on the "left side" of the line, i.e. the triangle given by (p, v1, v2) has
 * the same orientation as the triangle ( (0,0), (1,0), (0,1) ).
 */
struct edge_function
{
    float c = 0;
    ml::vec2 v_diff;

    /** Construct the edge function from two vectors. */
    edge_function(const ml::vec2& in_v0, const ml::vec2& in_v1)
    : v_diff(in_v1 - in_v0)
    {
        /*
         * Another interpretation of this formula: The value of c in the line equation
         * may be given as the scalar product of the line direction in_v1-in_v0 with
         * any of the two supplied points. Here, we choose v0.
         */
        c = in_v0.area(v_diff);
    }

    /**
     * Evaluate the edge function on a point.
     *
     * \param p The point on which this function is evaluated.
     * \return If the result is 0, p is on this edge. If it is <0, p is on the "left side" of the line defining the edge, i.e., 
     *         the triangle given by (p, v1, v2) has the same orientation as the triangle ( (0,0), (1,0), (0,1) ). If it is >0, 
     *         p is on the "right side" of the line defining the edge, i.e., the triangle given by (p, v1, v2) has the opposite 
     *         orientation as the triangle ( (0,0), (1,0), (0,1) ).
     */
    float evaluate(const ml::vec2& p) const
    {
        /*
         * The operation .area(v_diff) is the same as first constructing
         * the normal vector (-v_diff.y, v_diff.x) and then using the scalar product.
         */
        return p.area(v_diff) - c;
    }

    /** return the change rate for unit steps in x- and y-direction. this is the same as returning the line's normal. */
    const ml::vec2 get_change_rate() const
    {
        return {v_diff.y, -v_diff.x};
    }
};

/** 
 * edge_function for fixed-point numbers. 
 * 
 * NOTE: this is not just edge_function with float replaced by a fixed-point type, since multiplication changes the fixed-point type.
 */
struct edge_function_fixed
{
    /** the constant term has double the precision of v_diff to account for the multiplication involved. */
    ml::fixed_24_8_t c = 0;
    ml::vec2_fixed<4> v_diff;

    /** Construct the edge function from two vectors. */
    edge_function_fixed(const ml::vec2_fixed<4>& in_v0, const ml::vec2_fixed<4>& in_v1)
    : v_diff(in_v1 - in_v0)
    {
        c = in_v0.area(v_diff);
    }

    /** Evaluate the edge function on a point. The return value has double the precision of the points to account for the multiplication involved. */
    const ml::fixed_24_8_t evaluate(const ml::vec2_fixed<4>& p) const
    {
        return p.area(v_diff) - c;
    }

    /** return the change rate if we step one unit in x direction. */
    const ml::fixed_24_8_t get_change_x() const
    {
        return v_diff.y;
    }

    /** return the change rate if we step one unit in x direction. */
    const ml::fixed_24_8_t get_change_y() const
    {
        return -v_diff.x;
    }
};

} /* namespace geom */
