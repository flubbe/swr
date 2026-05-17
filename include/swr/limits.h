/**
 * swr - a software rasterizer
 *
 * renderer limits.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

namespace swr
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

/**
 * Maximal count of framebuffer color attachments.
 *
 * See https://wikis.khronos.org/opengl/Framebuffer_Object.
 */
constexpr int color_attachments = 8;

} /* namespace max */

} /* namespace limits */

}    // namespace swr
