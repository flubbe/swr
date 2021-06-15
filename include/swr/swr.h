/**
 * swr - a software rasterizer
 * 
 * public interface.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#pragma once

/*
 * dependencies.
 */

/* C++ headers. */
#include <vector>

/* SDL */
#ifndef __linux__
#    ifdef __APPLE__
#        include <SDL.h>
#    else
#        include "SDL.h"
#    endif
#else
#    include <SDL2/SDL.h>
#endif

/* user headers. */
#include "ml/all.h"

/*
 * public interface.
 */

namespace swr
{

/** List of possible errors that may occur in the renderer. */
enum class error
{
    none = 0,              /** no error. */
    invalid_value = 1,     /** an invalid parameter/value was detected */
    invalid_operation = 2, /** an invalid operation was performed */
    unimplemented = 3      /** the operation is not implemented */
};

/** return last error and clear error flag. */
error GetLastError();

/** Texturing modes. */
enum class wrap_mode
{
    repeat,          /** Repeat the texture. */
    mirrored_repeat, /** Repeat the texture and mirror it on each repetition */
    clamp_to_edge    /** Clamp the texture coordinates to [0,1] */
};

/** 
 * default positions of color, normal and texture coordinates inside the vertex attributes. 
 */
namespace default_index
{

enum
{
    position = 0,
    color = 1,
    tex_coord = 2,
    normal = 3,
    max = 4
};

} /* namespace default_index */

/*
 * Vertex buffers.
 */

/**
 * Create a vertex buffer from a std::vector of vertices.
 * \param vb Contains all vertices that should make up the vertex buffer.
 * \return Returns the unique ID of the newly created vertex buffer.
 */
uint32_t CreateVertexBuffer(const std::vector<ml::vec4>& vb);

/**
 * Create an index buffer from a std::vector of indices.
 * \param ib Contains all indices that should make up the index buffer.
 * \return Returns the unique ID of the newly created index buffer.
 */
uint32_t CreateIndexBuffer(const std::vector<uint32_t>& ib);

/**
 * Free the memory of a vertex buffer. If the supplied id does
 * not represent such a buffer, the function sets last_error to invalid_value.
 *
 * \param id Unique ID representing a vertex buffer;
 */
void DeleteVertexBuffer(uint32_t id);

/**
 * Free the memory of a vertex buffer. If the supplied id does
 * not represent such a buffer, the function sets last_error to invalid_value.
 *
 * \param id Unique ID representing an index buffer.
 */
void DeleteIndexBuffer(uint32_t id);

/**
 * Specifies how the vertex list in a vertex buffer (possibly in combination with an index buffer) should be interpretted.
 */
enum class vertex_buffer_mode
{
    points,         /** A list of separate points. */
    lines,          /** A list of lines. */
    triangles,      /** A list of triangles. */
    triangle_fan,   /** A triangle fan.     !!fixme: currently only available in immediate mode. */
    triangle_strip, /** A triangle strip.   !!fixme: currently only available in immediate mode. */
    quads,          /** A list of quads.    !!fixme: currently only available in immediate mode. */
    polygon         /** A (planar) polygon. !!fixme: currently only available in immediate mode. */
};

/*
 * Drawing functions.
 */

/**
 * Add a vertex_count vertices to the list of objects to be rendered.
 * \param vertex_count The vertex count.
 * \param mode Specifies how the contents of the subset of the vertex buffer should be interpretted.
 */
void DrawElements(std::size_t vertex_count, vertex_buffer_mode mode);

/**
 * Add a subset of a vertex buffer (as specified by the index buffer) to the list of objects to be rendered.
 * \param index_buffer_id Specifies an (ordered) subset of the vertex buffer which should be used.
 * \param mode Specifies how the contents of the subset of the vertex buffer should be interpretted.
 */
void DrawIndexedElements(uint32_t index_buffer_id, vertex_buffer_mode mode);

/*
 * Vertex attribute buffers.
 */

/**
 * Create an attribute buffer from std::vector of ml::vec4's.
 */
uint32_t CreateAttributeBuffer(const std::vector<ml::vec4>& Data);

/**
 * Delete an attribute buffer.
 */
void DeleteAttributeBuffer(uint32_t id);

/**
 * Activate attribute buffer.
 */
void EnableAttributeBuffer(uint32_t id, uint32_t slot);

/**
 * Deactivate buffer.
 */
void DisableAttributeBuffer(uint32_t id);

/*
 * Uniform variables.
 */
void BindUniform(uint32_t UniformId, int Value);
void BindUniform(uint32_t UniformId, float Value);
void BindUniform(uint32_t UniformId, ml::mat4x4 Value);
void BindUniform(uint32_t UniformId, ml::vec4 Value);

/*
 * Immediate mode support.
 */

/**
 * Begin the specification of new primitives.
 * \param Mode Specifies how the vertices in this primitive should be interpretted.
 */
void BeginPrimitives(vertex_buffer_mode Mode);

/**
 * End the primitive specification.
 */
void EndPrimitives();

/**
 * Set the current color. All components are internally clamped to [0,1].
 * \param r Red component.
 * \param g Green component.
 * \param b Blue compoment.
 * \param a Alpha component.
 */
void SetColor(float r, float g, float b, float a);

/**
 * Set the current texture coordinates.
 * \param u U component.
 * \param v V component.
 */
void SetTexCoord(float u, float v);

/**
 * Insert a vertex (with the current color and the current texture coordinates as additional data) at
 * a position (x,y,z,w) into the current primitive.
 * \param x X component.
 * \param y Y component.
 * \param z Z component.
 * \param w W component.
 */
void InsertVertex(float x, float y, float z = 0.0f, float w = 1.0f);

/*
 * Rasterization.
 */

/** a handle to a render context. */
typedef void* context_handle;

/**
 * This function sythesizes the image from the contents of the (internal) drawing lists.
 */
void Present();

/*
 * Depth buffering and testing.
 */

/** compare a new value against a stored value. */
enum class comparison_func
{
    pass,          /** test always accepts the new value. */
    fail,          /** test always rejects the new value. */
    equal,         /** test passes if both values are equal */
    not_equal,     /** test passes if the values are not equal */
    less,          /** test passes if the new value is smaller */
    less_equal,    /** test passes if the new value is smaller or equal */
    greater,       /** test passes if the new value is bigger */
    greater_equal, /** test passes if the new value is bigger or equal */
};

/**
 * Specify a new depth test.
 * \param func The new depth test.
 */
void SetDepthTest(comparison_func func);

/**
 * Return the current depth test.
 * \return The current depth test.
 */
comparison_func GetDepthTest();

/**
 * Specify the clear value for the depth buffer. The value is clamped to [0,1].
 * \param z The clear value.
 */
void SetClearDepth(float z);

/**
 * Clear the depth buffer.
 */
void ClearDepthBuffer();

/**
 * Specify mapping of depth values from normalized device coordinates to window coordinates.
 * See also https://www.opengl.org/sdk/docs/man2/xhtml/glDepthRange.xml
 *
 * \param zNear Specifies the mapping of the near clipping plane to window coordinates. The initial value is 0.
 * \param zFar Specifies the mapping of the far clipping plane to window coordinates. The initial value is 1.
 */
void DepthRange(float zNear, float zFar);

/*
 * Color buffer.
 */

/**
 * Specify clear values for the color buffers.
 * See also https://www.opengl.org/sdk/docs/man4/html/glClearColor.xhtml
 *
 * \param r red component of clear color
 * \param g green component of clear color
 * \param b blue component of clear color
 * \param a alpha component of clear color
 */
void SetClearColor(float r, float g, float b, float a);

/**
 * Clear the color buffer.
 */
void ClearColorBuffer();

/*
 * Culling.
 */

/** Specify front-facing triangles/polygons. */
enum class front_face_orientation
{
    cw, /** clockwise triangles are front-facing */
    ccw /** counter-clockwise triangles are front-facing */
};

/**
 * Define front- and back-facing polygons.
 * See also https://www.opengl.org/sdk/docs/man2/xhtml/glFrontFace.xml
 *
 * \param Mode Specifies the orientation of front-facing polygons. The initial value is ccw.
 */
void SetFrontFace(front_face_orientation Mode);

/** Return the current front-facing polygons. */
front_face_orientation GetFrontFace();

/** culling mode. */
enum class cull_face_direction
{
    front,         /** cull back facing triangles */
    back,          /** cull front facing triangles*/
    front_and_back /** cull all faces */
};

/**
 * Specify whether front- or back-facing facets can be culled.
 * See also https://www.opengl.org/sdk/docs/man/docbook4/xhtml/glCullFace.xml
 *
 * \param Face Specifies whether front- or back-facing facets are candidates for culling.
 */
void SetCullMode(cull_face_direction face);

/** Return the current cull mode. */
cull_face_direction GetCullMode();

/** Polygon rendering modes. */
enum class polygon_mode
{
    point, /** draw vertices as points */
    line,  /** draw line strips */
    fill,  /** draw filled polygons */
};

/**
 * Select a polygon rasterization mode (for both front- and back-facing polygons).
 * See also https://www.opengl.org/sdk/docs/man/html/glPolygonMode.xhtml
 *
 * \param Mode Specifies the rasterization mode. Must be one of the modes specified by EPolygonMode.
 */
void SetPolygonMode(polygon_mode Mode);

/** Return the current polygon rasterization mode, which is applied to both front- and back-facing polygons. */
polygon_mode GetPolygonMode();

/*
 * Texturing.
 */

/** Pixel formats. */
enum class pixel_format
{
    unsupported, /** An unsupported pixel format. */
    rgba8888,    /** 32-bit with 8 bits per channel in the order red, green, blue, alpha. */
    argb8888,    /** 32-bit with 8 bits per channel in the order alpha, red, green, blue. */
    bgra8888     /** 32-bit with 8 bits per channel in the order blue, green, red, alpha. */
};

/*
 * Texture management.
 */

/**
 * Allocate a texture and return its id.
 * \return If successful, a positive texture id of the newly created texture. Zero if an error occured.
 */
uint32_t CreateTexture();

/**
 * Free texture memory. If the texture is currently bound, it is unbound and then freed.
 * \param TextureId The id of the texture to be freed.
 */
void ReleaseTexture(uint32_t TextureId);

/** texture targets. */
enum class texture_target
{
    texture_2d, /** 2d texture target. */
};

/** texture unit. */
enum texture_unit
{
    texture_0 = 0,   /** texture unit 0 */
    texture_1 = 1,   /** texture unit 1 */
    texture_2 = 2,   /** texture unit 2 */
    texture_3 = 3,   /** texture unit 3 */
    texture_4 = 4,   /** texture unit 4 */
    texture_5 = 5,   /** texture unit 5 */
    texture_6 = 6,   /** texture unit 6 */
    texture_7 = 7,   /** texture unit 7 */
    texture_8 = 8,   /** texture unit 8 */
    texture_9 = 9,   /** texture unit 9 */
    texture_10 = 10, /** texture unit 10 */
    texture_11 = 11, /** texture unit 11 */
    texture_12 = 12, /** texture unit 12 */
    texture_13 = 13, /** texture unit 13 */
    texture_14 = 14, /** texture unit 14 */
    texture_15 = 15, /** texture unit 15 */
};

/**
 * Select active texture unit.
 * \param unit specifies which texture unit to activate.
 */
void ActiveTexture(uint32_t unit);

/**
 * Make the specified texture the active one. This also makes the texture parameters available for request.
 * \param target The texture target to bind the texture to.
 * \param id The id of the texture that should be bound to the texture unit.
 */
void BindTexture(texture_target target, uint32_t id);

/**
 * Allocate texture storage and, if data is non-empty, set the image data of a texture.
 * \param texture_id id of the texture
 * \param level the mipmap level of data
 * \param width the width of the texture
 * \param height the height of the texture
 * \param format the pixel format of the pixel data
 * \param data if non-empty, this contains the pixel data.
 */
void SetImage(uint32_t texture_id, uint32_t level, size_t width, size_t height, pixel_format format, const std::vector<uint8_t>& data);

/**
 * Update part of a texture.
 * \param texture_id id of the texture to be updated.
 * \param level mipmap level.
 * \param offset_x x-offset
 * \param offset_y y-offset
 * \param width width of the data
 * \param height height of the data
 * \param format pixel format of the data
 * \param data image data
 */
void SetSubImage(uint32_t texture_id, uint32_t level, size_t offset_x, size_t offset_y, size_t width, size_t height, pixel_format format, const std::vector<uint8_t>& data);

/**
 * Specify the texture wrapping mode with respect to a direction.
 *
 * \param id The unique texture id, as returned by CreateTexture.
 * \param s Wrapping mode in s direction
 * \param t Wrapping mode in t direction
 *
 * \note Currently only 2d textures are handled. If debugging and if an error occures while binding the texture, an assertion is raised.
 */
void SetTextureWrapMode(uint32_t id, wrap_mode s, wrap_mode t);

/**
 * Get the current texture wrapping mode with respect to a direction.
 *
 * \param id The unique texture id, as returned by CreateTexture.
 * \param s output for wrapping mode in s direction. may be set to null for no output.
 * \param t output for wrapping mode in s direction. may be set to null for no output.
 */
void GetTextureWrapMode(uint32_t id, wrap_mode* s, wrap_mode* t);

/** Texture Filter. */
enum class texture_filter
{
    nearest, /** Get the nearest texel in the nearest mipmap. */
    dithered /** Get a dithered texel in the nearest mipmap. This is an approximation to a Gaussian interpolation in the nearest mipmap. */
};

/**
 * Set the filter for the currently active texture which is used for minification.
 * \param Filter nearest or dithered.
 */
void SetTextureMinificationFilter(texture_filter Filter);

/** Return the minification filter for the currently active texture. */
texture_filter GetTextureMinificationFilter();

/**
 * Set the filter for the currently active texture which is used for magnification.
 * \param Filter nearest or dithered.
 */
void SetTextureMagnificationFilter(texture_filter Filter);

/** Return the magnification filter for the currently active texture. */
texture_filter GetTextureMagnificationFilter();

/*
 * Texture sampling.
 */

/** (floating-point) texture sampler. */
struct sampler_2d
{
    /** virtual destructor. */
    virtual ~sampler_2d() = default;

