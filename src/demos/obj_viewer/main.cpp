/**
 * swr - a software rasterizer
 *
 * This is a modified version of the simple .obj loader example from https://github.com/tinyobjloader/tinyobjloader,
 * adapted to work with the software rasterizer.
 *
 * NOTE The viewer currently only displays a non-textured version of the model.
 *
 * Copyright (c) 2012-Present, Syoyo Fujita and many contributors.
 * Copyright (c) 2022-Present, Felix Lubbe
 *
 * \author Syoyo Fujita and contributors.
 * \author Felix Lubbe
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <print>
#include <string>
#include <vector>
#include <unordered_map>

/* software rasterizer headers. */
#include "swr/swr.h"
#include "swr/shaders.h"

/* shaders for this demo. */
#include "shader.h"

/* application framework. */
#include "swr_app/framework.h"

/* logging. */
#include "../common/platform/platform.h"

#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_USE_MAPBOX_EARCUT
#include "tiny_obj_loader.h"

#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Weverything"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef __clang__
#    pragma clang diagnostic pop
#endif

/** demo title. */
const auto demo_title = ".obj viewer";

/** collect a set of geometric data into a single object. */
struct drawable_object
{
    static const std::uint32_t invalid_id = static_cast<std::uint32_t>(-1);

    /** vertex buffer id. */
    std::uint32_t vertex_buffer_id{invalid_id};

    /** normal buffer id. */
    std::uint32_t normal_buffer_id{invalid_id};

    /** color buffer id. */
    std::uint32_t color_buffer_id{invalid_id};

    /** texture buffer id. */
    std::uint32_t texture_buffer_id{invalid_id};

    /** triangle count. */
    std::size_t triangle_count{0};

    /** matrial id. */
    std::size_t material_id{0};

    /** constructors. */
    drawable_object() = default;
    drawable_object(const drawable_object&) = default;
    drawable_object(drawable_object&&) = default;

    drawable_object& operator=(const drawable_object&) = default;
    drawable_object& operator=(drawable_object&&) = default;

    /** release all data. */
    void release()
    {
        if(vertex_buffer_id != invalid_id)
        {
            swr::DeleteAttributeBuffer(vertex_buffer_id);
            vertex_buffer_id = invalid_id;
        }
        if(normal_buffer_id != invalid_id)
        {
            swr::DeleteAttributeBuffer(normal_buffer_id);
            normal_buffer_id = invalid_id;
        }
        if(color_buffer_id != invalid_id)
        {
            swr::DeleteAttributeBuffer(color_buffer_id);
            color_buffer_id = invalid_id;
        }
        if(texture_buffer_id != invalid_id)
        {
            swr::DeleteAttributeBuffer(texture_buffer_id);
            texture_buffer_id = invalid_id;
        }

        triangle_count = 0;
        material_id = 0;
    }
};

static std::string get_base_dir(const std::string& filepath)
{
    auto separator_pos = filepath.find_last_of("/\\");
    if(separator_pos != std::string::npos)
        return filepath.substr(0, separator_pos);
    return "";
}

static void check_errors(std::string desc)
{
    swr::error e = swr::GetLastError();
    if(e != swr::error::none)
    {
        std::println(stderr, "SWR error in \"{}\": {}", desc, static_cast<int>(e));
        std::exit(1);
    }
}

static ml::vec3 calc_normal(const ml::vec3& v0, const ml::vec3& v1, const ml::vec3& v2)
{
    return (v1 - v0).cross_product(v2 - v0).normalized();
}

/*
  There are 2 approaches here to automatically generating vertex normals. The
  old approach (computeSmoothingNormals) doesn't handle multiple smoothing
  groups properly, as it effectively merges all smoothing groups present in the
  OBJ file into a single group. However, it can be useful when the OBJ file
  contains vertex normals which you want to use, but is missing some, as it
  will attempt to fill in the missing normals without generating new shapes.

  The new approach (computeSmoothingShapes, computeAllSmoothingNormals) handles
  multiple smoothing groups but is a bit more complicated, as handling this
  correctly requires potentially generating new vertices (and hence shapes).
  In general, the new approach is most useful if your OBJ file is missing
  vertex normals entirely, and instead relies on smoothing groups to correctly
  generate them as a pre-process. That said, it can be used to reliably
  generate vertex normals in the general case. If you want to always generate
  normals in this way, simply force set regen_all_normals to true below. By
  default, it's only true when there are no vertex normals present. One other
  thing to keep in mind is that the statistics printed apply to the model
  *prior* to shape regeneration, so you'd need to print them again if you want
  to see the new statistics.

  TODO(syoyo): import computeSmoothingShapes and computeAllSmoothingNormals to
  tinyobjloader as utility functions.
*/

