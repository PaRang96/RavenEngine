#include "Assets/FbxMesh.hpp"

#include "Raven/Core/Files.hpp"
#include "Core/PathText.hpp"

#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{
	namespace
	{
		constexpr std::size_t MaxFileBytes = 64 * 1024 * 1024;
		constexpr std::size_t MaxParserBytes = 128 * 1024 * 1024;
		constexpr std::size_t MaxTriangles = 262144;
		constexpr std::size_t MaxVertices = std::size_t{std::numeric_limits<std::uint16_t>::max()} + 1;

		[[noreturn]] void FbxError(const std::filesystem::path& path, const char* reason)
		{
			throw std::runtime_error("Cannot import FBX " + PathToUtf8(path) + ": " + reason);
		}

		const ufbx_node& FindMeshNode(const ufbx_scene& scene, const std::filesystem::path& path)
		{
			const ufbx_node* found = nullptr;
			for (const auto* node : scene.nodes)
			{
				if (!node->mesh)
					continue;
				if (found)
					FbxError(path, "expected one mesh object");
				found = node;
			}
			if (!found)
				FbxError(path, "expected one mesh object");
			return *found;
		}

		float VertexComponent(ufbx_real value, const std::filesystem::path& path)
		{
			if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
				FbxError(path, "vertex attribute is non-finite or exceeds the float range");
			return static_cast<float>(value);
		}

		float ColorComponent(ufbx_real value, const std::filesystem::path& path)
		{
			if (!std::isfinite(value))
				FbxError(path, "invalid vertex color");
			return static_cast<float>(std::clamp(value, ufbx_real{0}, ufbx_real{1}));
		}

		ColorVertex ReadVertex(const ufbx_node& node, const ufbx_matrix& normalMatrix,
			std::uint32_t corner, const std::filesystem::path& path)
		{
			const auto& mesh = *node.mesh;
			const auto position = ufbx_transform_position(&node.geometry_to_world,
				ufbx_get_vertex_vec3(&mesh.vertex_position, corner));
			const auto transformed = ufbx_transform_direction(&normalMatrix,
				ufbx_get_vertex_vec3(&mesh.vertex_normal, corner));
			const double length = std::hypot(transformed.x, transformed.y, transformed.z);
			if (!std::isfinite(length) || length == 0)
				FbxError(path, "invalid mesh normal");
			const auto uv = mesh.vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh.vertex_uv, corner) : ufbx_vec2{};
			ufbx_vec4 color{1, 1, 1, 1};
			if (mesh.vertex_color.exists)
				color = ufbx_get_vertex_vec4(&mesh.vertex_color, corner);
			return {
				{ VertexComponent(position.x, path), VertexComponent(position.y, path), VertexComponent(position.z, path) },
				{ ColorComponent(color.x, path), ColorComponent(color.y, path), ColorComponent(color.z, path) },
				{ VertexComponent(transformed.x / length, path), VertexComponent(transformed.y / length, path), VertexComponent(transformed.z / length, path) },
				{ VertexComponent(uv.x, path), VertexComponent(1.0 - uv.y, path) }
			};
		}
	}

	std::vector<std::byte> ReadFbxBytes(const std::filesystem::path& path)
	{
		return Files::ReadBinary(path, MaxFileBytes);
	}

	FbxMesh DecodeFbxMesh(std::span<const std::byte> bytes, const std::filesystem::path& path)
	{
		if (bytes.empty())
			FbxError(path, "file is empty");
		if (bytes.size() > MaxFileBytes)
			FbxError(path, "file exceeds the import size limit");

		ufbx_load_opts options{};
		options.file_format = UFBX_FILE_FORMAT_FBX;
		options.ignore_animation = true;
		options.generate_missing_normals = true;
		options.ignore_embedded = true;
		options.target_axes = ufbx_axes_right_handed_z_up;
		options.target_unit_meters = 1.0;
		options.node_depth_limit = 128;
		options.temp_allocator.memory_limit = MaxParserBytes;
		options.result_allocator.memory_limit = MaxParserBytes;
		ufbx_error error{};
		const std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene(
			ufbx_load_memory(bytes.data(), bytes.size(), &options, &error), &ufbx_free_scene);
		if (!scene)
		{
			char description[512]{};
			ufbx_format_error(description, sizeof(description), &error);
			FbxError(path, description);
		}

		const auto& node = FindMeshNode(*scene, path);
		const auto& mesh = *node.mesh;
		if (mesh.all_deformers.count != 0)
			FbxError(path, "expected a static mesh");
		if (!mesh.vertex_position.exists || mesh.num_triangles == 0 || mesh.num_triangles > MaxTriangles)
			FbxError(path, "mesh is empty or exceeds the triangle limit");

		const auto determinant = ufbx_matrix_determinant(&node.geometry_to_world);
		if (!std::isfinite(determinant) || determinant == 0)
			FbxError(path, "mesh transform is singular or non-finite");

		const auto normalMatrix = ufbx_matrix_for_normals(&node.geometry_to_world);
		FbxMesh result;
		result.Vertices.reserve(mesh.num_triangles * 3);
		std::vector<std::uint32_t> faceIndices(mesh.max_face_triangles * 3);
		for (const auto face : mesh.faces)
		{
			if (face.num_indices < 3)
				continue;
			const auto count = ufbx_triangulate_face(faceIndices.data(), faceIndices.size(), &mesh, face);
			if (count != face.num_indices - 2)
				FbxError(path, "cannot triangulate polygon");
			for (std::size_t index = 0; index < count * 3; index += 3)
			{
				if (determinant < 0)
					std::swap(faceIndices[index + 1], faceIndices[index + 2]);
				for (std::size_t corner = 0; corner < 3; ++corner)
					result.Vertices.push_back(ReadVertex(node, normalMatrix, faceIndices[index + corner], path));
			}
		}

		// Merge complete vertex tuples while preserving normal, UV, and color seams.
		const ufbx_vertex_stream stream{result.Vertices.data(), result.Vertices.size(), sizeof(ColorVertex)};
		std::vector<std::uint32_t> indices(result.Vertices.size());
		const ufbx_allocator_opts allocator{ .memory_limit = MaxParserBytes };
		const auto unique = ufbx_generate_indices(&stream, 1, indices.data(), indices.size(), &allocator, &error);
		if (unique == 0 || unique > MaxVertices)
			FbxError(path, "cannot index mesh within the renderer's 16-bit vertex limit");
		result.Vertices.resize(unique);
		result.Indices.assign(indices.begin(), indices.end());
		return result;
	}
}
