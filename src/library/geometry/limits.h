/**
 * swr - a software rasterizer
 * 
 * limits for vertex attributes, varyings and uniforms.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

namespace geom
{

namespace limits
{

namespace max
{

/** Maximal count of user-defined attributes per vertex. */
constexpr int attributes = 16;

/** Maximal count of varyings per vertex. */
constexpr int varyings = 32;

/** Maximal count of uniform locations per program. */
constexpr int uniform_locations = 1024;

} /* namespace max */

} /* namespace limits */

} /* namespace geom */
