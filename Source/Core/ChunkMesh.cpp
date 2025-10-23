#include "ChunkMesh.h"
#include "Chunk.h"


namespace Minecraft
{
	namespace {
		// Build once, use forever. C++11 function-local statics are thread-safe. If your toolchain
		// breaks that, fix your toolchain – not the code.
		GLClasses::IndexBuffer& quad_index_buffer()
		{
			static GLClasses::IndexBuffer ibo;
			static bool inited = false;
			if (!inited) {
				inited = true;
				const size_t max_quads = size_t(CHUNK_SIZE_X) * CHUNK_SIZE_Y * CHUNK_SIZE_Z * 6; // worst case
				std::vector<GLuint> idx;
				idx.reserve(max_quads * 6);
				GLuint base = 0;
				for (size_t q = 0; q < max_quads; ++q) {
					// 0,1,2, 2,3,0 – it’s a quad, not rocket science
					idx.push_back(base + 0);
					idx.push_back(base + 1);
					idx.push_back(base + 2);
					idx.push_back(base + 2);
					idx.push_back(base + 3);
					idx.push_back(base + 0);
					base += 4;
				}
				ibo.BufferData(GLsizeiptr(idx.size() * sizeof(GLuint)), idx.data(), GL_STATIC_DRAW);
			}
			return ibo;
		}

