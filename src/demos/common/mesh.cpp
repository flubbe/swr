/**
 * swr - a software rasterizer
 *
 * mesh support.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers. */
#include <random> /* for non-uniform mesh generation. */

/* software rasterizer headers. */
#include "swr/swr.h"

/* mesh */
#include "mesh.h"

namespace mesh
{

mesh generate_tiling_mesh(float xmin, float xmax, float ymin, float ymax, std::size_t rows, std::size_t cols, float z)
{
    if(xmin >= xmax || ymin >= ymax)
    {
        return {};
    }

    if(rows == 0 || cols == 0)
    {
        return {};
    }

    float xstep = (xmax - xmin) / static_cast<float>(cols);
    float ystep = (ymax - ymin) / static_cast<float>(rows);

    const ml::vec4 rgb[3] = {
      {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    int rgb_ctr = 0;

    mesh m;

    // generate per-vertex data.
    m.has_colors = true;
    for(std::size_t i = 0; i <= rows; ++i)
    {
        for(std::size_t j = 0; j <= cols; ++j)
        {
            m.vertices.attribs.emplace_back(xmin + j * xstep, ymin + i * ystep, z);
            m.colors.attribs.emplace_back(rgb[rgb_ctr % 3]);

            ++rgb_ctr;
        }
    }

    // generate faces.
    for(std::size_t i = 0; i < rows; ++i)
    {
        for(std::size_t j = 0; j < cols; ++j)
        {
            m.indices.emplace_back(j * (cols + 1) + i);
            m.indices.emplace_back(j * (cols + 1) + i + 1);
            m.indices.emplace_back((j + 1) * (cols + 1) + i);

            m.indices.emplace_back(j * (cols + 1) + i + 1);
            m.indices.emplace_back((j + 1) * (cols + 1) + i + 1);
            m.indices.emplace_back((j + 1) * (cols + 1) + i);
        }
    }

    return m;
}

mesh generate_random_tiling_mesh(float xmin, float xmax, float ymin, float ymax, std::size_t rows, std::size_t cols, float z, float zrange, std::size_t mesh_start_x, std::size_t mesh_start_y, std::size_t mesh_end_x, std::size_t mesh_end_y)
{
    if(xmin >= xmax || ymin >= ymax)
    {
        return {};
    }

    if(zrange < 0)
    {
        return {};
    }

    if(rows == 0 || cols == 0)
    {
        return {};
    }

    if(mesh_end_x == static_cast<std::size_t>(-1))
    {
        mesh_end_x = cols;
    }

    if(mesh_end_y == static_cast<std::size_t>(-1))
    {
        mesh_end_y = rows;
    }

    if(mesh_start_x >= mesh_end_x || mesh_start_y >= mesh_end_y)
    {
        return {};
    }

    float xstep = (xmax - xmin) / static_cast<float>(cols);
    float ystep = (ymax - ymin) / static_cast<float>(rows);

    const std::array<ml::vec4, 3> rgb{
      ml::vec4{1, 0, 0}, ml::vec4{0, 1, 0}, ml::vec4{0, 0, 1}};
    int rgb_ctr = 0;

    // initialize random number generator to generate vertex offsets.
    std::random_device rnd_device;
    std::mt19937 mersenne_engine{rnd_device()};
    std::uniform_real_distribution<float> xdist{-xstep / 5, xstep / 5};
    std::uniform_real_distribution<float> ydist{-ystep / 5, ystep / 5};
    std::uniform_real_distribution<float> zdist{-zrange, zrange};

    auto xgen = [&xdist, &mersenne_engine]()
    { return xdist(mersenne_engine); };
    auto ygen = [&ydist, &mersenne_engine]()
    { return ydist(mersenne_engine); };
    auto zgen = [&zdist, &mersenne_engine]()
    { return zdist(mersenne_engine); };

    // generate per-vertex mesh data.
    mesh m;
    m.has_colors = true;
    for(std::size_t i = 0; i < rows + 1; ++i)
    {
        for(std::size_t j = 0; j < cols + 1; ++j)
        {
            m.vertices.attribs.emplace_back(xmin + j * xstep + xgen(), ymin + i * ystep + ygen(), z + zgen());
            m.colors.attribs.emplace_back(rgb[rgb_ctr % rgb.size()]);

            ++rgb_ctr;
        }
    }

    // generate faces.
    for(std::size_t i = mesh_start_y; i < mesh_end_y; ++i)
    {
        for(std::size_t j = mesh_start_x; j < mesh_end_x; ++j)
        {
            assert(j * (cols + 1) + i < m.vertices.attribs.size());
            assert(j * (cols + 1) + i + 1 < m.vertices.attribs.size());
            assert((j + 1) * (cols + 1) + i < m.vertices.attribs.size());

            m.indices.emplace_back(j * (cols + 1) + i);
            m.indices.emplace_back(j * (cols + 1) + i + 1);
            m.indices.emplace_back((j + 1) * (cols + 1) + i);

            assert(j * (cols + 1) + i + 1 < m.vertices.attribs.size());
            assert((j + 1) * (cols + 1) + i + 1 < m.vertices.attribs.size());
            assert((j + 1) * (cols + 1) + i < m.vertices.attribs.size());

            m.indices.emplace_back(j * (cols + 1) + i + 1);
            m.indices.emplace_back((j + 1) * (cols + 1) + i + 1);
            m.indices.emplace_back((j + 1) * (cols + 1) + i);
        }
    }

    return m;
}

} /* namespace mesh */