// Check if `mesh_t` contains smoothing group id.
bool hasSmoothingGroup(const tinyobj::shape_t& shape)
{
    for(auto id: shape.mesh.smoothing_group_ids)
    {
        if(id > 0)
        {
            return true;
        }
    }
    return false;
}

void computeSmoothingNormals(const tinyobj::attrib_t& attrib, const tinyobj::shape_t& shape,
                             std::map<int, ml::vec3>& smoothVertexNormals)
{
    smoothVertexNormals.clear();
    std::map<int, ml::vec3>::iterator iter;

    for(size_t f = 0; f < shape.mesh.indices.size() / 3; f++)
    {
        // Get the three indexes of the face (all faces are triangular)
        tinyobj::index_t idx0 = shape.mesh.indices[3 * f + 0];
        tinyobj::index_t idx1 = shape.mesh.indices[3 * f + 1];
        tinyobj::index_t idx2 = shape.mesh.indices[3 * f + 2];

        // Get the three vertex indexes and coordinates
        int vi[3];        // indexes
        ml::vec3 v[3];    // coordinates

        for(int k = 0; k < 3; k++)
        {
            vi[0] = idx0.vertex_index;
            vi[1] = idx1.vertex_index;
            vi[2] = idx2.vertex_index;
            assert(vi[0] >= 0);
            assert(vi[1] >= 0);
            assert(vi[2] >= 0);

            v[0][k] = attrib.vertices[3 * vi[0] + k];
            v[1][k] = attrib.vertices[3 * vi[1] + k];
            v[2][k] = attrib.vertices[3 * vi[2] + k];
        }

        // Compute the normal of the face
        ml::vec3 normal = calc_normal(v[0], v[1], v[2]);

        // Add the normal to the three vertexes
        for(size_t i = 0; i < 3; ++i)
        {
            iter = smoothVertexNormals.find(vi[i]);
            if(iter != smoothVertexNormals.end())
            {
                // add
                iter->second += normal;
            }
            else
            {
                smoothVertexNormals[vi[i]] = normal;
            }
        }

    }    // f

    // Normalize the normals, that is, make them unit vectors
    for(auto& it: smoothVertexNormals)
    {
        it.second.normalize();
    }

}    // computeSmoothingNormals

static void computeAllSmoothingNormals(tinyobj::attrib_t& attrib,
                                       std::vector<tinyobj::shape_t>& shapes)
{
    ml::vec3 p[3];
    for(const auto& shape: shapes)
    {
        size_t facecount = shape.mesh.num_face_vertices.size();
        assert(shape.mesh.smoothing_group_ids.size());

        for(size_t f = 0; f < facecount; ++f)
        {
            for(unsigned int v = 0; v < 3; ++v)
            {
                tinyobj::index_t idx = shape.mesh.indices[3 * f + v];
                assert(idx.vertex_index != -1);
                p[v] = {attrib.vertices[3 * idx.vertex_index], attrib.vertices[3 * idx.vertex_index + 1], attrib.vertices[3 * idx.vertex_index + 2]};
            }

            // cross(p[1] - p[0], p[2] - p[0])
            ml::vec3 n = (p[1] - p[0]).cross_product(p[2] - p[0]);

            // Don't normalize here.
            for(unsigned int v = 0; v < 3; ++v)
            {
                tinyobj::index_t idx = shape.mesh.indices[3 * f + v];
                attrib.normals[3 * idx.normal_index] += n.x;
                attrib.normals[3 * idx.normal_index + 1] += n.y;
                attrib.normals[3 * idx.normal_index + 2] += n.z;
            }
        }
    }

    assert(attrib.normals.size() % 3 == 0);
    for(size_t i = 0, nlen = attrib.normals.size() / 3; i < nlen; ++i)
    {
        tinyobj::real_t& nx = attrib.normals[3 * i];
        tinyobj::real_t& ny = attrib.normals[3 * i + 1];
        tinyobj::real_t& nz = attrib.normals[3 * i + 2];
#ifdef __GNUC__
        tinyobj::real_t len = sqrtf(nx * nx + ny * ny + nz * nz);
#else
        tinyobj::real_t len = std::sqrtf(nx * nx + ny * ny + nz * nz);
#endif
        tinyobj::real_t scale = len == 0 ? 0 : 1 / len;
        nx *= scale;
        ny *= scale;
        nz *= scale;
    }
}

