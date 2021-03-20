/**
 * swr - a software rasterizer
 * 
 * software renderer demonstration.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

/* C++ headers */
#include <vector>
#include <list>
#include <mutex>
#include <iostream>

/* boost headers. */
#include <boost/container/static_vector.hpp>

/* texture testing */
#include "lodepng.h"

/* platform code. */
#include "../common/platform/platform.h"

/* user headers. */
#include "swr/swr.h"
#include "swr/shaders.h"
#include "swr/stats.h"

#include "../common/utils.h"
#include "font.h"
#include "statistics.h"

/* shaders. */
#include "common/shaders/color.h"
#include "common/shaders/front_face_test.h"
#include "common/shaders/phong.h"
#include "common/shaders/tex_coords.h"
#include "common/shaders/texture.h"

/* application framework */
//!!todo: implement
//#include "tests/framework/framework.h"

/*-----------------------------------------------------------------------------
    Quick configuration.
-----------------------------------------------------------------------------*/

/*
 * possible defines for drawing different scene parts:
 * DRAW_OVERLAY, DRAW_ALPHA_TEXTURES, DRAW_TRIANGLE, DRAW_LIGHT, DRAW_CUBE
 */
#define DRAW_CUBE
#define DRAW_LIGHT
//#define DRAW_ALPHA_TEXTURES
//#define DRAW_TRIANGLE
#define DRAW_OVERLAY

/*
 * enable scissor test.
 */
#define DO_SCISSOR_TEST

/*
 * if BENCHMARK is defined, the cube will spin and we render BENCHMARK_FRAMECOUNT frames.
 */
//#define BENCHMARK
#define BENCHMARK_FRAMECOUNT 10000

/*
 * define TEXCOORDS_TO_COLOR to display the texture coordinates as color
 * instead of using the phong/blinn_phong shader.
 */
//#define TEXCOORDS_TO_COLOR

/*-----------------------------------------------------------------------------
    Constants.
-----------------------------------------------------------------------------*/

/** Render window width. */
const int Win_Width = 640;

/** Render window height. */
const int Win_Height = 480;

/*-----------------------------------------------------------------------------
    Classes.
-----------------------------------------------------------------------------*/

/** 
 * get the next power of two of a 32-bit unsigned integer.
 * 
 * source: https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
 */
static uint32_t next_power_of_two(uint32_t n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/** 
 * load textures, with dimensions possibly not being powers of two. data is RGBA with 8 bits per channel. 
 * the largest valid texture coordinates are written to max_u and max_v.
 */
static uint32_t load_texture(uint32_t w, uint32_t h, const std::vector<uint8_t>& data, float* max_u = nullptr, float* max_v = nullptr)
{
    int adjusted_w = next_power_of_two(w);
    int adjusted_h = next_power_of_two(h);

    std::vector<uint8_t> resized_tex;
    resized_tex.resize(adjusted_w * adjusted_h * sizeof(uint32_t)); /* sizeof(...) for RGBA */

    // copy texture.
    for(uint32_t j = 0; j < h; ++j)
    {
        for(uint32_t i = 0; i < w; ++i)
        {
            *reinterpret_cast<uint32_t*>(&resized_tex[(j * adjusted_w + i) * sizeof(uint32_t)]) = *reinterpret_cast<const uint32_t*>(&data[(j * w + i) * sizeof(uint32_t)]);
        }
    }

    auto tex_id = swr::CreateTexture(adjusted_w, adjusted_h, swr::pixel_format::rgba8888, swr::wrap_mode::repeat, swr::wrap_mode::repeat, resized_tex);
    if(tex_id)
    {
        if(max_u)
        {
            *max_u = (adjusted_w != 0) ? static_cast<float>(w) / static_cast<float>(adjusted_w) : 0;
        }
        if(max_v)
        {
            *max_v = (adjusted_h != 0) ? static_cast<float>(h) / static_cast<float>(adjusted_h) : 0;
        }
    }
    return tex_id;
}

/** Logging to stdout using C++ iostream. */
class log_iostream : public platform::log_device
{
    std::mutex mtx;

  protected:
    void log_n(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mtx); /* prevent unpredictable output interleaving */
        std::cout << message << std::endl;
    }
};
static log_iostream global_log;

class DemoEngine
{
  protected:
    /*
     * Resources.
     */

    /** The vertex buffer used for testing. Will be set up with the vertices of a cube. */
    uint32_t VBufferId = 0;

    /** The index buffer corresponding to the vertex buffer. */
    uint32_t IBufferId = 0;

    /** Buffer id for colors */
    uint32_t ColorBufferId = 0;

    /** Buffer id for face normals */
    uint32_t NormalBufferId = 0;

    /** Buffer id for face tangents */
    uint32_t TangentBufferId = 0;

    /** Buffer id for face bitangents */
    uint32_t BitangentBufferId = 0;

    /** Buffer holding u/v texture coorindates */
    uint32_t UVBufferId = 0;

    /** Vertex data for a 2d rectangle. */
    uint32_t RectVBufferId = 0;

    /** color data for a 2d rectangle. */
    uint32_t RectCBufferId = 0;

    /** texture coordinate data for a 2d rectangle. */
    uint32_t RectTBufferId = 0;

    /** index data for a 2d rectangle. */
    uint32_t RectIBufferId = 0;

    /** texture ids. */
    uint32_t CrateTexId = 0;
    uint32_t NormalTexId = 0;
    uint32_t FontTexId = 0;

    /** font texture width and height. */
    uint32_t font_tex_width{0}, font_tex_height{0};

    /** Light Position. */
    ml::vec4 LightPos = ml::vec4(0, 0, 0, 0);

    /** Shaders. */
    enum
    {
        DEMO_SHADER_COUNT = 5
    };
    swr::program* Shaders[DEMO_SHADER_COUNT] = {nullptr};
    uint32_t ShaderIds[DEMO_SHADER_COUNT] = {0};

