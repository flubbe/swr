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

/**
 * Maximal count of user-defined attributes (`ml::vec4`'s) per vertex.
 *
 * See GL_MAX_VERTEX_ATTRIBS here: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGet.xhtml
 */
constexpr int attributes = 16;

/**
 * Maximal count of varyings (`ml::vec4`'s) per vertex.
 *
 * See GL_MAX_VARYING_VECTORS here: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGet.xhtml
 */
constexpr int varyings = 15;

/**
 * Maximal count of uniform locations per program.
 *
 * See GL_MAX_UNIFORM_LOCATIONS here: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGet.xhtml
 */
constexpr int uniform_locations = 1024;

/**
 * Available texture units.
 *
 * See GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS here: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGet.xhtml
 */
constexpr int texture_units = 16;

} /* namespace max */

} /* namespace limits */

} /* namespace geom */