static void computeSmoothingShape(tinyobj::attrib_t& inattrib, tinyobj::shape_t& inshape,
                                  std::vector<std::pair<unsigned int, unsigned int>>& sortedids,
                                  unsigned int idbegin, unsigned int idend,
                                  std::vector<tinyobj::shape_t>& outshapes,
                                  tinyobj::attrib_t& outattrib)
{
    unsigned int sgroupid = sortedids[idbegin].first;
    bool hasmaterials = inshape.mesh.material_ids.size();
    // Make a new shape from the set of faces in the range [idbegin, idend).
    outshapes.emplace_back();
    tinyobj::shape_t& outshape = outshapes.back();
    outshape.name = inshape.name;
    // Skip lines and points.

    std::unordered_map<unsigned int, unsigned int> remap;
    for(unsigned int id = idbegin; id < idend; ++id)
    {
        unsigned int face = sortedids[id].second;

        outshape.mesh.num_face_vertices.push_back(3);    // always triangles
        if(hasmaterials)
            outshape.mesh.material_ids.push_back(inshape.mesh.material_ids[face]);
        outshape.mesh.smoothing_group_ids.push_back(sgroupid);
        // Skip tags.

        for(unsigned int v = 0; v < 3; ++v)
        {
            tinyobj::index_t inidx = inshape.mesh.indices[3 * face + v], outidx;
            assert(inidx.vertex_index != -1);
            auto iter = remap.find(inidx.vertex_index);
            // Smooth group 0 disables smoothing so no shared vertices in that case.
            if(sgroupid && iter != remap.end())
            {
                outidx.vertex_index = (*iter).second;
                outidx.normal_index = outidx.vertex_index;
                outidx.texcoord_index = (inidx.texcoord_index == -1) ? -1 : outidx.vertex_index;
            }
            else
            {
                assert(outattrib.vertices.size() % 3 == 0);
                unsigned int offset = static_cast<unsigned int>(outattrib.vertices.size() / 3);
                outidx.vertex_index = outidx.normal_index = offset;
                outidx.texcoord_index = (inidx.texcoord_index == -1) ? -1 : offset;
                outattrib.vertices.push_back(inattrib.vertices[3 * inidx.vertex_index]);
                outattrib.vertices.push_back(inattrib.vertices[3 * inidx.vertex_index + 1]);
                outattrib.vertices.push_back(inattrib.vertices[3 * inidx.vertex_index + 2]);
                outattrib.normals.push_back(0.0f);
                outattrib.normals.push_back(0.0f);
                outattrib.normals.push_back(0.0f);
                if(inidx.texcoord_index != -1)
                {
                    outattrib.texcoords.push_back(inattrib.texcoords[2 * inidx.texcoord_index]);
                    outattrib.texcoords.push_back(inattrib.texcoords[2 * inidx.texcoord_index + 1]);
                }
                remap[inidx.vertex_index] = offset;
            }
            outshape.mesh.indices.push_back(outidx);
        }
    }
}

static void computeSmoothingShapes(tinyobj::attrib_t& inattrib,
                                   std::vector<tinyobj::shape_t>& inshapes,
                                   std::vector<tinyobj::shape_t>& outshapes,
                                   tinyobj::attrib_t& outattrib)
{
    for(auto& inshape: inshapes)
    {
        unsigned int numfaces = static_cast<unsigned int>(inshape.mesh.smoothing_group_ids.size());
        assert(numfaces);
        std::vector<std::pair<unsigned int, unsigned int>> sortedids(numfaces);
        for(unsigned int i = 0; i < numfaces; ++i)
            sortedids[i] = std::make_pair(inshape.mesh.smoothing_group_ids[i], i);
        sort(sortedids.begin(), sortedids.end());

        unsigned int activeid = sortedids[0].first;
        unsigned int id = activeid, idbegin = 0, idend = 0;
        // Faces are now bundled by smoothing group id, create shapes from these.
        while(idbegin < numfaces)
        {
            while(activeid == id && ++idend < numfaces)
                id = sortedids[idend].first;
            computeSmoothingShape(inattrib, inshape, sortedids, idbegin, idend,
                                  outshapes, outattrib);
            activeid = id;
            idbegin = idend;
        }
    }
}

