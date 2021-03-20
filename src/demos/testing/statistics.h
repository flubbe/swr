/**
 * swr - a software rasterizer
 * 
 * record and display performance data.
 * 
 * NOTE: the used font has to have a fixed glyph width and height of 10, respectively. this is currently hardcoded.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* boost dependency */
#include <boost/circular_buffer.hpp>

namespace stats
{

/** hardcoded glyph width */
constexpr int fixed_glyph_width{10};

/** hardcoded glyph height */
constexpr int fixed_glyph_height{10};

/** record msec per frame and FPS. */
class performance_data
{
  public:
    static const int size = 70;

  private:
    boost::circular_buffer<float> fps_buffer{size};
    boost::circular_buffer<float> msec_buffer{size};

    float fps_peak = 0;
    float msec_peak = 0;

    float fps_mean = 0;
    float msec_mean = 0;

    float fps_stddev = 0;
    float msec_stddev = 0;

  public:
    /** default constructor. */
    performance_data() = default;

    void reset()
    {
        fps_buffer.resize(0);
        msec_buffer.resize(0);
    }

    /** add data point. does not add infinite data points. */
    void add(float fps, float msec)
    {
        // validate arguments
        if(isfinite(fps) && isfinite(msec))
        {
            fps_buffer.push_back(fps);
            msec_buffer.push_back(msec);
        }
    }

    /** update statistical quantities (peaks and mean). */
    void update()
    {
        if(fps_buffer.size() > 0)
        {
            // calculate mean and peak values.
            fps_mean = 0;
            fps_peak = 0;
            for(auto f: fps_buffer)
            {
                if(f > fps_peak)
                {
                    fps_peak = f;
                }
                fps_mean += f;
            }
            fps_mean /= fps_buffer.size();

            // calculate standard deviation.
            fps_stddev = 0;
            for(auto f: fps_buffer)
            {
                fps_stddev += (f - fps_mean) * (f - fps_mean);
            }
            fps_stddev = std::sqrt(fps_stddev) / fps_buffer.size();
        }

        if(msec_buffer.size() > 0)
        {
            msec_mean = 0;
            msec_peak = 0;
            for(auto f: msec_buffer)
            {
                if(f > msec_peak)
                {
                    msec_peak = f;
                }
                msec_mean += f;
            }
            msec_mean /= msec_buffer.size();

            // calculate standard deviation.
            msec_stddev = 0;
            for(auto f: msec_buffer)
            {
                msec_stddev += (f - msec_mean) * (f - msec_mean);
            }
            msec_stddev = std::sqrt(msec_stddev) / msec_buffer.size();
        }
    }

    /*
     * getters.
     */

    float get_fps_peak() const
    {
        return fps_peak;
    }

    float get_fps_mean() const
    {
        return fps_mean;
    }

    float get_fps_stddev() const
    {
        return fps_stddev;
    }

    float get_msec_peak() const
    {
        return msec_peak;
    }

    float get_msec_mean() const
    {
        return msec_mean;
    }

    float get_msec_stddev() const
    {
        return msec_stddev;
    }

    const boost::circular_buffer<float>& get_fps_buffer() const
    {
        return fps_buffer;
    }

    const boost::circular_buffer<float> get_msec_buffer() const
    {
        return msec_buffer;
    }
};

/** display performance data. adjust graph scaling for big/small data points, based on a heuristic. */
class overlay
{
    const performance_data* data = nullptr;
    font::extended_ascii_bitmap_font font;
    font::renderer font_rend;

    /** the shader used for rendering the graph. */
    uint32_t graph_shader_id{0};

    float calc_fps_rescale(float rescale_factor) const
    {
        if(!data)
        {
            return 0;
        }

        return 0.5 * (rescale_factor * (1.0 - 4 * data->get_fps_stddev() / data->get_fps_mean()) / data->get_fps_peak() + rescale_factor * (1.0 + 4 * data->get_fps_stddev() / data->get_fps_peak()) / data->get_fps_mean());
    }

    float calc_msec_rescale(float rescale_factor) const
    {
        if(!data)
        {
            return 0;
        }

        return 0.5 * (rescale_factor * (1.0 - 4 * data->get_msec_stddev() / data->get_msec_mean()) / data->get_msec_peak() + rescale_factor * (1.0 + 4 * data->get_msec_stddev() / data->get_msec_peak()) / data->get_msec_mean());
    }