    /** Return a texel (as a 4-vector) while respecting the active texture filters. */
    virtual ml::vec4 sample_at(const ml::vec2 UV) const = 0;
};

/*
 * Alpha blending.
 */

/** blending operation. */
enum class blend_func
{
    zero,               /** return zero. */
    one,                /** return one. */
    src_alpha,          /** multiply by the source alpha value */
    src_color,          /** multiply (component-wise) by the source's color values. */
    one_minus_src_alpha /** multiply this factor by (1-A), where A in [0,1] is the source's alpha value. */
};

/**
 * Specify pixel arithmetic.
 * \param SourceFactor Can be one of the EBlendFunc values. The initial value is Blend_One.
 * \param DestinationFactor Can be one of the EBlendFunc values. The initial value is Blend_Zero.
 */
void SetBlendFunc(blend_func SourceFactor, blend_func DestinationFactor);

/** Return the blend function for the source. */
blend_func GetSourceBlendFunc();

/** Return the blend function for the destination. */
blend_func GetDestinationBlendFunc();

/*
 * Scissor Test .
 */

/**
 * Set the scissor box, in viewport coordinates.
 * See also https://www.opengl.org/sdk/docs/man2/xhtml/glScissor.xml
 *
 * \param x X coordinate of the scissor box.
 * \param y Y coordinate of the scissor box.
 * \param width Width of the scissor box.
 * \param height Height of the scissor box.
 */
void SetScissorBox(int x, int y, int width, int height);

/*
 * States.
 */

/** States of the graphics pipeline, which can be enabled or disabled. */
enum class state
{
    blend,        /** Blending. Initially disabled. */
    cull_face,    /** Face culling. Initially disabled. */
    depth_test,   /** Depth testing. Initially enabled. */
    depth_write,  /** Depth writing. Initially enabled. */
    scissor_test, /** Scissor test. Initially disabled. */
    texture,      /** Texturing. Initially disabled. */
};

/**
 * Enable or disable a specific state.
 * \param State The state to modify.
 * \param bNewState _true_ enables the state, _false_ disables it.
 */
void SetState(state s, bool new_state);

/**
 * Return the value of a given state.
 * \param State A state.
 */
bool GetState(state s);

/*
 * Render contexts.
 */

/**
 * Set the current viewport's dimensions.
 * \param x x coordinate of top-left corner of viewport rectangle.
 * \param y y coordinate of top-left corner of viewport rectangle.
 * \param width Width of viewport rectangle.
 * \param height Height of viewport rectangle.
 */
void SetViewport(int x, int y, unsigned int width, unsigned int height);

/**
 * Create a rendering context from an SDL window and an SDL renderer. The context has the same
 * internal dimension as the supplied window.
 *
 * \param Window A valid SDL window.
 * \param Renderer A valid SDL renderer.
 * \param thread_hint A hint to the rasterizer how many threads to use.
 * \return A rendering context that may be used for software rasterization.
 */
context_handle CreateSDLContext(SDL_Window* Window, SDL_Renderer* Renderer, uint32_t thread_hint = 0);

/**
 * Destroy a context created with CreateSDLContext. Frees all memory associated to the context
 * (e.g. color buffers, depth buffers, texture memory).
 *
 * \param Context A context to destroy.
 */
void DestroyContext(context_handle Context);

/**
 * Make the supplied context active.
 * \param Context The context to be made active.
 * \return _true_ on success, and _false_ on failure.
 */
bool MakeContextCurrent(context_handle Context);

/**
 * Copies the contents of the default color buffer of the context into the active window.
 * \param Context The context to use for color buffer copying.
 */
void CopyDefaultColorBuffer(context_handle Context);

/*
 * Versioning.
 */

/** Get the current library version. */
void GetVersion(int& major, int& minor, int& patch);

} /* namespace swr */