static bool LoadObjAndConvert(ml::vec3& bmin, ml::vec3& bmax,
                              std::vector<drawable_object>* drawObjects,
                              std::vector<tinyobj::material_t>& materials,
                              std::map<std::string, uint32_t>& textures,
                              const char* filename)
{
    tinyobj::attrib_t inattrib;
    std::vector<tinyobj::shape_t> inshapes;

    auto timer_start = std::chrono::high_resolution_clock::now();

    std::string base_dir = get_base_dir(filename);
    if(base_dir.empty())
    {
        base_dir = ".";
    }
#ifdef _WIN32
    base_dir += "\\";
#else
    base_dir += "/";
#endif

    std::string warn;
    std::string err;
    bool ret = tinyobj::LoadObj(&inattrib, &inshapes, &materials, &warn, &err, filename,
                                base_dir.c_str());
    if(!warn.empty())
    {
        std::println("WARN: {}", warn);
    }
    if(!err.empty())
    {
        std::println(stderr, "{}", err);
    }

    auto timer_end = std::chrono::high_resolution_clock::now();

    if(!ret)
    {
        std::println(stderr, "Failed to load {}", filename);
        return false;
    }

    std::println("Parsing time: {:.2f} [ms]", std::chrono::duration<float>(timer_end - timer_start).count());

    std::println("# of vertices  = {}", (int)(inattrib.vertices.size()) / 3);
    std::println("# of normals   = {}", (int)(inattrib.normals.size()) / 3);
    std::println("# of texcoords = {}", (int)(inattrib.texcoords.size()) / 2);
    std::println("# of materials = {}", (int)materials.size());
    std::println("# of shapes    = {}", (int)inshapes.size());

    // Append `default` material
    materials.push_back(tinyobj::material_t());

    for(size_t i = 0; i < materials.size(); i++)
    {
        std::println("material[{}].diffuse_texname = {}", int(i),
                     materials[i].diffuse_texname.c_str());
    }

    // Load diffuse textures
    for(auto& material: materials)
    {
        if(material.diffuse_texname.length() > 0)
        {
            // Only load the texture if it is not already loaded
            if(textures.find(material.diffuse_texname) == textures.end())
            {
                uint32_t texture_id;
                int w, h;
                int comp;

                std::string texture_filename = material.diffuse_texname;
                if(!std::filesystem::exists(texture_filename))
                {
                    // Append base dir.
                    texture_filename = base_dir + material.diffuse_texname;
                    if(!std::filesystem::exists(texture_filename))
                    {
                        std::println(stderr, "Unable to find file: {}", material.diffuse_texname);
                        std::exit(1);
                    }
                }

                unsigned char* image =
                  stbi_load(texture_filename.c_str(), &w, &h, &comp, STBI_default);
                if(!image)
                {
                    std::println(stderr, "Unable to load texture: {}", texture_filename);
                    std::exit(1);
                }
                std::println("Loaded texture: {}, w = {}, h = {}, comp = {}", texture_filename, w, h, comp);

                texture_id = swr::CreateTexture();
                swr::BindTexture(swr::texture_target::texture_2d, texture_id);
                swr::SetTextureMagnificationFilter(swr::texture_filter::linear);
                swr::SetTextureMinificationFilter(swr::texture_filter::linear);

                std::vector<uint8_t> image_rgba(4 * w * h);
                if(comp == 3)
                {
                    // convert to rgba
                    for(int j = 0; j < h; ++j)
                    {
                        for(int i = 0; i < w; ++i)
                        {
                            image_rgba.push_back(image[4 * (j * w + i) + 0]);    // r
                            image_rgba.push_back(image[4 * (j * w + i) + 1]);    // g
                            image_rgba.push_back(image[4 * (j * w + i) + 2]);    // b
                            image_rgba.push_back(255);                           // a
                        }
                    }
                }
                else if(comp == 4)
                {
                    image_rgba = {image, image + 4 * w * h};
                }
                else
                {
                    assert(0);    // TODO
                }
                swr::SetImage(texture_id, 0, w, h, swr::pixel_format::srgb8_alpha8, image_rgba);
                swr::BindTexture(swr::texture_target::texture_2d, 0);

                stbi_image_free(image);
                textures.insert(std::make_pair(material.diffuse_texname, texture_id));
            }
        }
    }

    bmin[0] = bmin[1] = bmin[2] = std::numeric_limits<float>::max();
    bmax[0] = bmax[1] = bmax[2] = -std::numeric_limits<float>::max();

    bool regen_all_normals = inattrib.normals.size() == 0;
    tinyobj::attrib_t outattrib;
    std::vector<tinyobj::shape_t> outshapes;
    if(regen_all_normals)
    {
        computeSmoothingShapes(inattrib, inshapes, outshapes, outattrib);
        computeAllSmoothingNormals(outattrib, outshapes);
    }

    std::vector<tinyobj::shape_t>& shapes = regen_all_normals ? outshapes : inshapes;
    tinyobj::attrib_t& attrib = regen_all_normals ? outattrib : inattrib;

    std::size_t s = 0;
    for(auto& shape: shapes)
    {
        ++s;    // only for logging

        drawable_object o;
        std::vector<ml::vec4> pos_buffer;
        std::vector<ml::vec4> normal_buffer;
        std::vector<ml::vec4> color_buffer;
        std::vector<ml::vec4> tex_buffer;

        // Check for smoothing group and compute smoothing normals
        std::map<int, ml::vec3> smoothVertexNormals;
        if(!regen_all_normals && (hasSmoothingGroup(shape) > 0))
        {
            std::println("Compute smoothingNormal for shape [{}]", s);
            computeSmoothingNormals(attrib, shape, smoothVertexNormals);
        }

        for(size_t f = 0; f < shape.mesh.indices.size() / 3; f++)
        {
            tinyobj::index_t idx0 = shape.mesh.indices[3 * f + 0];
            tinyobj::index_t idx1 = shape.mesh.indices[3 * f + 1];
            tinyobj::index_t idx2 = shape.mesh.indices[3 * f + 2];

            int current_material_id = shape.mesh.material_ids[f];

            if((current_material_id < 0) || (current_material_id >= static_cast<int>(materials.size())))
            {
                // Invaid material ID. Use default material.
                current_material_id =
                  materials.size() - 1;    // Default material is added to the last item in `materials`.

                static bool warned = false;
                if(!warned)
                {
                    std::println("WARN Invalid material ID for shape [{}], tri [{} {} {}]", s, 3 * f, 3 * f + 1, 3 * f + 2);
                    std::println("     Using default material.");

                    std::println("INFO Further invalid material ID warnings are suppressed.");
                    warned = true;
                }
            }
            ml::vec3 diffuse;
            for(size_t i = 0; i < 3; i++)
            {
                diffuse[i] = materials[current_material_id].diffuse[i];
            }
            ml::vec2 tc[3];
            if(attrib.texcoords.size() > 0)
            {
                if((idx0.texcoord_index < 0) || (idx1.texcoord_index < 0) || (idx2.texcoord_index < 0))
                {
                    // face does not contain valid uv index.
                }
                else
                {
                    assert(attrib.texcoords.size() > size_t(2 * idx0.texcoord_index + 1));
                    assert(attrib.texcoords.size() > size_t(2 * idx1.texcoord_index + 1));
                    assert(attrib.texcoords.size() > size_t(2 * idx2.texcoord_index + 1));

                    // Flip Y coord.
                    tc[0] = ml::vec2{
                      attrib.texcoords[2 * idx0.texcoord_index],
                      1.0f - attrib.texcoords[2 * idx0.texcoord_index + 1]};
                    tc[1] = ml::vec2{
                      attrib.texcoords[2 * idx1.texcoord_index],
                      1.0f - attrib.texcoords[2 * idx1.texcoord_index + 1]};
                    tc[2] = ml::vec2{
                      attrib.texcoords[2 * idx2.texcoord_index],
                      1.0f - attrib.texcoords[2 * idx2.texcoord_index + 1]};
                }
            }

            int f0 = idx0.vertex_index;
            int f1 = idx1.vertex_index;
            int f2 = idx2.vertex_index;
            assert(f0 >= 0);
            assert(f1 >= 0);
            assert(f2 >= 0);

            ml::vec3 v[3];
            for(int k = 0; k < 3; k++)
            {
                v[0][k] = attrib.vertices[3 * f0 + k];
                v[1][k] = attrib.vertices[3 * f1 + k];
                v[2][k] = attrib.vertices[3 * f2 + k];
                bmin[k] = std::min(v[0][k], bmin[k]);
                bmin[k] = std::min(v[1][k], bmin[k]);
                bmin[k] = std::min(v[2][k], bmin[k]);
                bmax[k] = std::max(v[0][k], bmax[k]);
                bmax[k] = std::max(v[1][k], bmax[k]);
                bmax[k] = std::max(v[2][k], bmax[k]);
            }

            ml::vec3 n[3];
            bool invalid_normal_index = false;
            if(attrib.normals.size() > 0)
            {
                int nf0 = idx0.normal_index;
                int nf1 = idx1.normal_index;
                int nf2 = idx2.normal_index;

                if((nf0 < 0) || (nf1 < 0) || (nf2 < 0))
                {
                    // normal index is missing from this face.
                    invalid_normal_index = true;
                }
                else
                {
                    for(int k = 0; k < 3; k++)
                    {
                        assert(size_t(3 * nf0 + k) < attrib.normals.size());
                        assert(size_t(3 * nf1 + k) < attrib.normals.size());
                        assert(size_t(3 * nf2 + k) < attrib.normals.size());
                        n[0][k] = attrib.normals[3 * nf0 + k];
                        n[1][k] = attrib.normals[3 * nf1 + k];
                        n[2][k] = attrib.normals[3 * nf2 + k];
                    }
                }
            }
            else
            {
                invalid_normal_index = true;
            }

            if(invalid_normal_index && !smoothVertexNormals.empty())
            {
                // Use smoothing normals
                int f0 = idx0.vertex_index;
                int f1 = idx1.vertex_index;
                int f2 = idx2.vertex_index;

                if(f0 >= 0 && f1 >= 0 && f2 >= 0)
                {
                    n[0] = smoothVertexNormals[f0];
                    n[1] = smoothVertexNormals[f1];
                    n[2] = smoothVertexNormals[f2];

                    invalid_normal_index = false;
                }
            }

            if(invalid_normal_index)
            {
                // compute geometric normal
                n[0] = calc_normal(v[0], v[1], v[2]);
                n[1] = n[0];
                n[2] = n[0];
            }

            for(int k = 0; k < 3; k++)
            {
                pos_buffer.push_back(v[k]);
                normal_buffer.push_back(n[k]);

                // Combine normal and diffuse to get color.
                float normal_factor = 0.2;
                float diffuse_factor = 1 - normal_factor;
                ml::vec3 c = n[k] * normal_factor + diffuse * diffuse_factor;
                c.normalize();
                color_buffer.push_back(ml::vec4(c, 0.f) * 0.5 + 0.5);
                tex_buffer.push_back(ml::vec4{tc[k][0], tc[k][1], 0, 0});
            }
        }

        // OpenGL viewer does not support texturing with per-face material.
        if(shape.mesh.material_ids.size() > 0 && shape.mesh.material_ids.size() > s)
        {
            o.material_id = shape.mesh.material_ids[0];    // use the material ID
                                                           // of the first face.
        }
        else
        {
            o.material_id = materials.size() - 1;    // = ID for default material.
        }
        std::println("shape[{}] name: {}", int(s), shape.name);
        std::println("shape[{}] material_id {}", int(s), int(o.material_id));

        std::println("shape[{}] vertices {}", int(s), pos_buffer.size());
        std::println("shape[{}] normals {}", int(s), normal_buffer.size());
        std::println("shape[{}] colors {}", int(s), color_buffer.size());
        std::println("shape[{}] tex coords {}", int(s), tex_buffer.size());

        if(pos_buffer.size() > 0 && normal_buffer.size() > 0 && color_buffer.size() > 0 && tex_buffer.size() > 0)
        {
            o.vertex_buffer_id = swr::CreateAttributeBuffer(pos_buffer);
            o.normal_buffer_id = swr::CreateAttributeBuffer(normal_buffer);
            o.color_buffer_id = swr::CreateAttributeBuffer(color_buffer);
            o.texture_buffer_id = swr::CreateAttributeBuffer(tex_buffer);

            o.triangle_count = pos_buffer.size() / 3;

            std::println("shape[{}] # of triangles = {}", static_cast<int>(s), o.triangle_count);
        }

        drawObjects->emplace_back(o);
    }

    std::println("bmin = {}, {}, {}", bmin[0], bmin[1], bmin[2]);
    std::println("bmax = {}, {}, {}", bmax[0], bmax[1], bmax[2]);

    return true;
}