    enum
    {
        SI_ColorOnly = 0,
        SI_TextureOnly = 1,
        SI_Phong = 2,
        SI_BlinnPhong = 3,
        SI_FrontFaceTest = 4
    } EShaderIndex;

#ifdef TEXCOORDS_TO_COLOR
    uint32_t CurrentLightingShader = SI_BlinnPhong; /* in this instance, this actually is the shader::display_tex_coords shader. */
#else
    uint32_t CurrentLightingShader = SI_Phong;
#endif

    /*
     * States.
     */

    /** Camera position. */
    ml::vec3 Position = ml::vec3(0, 0, -7.f);

    /** min 1, max 4 */
    int cube_count = 1;

    /** default cube positins. */
    const ml::vec3 default_cube_pos[4] = {{0, 0, -7}, {4, 0, -8}, {4, 4, -8}, {4, 4, -8}};

    /** This holds a (rotational) offset of the cube to do some animation. */
    float Offset = 0;    //M_PI/4;

    /** Light rotation offset. */
    float LightRotation = 0;

    /** Whether to update the rotation offset on each frame. */
#ifdef BENCHMARK
    bool DoRotation = true;
#else
    bool DoRotation = false;
#endif

    /** Render textures */
    bool RenderTextures = true;

    /** Projection matrix. */
    ml::mat4x4 ProjectionMatrix;

    /*
     * Statistics.
     */
    /** Frames rendered since the last statistics output. */
    uint32_t RenderedFrames = 0;

    /** The total number of rendered frames. */
    uint32_t TotalRenderedFrames = 0;

    /**
     * Set at program start and at each call of GUpdateLogic to obtain
     * time differences.
     */
    Uint32 MSecTimerRef = 0;

    /** Time since last update. */
    float UpdateTimeDelta = 0;

    /** Seconds spent drawing the frame. */
    float RenderTime = 0;

    /** Milliseconds the render loop runs (only updated at loop entry and exit). */
    Uint32 TotalRenderTime = 0;

    float RenderingTime = 0;
    float FPS = 0;

    int ShowInfo = 1;

    bool bRenderLight = true;

#ifdef DO_SCISSOR_TEST
    /*
     * scissor test.
     */
    float scissor_timer{0};

    bool bScissor = false;

    // (x,y) is the bottom left corner, and the box extends upwards and to the right.
    int scissor_x{290}, scissor_y{0}, scissor_width{350}, scissor_height{350};
    int scissor_inc_x{1}, scissor_inc_y{1};

    void update_scissor_rect()
    {
        if(!bScissor)
        {
            return;
        }

        scissor_x += scissor_inc_x;
        scissor_y += scissor_inc_y;

        if(scissor_x + scissor_width >= Win_Width)
        {
            scissor_x = Win_Width - scissor_width;
            scissor_inc_x = -1;
        }
        else if(scissor_x + scissor_inc_x < 0)
        {
            scissor_x = 0;
            scissor_inc_x = 1;
        }

        if(scissor_y + scissor_height >= Win_Height)
        {
            scissor_y = Win_Height - scissor_height;
            scissor_inc_y = -1;
        }
        else if(scissor_y - scissor_inc_y < 0)
        {
            scissor_y = 0;
            scissor_inc_y = 1;
        }

        swr::SetScissorBox(scissor_x, scissor_y, scissor_width, scissor_height);
    }
#endif

    /*
     * Software rasterizer variables.
     */

    /** The software rendering context we are going to use. */
    swr::context_handle RasterizerContext = nullptr;

    stats::performance_data GlobalPerformanceData;

  public:
    virtual void Draw() = 0;
};

class SDLDemoEngine : public DemoEngine
{
    /** The main SDL window. */
    SDL_Window* MainWindow = nullptr;

    /** Main SDL renderer. */
    SDL_Renderer* Renderer = nullptr;

    /** A global indicator showing whether we want to run or exit the program. */
    bool GQuitProgram = false;

    stats::overlay Stats;

  public:
    virtual ~SDLDemoEngine()
    {
        Shutdown();
    }

    /** Module interface. */
    void Initialize()
    {
        platform::logf("[SDLDemoEngine.Initialize]");

        /* Create window and renderer for given surface */
        platform::logf(" * SDL Window...");
        MainWindow = SDL_CreateWindow("Software Rasterizer Demo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, Win_Width, Win_Height, SDL_WINDOW_SHOWN);
        if(!MainWindow)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Window creation failed: %s\n", SDL_GetError());
            //!!fixme: call platform::global_shutdown() or register exit handler?
            std::exit(EXIT_FAILURE);
        }
        auto surface = SDL_GetWindowSurface(MainWindow);
        platform::logf(" * SDL Renderer...");
        Renderer = SDL_CreateSoftwareRenderer(surface);
        if(!Renderer)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Render creation for surface failed: %s\n", SDL_GetError());
            //!!fixme: call platform::global_shutdown() or register exit handler?
            std::exit(EXIT_FAILURE);
        }

        /* Clear the rendering surface with the specified color */
        SDL_SetRenderDrawColor(Renderer, 0xff, 0xff, 0xff, 0xff);
        SDL_RenderClear(Renderer);

        // Set up reference timers
        MSecTimerRef = SDL_GetTicks();

        /*
         * Rasterizer setup.
         */
        platform::logf(" * Rasterizer...");

        // Initialize the rasterizer context.
        // This is independent of the pipeline initialization and can be done later.
        RasterizerContext = swr::CreateSDLContext(MainWindow, Renderer);
        if(!swr::MakeContextCurrent(RasterizerContext))
        {
            platform::logf("[!!] swr::MakeContextCurrent failed.");
            //!!fixme: call platform::global_shutdown() or register exit handler?
            std::exit(EXIT_FAILURE);
        }
        swr::SetClearColor(0, 0, 0, 0);
        swr::SetClearDepth(1.0f);

        swr::SetViewport(0, 0, Win_Width, Win_Height);
        swr::DepthRange(0, 1);

