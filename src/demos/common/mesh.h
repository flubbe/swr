/**
 * swr - a software rasterizer
 *
 * mesh generator for filling a 2d rectangle.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

namespace mesh
{

/** attribute buffer interface. */
struct swr_attribute_buffer
{
    /** vertex attributes. */
    std::vector<ml::vec4> attribs;

    /** buffer id. */
    std::int32_t id{-1};

    /** default constructor. */
    swr_attribute_buffer() = default;

    /** destructor. */
    ~swr_attribute_buffer()
    {
        unload();
    }

    /** upload buffer to graphics library. if keep is false, unloads the local copy. */
    bool upload(bool keep = false)
    {
        // don't upload twice.
        if(id != -1)
        {
            return false;
        }

        // upload.
        id = swr::CreateAttributeBuffer(attribs);

        // unload local copy
        if(!keep)
        {
            attribs.clear();
            attribs.shrink_to_fit();
        }

        return true;
    }

    /** unload buffer. */
    void unload()
    {
        if(id != -1)
        {
            swr::DeleteAttributeBuffer(id);
            id = -1;
        }
    }

    /** bind this buffer to a attribute slot. */
    void enable(std::uint32_t slot) const
    {
        if(id != -1)
        {
            swr::EnableAttributeBuffer(id, slot);
        }
    }

    /** unbind this buffer from an attribute slot. */
    void disable() const
    {
        if(id != -1)
        {
            swr::DisableAttributeBuffer(id);
        }
    }
};

inline void swr_buffer_enable_if(bool pred, swr_attribute_buffer& buf, uint32_t slot)
{
    if(pred)
    {
        buf.enable(slot);
    }
}

inline void swr_buffer_disable_if(bool pred, swr_attribute_buffer& buf)
{
    if(pred)
    {
        buf.disable();
    }
}

inline void swr_buffer_upload_if(bool pred, swr_attribute_buffer& buf, bool keep = false)
{
    if(pred)
    {
        buf.upload(keep);
    }
}

/** mesh. */
struct mesh
{
    /** mesh vertices. */
    swr_attribute_buffer vertices;

    /** normals. */
    swr_attribute_buffer normals;

    /** tangents. */
    swr_attribute_buffer tangents;

    /** bitangents. */
    swr_attribute_buffer bitangents;

    /** colors. */
    swr_attribute_buffer colors;

    /** texture coordinates. */
    swr_attribute_buffer texture_coordinates;

    /** mesh triangle face indices. */
    std::vector<std::uint32_t> indices;

    /** whether this mesh has normals. */
    bool has_normals{false};

    /** whether this mesh has tangents. */
    bool has_tangents{false};

    /** whether this mesh has bitangents. */
    bool has_bitangents{false};

    /** whether this mesh has colors. */
    bool has_colors{false};

    /** whether this mesh has texture coordinates. */
    bool has_texture_coordinates{false};

    /** default constructor. */
    mesh() = default;

    /** destructor. */
    ~mesh()
    {
        unload();
    }

    /** upload data to graphics library. */
    void upload(bool keep = false)
    {
        vertices.upload(keep);
        swr_buffer_upload_if(has_normals, normals, keep);
        swr_buffer_upload_if(has_tangents, tangents, keep);
        swr_buffer_upload_if(has_bitangents, bitangents, keep);
        swr_buffer_upload_if(has_colors, colors, keep);
        swr_buffer_upload_if(has_texture_coordinates, texture_coordinates, keep);
    }

    /** unload mesh. */
    void unload()
    {
        vertices.unload();
        normals.unload();
        tangents.unload();
        bitangents.unload();
        colors.unload();
        texture_coordinates.unload();
    }

    /** render mesh. */
    void render()
    {
        vertices.enable(0);
        swr_buffer_enable_if(has_normals, normals, 1);
        swr_buffer_enable_if(has_tangents, tangents, 2);
        swr_buffer_enable_if(has_bitangents, bitangents, 3);
        swr_buffer_enable_if(has_colors, colors, 4);
        swr_buffer_enable_if(has_texture_coordinates, texture_coordinates, 5);

        // draw the buffer.
        swr::DrawIndexedElements(swr::vertex_buffer_mode::triangles, indices.size(), indices);

        swr_buffer_disable_if(has_texture_coordinates, texture_coordinates);
        swr_buffer_disable_if(has_colors, colors);
        swr_buffer_disable_if(has_bitangents, bitangents);
        swr_buffer_disable_if(has_tangents, tangents);
        swr_buffer_disable_if(has_normals, normals);
        vertices.disable();
    }
};

/** generate colored regular tiling of a rectangle. */
mesh generate_tiling_mesh(float xmin, float xmax, float ymin, float ymax, std::size_t rows, std::size_t cols, float z);

/** generate colored tiling of a rectangle with some random offsets w.r.t. a regular tiling. */
mesh generate_random_tiling_mesh(float xmin, float xmax, float ymin, float ymax, std::size_t rows, std::size_t cols, float z, float zrange = 1, std::size_t mesh_start_x = 0, std::size_t mesh_start_y = 0, std::size_t mesh_end_x = static_cast<std::size_t>(-1), std::size_t mesh_end_y = static_cast<std::size_t>(-1));

} /* namespace mesh */