  public:
    bool initialize(uint32_t in_graph_shader, uint32_t font_shader, uint32_t font_tex_id, uint32_t tex_width, uint32_t tex_height, uint32_t font_map_width, uint32_t font_map_height)
    {
        graph_shader_id = in_graph_shader;

        // create font.
        font = font::extended_ascii_bitmap_font::create_uniform_font(font_tex_id, tex_width, tex_height, font_map_width, font_map_height, fixed_glyph_width, fixed_glyph_height);
        font_rend.update(font_shader, font);
        return true;
    }

    void set_data(const performance_data* in_data)
    {
        data = in_data;
    }

    void draw_graph(int x, int y)
    {
        if(data == nullptr)
        {
            return;
        }

        bool bTex = swr::GetState(swr::state::texture);
        swr::SetState(swr::state::texture, false);

        bool bCull = swr::GetState(swr::state::cull_face);
        swr::SetState(swr::state::cull_face, false);

        bool bDepth = swr::GetState(swr::state::depth_test);
        swr::SetState(swr::state::depth_test, false);

        swr::BindShader(graph_shader_id);

        swr::BeginPrimitives(swr::vertex_buffer_mode::quads);
        swr::SetColor(1, 0, 0, 1);

        float Scaling = calc_fps_rescale(50);

        // right-justify output.
        x += 2 * (performance_data::size - data->get_fps_buffer().size());

        for(auto f: data->get_fps_buffer())
        {
            // normalize data to 50 pixels (compared to peak value).
            float Height = f * Scaling;

            swr::InsertVertex(x, y, 0, 1);
            swr::InsertVertex(x + 1, y, 0, 1);
            swr::InsertVertex(x + 1, y - Height, 0, 1);
            swr::InsertVertex(x, y - Height, 0, 1);

            x += 2;
        }
        swr::EndPrimitives();

        Scaling = calc_msec_rescale(50);

        swr::BeginPrimitives(swr::vertex_buffer_mode::quads);
        swr::SetColor(0, 1, 0, 1);

        // right-justify output.
        x += 2 * (performance_data::size - data->get_msec_buffer().size());

        for(auto f: data->get_msec_buffer())
        {
            float Height = f * Scaling;

            swr::InsertVertex(x, y, 0, 1);
            swr::InsertVertex(x + 1, y, 0, 1);
            swr::InsertVertex(x + 1, y - Height, 0, 1);
            swr::InsertVertex(x, y - Height, 0, 1);

            x += 2;
        }
        swr::EndPrimitives();

        swr::BindShader(0);
        swr::SetState(swr::state::depth_test, bDepth);
        swr::SetState(swr::state::cull_face, bCull);
        swr::SetState(swr::state::texture, bTex);
    }

    void draw_caption(int x, int y)
    {
        if(data == nullptr)
        {
            return;
        }

        bool bTex = swr::GetState(swr::state::texture);
        swr::SetState(swr::state::texture, false);

        bool bCull = swr::GetState(swr::state::cull_face);
        swr::SetState(swr::state::cull_face, false);

        bool bDepth = swr::GetState(swr::state::depth_test);
        swr::SetState(swr::state::depth_test, false);

        if(data->get_fps_buffer().size() > 0)
        {
            font_rend.draw_string(fmt::format("sdev: {:.2f}", data->get_fps_stddev()), x, y - 66);
            font_rend.draw_string(fmt::format("mean: {:.2f}", data->get_fps_mean()), x, y - 82);
            font_rend.draw_string(fmt::format("peak: {:.2f}", data->get_fps_peak()), x, y - 98);
        }

        if(data->get_msec_buffer().size() > 0)
        {
            font_rend.draw_string(fmt::format("sdev: {:.2f}", data->get_msec_stddev()), x + 2 * performance_data::size, y - 66);
            font_rend.draw_string(fmt::format("mean: {:.2f}", data->get_msec_mean()), x + 2 * performance_data::size, y - 82);
            font_rend.draw_string(fmt::format("peak: {:.2f}", data->get_msec_peak()), x + 2 * performance_data::size, y - 98);
        }

        swr::SetState(swr::state::depth_test, bDepth);
        swr::SetState(swr::state::cull_face, bCull);
        swr::SetState(swr::state::texture, bTex);
    }

    /** getters. */
    font::renderer& get_font_renderer()
    {
        return font_rend;
    }
};

} /* namespace stats */