/** demo window. */
class demo_viewer : public swr_app::renderwindow
{
    shader::color_flat flat_shader;
    shader::wireframe wireframe_shader;

    /** flat shader id. */
    uint32_t flat_shader_id{0};

    /** wireframe shader id. */
    uint32_t wireframe_shader_id{0};

    /** projection matrix. */
    ml::mat4x4 proj;

    /** view matrix. */
    ml::mat4x4 view;

    /** frame counter. */
    uint32_t frame_count{0};

    /** whether to show an overlayed wireframe (currently non-interactive). */
    bool show_wireframe{true};

    /** a list of all loaded objects. */
    std::vector<drawable_object> objects;

    /** material list. */
    std::vector<tinyobj::material_t> materials;

    /** textures. */
    std::map<std::string, uint32_t> textures;

    /** scale factor of the model. */
    float scale_factor{1.0f};

    /** center of the model. */
    ml::vec3 center;

    /** the current rotation angle. */
    float angle{0};

    /** viewport width. */
    static const int width = 400;

    /** viewport height. */
    static const int height = 400;

public:
    /** constructor. */
    demo_viewer()
    : swr_app::renderwindow(demo_title, width, height)
    {
    }

    bool create() override
    {
        if(!renderwindow::create())
        {
            return false;
        }

        if(context)
        {
            // something went wrong here. the context should not exist.
            return false;
        }

        int thread_hint = swr_app::application::get_instance().get_argument("--threads", 0);
        if(thread_hint > 0)
        {
            platform::logf("suggesting rasterizer to use {} thread{}", thread_hint, ((thread_hint > 1) ? "s" : ""));
        }

        context = swr::CreateSDLContext(sdl_window, sdl_renderer, thread_hint);
        if(!swr::MakeContextCurrent(context))
        {
            throw std::runtime_error("MakeContextCurrent failed");
        }

        swr::SetClearColor(0, 0, 0, 1);
        swr::SetClearDepth(1.0f);
        swr::SetViewport(0, 0, width, height);

        int cmd_cull_face = swr_app::application::get_instance().get_argument("--cull_face", 1);
        swr::SetState(swr::state::cull_face, cmd_cull_face == 1);

        int cmd_depth_test = swr_app::application::get_instance().get_argument("--depth_test", 1);
        swr::SetState(swr::state::depth_test, cmd_depth_test == 1);

        int cmd_show_wireframe = swr_app::application::get_instance().get_argument("--wireframe", 1);
        show_wireframe = cmd_show_wireframe == 1;

        flat_shader_id = swr::RegisterShader(&flat_shader);
        wireframe_shader_id = swr::RegisterShader(&wireframe_shader);

        // set projection matrix.
        proj = ml::matrices::perspective_projection(static_cast<float>(width) / static_cast<float>(height), static_cast<float>(M_PI) / 4, 0.01f, 100.f);

        // get file to open
        std::string filename = swr_app::application::get_instance().get_argument("--file", std::string());
        if(filename.length() == 0)
        {
            std::println("No file specified. Use --file=filename to load a file.");
            throw std::runtime_error("No file specified.");
        }

        ml::vec3 bmin, bmax;
        std::vector<tinyobj::material_t> materials;
        std::map<std::string, uint32_t> textures;
        if(false == LoadObjAndConvert(bmin, bmax, &objects, materials, textures, filename.c_str()))
        {
            throw std::runtime_error("LoadObjAndConvert failed.");
        }

        // set view matrix
        float max_extent = 0.5f * (bmax[0] - bmin[0]);
        if(max_extent < 0.5f * (bmax[1] - bmin[1]))
        {
            max_extent = 0.5f * (bmax[1] - bmin[1]);
        }
        if(max_extent < 0.5f * (bmax[2] - bmin[2]))
        {
            max_extent = 0.5f * (bmax[2] - bmin[2]);
        }
        if(max_extent > 0)
        {
            scale_factor = 1.0f / max_extent;
        }
        center = (bmin + bmax) * 0.5f;

        return true;
    }