		inline void setup_vao(GLClasses::VertexArray& vao, GLClasses::VertexBuffer& vbo)
		{
			vao.Bind();
			vbo.Bind();
			quad_index_buffer().Bind();
			vbo.VertexAttribIPointer(0, 3, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, position));
			vbo.VertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, texture_coords));
			vbo.VertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, lighting_level));
			vbo.VertexAttribIPointer(3, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, block_face_lighting));
			vao.Unbind();
		}

		struct Neighborhood {
			ChunkDataTypePtr c, l, r, f, b;           // center, left(-x), right(+x), forward(+z), backward(-z)
			ChunkLightDataTypePtr cl, ll, rl, fl, bl; // matching light arrays
		};

		inline bool in_y_range(int y) {
			return y >= 0 && y < CHUNK_SIZE_Y;
		}

		// Resolve (x,y,z) to the correct chunk data, mapping out-of-bounds X/Z to neighbors.
		inline const Block* get_block_ptr(const Neighborhood& n, int x, int y, int z)
		{
			if (!in_y_range(y)) 
				return nullptr; // treat out-of-bounds Y as air
			if (x < 0) 
				return n.l ? &n.l->at(CHUNK_SIZE_X - 1).at(y).at(z) : nullptr;
			if (x >= CHUNK_SIZE_X)
				return n.r ? &n.r->at(0).at(y).at(z) : nullptr;
			if (z < 0)   
				return n.b ? &n.b->at(x).at(y).at(CHUNK_SIZE_Z - 1) : nullptr;
			if (z >= CHUNK_SIZE_Z)
				return n.f ? &n.f->at(x).at(y).at(0) : nullptr;
			return &n.c->at(x).at(y).at(z);
		}

		inline uint8_t get_light(const Neighborhood& n, int x, int y, int z)
		{
			if (!in_y_range(y))
				return 0;
			if (x < 0)    
				return n.ll ? n.ll->at(CHUNK_SIZE_X - 1).at(y).at(z) : 0;
			if (x >= CHUNK_SIZE_X)
				return n.rl ? n.rl->at(0).at(y).at(z) : 0;
			if (z < 0)         
				return n.bl ? n.bl->at(x).at(y).at(CHUNK_SIZE_Z - 1) : 0;
			if (z >= CHUNK_SIZE_Z)
				return n.fl ? n.fl->at(x).at(y).at(0) : 0;
			return n.cl->at(x).at(y).at(z);
		}

		// One visibility rule. Stop duplicating it.
		inline bool face_visible(const Block& cur, const Block* nb)
		{
			if (cur.IsTransparent()) {
				// Show the interface only if neighbor is also transparent *and different type*,
				// or neighbor is air (nullptr treated as air).
				if (!nb) return true;
				return nb->IsTransparent() && nb->p_BlockType != cur.p_BlockType;
			}
			else {
				// Opaque faces are visible if the neighbor is *not* opaque (air or transparent)
				if (!nb) return true;
				return !nb->IsOpaque();
			}
		}

		struct Dir {
			BlockFaceType face;
			int dx, dy, dz;
		};

		// Keep the same face order semantics used by AddFace() lighting LUT.
		static constexpr Dir Dirs[6] = {
			{ BlockFaceType::top,       0, +1,  0 },
			{ BlockFaceType::bottom,    0, -1,  0 },
			{ BlockFaceType::front,     0,  0, +1 },
			{ BlockFaceType::backward,  0,  0, -1 },
			{ BlockFaceType::left,     -1,  0,  0 },
			{ BlockFaceType::right,    +1,  0,  0 },
		};
	} // anonymous namespace

	ChunkMesh::ChunkMesh() : m_VBO(GL_ARRAY_BUFFER), m_TransparentVBO(GL_ARRAY_BUFFER), m_ModelVBO(GL_ARRAY_BUFFER)
	{
		// One VAO/VBO setup per stream. No copy-paste, no nonsense.
		setup_vao(p_VAO, m_VBO);
		setup_vao(p_TransparentVAO, m_TransparentVBO);
		setup_vao(p_ModelVAO, m_ModelVBO);

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
		// Vectors free themselves. If you think calling clear() in a destructor is useful,
		// no, it just wastes cycles.
	}

	// Construct mesh using greedy meshing for maximum performance
	bool ChunkMesh::ConstructMesh(Chunk* chunk, const glm::vec3& chunk_pos)
	{
		ChunkDataTypePtr ChunkData = &chunk->p_ChunkContents;
		ChunkLightDataTypePtr ChunkLData = &chunk->p_ChunkLightInformation;

		glm::vec3 local_position;
		m_Vertices.clear();
		m_TransparentVertices.clear();
		m_ModelVertices.clear();

		// Pull neighbors once. If they are missing, we still mesh – treat as air.
		Neighborhood nb{
			ChunkData,
			_GetChunkDataForMeshing(static_cast<int>(chunk_pos.x - 1), static_cast<int>(chunk_pos.z)),
			_GetChunkDataForMeshing(static_cast<int>(chunk_pos.x + 1), static_cast<int>(chunk_pos.z)),
			_GetChunkDataForMeshing(static_cast<int>(chunk_pos.x),     static_cast<int>(chunk_pos.z + 1)),
			_GetChunkDataForMeshing(static_cast<int>(chunk_pos.x),     static_cast<int>(chunk_pos.z - 1)),
			ChunkLData,
			_GetChunkLightDataForMeshing(static_cast<int>(chunk_pos.x - 1), static_cast<int>(chunk_pos.z)),
			_GetChunkLightDataForMeshing(static_cast<int>(chunk_pos.x + 1), static_cast<int>(chunk_pos.z)),
			_GetChunkLightDataForMeshing(static_cast<int>(chunk_pos.x),     static_cast<int>(chunk_pos.z + 1)),
			_GetChunkLightDataForMeshing(static_cast<int>(chunk_pos.x),     static_cast<int>(chunk_pos.z - 1))
		};

		for (int x = 0; x < CHUNK_SIZE_X; ++x) {
			for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
				for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
					const Block& blk = ChunkData->at(x).at(y).at(z);
					if (blk.p_BlockType == BlockType::Air)
						continue;

					local_position = glm::vec3(x, y, z);

					// Models (flowers etc.) are special; shove them into the model stream.
					// Lighting uses the block above if available (as in original behavior).
					float base_light = ChunkLData->at(x).at(y).at(z);
					if (y >= 0 && y < CHUNK_SIZE_Y - 1) base_light = ChunkLData->at(x).at(y + 1).at(z);
					if (blk.IsModel()) {
						AddModel(chunk, local_position, blk.p_BlockType, base_light);
						continue;
					}

					const bool opaque = blk.IsOpaque();
					for (const auto& d : Dirs) {
						const int nx = x + d.dx;
						const int ny = y + d.dy;
						const int nz = z + d.dz;
						const Block* nbp = get_block_ptr(nb, nx, ny, nz);
						if (!face_visible(blk, nbp))
							continue;
						const uint8_t light = get_light(nb, nx, ny, nz);
						// The last arg is "buffer": in the old code 'true' meant opaque stream.
						AddFace(chunk, d.face, local_position, blk.p_BlockType, light, /*buffer=*/opaque);
					}
				}
			}
		}

		// Upload the data to the GPU whenever the mesh is reconstructed
		p_VerticesCount = 0;
		p_TransparentVerticesCount = 0;
		p_ModelVerticesCount = 0;

		if (!m_Vertices.empty())
		{
			m_VBO.BufferData(GLsizeiptr(m_Vertices.size() * sizeof(Vertex)), m_Vertices.data(), GL_STATIC_DRAW);
			p_VerticesCount = static_cast<uint32_t>(m_Vertices.size());
			m_Vertices.clear();
		}

		if (!m_TransparentVertices.empty())
		{
			m_TransparentVBO.BufferData(GLsizeiptr(m_TransparentVertices.size() * sizeof(Vertex)), m_TransparentVertices.data(), GL_STATIC_DRAW);
			p_TransparentVerticesCount = static_cast<uint32_t>(m_TransparentVertices.size());
			m_TransparentVertices.clear();
		}

		if (!m_ModelVertices.empty())
		{
			m_ModelVBO.BufferData(GLsizeiptr(m_ModelVertices.size() * sizeof(Vertex)), m_ModelVertices.data(), GL_STATIC_DRAW);
			p_ModelVerticesCount = static_cast<uint32_t>(m_ModelVertices.size());
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

	void ChunkMesh::AddFace(Chunk* chunk, BlockFaceType face_type, const glm::vec3& position, BlockType type, uint8_t light_level,
		bool buffer)
	{
		glm::vec4 translation = glm::vec4(position, 0.0f); // Model matrix for cubes is theatrical. Translation is enough.
		// Adding the position to the translation will do the samething but much much faster

		Vertex v1, v2, v3, v4;

		// Yes, some faces need UV reversal to match the atlas orientation. Do it here, data-driven, not by copy-paste.
		bool reverse_texture_coordinates = false;

		// Order
		// Top, bottom, front, back, left, right
		static const uint8_t lighting_levels[6] = { 10, 3, 6, 7, 6, 7 };

		switch (face_type)
		{
		case BlockFaceType::top:
		{
			uint8_t face_light_level = 10;

			if (HasShadow(chunk, position.x, position.y, position.z))
			{
				face_light_level -= 2;
			}

			v1.position = translation + m_TopFace[0];
			v2.position = translation + m_TopFace[1];
			v3.position = translation + m_TopFace[2];
			v4.position = translation + m_TopFace[3];

			// Set the lighting level for the vertex
			v1.lighting_level = light_level;
			v2.lighting_level = light_level;
			v3.lighting_level = light_level;
			v4.lighting_level = light_level;

			v1.block_face_lighting = face_light_level;
			v2.block_face_lighting = face_light_level;
			v3.block_face_lighting = face_light_level;
			v4.block_face_lighting = face_light_level;

			break;
		}

		case BlockFaceType::bottom:
		{
			v1.position = translation + m_BottomFace[3];
			v2.position = translation + m_BottomFace[2];
			v3.position = translation + m_BottomFace[1];
			v4.position = translation + m_BottomFace[0];

			// Set the lighting level for the vertex
			v1.lighting_level = light_level;
			v2.lighting_level = light_level;
			v3.lighting_level = light_level;
			v4.lighting_level = light_level;

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

			// Set the lighting level for the vertex
			v1.lighting_level = light_level;
			v2.lighting_level = light_level;
			v3.lighting_level = light_level;
			v4.lighting_level = light_level;

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

			v1.lighting_level = light_level;
			v2.lighting_level = light_level;
			v3.lighting_level = light_level;
			v4.lighting_level = light_level;

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

			v1.lighting_level = light_level;
			v2.lighting_level = light_level;
			v3.lighting_level = light_level;
			v4.lighting_level = light_level;

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

			v1.lighting_level = light_level;
			v2.lighting_level = light_level;
			v3.lighting_level = light_level;
			v4.lighting_level = light_level;

			v1.block_face_lighting = lighting_levels[5];
			v2.block_face_lighting = lighting_levels[5];
			v3.block_face_lighting = lighting_levels[5];
			v4.block_face_lighting = lighting_levels[5];

			break;
		}

		default:
		{
			// Todo : Throw an error here
			break;
		}
		}

		if (type == BlockType::Water) {

			v1.block_face_lighting = 85;
			v2.block_face_lighting = 85;
			v3.block_face_lighting = 85;
			v4.block_face_lighting = 85;
		}

		// Get required texture coordinates

		const std::array<uint16_t, 8>& TextureCoordinates = BlockDatabase::GetBlockTexture(type, face_type);

		if (reverse_texture_coordinates)
		{
			v1.texture_coords = { TextureCoordinates[6], TextureCoordinates[7] };
			v2.texture_coords = { TextureCoordinates[4], TextureCoordinates[5] };
			v3.texture_coords = { TextureCoordinates[2], TextureCoordinates[3] };
			v4.texture_coords = { TextureCoordinates[0], TextureCoordinates[1] };
		}

		else
		{
			v1.texture_coords = { TextureCoordinates[0], TextureCoordinates[1] };
			v2.texture_coords = { TextureCoordinates[2], TextureCoordinates[3] };
			v3.texture_coords = { TextureCoordinates[4], TextureCoordinates[5] };
			v4.texture_coords = { TextureCoordinates[6], TextureCoordinates[7] };
		}

		if (buffer)
		{
			m_Vertices.push_back(v1);
			m_Vertices.push_back(v2);
			m_Vertices.push_back(v3);
			m_Vertices.push_back(v4);
		}

		else if (!buffer)
		{
			m_TransparentVertices.push_back(v1);
			m_TransparentVertices.push_back(v2);
			m_TransparentVertices.push_back(v3);
			m_TransparentVertices.push_back(v4);
		}
	}

	// Adds a model such as a flower or a deadbush to the chunk mesh
	void ChunkMesh::AddModel(Chunk* chunk, const glm::vec3& local_pos, BlockType type, float light_level)
	{
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), local_pos);
		Model model(type);

		uint8_t face_light = 10;

		if (HasShadow(chunk, local_pos.x, local_pos.y, local_pos.z))
		{
			face_light -= 2;
		}

		for (int i = 0; i < model.p_ModelVertices.size(); i++)
		{
			Vertex vertex;

			glm::vec4 pos = glm::vec4(model.p_ModelVertices.at(i).position, 1.0f);
			pos = translation * pos;

			vertex.position = glm::vec3(pos.x, pos.y, pos.z);
			vertex.texture_coords = model.p_ModelVertices.at(i).tex_coords;
			vertex.lighting_level = light_level;
			vertex.block_face_lighting = face_light;

			m_ModelVertices.push_back(vertex);
		}
	}
}
