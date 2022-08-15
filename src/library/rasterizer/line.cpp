/**
 * swr - a software rasterizer
 *
 * line setup and drawing.
 *
 * some references:
 *
 *  [1] Direct3D 11 fill rules: https://msdn.microsoft.com/de-de/library/windows/desktop/cc627092(v=vs.85).aspx#Line_1
 *  [2] https://github.com/MIvanchev/diamond-exit-line/blob/master/src/com/podrug/line/LineRenderer.java
 *  [3] http://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
 *  [4] http://rosettacode.org/wiki/Bitmap/Bresenham%27s_line_algorithm#C.2B.2B
 *  [5] Diamond-exit: https://msdn.microsoft.com/de-de/library/windows/desktop/cc627092(v=vs.85).aspx
 *  [6] Mesa 3D: https://github.com/anholt/mesa/blob/master/src/gallium/drivers/llvmpipe/lp_setup_line.c
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* user headers. */
#include "../swr_internal.h"

#include "interpolators.h"
#include "fragment.h"
#include "sweep.h"

namespace rast
{

/** Line setup info. */
struct line_info
{
    /** the vertices of the line. after calling setup, v1 contains the left-most vertex (for x-major lines) resp. the upper-most (for y-major lines) vertex. */
    const geom::vertex *v1, *v2;

    /** Line deltas. These are always with respect to v1 as initial vertex and v2 as end vertex. */
    float dx, dy;

    /** maximum of the absolute values of the deltas. */
    const float max_absolute_delta;

    /** If the line is parameterized over the x axis (i.e. if abs(dy)<=abs(dx)), this is true. */
    const bool is_x_major;

    /** Calculated start- and end offsets. */
    ml::vec2 offset_v1{0.f, 0.f};
    ml::vec2 offset_v2{0.f, 0.f};

    /** no default constructor. */
    line_info() = delete;

    /** constructor. */
    line_info(const geom::vertex& in_v1, const geom::vertex& in_v2)
    : v1{&in_v1}
    , v2{&in_v2}
    , dx{in_v2.coords.x - in_v1.coords.x}
    , dy{in_v2.coords.y - in_v1.coords.y}
    , max_absolute_delta{std::max(std::abs(dx), std::abs(dy))}
    , is_x_major{std::abs(dy) <= std::abs(dx)}
    {
    }