    void destroy() override
    {
        if(context)
        {
            for(auto& it: objects)
            {
                it.release();
            }

            swr::UnregisterShader(wireframe_shader_id);
            wireframe_shader_id = 0;

            swr::UnregisterShader(flat_shader_id);
            flat_shader_id = 0;

            swr::DestroyContext(context);
            context = nullptr;
        }

        renderwindow::destroy();
    }

    void update(float delta_time) override
    {
        // gracefully exit when asked.
        SDL_Event e;
        if(SDL_PollEvent(&e))
        {
            if(e.type == SDL_EVENT_QUIT)
            {
                swr_app::application::quit();
                return;
            }
        }

        angle += delta_time * 0.2f;

        view = ml::matrices::look_at(
          {3 * std::sin(angle), 1, 3 * std::cos(angle)},
          {0, 0, 0},
          {0, 1, 0});
        view *= ml::matrices::scaling(scale_factor);
        view *= ml::matrices::translation(-center[0], -center[1], -center[2]);

        begin_render();
        draw_objects(objects, materials, textures);
        end_render();

        ++frame_count;
    }

    void begin_render()
    {
        swr::ClearColorBuffer();
        swr::ClearDepthBuffer();
    }

    void end_render()
    {
        swr::Present();
        swr::CopyDefaultColorBuffer(context);
    }

