#include "ChunkMesh.h"
#include "Chunk.h"

// oneTBB
// @credit: https://github.com/uxlfoundation/oneTBB
#include <tbb/parallel_for.h>
#include <tbb/blocked_range3d.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/partitioner.h>

/*
	-- The Chunk Meshing Process --

    -- Normal blocks --
    This algorithm iterates throught every block in a chunk, 
    if the left block is an air block, it adds that faces to the mesh, same for the other surrounding blocks;

    -- Transparent blocks --
    This mesher adds all transparent blocks of the same type together. eg : a group of water faces or leaf faces. 
    What we are left with is like a shell of that transparent block which is quite efficient.

    -- Meshes --
    There are 3 meshes per chunk that are all using std::vector as it is fast and efficient. The buffer is cleared after it is uploaded to the gpu.
    Meshes : 
    - Normal block mesh
    - Transparent block mesh
    - Model mesh 

    -- Shadows -- 
    A basic algorithm is used to calculate shadows. It checks the highest block in a chunk and sets the shadow level if it is a particular range

    -- Lighting -- 
    I retrieve the light value from the 3d light value array in a chunk and store it in each vertex

    -- Info --
    When ever a chunk is updated. The entire mesh is regenerated instead of modifying the existing vertices..
    Index buffers are used to maximize performance

    The _GetchunkDataForMeshing() functions are forward declarations and are implemented in main.cpp 
*/

namespace Minecraft
{
    bool HasShadow(Chunk* chunk, int x, int y, int z)
    {
        constexpr int max_shadow = 24;
        for (int i = y + 1; i < y + max_shadow; i++)
        {
            if (i < CHUNK_SIZE_Y)
            {
                if (chunk->p_ChunkContents.at(x).at(i).at(z).CastsShadow())
                {
                    return true;
                }
            }
        }

        return false;
    }