    /** implement diamond exit rule and set up line info. */
    void setup();
};

/** extract the fractional part of a positive float */
static float fracf(float f)
{
    return f - std::floor(f);
}

/*
 * 1) a note on the coordinate system used:
 *
 *    line_info contains information used by sweep_rasterizer::draw_line, which in turn calls
 *    sweep_rasterizer::process_fragment to write the fragment. this last function takes (x,y)
 *    coordinates with respect to screen space; in particular, the x-axis extends to the right
 *    and the y-axis extends downwards.
 *
 *    for the differences to the pixel centers, this means:
 *     *) the calculated differences are all in the half-open interval [-0.5f, 0.5f).
 *     *) an x-/y-difference of -0.5 means that the point is vertically/horizontally exactly half-way between two pixels.
 *
 * 2) from the knowledge of dx, dy and the x-/y-differences of the starting/ending points, we can infer
 *    if the line crosses the (x/y) halfway line of the pixel.
 *
 *    for example:
 *     *) if dx>0 (i.e., the line extends to the right) and v1_diff.x>0, we do not cross the halfway line.
 *     *) if dx>0 and v1_diff.x < 0, we do cross the halfway line.
 */
void line_info::setup()
{
    /* distance from vertices to the mid-point of the pixel. */
    ml::vec2 v1_diff = {fracf(v1->coords.x) - 0.5f, fracf(v1->coords.y) - 0.5f};
    ml::vec2 v2_diff = {fracf(v2->coords.x) - 0.5f, fracf(v2->coords.y) - 0.5f};

    /* should we draw the start (resp. end) pixel? */
    bool should_draw_start{false}, should_draw_end{false};

    /* are we already drawing the start (resp. end) pixel? */
    bool is_drawing_start{true}, is_drawing_end{false};

    if(is_x_major)
    {
        auto dydx = dy / dx;
        assert(dydx >= -1 && dydx <= 1);

        /* check if the end point lies vertically exactly between two pixels and the line is coming from above */
        if(v2_diff.y == -0.5f && dy >= 0)
        {
            /* in this case, the pixel above should be considered as the ending pixel, so we need to adjust v2_diff.y. */
            v2_diff.y = 0.5f;
        }

        /*
         * check if the starting pixel should (potentially) be drawn.
         */

        if(std::abs(v1_diff.x) + std::abs(v1_diff.y) < 0.5f)
        {
            // we start inside the diamond.
            should_draw_start = true;
        }
        else if(boost::math::sign(-dx) == boost::math::sign(-v1_diff.x))
        {
            /*
             * we start outside the diamond, and one of the following holds:
             *  *) dx>0 (i.e., the line extends to the right) and v1_diff.x>0
             *     (i.e., the start point lies in the right half of the starting pixel outside the diamond)
             *  *) dx<0 (i.e., the line extends to the left) and v1_diff.x<0
             *     (i.e., the start point lies in the left half of the starting pixel outside the diamond)
             *  *) dx=0 and v_1.diff.x=0 (in this case there is no line)
             */
            should_draw_start = false;
        }
        else if(boost::math::sign(-dy) != boost::math::sign(-v1_diff.y))
        {
            /*
             * from the previous if-clauses, we start outside the diamond and potentially cross it horizontally.
             * here, we also know that:
             *  *) dy>0 and v1_diff.y<=0, i.e., we start in the upper half and the line extends downwards.
             *  *) dy<0 and v1_diff.y>=0, i.e., we start in the lower half and the line extends upwards.
             *  *) dy=0 and v1_diff.y!=0, i.e., we start anywhere (outside the diamond) and the line is horizontal.
             */
            should_draw_start = true;
        }
        else
        {
            /*
             * we need to perform an intersection test to obtain a result.
             *
             * if the line extends to the right and we hit the pixel's vertical center axis
             * in the range (0,1), we draw the starting pixel. This is equivalent to the
             * line exiting the diamond of the starting pixel.
             */
            float yintersect = fracf(v1->coords.y) + dydx * v1_diff.x;
            should_draw_start = (yintersect >= 0 && yintersect < 1);
        }

        /*
         * End pixel.
         */

        if(std::abs(v2_diff.x) + std::abs(v2_diff.y) < 0.5f)
        {
            // we end inside the diamond.
            should_draw_end = false;
        }
        else if(boost::math::sign(-dx) == boost::math::sign(v2_diff.x))
        {
            /*
             * we start outside the diamond, and one of the following holds:
             *  *) dx>0 (i.e., the line extends to the right) and v2_diff.x<0
             *     (i.e., the end point lies in the left half of the starting pixel outside the diamond)
             *  *) dx<0 (i.e., the line extends to the left) and v2_diff.x>0
             *     (i.e., the end point lies in the right half of the starting pixel outside the diamond)
             *  *) dx=0 and v_1.diff.x=0 (in this case there is no line)
             */
            should_draw_end = false;
        }
        else if(boost::math::sign(dy) == boost::math::sign(v2_diff.y))
        {
            /*
             * from the previous if-clauses, we end outside the diamond and potentially crossed it horizontally.
             * here, we also know that:
             *  *) dy>0 and v2_diff.y>0, i.e., we end in the lower half and the line extended downwards.
             *  *) dy<0 and v2_diff.y<0, i.e., we end in the upper half and the line extended upwards.
             *  *) dy=0 and v1_diff.y=0 (in this case there is no line)
             */
            should_draw_end = true;
        }
        else
        {
            /*
             * we need to perform an intersection test to obtain a result.
             *
             * if the line extends to the right and we hit the pixel's vertical center axis
             * in the range (0,1), we draw the end pixel. This is equivalent to the
             * line exiting the diamond of the end pixel.
             */
            float yintersect = fracf(v2->coords.y) + dydx * v2_diff.x;
            should_draw_end = (yintersect >= 0 && yintersect < 1);
        }

        /*
         * Check if the calculated pixel center directions agree with the line directions.
         * If they do not agree, we need to modify the line's start point (resp. end point).
         */
        is_drawing_start = boost::math::sign(v1_diff.x) != boost::math::sign(dx);
        is_drawing_end = boost::math::sign(v2_diff.x) != boost::math::sign(-dx);

        if(dx < 0)
        {
            std::swap(v1, v2);
            std::swap(v1_diff, v2_diff);

            dx = -dx;
            dy = -dy;

            if(should_draw_start != is_drawing_start)
            {
                offset_v2.x = -v2_diff.x + 0.5f;
                offset_v2.y = offset_v2.x * dydx;
            }

            if(should_draw_end != is_drawing_end)
            {
                offset_v1.x = -v1_diff.x + 0.5f;
                offset_v1.y = offset_v1.x * dydx;
            }
        }
        else
        {
            if(should_draw_start != is_drawing_start)
            {
                offset_v1.x = -v1_diff.x - 0.5f;
                offset_v1.y = offset_v1.x * dydx;
            }

            if(should_draw_end != is_drawing_end)
            {
                offset_v2.x = -v2_diff.x - 0.5f;
                offset_v2.y = offset_v2.x * dydx;
            }
        }
    }
    else
    {
        auto dxdy = dx / dy;
        assert(dxdy >= -1 && dxdy <= 1);

        if(v2_diff.x == -0.5f && dx >= 0)
        {
            v2_diff.x = 0.5f;
        }

        /*
         * Start pixel.
         */

        if(std::abs(v1_diff.x) + std::abs(v1_diff.y) < 0.5f)
        {
            should_draw_start = true;
        }
        else if(boost::math::sign(-dy) == boost::math::sign(-v1_diff.y))
        {
            should_draw_start = false;
        }
        else if(boost::math::sign(-dx) != boost::math::sign(-v1_diff.x))
        {
            should_draw_start = true;
        }
        else
        {
            float xintersect = fracf(v1->coords.x) + dxdy * v1_diff.y;
            should_draw_start = (xintersect >= 0 && xintersect < 1);
        }

        /*
         * End pixel.
         */

        if(std::abs(v2_diff.x) + std::abs(v2_diff.y) < 0.5f)
        {
            should_draw_end = false;
        }
        else if(boost::math::sign(dy) != boost::math::sign(v2_diff.y))
        {
            should_draw_end = false;
        }
        else if(boost::math::sign(dx) == boost::math::sign(v2_diff.x))
        {
            should_draw_end = true;
        }
        else
        {
            float xintersect = fracf(v2->coords.x) + dxdy * v2_diff.y;
            should_draw_end = (xintersect >= 0 && xintersect < 1);
        }

        /*
         * Check if the calculated pixel center directions agree with the line directions.
         * If they do not agree, we need to modify the line's start point (resp. end point).
         */
        is_drawing_start = boost::math::sign(v1_diff.y) == boost::math::sign(-dy);
        is_drawing_end = boost::math::sign(-v2_diff.y) == boost::math::sign(-dy) || (v2_diff.y == 0);

        if(dy < 0)
        {
            std::swap(v1, v2);
            std::swap(v1_diff, v2_diff);

            dx = -dx;
            dy = -dy;

            if(should_draw_start != is_drawing_start)
            {
                offset_v2.y = -v2_diff.y + 0.5f;
                offset_v2.x = offset_v2.y * dxdy;
            }

            if(should_draw_end != is_drawing_end)
            {
                offset_v1.y = -v1_diff.y + 0.5f;
                offset_v1.x = offset_v1.y * dxdy;
            }
        }
        else
        {
            if(should_draw_start != is_drawing_start)
            {
                offset_v1.y = -v1_diff.y - 0.5f;
                offset_v1.x = offset_v1.y * dxdy;
            }

            if(should_draw_end != is_drawing_end)
            {
                offset_v2.y = -v2_diff.y - 0.5f;
                offset_v2.x = offset_v2.y * dxdy;
            }
        }
    }
}

/*
 * note: in_draw_endpoint should be used for correctly drawing line-strips, but is currently unused.
 */
void sweep_rasterizer::draw_line(const swr::impl::render_states& states, [[maybe_unused]] bool in_draw_endpoint, const geom::vertex& v1, const geom::vertex& v2)
{
    line_info info{v1, v2};

    // early-out for lines of zero length.
    if(info.max_absolute_delta == 0)
    {
        return;
    }

    // set up line info.
    info.setup();

    /*
     * Initialize Bresenham's line drawing algorithm.
     */

    // set parameter (x for x-major lines and y for y-major lines) and value
    ml::fixed_t fix_dp, fix_dv;
    ml::fixed_t p, v;
    ml::fixed_t inc_v;

    /*
     * Initialize the error/decision variable.
     *
     * Assume the line to be x-major (otherwise exchange x and y) and further assume x0<x1 (otherwise exchange start and end vertex).
     * Then let dx=x2-x1 and dy=y2-y1 denote the x-/y-deltas. With this notation, the line has the implicit representation
     *
     *     2*F(x,y) = 2*dy*x - 2*dx*y + 2*dx*b = 0 ,
     *
     * where we multiplied everything by 2 for convenience. Assume that a point (x0,y0) lies exactly on the line. Then the (vertically centered)
     * point (x0+1,y0+1/2) is used to determine if the pixel to right right or to the top-right should be colored (where we assumed w.l.o.g.
     * that y2>=y1, since in the other case the appropriate quantities have to be decremented instead of incremented).
     *
     * To initialize the error variable D=F(x0+1,y0+1/2), we use the fact that F vanishes for coordinates lying exactly on the line. In particular,
     * the point (TempV1.coords.x, TempV2.coords.y) lies on the line, so that the initial value for D is given by
     *
     *    D = 2*F(TempV1.coords.x-V1SignedDist.x+1, TempV1.coords.y-V1SignedDist.y+1/2)
     *      = dx - 2*dy - 2*dx*V1SignedDist.y + 2*dy*V1SignedDist.x .
     *
     * Note that here we used the relation LineCoord=PixelCenter+SignedDist. In particular, to advance the line coordinate to the
     * corresponding pixel center, we need to subtract SignedDist.
     */

    ml::fixed_t error;
    ml::fixed_t end_p;

    // get line values.
    ml::fixed_t start_x = info.v1->coords.x + info.offset_v1.x;
    ml::fixed_t start_y = info.v1->coords.y + info.offset_v1.y;

    // initialize gradients along the line.
    rast::line_interpolator attr(*info.v1, *info.v2, v1, states.shader_info->iqs, 1.0f / info.max_absolute_delta);

    // advance to pixel center and initialize end coordinate.
    if(info.is_x_major)
    {
        // calculate parameter range, value range and parameter increment.
        fix_dp = ml::fixed_t(info.dx);
        fix_dv = ml::fixed_t(std::abs(info.dy));

        inc_v = boost::math::sign(info.dy);

        /*
         * set initial parameter, value and error.
         *
         * note that possible terms coming from fill-rule corrections above do not influence the sign-change-behavior
         * of the error term, so we ignore them here.
         */
        p = start_x;
        v = start_y;
        error = fix_dv * 2 - fix_dp;

        // set final parameter
        end_p = ml::fixed_t(std::min(info.v2->coords.x + info.offset_v2.x, static_cast<float>(states.draw_target->properties.height - 1)));
    }
    else
    {
        // calculate parameter range, value range and parameter increment.
        fix_dp = ml::fixed_t(info.dy);
        fix_dv = ml::fixed_t(std::abs(info.dx));

        inc_v = boost::math::sign(info.dx);

        /*
         * set initial parameter, value and error.
         *
         * note that possible terms coming from fill-rule corrections above do not influence the sign-change-behavior
         * of the error term, so we ignore them here.
         */
        p = start_y;
        v = start_x;
        error = fix_dv * 2 - fix_dp;

        // set final parameter
        end_p = ml::fixed_t(std::min(info.v2->coords.y + info.offset_v2.y, static_cast<float>(states.draw_target->properties.height - 1)));
    }

    /*
     * Execute Bresenham's line drawing algorithm.
     */

    boost::container::static_vector<swr::varying, geom::limits::max::varyings> temp_varyings(attr.varyings.size());
    if(info.is_x_major)
    {
        while(p < end_p)
        {
            attr.get_varyings<0, 0>(temp_varyings);

            // only draw the fragment if it is inside the viewport.
            if(p >= 0 && v >= 0 && v < states.draw_target->properties.height)
            {
                rast::fragment_info info(attr.depth_value.value, true, temp_varyings);
                swr::impl::fragment_output out;

                process_fragment(ml::integral_part(p), ml::integral_part(v), states, attr.one_over_viewport_z.value, info, out);
                states.draw_target->merge_color(0, ml::integral_part(p), ml::integral_part(v), out, states.blending_enabled, states.blend_src, states.blend_dst);
            }

            // update error variable.
            if(error > 0)
            {
                v += inc_v;
                error -= fix_dp * 2;
            }
            error += fix_dv * 2;

            ++p;
            attr.advance();
        }
    }
    else
    {
        while(p < end_p)
        {
            attr.get_varyings<0, 0>(temp_varyings);

            // only draw the fragment if it is inside the viewport.
            if(p >= 0 && v >= 0 && v < states.draw_target->properties.width)
            {
                rast::fragment_info info(attr.depth_value.value, true, temp_varyings);
                swr::impl::fragment_output out;

                process_fragment(ml::integral_part(v), ml::integral_part(p), states, attr.one_over_viewport_z.value, info, out);
                states.draw_target->merge_color(0, ml::integral_part(v), ml::integral_part(p), out, states.blending_enabled, states.blend_src, states.blend_dst);
            }

            // update error variable.
            error += fix_dv * 2;

            if(error > 0)
            {
                v += inc_v;
                error -= fix_dp * 2;
            }

            ++p;
            attr.advance();
        }
    }
}

} /* namespace rast */