    void draw_objects(const std::vector<drawable_object>& drawObjects,
                      std::vector<tinyobj::material_t>& materials,
                      std::map<std::string, uint32_t>& textures)
    {
        swr::SetPolygonMode(swr::polygon_mode::fill);

        swr::SetState(swr::state::polygon_offset_fill, true);
        swr::PolygonOffset(1.0, 1.0);

        swr::BindShader(flat_shader_id);
        swr::BindUniform(0, proj);
        swr::BindUniform(1, view);

        for(const auto& o: drawObjects)
        {
            swr::EnableAttributeBuffer(o.vertex_buffer_id, 0);
            swr::EnableAttributeBuffer(o.normal_buffer_id, 1);
            swr::EnableAttributeBuffer(o.color_buffer_id, 2);
            swr::EnableAttributeBuffer(o.texture_buffer_id, 3);

            swr::BindTexture(swr::texture_target::texture_2d, 0);
            if((o.material_id < materials.size()))
            {
                std::string diffuse_texname = materials[o.material_id].diffuse_texname;
                if(textures.find(diffuse_texname) != textures.end())
                {
                    swr::BindTexture(swr::texture_target::texture_2d, textures[diffuse_texname]);
                }
            }

            swr::DrawElements(3 * o.triangle_count, swr::vertex_buffer_mode::triangles);

            check_errors("DrawElements");

            swr::BindTexture(swr::texture_target::texture_2d, 0);

            swr::DisableAttributeBuffer(o.texture_buffer_id);
            swr::DisableAttributeBuffer(o.color_buffer_id);
            swr::DisableAttributeBuffer(o.normal_buffer_id);
            swr::DisableAttributeBuffer(o.vertex_buffer_id);
        }

        // draw wireframe
        if(show_wireframe)
        {
            swr::SetPolygonMode(swr::polygon_mode::line);
            swr::SetState(swr::state::polygon_offset_fill, false);

            swr::BindShader(wireframe_shader_id);

            for(const auto& o: drawObjects)
            {
                swr::EnableAttributeBuffer(o.vertex_buffer_id, 0);

                swr::BindTexture(swr::texture_target::texture_2d, 0);
                if((o.material_id < materials.size()))
                {
                    std::string diffuse_texname = materials[o.material_id].diffuse_texname;
                    if(textures.find(diffuse_texname) != textures.end())
                    {
                        swr::BindTexture(swr::texture_target::texture_2d, textures[diffuse_texname]);
                    }
                }

                swr::DrawElements(3 * o.triangle_count, swr::vertex_buffer_mode::triangles);

                check_errors("DrawElements");

                swr::BindTexture(swr::texture_target::texture_2d, 0);

                swr::DisableAttributeBuffer(o.vertex_buffer_id);
            }
        }
    }

    int get_frame_count() const
    {
        return frame_count;
    }
};

/** Logging to stdout using std::print. */
class log_std : public platform::log_device
{
protected:
    void log_n(const std::string& message)
    {
        std::println("{}", message);
    }
};

/** demo application class. */
class demo_app : public swr_app::application
{
    log_std log;

public:
    /** create a window. */
    void initialize()
    {
        application::initialize();
        platform::set_log(&log);

        window = std::make_unique<demo_viewer>();
        window->create();
    }

    /** destroy the window. */
    void shutdown()
    {
        if(window)
        {
            auto* w = static_cast<demo_viewer*>(window.get());
            float fps = static_cast<float>(w->get_frame_count()) / get_run_time();
            platform::logf("frames: {}     runtime: {:.2f}s     fps: {:.2f}     msec: {:.2f}", w->get_frame_count(), get_run_time(), fps, 1000.f / fps);

            window->destroy();
            window.reset();
        }
    }
};

/** application instance. */
demo_app the_app;