	ChunkMesh::ChunkMesh() : m_VBO(GL_ARRAY_BUFFER), m_TransparentVBO(GL_ARRAY_BUFFER), m_ModelVBO(GL_ARRAY_BUFFER)
	{
		static bool IndexBufferInitialized = false;

		// Static index buffer
		static GLClasses::IndexBuffer StaticIBO;

		if (IndexBufferInitialized == false)
		{
			IndexBufferInitialized = true;

			GLuint* IndexBuffer = nullptr;

			int index_size = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z * 6;
			int index_offset = 0;

			IndexBuffer = new GLuint[index_size * 6];

			for (size_t i = 0; i < index_size; i += 6)
			{
				IndexBuffer[i] = 0 + index_offset;
				IndexBuffer[i + 1] = 1 + index_offset;
				IndexBuffer[i + 2] = 2 + index_offset;
				IndexBuffer[i + 3] = 2 + index_offset;
				IndexBuffer[i + 4] = 3 + index_offset;
				IndexBuffer[i + 5] = 0 + index_offset;

				index_offset = index_offset + 4;
			}

			StaticIBO.BufferData(index_size * 6 * sizeof(GLuint), IndexBuffer, GL_STATIC_DRAW);

			delete[] IndexBuffer;
		}

		p_VAO.Bind();
		m_VBO.Bind();
		StaticIBO.Bind();
		m_VBO.VertexAttribIPointer(0, 3, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, position));
		m_VBO.VertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, texture_coords));
		m_VBO.VertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, lighting_level));
		m_VBO.VertexAttribIPointer(3, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, block_face_lighting));
		p_VAO.Unbind();

		p_TransparentVAO.Bind();
		m_TransparentVBO.Bind();
		StaticIBO.Bind();
		m_TransparentVBO.VertexAttribIPointer(0, 3, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, position));
		m_TransparentVBO.VertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, texture_coords));
		m_TransparentVBO.VertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, lighting_level));
		m_TransparentVBO.VertexAttribIPointer(3, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, block_face_lighting));
		p_TransparentVAO.Unbind();

		p_ModelVAO.Bind();
		m_ModelVBO.Bind();
		StaticIBO.Bind();
		m_ModelVBO.VertexAttribIPointer(0, 3, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, position));
		m_ModelVBO.VertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, texture_coords));
		m_ModelVBO.VertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, lighting_level));
		m_ModelVBO.VertexAttribIPointer(3, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, block_face_lighting));
		p_ModelVAO.Unbind();

		// Set the values of the 2D planes

		m_ForwardFace[0] = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
		m_ForwardFace[1] = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
		m_ForwardFace[2] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		m_ForwardFace[3] = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);

		m_BackFace[0] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		m_BackFace[1] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		m_BackFace[2] = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
		m_BackFace[3] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

		m_TopFace[0] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
		m_TopFace[1] = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
		m_TopFace[2] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		m_TopFace[3] = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);

		m_BottomFace[0] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		m_BottomFace[1] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		m_BottomFace[2] = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
		m_BottomFace[3] = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

		m_LeftFace[0] = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
		m_LeftFace[1] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
		m_LeftFace[2] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		m_LeftFace[3] = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

		m_RightFace[0] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		m_RightFace[1] = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
		m_RightFace[2] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		m_RightFace[3] = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
	}

	ChunkMesh::~ChunkMesh()
	{
		m_Vertices.clear();
	}

	// Construct mesh using greedy meshing for maximum performance
    bool ChunkMesh::ConstructMesh(Chunk* chunk, const glm::vec3& chunk_pos)
    {
        // Local aliases (read-only during meshing)
        ChunkDataTypePtr ChunkData = &chunk->p_ChunkContents;
        ChunkLightDataTypePtr ChunkLData = &chunk->p_ChunkLightInformation;

        m_Vertices.clear();
        m_TransparentVertices.clear();
        m_ModelVertices.clear();

        // Neighbor chunks (read-only)
        ChunkDataTypePtr ForwardChunkData = _GetChunkDataForMeshing(static_cast<int>(chunk_pos.x), static_cast<int>(chunk_pos.z + 1));
        ChunkDataTypePtr BackwardChunkData = _GetChunkDataForMeshing(static_cast<int>(chunk_pos.x), static_cast<int>(chunk_pos.z - 1));
        ChunkDataTypePtr RightChunkData = _GetChunkDataForMeshing(static_cast<int>(chunk_pos.x + 1), static_cast<int>(chunk_pos.z));
        ChunkDataTypePtr LeftChunkData = _GetChunkDataForMeshing(static_cast<int>(chunk_pos.x - 1), static_cast<int>(chunk_pos.z));

        ChunkLightDataTypePtr ForwardChunkLData = _GetChunkLightDataForMeshing(static_cast<int>(chunk_pos.x), static_cast<int>(chunk_pos.z + 1));
        ChunkLightDataTypePtr BackwardChunkLData = _GetChunkLightDataForMeshing(static_cast<int>(chunk_pos.x), static_cast<int>(chunk_pos.z - 1));
        ChunkLightDataTypePtr RightChunkLData = _GetChunkLightDataForMeshing(static_cast<int>(chunk_pos.x + 1), static_cast<int>(chunk_pos.z));
        ChunkLightDataTypePtr LeftChunkLData = _GetChunkLightDataForMeshing(static_cast<int>(chunk_pos.x - 1), static_cast<int>(chunk_pos.z));

        if (!(ForwardChunkData && BackwardChunkData && RightChunkData && LeftChunkData))
            return false;

        // Thread-local vertex buckets to avoid locks/contention.
        tbb::enumerable_thread_specific<std::vector<Vertex>> tlsOpaque;
        tbb::enumerable_thread_specific<std::vector<Vertex>> tlsTransparent;
        tbb::enumerable_thread_specific<std::vector<Vertex>> tlsModel;

        // Local emitter mirrors AddFace() exactly, but writes to thread-local vectors instead of class members.
        auto emit_face_local = [this](Chunk* chunk, BlockFaceType face_type, const glm::vec3& position,
            BlockType type, uint8_t light_level, bool to_opaque,
            std::vector<Vertex>& opaque_out, std::vector<Vertex>& transp_out)
            {
                glm::vec4 translation = glm::vec4(position, 0.0f);

                Vertex v1, v2, v3, v4;
                bool reverse_texture_coordinates = false;

                // Order: Top, bottom, front, back, left, right
                constexpr uint8_t lighting_levels[6] = { 10, 3, 6, 7, 6, 7 };

                switch (face_type)
                {
                case BlockFaceType::top:
                {
                    uint8_t face_light_level = 10;
                    if (HasShadow(chunk, (int)position.x, (int)position.y, (int)position.z)) {
                        face_light_level = static_cast<uint8_t>(face_light_level - 2);
                    }

                    v1.position = translation + m_TopFace[0];
                    v2.position = translation + m_TopFace[1];
                    v3.position = translation + m_TopFace[2];
                    v4.position = translation + m_TopFace[3];

                    v1.lighting_level = light_level; v2.lighting_level = light_level;
                    v3.lighting_level = light_level; v4.lighting_level = light_level;

                    v1.block_face_lighting = face_light_level; v2.block_face_lighting = face_light_level;
                    v3.block_face_lighting = face_light_level; v4.block_face_lighting = face_light_level;
                    break;
                }
                case BlockFaceType::bottom:
                {
                    v1.position = translation + m_BottomFace[3];
                    v2.position = translation + m_BottomFace[2];
                    v3.position = translation + m_BottomFace[1];
                    v4.position = translation + m_BottomFace[0];

                    v1.lighting_level = light_level; v2.lighting_level = light_level;
                    v3.lighting_level = light_level; v4.lighting_level = light_level;

                    v1.block_face_lighting = lighting_levels[1];
                    v2.block_face_lighting = lighting_levels[1];
                    v3.block_face_lighting = lighting_levels[1];
                    v4.block_face_lighting = lighting_levels[1];

                    reverse_texture_coordinates = true;
                    break;
                }
                case BlockFaceType::front:
                {
                    v1.position = translation + m_ForwardFace[3];
                    v2.position = translation + m_ForwardFace[2];
                    v3.position = translation + m_ForwardFace[1];
                    v4.position = translation + m_ForwardFace[0];

                    v1.lighting_level = light_level; v2.lighting_level = light_level;
                    v3.lighting_level = light_level; v4.lighting_level = light_level;

                    v1.block_face_lighting = lighting_levels[2];
                    v2.block_face_lighting = lighting_levels[2];
                    v3.block_face_lighting = lighting_levels[2];
                    v4.block_face_lighting = lighting_levels[2];

                    reverse_texture_coordinates = true;
                    break;
                }
                case BlockFaceType::backward:
                {
                    v1.position = translation + m_BackFace[0];
                    v2.position = translation + m_BackFace[1];
                    v3.position = translation + m_BackFace[2];
                    v4.position = translation + m_BackFace[3];

                    v1.lighting_level = light_level; v2.lighting_level = light_level;
                    v3.lighting_level = light_level; v4.lighting_level = light_level;

                    v1.block_face_lighting = lighting_levels[3];
                    v2.block_face_lighting = lighting_levels[3];
                    v3.block_face_lighting = lighting_levels[3];
                    v4.block_face_lighting = lighting_levels[3];
                    break;
                }
                case BlockFaceType::left:
                {
                    v1.position = translation + m_LeftFace[3];
                    v2.position = translation + m_LeftFace[2];
                    v3.position = translation + m_LeftFace[1];
                    v4.position = translation + m_LeftFace[0];

                    v1.lighting_level = light_level; v2.lighting_level = light_level;
                    v3.lighting_level = light_level; v4.lighting_level = light_level;

                    v1.block_face_lighting = lighting_levels[4];
                    v2.block_face_lighting = lighting_levels[4];
                    v3.block_face_lighting = lighting_levels[4];
                    v4.block_face_lighting = lighting_levels[4];

                    reverse_texture_coordinates = true;
                    break;
                }
                case BlockFaceType::right:
                {
                    v1.position = translation + m_RightFace[0];
                    v2.position = translation + m_RightFace[1];
                    v3.position = translation + m_RightFace[2];
                    v4.position = translation + m_RightFace[3];

                    v1.lighting_level = light_level; v2.lighting_level = light_level;
                    v3.lighting_level = light_level; v4.lighting_level = light_level;

                    v1.block_face_lighting = lighting_levels[5];
                    v2.block_face_lighting = lighting_levels[5];
                    v3.block_face_lighting = lighting_levels[5];
                    v4.block_face_lighting = lighting_levels[5];
                    break;
                }
                default:
                    break;
                }

                if (type == BlockType::Water) {
                    v1.block_face_lighting = 85;
                    v2.block_face_lighting = 85;
                    v3.block_face_lighting = 85;
                    v4.block_face_lighting = 85;
                }

                const std::array<uint16_t, 8>& tex = BlockDatabase::GetBlockTexture(type, face_type);
                if (reverse_texture_coordinates) {
                    v1.texture_coords = { tex[6], tex[7] };
                    v2.texture_coords = { tex[4], tex[5] };
                    v3.texture_coords = { tex[2], tex[3] };
                    v4.texture_coords = { tex[0], tex[1] };
                }
                else {
                    v1.texture_coords = { tex[0], tex[1] };
                    v2.texture_coords = { tex[2], tex[3] };
                    v3.texture_coords = { tex[4], tex[5] };
                    v4.texture_coords = { tex[6], tex[7] };
                }

                auto& dst = to_opaque ? opaque_out : transp_out;
                dst.push_back(v1);
                dst.push_back(v2);
                dst.push_back(v3);
                dst.push_back(v4);
            };

        // Local model emitter (mirrors AddModel, but writes to TLS)
        auto add_model_local = [](Chunk* chunk, const glm::vec3& local_pos, BlockType type, float light_level,
            std::vector<Vertex>& model_out)
            {
                glm::mat4 translation = glm::translate(glm::mat4(1.0f), local_pos);
                Model model(type);

                uint8_t face_light = 10;
                if (HasShadow(chunk, (int)local_pos.x, (int)local_pos.y, (int)local_pos.z)) {
                    face_light = static_cast<uint8_t>(face_light - 2);
                }

                model_out.reserve(model_out.size() + model.p_ModelVertices.size()); // mild hint
                for (size_t i = 0; i < model.p_ModelVertices.size(); i++)
                {
                    Vertex vertex;
                    glm::vec4 pos = glm::vec4(model.p_ModelVertices[i].position, 1.0f);
                    pos = translation * pos;

                    vertex.position = glm::vec3(pos.x, pos.y, pos.z);
                    vertex.texture_coords = model.p_ModelVertices[i].tex_coords;
                    vertex.lighting_level = static_cast<uint8_t>(light_level);
                    vertex.block_face_lighting = face_light;

                    model_out.push_back(vertex);
                }
            };

        // Parallel meshing:
        // We use a coarse grainsize to reduce scheduler overhead on uniform work.
        tbb::static_partitioner partitioner;
        tbb::parallel_for(
            tbb::blocked_range3d<int>(
                0, CHUNK_SIZE_X, 4,
                0, CHUNK_SIZE_Y, 8,
                0, CHUNK_SIZE_Z, 4),
            [&](const tbb::blocked_range3d<int>& r)
            {
                auto& opaque_out = tlsOpaque.local();
                auto& transp_out = tlsTransparent.local();
                auto& model_out = tlsModel.local();

                // Optional small reserves to reduce reallocation churn.
                if (opaque_out.capacity() == 0)   opaque_out.reserve(4096);
                if (transp_out.capacity() == 0)   transp_out.reserve(2048);
                if (model_out.capacity() == 0)    model_out.reserve(512);

                for (int x = r.pages().begin(); x != r.pages().end(); ++x)
                    for (int y = r.rows().begin(); y != r.rows().end(); ++y)
                        for (int z = r.cols().begin(); z != r.cols().end(); ++z)
                        {
                            const Block& blk = ChunkData->at(x).at(y).at(z);
                            if (blk.p_BlockType == BlockType::Air) continue;

                            uint8_t light_level = ChunkLData->at(x).at(y).at(z);
                            if (y >= 0 && y < CHUNK_SIZE_Y - 1) {
                                light_level = ChunkLData->at(x).at(y + 1).at(z);
                            }

                            glm::vec3 local_position(x, y, z);

                            if (blk.IsModel()) {
                                add_model_local(chunk, local_position, blk.p_BlockType, light_level, model_out);
                                continue;
                            }

                            // Z faces (front/back)
                            if (z <= 0)
                            {
                                if (blk.IsTransparent())
                                {
                                    if (BackwardChunkData->at(x).at(y).at(CHUNK_SIZE_Z - 1).IsTransparent() &&
                                        BackwardChunkData->at(x).at(y).at(CHUNK_SIZE_Z - 1).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = BackwardChunkLData->at(x).at(y).at(CHUNK_SIZE_Z - 1);
                                        emit_face_local(chunk, BlockFaceType::front, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::backward, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                    else if (ChunkData->at(x).at(y).at(1).IsTransparent() &&
                                        ChunkData->at(x).at(y).at(1).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y).at(1);
                                        emit_face_local(chunk, BlockFaceType::front, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::backward, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                }
                                else
                                {
                                    if (BackwardChunkData->at(x).at(y).at(CHUNK_SIZE_Z - 1).IsOpaque() == false)
                                    {
                                        uint8_t ll = BackwardChunkLData->at(x).at(y).at(CHUNK_SIZE_Z - 1);
                                        emit_face_local(chunk, BlockFaceType::front, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::backward, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                    else if (ChunkData->at(x).at(y).at(1).IsOpaque() == false)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y).at(1);
                                        emit_face_local(chunk, BlockFaceType::front, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::backward, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                }
                            }
                            else if (z >= CHUNK_SIZE_Z - 1)
                            {
                                if (blk.IsTransparent())
                                {
                                    if (ForwardChunkData->at(x).at(y).at(0).IsTransparent() &&
                                        ForwardChunkData->at(x).at(y).at(0).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ForwardChunkLData->at(x).at(y).at(0);
                                        emit_face_local(chunk, BlockFaceType::front, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::backward, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                    else if (ChunkData->at(x).at(y).at(CHUNK_SIZE_Z - 2).IsTransparent() &&
                                        ChunkData->at(x).at(y).at(CHUNK_SIZE_Z - 2).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y).at(CHUNK_SIZE_Z - 2);
                                        emit_face_local(chunk, BlockFaceType::front, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::backward, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                }
                                else
                                {
                                    if (ForwardChunkData->at(x).at(y).at(0).IsOpaque() == false)
                                    {
                                        uint8_t ll = ForwardChunkLData->at(x).at(y).at(0);
                                        emit_face_local(chunk, BlockFaceType::front, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::backward, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                    else if (ChunkData->at(x).at(y).at(CHUNK_SIZE_Z - 2).IsOpaque() == false)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y).at(CHUNK_SIZE_Z - 2);
                                        emit_face_local(chunk, BlockFaceType::front, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::backward, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                }
                            }
                            else
                            {
                                if (blk.IsTransparent())
                                {
                                    if (ChunkData->at(x).at(y).at(z + 1).IsTransparent() &&
                                        ChunkData->at(x).at(y).at(z + 1).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y).at(z + 1);
                                        emit_face_local(chunk, BlockFaceType::front, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                    if (ChunkData->at(x).at(y).at(z - 1).IsTransparent() &&
                                        ChunkData->at(x).at(y).at(z - 1).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y).at(z - 1);
                                        emit_face_local(chunk, BlockFaceType::backward, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                }
                                else
                                {
                                    if (ChunkData->at(x).at(y).at(z + 1).IsOpaque() == false)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y).at(z + 1);
                                        emit_face_local(chunk, BlockFaceType::front, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                    if (ChunkData->at(x).at(y).at(z - 1).IsOpaque() == false)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y).at(z - 1);
                                        emit_face_local(chunk, BlockFaceType::backward, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                }
                            }

                            // X faces (left/right)
                            if (x <= 0)
                            {
                                if (blk.IsTransparent())
                                {
                                    if (LeftChunkData->at(CHUNK_SIZE_X - 1).at(y).at(z).IsTransparent() &&
                                        LeftChunkData->at(CHUNK_SIZE_X - 1).at(y).at(z).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = LeftChunkLData->at(CHUNK_SIZE_X - 1).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::left, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::right, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                    else if (ChunkData->at(1).at(y).at(z).IsTransparent() &&
                                        ChunkData->at(1).at(y).at(z).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ChunkLData->at(1).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::right, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::left, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                }
                                else
                                {
                                    if (LeftChunkData->at(CHUNK_SIZE_X - 1).at(y).at(z).IsOpaque() == false)
                                    {
                                        uint8_t ll = LeftChunkLData->at(CHUNK_SIZE_X - 1).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::left, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::right, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                    else if (ChunkData->at(1).at(y).at(z).IsOpaque() == false)
                                    {
                                        uint8_t ll = ChunkLData->at(1).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::right, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::left, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                }
                            }
                            else if (x >= CHUNK_SIZE_X - 1)
                            {
                                if (blk.IsTransparent())
                                {
                                    if (RightChunkData->at(0).at(y).at(z).IsTransparent() &&
                                        RightChunkData->at(0).at(y).at(z).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = RightChunkLData->at(0).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::left, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::right, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                    else if (ChunkData->at(CHUNK_SIZE_X - 2).at(y).at(z).IsTransparent() &&
                                        ChunkData->at(CHUNK_SIZE_X - 2).at(y).at(z).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ChunkLData->at(CHUNK_SIZE_X - 2).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::left, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::right, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                }
                                else
                                {
                                    if (RightChunkData->at(0).at(y).at(z).IsOpaque() == false)
                                    {
                                        uint8_t ll = RightChunkLData->at(0).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::left, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::right, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                    else if (ChunkData->at(CHUNK_SIZE_X - 2).at(y).at(z).IsOpaque() == false)
                                    {
                                        uint8_t ll = ChunkLData->at(CHUNK_SIZE_X - 2).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::left, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                        emit_face_local(chunk, BlockFaceType::right, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                }
                            }
                            else
                            {
                                if (blk.IsTransparent())
                                {
                                    if (ChunkData->at(x + 1).at(y).at(z).IsTransparent() &&
                                        ChunkData->at(x + 1).at(y).at(z).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ChunkLData->at(x + 1).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::right, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }

                                    if (ChunkData->at(x - 1).at(y).at(z).IsTransparent() &&
                                        ChunkData->at(x - 1).at(y).at(z).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ChunkLData->at(x - 1).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::left, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                }
                                else
                                {
                                    if (ChunkData->at(x + 1).at(y).at(z).IsOpaque() == false)
                                    {
                                        uint8_t ll = ChunkLData->at(x + 1).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::right, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }

                                    if (ChunkData->at(x - 1).at(y).at(z).IsOpaque() == false)
                                    {
                                        uint8_t ll = ChunkLData->at(x - 1).at(y).at(z);
                                        emit_face_local(chunk, BlockFaceType::left, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                }
                            }

                            // Y faces (top/bottom)
                            if (y <= 0)
                            {
                                if (ChunkData->at(x).at(y + 1).at(z).IsOpaque() == false)
                                {
                                    emit_face_local(chunk, BlockFaceType::bottom, local_position, blk.p_BlockType, light_level, true, opaque_out, transp_out);
                                }
                            }
                            else if (y >= CHUNK_SIZE_Y - 1)
                            {
                                emit_face_local(chunk, BlockFaceType::top, local_position, blk.p_BlockType, light_level, true, opaque_out, transp_out);
                            }
                            else
                            {
                                if (blk.IsTransparent())
                                {
                                    if (ChunkData->at(x).at(y - 1).at(z).IsTransparent() &&
                                        ChunkData->at(x).at(y - 1).at(z).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y - 1).at(z);
                                        emit_face_local(chunk, BlockFaceType::bottom, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }

                                    if (ChunkData->at(x).at(y + 1).at(z).IsTransparent() &&
                                        ChunkData->at(x).at(y + 1).at(z).p_BlockType != blk.p_BlockType)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y + 1).at(z);
                                        emit_face_local(chunk, BlockFaceType::top, local_position, blk.p_BlockType, ll, false, opaque_out, transp_out);
                                    }
                                }
                                else
                                {
                                    if (ChunkData->at(x).at(y - 1).at(z).IsOpaque() == false)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y - 1).at(z);
                                        emit_face_local(chunk, BlockFaceType::bottom, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }

                                    if (ChunkData->at(x).at(y + 1).at(z).IsOpaque() == false)
                                    {
                                        uint8_t ll = ChunkLData->at(x).at(y + 1).at(z);
                                        emit_face_local(chunk, BlockFaceType::top, local_position, blk.p_BlockType, ll, true, opaque_out, transp_out);
                                    }
                                }
                            }
                        } // for z
            }, // lambda
            partitioner
        ); // parallel_for

        // Merge TLS → contiguous vectors for a single upload each.
        auto merge_tls = [](tbb::enumerable_thread_specific<std::vector<Vertex>>& tls, std::vector<Vertex>& out)
            {
                size_t total = 0;
                for (auto& v : tls) total += v.size();
                out.clear();
                out.reserve(total);
                for (auto& v : tls) {
                    out.insert(out.end(),
                        std::make_move_iterator(v.begin()),
                        std::make_move_iterator(v.end()));
                    v.clear();
                    v.shrink_to_fit();
                }
            };

        merge_tls(tlsOpaque, m_Vertices);
        merge_tls(tlsTransparent, m_TransparentVertices);
        merge_tls(tlsModel, m_ModelVertices);

        // Upload the data to the GPU (same as before)
        p_VerticesCount = 0;
        p_TransparentVerticesCount = 0;
        p_ModelVerticesCount = 0;

        if (!m_Vertices.empty())
        {
            m_VBO.BufferData(m_Vertices.size() * sizeof(Vertex), m_Vertices.data(), GL_STATIC_DRAW);
            p_VerticesCount = static_cast<std::uint32_t>(m_Vertices.size());
            m_Vertices.clear();
        }

        if (!m_TransparentVertices.empty())
        {
            m_TransparentVBO.BufferData(m_TransparentVertices.size() * sizeof(Vertex), m_TransparentVertices.data(), GL_STATIC_DRAW);
            p_TransparentVerticesCount = static_cast<std::uint32_t>(m_TransparentVertices.size());
            m_TransparentVertices.clear();
        }

        if (!m_ModelVertices.empty())
        {
            m_ModelVBO.BufferData(m_ModelVertices.size() * sizeof(Vertex), m_ModelVertices.data(), GL_STATIC_DRAW);
            p_ModelVerticesCount = static_cast<std::uint32_t>(m_ModelVertices.size());
            m_ModelVertices.clear();
        }

        return true;
    }

    glm::ivec3 ConvertWorldPosToBlock(const glm::vec3& position)
    {
        int block_chunk_x = static_cast<int>(floor(position.x / CHUNK_SIZE_X));
        int block_chunk_z = static_cast<int>(floor(position.z / CHUNK_SIZE_Z));
        int lx = position.x - (block_chunk_x * CHUNK_SIZE_X);
        int ly = static_cast<int>(floor(position.y));
        int lz = position.z - (block_chunk_z * CHUNK_SIZE_Z);

        return glm::ivec3(lx, ly, lz);
    }
}