        // set up pipeline.
        swr::SetState(swr::state::depth_test, true);
        swr::SetDepthTest(swr::comparison_func::less);

        swr::SetState(swr::state::blend, false);

        swr::SetState(swr::state::cull_face, true);
        swr::SetCullMode(swr::cull_face_direction::back);
        swr::SetFrontFace(swr::front_face_orientation::ccw);

        swr::SetState(swr::state::texture, true);

        ProjectionMatrix = ml::matrices::perspective_projection(static_cast<float>(Win_Width) / static_cast<float>(Win_Height), static_cast<float>(M_PI / 2.f), 1.f, 10.f);

        platform::logf(" * Textures...");
        LoadTextures();

        platform::logf(" * Shaders...");

        Shaders[SI_ColorOnly] = new shader::color();
        Shaders[SI_TextureOnly] = new shader::texture();
        Shaders[SI_Phong] = new shader::phong();
#ifdef TEXCOORDS_TO_COLOR
        Shaders[SI_BlinnPhong] = new shader::display_tex_coords();
#else
        Shaders[SI_BlinnPhong] = new shader::blinn_phong();
#endif
        Shaders[SI_FrontFaceTest] = new shader::front_face_test();

        for(int i = 0; i < DEMO_SHADER_COUNT; ++i)
        {
            ShaderIds[i] = swr::RegisterShader(Shaders[i]);
            if(ShaderIds[i] == 0)
            {
                platform::logf("!! Shader registration failed for shader {}", i);
                //!!fixme: call platform::global_shutdown() or register exit handler?
                std::exit(EXIT_FAILURE);
            }
        }

        platform::logf("   Color shader id:         {}", ShaderIds[SI_ColorOnly]);
        platform::logf("   Texture shader id:       {}", ShaderIds[SI_TextureOnly]);
        platform::logf("   Phong shader id:         {}", ShaderIds[SI_Phong]);
        platform::logf("   Blinn-Phong shader id:   {}", ShaderIds[SI_BlinnPhong]);

        platform::logf(" * Statistics Overlay...");
        if(!Stats.initialize(ShaderIds[SI_ColorOnly], ShaderIds[SI_TextureOnly], FontTexId, next_power_of_two(font_tex_width), next_power_of_two(font_tex_height), font_tex_width, font_tex_height))
        {
            platform::logf("[!!] Statistics Overlay failed to initialize");
            std::exit(EXIT_FAILURE);
        }

        platform::logf(" * Geometry...");
        LoadGeometry();

        TotalRenderTime = -SDL_GetTicks();

        platform::logf("[------ initialized -----]");
    }

    void Shutdown()
    {
        bool bFirstCall = false;

        if(RasterizerContext)
        {
            bFirstCall = true;
            platform::logf("[SDLDemoEngine.Shutdown]");

            platform::logf(" * Releasing geometry...");
            swr::DeleteIndexBuffer(RectIBufferId);
            RectIBufferId = 0;

            swr::DeleteVertexBuffer(RectVBufferId);
            RectVBufferId = 0;

            swr::DeleteAttributeBuffer(UVBufferId);
            UVBufferId = 0;

            swr::DeleteAttributeBuffer(ColorBufferId);
            ColorBufferId = 0;

            swr::DeleteAttributeBuffer(NormalBufferId);
            NormalBufferId = 0;

            swr::DeleteAttributeBuffer(TangentBufferId);
            TangentBufferId = 0;

            swr::DeleteAttributeBuffer(BitangentBufferId);
            BitangentBufferId = 0;

            swr::DeleteIndexBuffer(IBufferId);
            IBufferId = 0;

            swr::DeleteVertexBuffer(VBufferId);
            VBufferId = 0;

            platform::logf(" * Releasing shaders...");

            for(int i = 0; i < DEMO_SHADER_COUNT; ++i)
            {
                swr::UnregisterShader(ShaderIds[i]);
                ShaderIds[i] = 0;

                delete Shaders[i];
                Shaders[i] = nullptr;
            }

            platform::logf(" * Releasing textures...");
            swr::ReleaseTexture(CrateTexId);
            swr::ReleaseTexture(NormalTexId);
            swr::ReleaseTexture(FontTexId);

            platform::logf(" * Destroying rasterizer context...");
            swr::DestroyContext(RasterizerContext);
            RasterizerContext = nullptr;
        }

        if(SDL_WasInit(0) != 0)
        {
            TotalRenderTime += SDL_GetTicks();

#ifdef BENCHMARK
            platform::set_log(&global_log);
#endif

            /* Print average frame statistics. */
            platform::log_n();
            platform::logf(" Total rendered frames:  {}", TotalRenderedFrames);
            platform::logf(" Total render time:      {} ms", TotalRenderTime);
            platform::logf(" Average ms per frame:   {:.4f}", static_cast<float>(TotalRenderTime) / static_cast<float>(TotalRenderedFrames));
            platform::logf(" Average frames per sec: {:.4f}", static_cast<float>(TotalRenderedFrames) / static_cast<float>(TotalRenderTime) * 1000.0f);
            platform::log_n();
#ifdef BENCHMARK
            platform::set_log(nullptr);
#endif
        }

        if(bFirstCall)
        {
            platform::logf("[----- shut down ------]");
        }
    }

    void LoadTextures()
    {
        // load test texture from file.
        std::vector<uint8_t> ImageData;
        std::vector<uint8_t> FontImageData;
        uint32_t Width = 0, Height = 0;

        //       uint32_t Err = lodepng::decode(ImageData, Width, Height, "../textures/crate1/crate1_diffuse.png" );
        uint32_t Err = lodepng::decode(ImageData, Width, Height, "../textures/crate1_lowres.png");
        if(Err != 0)
        {
            platform::logf("[!!] lodepng error: {}", lodepng_error_text(Err));
            std::exit(EXIT_FAILURE);    // !!fixme: (see above)
        }
        CrateTexId = swr::CreateTexture(Width, Height, swr::pixel_format::rgba8888, swr::wrap_mode::clamp_to_edge, swr::wrap_mode::mirrored_repeat, ImageData);
        platform::logf("   diffuse tex id: ", CrateTexId);

        Err = lodepng::decode(ImageData, Width, Height, "../textures/crate1/crate1_normal.png");
        if(Err != 0)
        {
            platform::logf("[!!] lodepng error: {}", lodepng_error_text(Err));
            std::exit(EXIT_FAILURE);    // !!fixme: (see above)
        }
        NormalTexId = swr::CreateTexture(Width, Height, swr::pixel_format::rgba8888, swr::wrap_mode::clamp_to_edge, swr::wrap_mode::mirrored_repeat, ImageData);
        platform::logf("   normal map id: {}", NormalTexId);

        Err = lodepng::decode(FontImageData, font_tex_width, font_tex_height, "../textures/rexpaint_cp437_10x10_alpha.png");
        if(Err != 0)
        {
            platform::logf("[!!] lodepng error: {}", lodepng_error_text(Err));
            std::exit(EXIT_FAILURE);
        }
        FontTexId = load_texture(font_tex_width, font_tex_height, FontImageData);

        // set texture parameters.
        swr::BindTexture(CrateTexId);
        swr::SetTextureMagnificationFilter(swr::texture_filter::dithered);
        swr::SetTextureMinificationFilter(swr::texture_filter::nearest);

        swr::BindTexture(NormalTexId);
        swr::SetTextureMagnificationFilter(swr::texture_filter::dithered);
        swr::SetTextureMinificationFilter(swr::texture_filter::nearest);

        swr::BindTexture(FontTexId);
        swr::SetTextureMagnificationFilter(swr::texture_filter::nearest);
        swr::SetTextureMinificationFilter(swr::texture_filter::nearest);
    }

    void LoadGeometry()
    {
        // cube vertices
        std::vector<ml::vec4> VBuffer = {
#define VERTEX_LIST(...) __VA_ARGS__
#include "../common/cube.geom"
#undef VERTEX_LIST
        };
        VBufferId = swr::CreateAttributeBuffer(VBuffer);

        // color buffer.
        std::vector<ml::vec4> Colors = {
#define COLOR_LIST(...) __VA_ARGS__
#include "../common/cube.geom"
#undef COLOR_LIST
        };

        // face normals.
        std::vector<ml::vec4> Normals = {
#define NORMAL_LIST(...) __VA_ARGS__
#include "../common/cube.geom"
#undef NORMAL_LIST
        };

        // cube tangents and bitangents
        std::vector<ml::vec4> Tangents = {
#define TANGENT_LIST(...) __VA_ARGS__
#include "../common/cube.geom"
#undef TANGENT_LIST
        };

        std::vector<ml::vec4> Bitangents = {
#define BITANGENT_LIST(...) __VA_ARGS__
#include "../common/cube.geom"
#undef BITANGENT_LIST
        };

        // u/v texture coords.
        std::vector<ml::vec4> UVs = {
#define UV_LIST(...) __VA_ARGS__
#include "../common/cube.geom"
#undef UV_LIST
        };

        ColorBufferId = swr::CreateAttributeBuffer(Colors);
        NormalBufferId = swr::CreateAttributeBuffer(Normals);
        TangentBufferId = swr::CreateAttributeBuffer(Tangents);
        BitangentBufferId = swr::CreateAttributeBuffer(Bitangents);
        UVBufferId = swr::CreateAttributeBuffer(UVs);

        // cube face indices.
        std::vector<uint32_t> IBuffer = {
#define FACE_LIST(...) __VA_ARGS__
#include "common/cube.geom"
#undef FACE_LIST
        };
        IBufferId = swr::CreateIndexBuffer(IBuffer);

        // 2d screen rectangle.
        std::vector<ml::vec4> RectVBuffer = {
          {0, 0, 0}, {Win_Width / 4, 0, 0}, {Win_Width / 4, Win_Height / 4, 0}, {0, Win_Height / 4, 0}};

        std::vector<ml::vec4> rect_colors = {
          {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};

        std::vector<ml::vec4> rect_tex_coords = {
          {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};

        RectVBufferId = swr::CreateAttributeBuffer(RectVBuffer);
        RectCBufferId = swr::CreateAttributeBuffer(rect_colors);
        RectTBufferId = swr::CreateAttributeBuffer(rect_tex_coords);

        std::vector<uint32_t> RectIBuffer = {0, 1, 2, 3};
        RectIBufferId = swr::CreateIndexBuffer(RectIBuffer);
    }

    void PrintResourceInfo()
    {
        // print some info on the created objects.
        platform::logf("Resource Information");
        platform::logf("---------------------------------");
        platform::logf("   Cube vertex buffer:      {}", VBufferId);
        platform::logf("   Cube index buffer:       {}", IBufferId);
        platform::logf("   Cube colors:             {}", ColorBufferId);
        platform::logf("   Cube texture:            {}", CrateTexId);
        platform::logf("   Rectangle vertex buffer: {}", RectVBufferId);
        platform::logf("   Rectangle index buffer:  {}", RectIBufferId);
        platform::logf("   Font texture:            {}", FontTexId);
        platform::logf("   Normal map:              {}", NormalTexId);
        platform::logf("   Color shader id:         {}", ShaderIds[SI_ColorOnly]);
        platform::logf("   Texture shader id:       {}", ShaderIds[SI_TextureOnly]);
        platform::logf("   Phong shader id:         {}", ShaderIds[SI_Phong]);
        platform::logf("   Blinn-Phong shader id:   {}", ShaderIds[SI_BlinnPhong]);
        platform::logf("   front face shader id:    {}", ShaderIds[SI_FrontFaceTest]);
        platform::log_n();
    }

    /**
     * Update all logic-related variables, e.g. animation-related variables or
     * time intervals.
     */
    void UpdateLogic(float DeltaTime)
    {
        UpdateTimeDelta += DeltaTime;
        scissor_timer += DeltaTime;

        // Update animation logic and force Offset to be in the interval [0, 2*M_PI)
        // This assumes that Offset is not 'far off', i.e. is inside [-2*M_PI,4*M_PI).
        if(DoRotation)
        {
            Offset += 0.2f * DeltaTime;
            Offset = Offset < 0 ? (Offset + static_cast<float>(2 * M_PI)) : (Offset >= static_cast<float>(2 * M_PI) ? Offset - static_cast<float>(2 * M_PI) : Offset);
        }

        // Update light position.
        LightRotation += DeltaTime * 0.2;
        LightPos = ml::vec4(std::cos(LightRotation), std::sin(LightRotation), 0, 0) * 4;
        LightPos.z = -4;
        LightPos.w = 1;

#ifdef DO_SCISSOR_TEST
        // update scissor rect.
        if(scissor_timer > 0.02)
        {
            update_scissor_rect();
            scissor_timer = 0;
        }
#endif
        // update statistics.
        if(UpdateTimeDelta >= 0.5)
        {
            RenderingTime = static_cast<float>(RenderTime) / static_cast<float>(RenderedFrames);

            FPS = 1.0f / RenderingTime * 1000.0f;

            GlobalPerformanceData.add(FPS, RenderingTime);
            GlobalPerformanceData.update();

            RenderedFrames = 0;
            UpdateTimeDelta = 0;
            RenderTime = 0;
        }
    }

    void draw_cube(ml::vec3 pos, float angle)
    {
        ml::mat4x4 view = ml::mat4x4::identity();
        view *= ml::matrices::rotation_z(angle);

        view *= ml::matrices::translation(pos.x, pos.y, pos.z);
        view *= ml::matrices::scaling(2.0f);
        view *= ml::matrices::rotation_y(Offset);
        view *= ml::matrices::rotation_z(2 * Offset);
        view *= ml::matrices::rotation_x(3 * Offset);

        swr::BindShader(ShaderIds[CurrentLightingShader]);

        swr::EnableAttributeBuffer(VBufferId, 0);
        swr::EnableAttributeBuffer(NormalBufferId, 1);
        swr::EnableAttributeBuffer(UVBufferId, 2);

        swr::BindUniform(0, ProjectionMatrix);
        swr::BindUniform(1, view);
        swr::BindUniform(2, LightPos);
        swr::BindUniform(3, static_cast<int>(CrateTexId));

        // draw the buffer.
        swr::DrawIndexedElements(IBufferId, swr::vertex_buffer_mode::triangles);

        swr::DisableAttributeBuffer(UVBufferId);
        swr::DisableAttributeBuffer(NormalBufferId);
        swr::DisableAttributeBuffer(VBufferId);

        swr::BindShader(0);
    }

    /**
     * Render a frame and update the program logic, e.g. animation-related variables.
     */
    virtual void Draw()
    {
        if(RasterizerContext == nullptr)
        {
            return;
        }

        // update rendering time
        RenderTime -= SDL_GetTicks();

#ifdef DO_SCISSOR_TEST
        swr::SetState(swr::state::scissor_test, false);
#endif

        // clear screen.
        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();

#ifdef DO_SCISSOR_TEST
        swr::SetState(swr::state::scissor_test, bScissor);
        if(bScissor)
        {
            swr::SetClearColor(0.2, 0.2, 0.2, 0);
            swr::ClearColorBuffer();
            swr::SetClearColor(0, 0, 0, 0);
        }
#endif

        ml::mat4x4 view;

#ifdef DRAW_CUBE
        if(cube_count <= 1)
        {
            draw_cube({-Position.x, -Position.y, Position.z}, 0);
        }
        else if(cube_count == 2)
        {
            draw_cube({-Position.x, -Position.y, Position.z}, -Offset);
            draw_cube({Position.x, -Position.y, Position.z}, -Offset);
        }
        else if(cube_count == 3)
        {
            draw_cube({-Position.x, -Position.y, Position.z}, -Offset);
            draw_cube({Position.x, -Position.y, Position.z}, -Offset);
            draw_cube({-Position.x, Position.y, Position.z}, -Offset);
        }
        else if(cube_count >= 4)
        {
            draw_cube({-Position.x, -Position.y, Position.z}, -Offset);
            draw_cube({Position.x, -Position.y, Position.z}, -Offset);
            draw_cube({-Position.x, Position.y, Position.z}, -Offset);
            draw_cube({Position.x, Position.y, Position.z}, -Offset);
        }
#endif /* DRAW_CUBE */

#ifdef DRAW_LIGHT
        /*
         * Render light.
         */

        if(bRenderLight)
        {
            swr::BindShader(ShaderIds[SI_ColorOnly]);
            swr::EnableAttributeBuffer(VBufferId, 0);
            swr::EnableAttributeBuffer(ColorBufferId, 1);

            view = ml::matrices::translation(LightPos.x, LightPos.y, LightPos.z);
            view *= ml::matrices::diagonal(0.2f, 0.2f, 0.2f, 1.0f);

            swr::BindUniform(1, view);

            // draw the buffer.
            swr::SetState(swr::state::texture, false);
            swr::DrawIndexedElements(IBufferId, swr::vertex_buffer_mode::triangles);
            swr::SetState(swr::state::texture, RenderTextures);

            swr::DisableAttributeBuffer(ColorBufferId);
            swr::DisableAttributeBuffer(VBufferId);
            swr::BindShader(0);
        }
#endif /* DRAW_LIGHT */

#ifdef DRAW_TRIANGLE
        swr::BindShader(ShaderIds[SI_ColorOnly]);

        swr::SetState(swr::state::texture, false);

        view = ml::matrices::translation(-Position.x * 0.1, -Position.y * 0.1, -Position.z * 0.1);
        view *= ml::matrices::rotation_z(Offset);
        swr::BindUniform(0, ml::mat4x4::identity());
        swr::BindUniform(1, view);

        swr::BeginPrimitives(swr::vertex_buffer_mode::triangles);
        swr::SetColor(1, 0, 0, 1);
        swr::InsertVertex(-0.5, -0.5);
        swr::SetColor(0, 1, 0, 1);
        swr::InsertVertex(0.5, -0.5);
        swr::SetColor(0, 0, 1, 1);
        swr::InsertVertex(0, 0.5);
        swr::EndPrimitives();

        swr::SetState(swr::state::texture, RenderTextures);
        swr::BindShader(0);
#endif /* DRAW_TRIANGLE */

#ifdef DRAW_ALPHA_TEXTURES
        swr::BindShader(ShaderIds[SI_ColorOnly]);

        // Test immediate mode and alpha blending by drawing a textured quad.
        swr::SetState(swr::state::texture, false);
        swr::SetState(swr::state::depth_test, false);

        swr::SetState(swr::state::blend, true);
        swr::SetBlendFunc(swr::blend_func::src_alpha, swr::blend_func::one_minus_src_alpha);

        view = ml::matrices::rotation_z(-Offset);
        view *= ml::matrices::translation(-1, -1, -5);

        swr::BindUniform(0, ProjectionMatrix);
        swr::BindUniform(1, view);

        const float AlphaComponent = 0.5 + 0.5 * std::sin(2 * Offset);

        swr::BeginPrimitives(swr::vertex_buffer_mode::quads);
        swr::BindTexture(FontTexId);

        swr::SetColor(1, 0, 0, AlphaComponent);
        swr::SetTexCoord(0, 1);
        swr::InsertVertex(-1, -1, 0, 1);

        swr::SetColor(0, 1, 0, AlphaComponent);
        swr::SetTexCoord(1, 1);
        swr::InsertVertex(1, -1, 0, 1);

        swr::SetColor(0, 0, 1, AlphaComponent);
        swr::SetTexCoord(1, 0);
        swr::InsertVertex(1, 1, 0, 1);

        swr::SetColor(1, 1, 1, AlphaComponent);
        swr::SetTexCoord(0, 0);
        swr::InsertVertex(-1, 1, 0, 1);

        swr::SetColor(1, 0, 0, 1 - AlphaComponent);
        swr::SetTexCoord(0, 1);
        swr::InsertVertex(1, 1, 0, 1);

        swr::SetColor(0, 1, 0, 1 - AlphaComponent);
        swr::SetTexCoord(1, 1);
        swr::InsertVertex(3, 1, 0, 1);

        swr::SetColor(0, 0, 1, 1 - AlphaComponent);
        swr::SetTexCoord(1, 0);
        swr::InsertVertex(3, 3, 0, 1);

        swr::SetColor(1, 1, 1, 1 - AlphaComponent);
        swr::SetTexCoord(0, 0);
        swr::InsertVertex(1, 3, 0, 1);
        swr::EndPrimitives();

        swr::SetState(swr::state::blend, false);
        swr::SetState(swr::state::depth_test, true);

        swr::BindShader(0);
#endif /* DRAW_ALPHA_TEXTURES */

#ifdef DRAW_OVERLAY
        //!!fixme
        auto& fr = Stats.get_font_renderer();

        // set orthonormal projection.
        swr::BindUniform(0, ml::matrices::orthographic_projection(0, Win_Width, Win_Height, 0, -1000, 1000));
        swr::BindUniform(1, ml::mat4x4::identity());

        // font test: Output current FPS and
        int text_y = 0;
        fr.draw_string(fmt::format("msec per frame:          {:.2f}", RenderingTime), 0, text_y);
        text_y += 16;

        fr.draw_string(fmt::format("frames per sec:          {:.2f}", FPS), 0, text_y);
        text_y += 16;

#    ifdef DO_SCISSOR_TEST
        // scissor test info
        if(bScissor)
        {
            swr::SetState(swr::state::scissor_test, false);
            fr.draw_string("[scissor test]", Win_Width - 14 * 10, 0);
            swr::SetState(swr::state::scissor_test, true);
        }
#    endif

        if(ShowInfo >= 3)
        {
            // benchmark data.
            swr::stats::fragment_data benchmark_data;
            swr::stats::get_fragment_data(benchmark_data);
            fr.draw_string(fmt::format("fragments processed:    {}", benchmark_data.count), 0, text_y);
            text_y += 16;
            fr.draw_string(fmt::format("fragments discarded:    {}", benchmark_data.discard_alpha + benchmark_data.discard_depth + benchmark_data.discard_scissor + benchmark_data.discard_shader), 0, text_y);
            text_y += 16;
            fr.draw_string(fmt::format("fragments blended:      {}", benchmark_data.blending), 0, text_y);
            text_y += 16;
#    ifdef DO_BENCHMARKING
            fr.draw_string(fmt::format("fragment shader cycles: {}", benchmark_data.cycles), 0, text_y);
            text_y += 16;
#    endif
        }

        // show further info, if desired
        if(ShowInfo >= 4)
        {
            fr.draw_string("[esc] quit", 0, text_y);
            text_y += 16;

            fr.draw_string("  [h] show info", 0, text_y);
            text_y += 16;

            fr.draw_string("[up/down/left/right/page-up/page-down]", 0, text_y);
            text_y += 16;

            fr.draw_string(fmt::format("      cube position:           ({:.2f},{:.2f},{:.2f})", Position.x, Position.y, Position.z), 0, text_y);
            text_y += 16;

            fr.draw_string("[r/0/+/-]", 0, text_y);
            text_y += 16;

            if(!DoRotation)
            {
                fr.draw_string("      cube rotation:           disabled", 0, text_y);
            }
            else
            {
                fr.draw_string(fmt::format("      cube rotation:           {:.2f}", Offset), 0, text_y);
            }
            text_y += 16;

            fr.draw_string(fmt::format("      light position:          ({:.2f},{:.2f},{:.2f})", LightPos.x, LightPos.y, LightPos.z), 0, text_y);
            text_y += 16;

            // cycle through the polygon modes.
            if(swr::GetPolygonMode() == swr::polygon_mode::point)
            {
                fr.draw_string("  [p] polygon mode:            point", 0, text_y);
            }
            else if(swr::GetPolygonMode() == swr::polygon_mode::line)
            {
                fr.draw_string("  [p] polygon mode:            line", 0, text_y);
            }
            else if(swr::GetPolygonMode() == swr::polygon_mode::fill)
            {
                if(RenderTextures)
                {
                    fr.draw_string("  [p] polygon mode:            fill (texturing enabled)", 0, text_y);
                }
                else
                {
                    fr.draw_string("  [p] polygon mode:            fill (texturing disabled)", 0, text_y);
                }
            }
            text_y += 16;

            // cycle through the cull modes.
            if(!swr::GetState(swr::state::cull_face))
            {
                fr.draw_string("  [c] face culling:            disabled", 0, text_y);
            }
            else if(swr::GetFrontFace() == swr::front_face_orientation::ccw)
            {
                fr.draw_string("  [c] face culling:            ccw", 0, text_y);
            }
            else if(swr::GetFrontFace() == swr::front_face_orientation::cw)
            {
                fr.draw_string("  [c] face culling:            cw", 0, text_y);
            }
            text_y += 16;

            if(swr::GetDebugState(swr::debug_state::show_depth_buffer))
            {
                fr.draw_string("  [d] displaying depth buffer: yes", 0, text_y);
            }
            else
            {
                fr.draw_string("  [d] displaying depth buffer: no", 0, text_y);
            }
            text_y += 16;

            if(bRenderLight)
            {
                fr.draw_string("  [l] showing light source:    yes", 0, text_y);
            }
            else
            {
                fr.draw_string("  [l] showing light source:    no", 0, text_y);
            }
            text_y += 16;

            if(CurrentLightingShader == SI_Phong)
            {
                fr.draw_string("  [k] shading model:           phong", 0, text_y);
            }
            else if(CurrentLightingShader == SI_BlinnPhong)
            {
                fr.draw_string("  [k] shading model:           blinn-phong", 0, text_y);
            }
            else if(CurrentLightingShader == SI_FrontFaceTest)
            {
                fr.draw_string("  [k] front face test", 0, text_y);
            }
            else
            {
                fr.draw_string("  [k] shading model:           <unknown>", 0, text_y);
            }
            text_y += 16;

            if(cube_count <= 1)
            {
                fr.draw_string("  [g] cubes: 1", 0, text_y);
            }
            else if(cube_count == 2)
            {
                fr.draw_string("  [g] cubes: 2", 0, text_y);
            }
            else if(cube_count == 3)
            {
                fr.draw_string("  [g] cubes: 3", 0, text_y);
            }
            else
            {
                fr.draw_string("  [g] cubes: 4", 0, text_y);
            }
            text_y += 16;

#    ifdef DO_SCISSOR_TEST
            fr.draw_string("  [s] scissor test", 0, text_y);
            text_y += 16;
#    endif
        }

        swr::BindShader(0);

        if(ShowInfo == 2)
        {
            // draw performance graph.
            Stats.set_data(&GlobalPerformanceData);

            swr::BindShader(ShaderIds[SI_ColorOnly]);
            Stats.draw_graph(Win_Width - 4 * stats::performance_data::size, Win_Height);

            // draw_caption calls draw_string, which sets its own shader.
            Stats.draw_caption(Win_Width - 4 * stats::performance_data::size, Win_Height);

            swr::BindShader(0);
        }
#endif /* DRAW_OVERLAY */
        /*
         * End rendering setup.
         */

        // Display the data given to the renderer. The actual work
        // is done inside swr::Present.
        swr::Present();

        // Display the color buffer. context.
        swr::CopyDefaultColorBuffer(RasterizerContext);

        // update statistics.
        ++RenderedFrames;
        ++TotalRenderedFrames;

        RenderTime += SDL_GetTicks();

#ifdef BENCHMARK
        if(TotalRenderedFrames >= BENCHMARK_FRAMECOUNT)
        {
            GQuitProgram = true;
        }
#endif
    }

    /**
     * Handle (keyboard-)inputs.
     */
    void UpdateInput()
    {
        SDL_Event e;
        if(SDL_PollEvent(&e))
        {
            if(e.type == SDL_QUIT)
            {
                GQuitProgram = true;
                return;
            }

            // check for key-down events
            if(e.type == SDL_KEYDOWN)
            {
                if(e.key.keysym.sym == SDLK_ESCAPE)
                {
                    GQuitProgram = true;
                    return;
                }

                if(e.key.keysym.sym == SDLK_UP)
                {
                    Position.z += 0.1f;
                }
                if(e.key.keysym.sym == SDLK_DOWN)
                {
                    Position.z -= 0.1f;
                }

                if(e.key.keysym.sym == SDLK_LEFT)
                {
                    Position.x += 0.1f;
                }
                if(e.key.keysym.sym == SDLK_RIGHT)
                {
                    Position.x -= 0.1f;
                }

                if(e.key.keysym.sym == SDLK_PAGEUP)
                {
                    Position.y += 0.1f;
                }
                if(e.key.keysym.sym == SDLK_PAGEDOWN)
                {
                    Position.y -= 0.1f;
                }
                if(e.key.keysym.sym == SDLK_p)
                {
                    // cycle through the polygon modes.
                    if(swr::GetPolygonMode() == swr::polygon_mode::point)
                    {
                        swr::SetPolygonMode(swr::polygon_mode::line);
                    }
                    else if(swr::GetPolygonMode() == swr::polygon_mode::line)
                    {
                        swr::SetPolygonMode(swr::polygon_mode::fill);
                    }
                    else if(swr::GetPolygonMode() == swr::polygon_mode::fill)
                    {
                        swr::SetPolygonMode(swr::polygon_mode::point);
                    }
                }
                if(e.key.keysym.sym == SDLK_c)
                {
                    // cycle through the cull modes.
                    if(!swr::GetState(swr::state::cull_face))
                    {
                        swr::SetState(swr::state::cull_face, true);
                    }
                    else if(swr::GetFrontFace() == swr::front_face_orientation::ccw)
                    {
                        swr::SetFrontFace(swr::front_face_orientation::cw);
                    }
                    else if(swr::GetFrontFace() == swr::front_face_orientation::cw)
                    {
                        swr::SetFrontFace(swr::front_face_orientation::ccw);
                        swr::SetState(swr::state::cull_face, false);
                    }
                }
                if(e.key.keysym.sym == SDLK_PLUS)
                {
                    Offset += 0.01f;
                }
                if(e.key.keysym.sym == SDLK_MINUS)
                {
                    Offset -= 0.01f;
                }
                if(e.key.keysym.sym == SDLK_r)
                {
                    DoRotation = !DoRotation;
                }
                if(e.key.keysym.sym == SDLK_0)
                {
                    Offset = 0;
                }
                if(e.key.keysym.sym == SDLK_d)
                {
                    bool DisplayDepth = swr::GetDebugState(swr::debug_state::show_depth_buffer);
                    swr::SetDebugState(swr::debug_state::show_depth_buffer, !DisplayDepth);
                }
                else if(e.key.keysym.sym == SDLK_h)
                {
                    ++ShowInfo;
                    if(ShowInfo > 4)
                    {
                        ShowInfo = 1;
                    }
                }
                else if(e.key.keysym.sym == SDLK_l)
                {
                    bRenderLight = !bRenderLight;
                }
                else if(e.key.keysym.sym == SDLK_k)
                {
                    if(CurrentLightingShader == SI_Phong)
                    {
                        CurrentLightingShader = SI_BlinnPhong;
                    }
                    else if(CurrentLightingShader == SI_BlinnPhong)
                    {
                        CurrentLightingShader = SI_FrontFaceTest;
                    }
                    else
                    {
                        CurrentLightingShader = SI_Phong;
                    }
                }
                else if(e.key.keysym.sym == SDLK_g)
                {
                    ++cube_count;
                    if(cube_count > 4)
                    {
                        cube_count = 1;
                    }
                    Position = default_cube_pos[cube_count - 1];
                }
#ifdef DO_SCISSOR_TEST
                else if(e.key.keysym.sym == SDLK_s)
                {
                    bScissor = !bScissor;
                }
#endif
            }
        }
    }

    void MainLoop()
    {
        uint32_t RefTime = -SDL_GetTicks();

        while(!GQuitProgram)
        {
            UpdateInput();

            uint32_t Ticks = SDL_GetTicks();
            uint32_t DeltaTime = RefTime + Ticks;
            RefTime = -Ticks;

            UpdateLogic(DeltaTime / 1000.f);
            Draw();
        }
    }
};

/*-----------------------------------------------------------------------------
	Implementation.
-----------------------------------------------------------------------------*/

/** Print copyright and version info to platform log. */
void GPrintCopyright()
{
    int major{0}, minor{0}, patch{0};
    swr::GetVersion(major, minor, patch);

    platform::logf("SWRaster Demo, Library Version {0}.{1}.{2}", major, minor, patch);
    platform::logf("Copyright 2015-2021 Felix Lubbe");
    platform::log_n();

    platform::logf("For 3rd-party libraries, assets and further resources, see Readme.txt.");
    platform::log_n();
}

/**
 * Print 3rd party library licenses.
 *
 * 3rd party libraries:
 *   boost, fmt, lodepng, SDL
 */
void GPrintLibraryLicenses()
{
    platform::log_n();
    platform::logf("Library versions and licenses:");
    platform::log_n();

    platform::logf("boost:            version: {}.{}.{}", static_cast<int>(BOOST_VERSION / 100000), static_cast<int>(BOOST_VERSION / 100) % 1000, BOOST_VERSION % 100);
    platform::logf("                  license: BSL-1.0");
    platform::log_n();

    platform::logf("cnl:              version: commit 5037d10");
    platform::logf("                  license: BSL-1.0");
    platform::log_n();

    platform::logf("cpu_features:     version: commit b9593c8");
    platform::logf("                  license: Apache-2.0");
    platform::log_n();

    platform::logf("fmt:              version: {}.{}.{}", static_cast<int>(FMT_VERSION / 10000), static_cast<int>(FMT_VERSION / 100) % 100, FMT_VERSION % 100);
    platform::logf("                  license: BSD-2-Clause");
    platform::log_n();

    platform::logf("lodepng:          version: {}", LODEPNG_VERSION_STRING);
    platform::logf("                  license: Zlib");
    platform::log_n();

    SDL_version sdl_compiled_version, sdl_linked_version;
    SDL_VERSION(&sdl_compiled_version);
    SDL_GetVersion(&sdl_linked_version);

    platform::logf("SDL:              version: {}.{}.{} (compiled), {}.{}.{} (loaded)", sdl_compiled_version.major, sdl_compiled_version.minor, sdl_compiled_version.patch, sdl_linked_version.major, sdl_linked_version.minor, sdl_linked_version.patch);
    platform::logf("                  license: Zlib");
    platform::log_n();
}

/** Program entry point. */
#if defined(_WIN32) || defined(_WIN64)
int _tmain(int /* argc*/, _TCHAR* /*argv*/[])
#else
int main(int /* argc*/, char* /*argv*/[])
#endif
{
#ifdef BENCHMARK
    platform::global_initialize();
#else
    platform::global_initialize(&global_log);
#endif

    /* print some copyright info. */
    GPrintCopyright();
    GPrintLibraryLicenses();

    /* Enable standard application logging */
    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    /* Initialize SDL */
    if(SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init fail : %s\n", SDL_GetError());

        platform::global_shutdown();
        return 1;
    }

    SDLDemoEngine EngineInstance;
    EngineInstance.Initialize();

    EngineInstance.MainLoop();

    EngineInstance.Shutdown();

    SDL_Quit();

    platform::global_shutdown();
    return EXIT_SUCCESS;
}
