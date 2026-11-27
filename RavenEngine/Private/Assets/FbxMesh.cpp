#include "Assets/FbxMesh.hpp"

#include "Raven/Core/Files.hpp"
#include "Core/PathText.hpp"

#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
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

		using SceneHandle = std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)>;

		[[noreturn]] void FbxError(const std::filesystem::path& path, const std::string& reason)
		{
			throw std::runtime_error("Cannot import FBX " + PathToUtf8(path) + ": " + reason);
		}

		// Model space keeps the conversion to the engine's axes and metres in the root, so every mesh is
		// baked into one model, as the file's own ID has always drawn it. Part space folds the conversion
		// into the nodes and geometry, so each node's transform relative to its parent is what an entity
		// holds and each part's vertices are in that node's own space (FD-23).
		SceneHandle LoadScene(std::span<const std::byte> bytes, const std::filesystem::path& path, bool partSpace,
			bool geometry)
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
			options.ignore_geometry = !geometry;
			options.target_axes = ufbx_axes_right_handed_z_up;
			options.target_unit_meters = 1.0;
			if (partSpace)
				options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
			options.node_depth_limit = 128;
			options.temp_allocator.memory_limit = MaxParserBytes;
			options.result_allocator.memory_limit = MaxParserBytes;
			ufbx_error error{};
			SceneHandle scene(ufbx_load_memory(bytes.data(), bytes.size(), &options, &error), &ufbx_free_scene);
			if (!scene)
			{
				char description[512]{};
				ufbx_format_error(description, sizeof(description), &error);
				FbxError(path, description);
			}
			return scene;
		}

		// A node name in a name path: '/' separates levels, and '#' starts a repeat number, so both are
		// escaped with a backslash, as is the backslash itself.
		std::string EscapeName(std::string_view name)
		{
			if (name.empty())
				return "(unnamed)";
			std::string result;
			for (const char character : name)
			{
				if (character == '/' || character == '#' || character == '\\')
					result += '\\';
				result += character;
			}
			return result;
		}

		struct NamedNode
		{
			const ufbx_node* Node = nullptr;
			std::string Path;
			std::int32_t Parent = -1;
		};

		// Every node under the root, parents first, each with its name path from the top level. Siblings
		// with one name are told apart by their order: the second is "Name#2".
		std::vector<NamedNode> NameNodes(const ufbx_scene& scene)
		{
			std::vector<NamedNode> result;
			const auto visit = [&](auto& self, const ufbx_node& parent, const std::string& parentPath, std::int32_t parentIndex) -> void
			{
				std::map<std::string, std::uint32_t> seen;
				for (const auto* child : parent.children)
				{
					auto name = EscapeName(std::string_view(child->name.data, child->name.length));
					if (const auto repeat = ++seen[name]; repeat > 1)
						name += '#' + std::to_string(repeat);
					auto path = parentPath.empty() ? name : parentPath + '/' + name;
					const auto index = static_cast<std::int32_t>(result.size());
					result.push_back({ child, path, parentIndex });
					self(self, *child, path, index);
				}
			};
			visit(visit, *scene.root_node, {}, -1);
			return result;
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

		ColorVertex ReadVertex(const ufbx_mesh& mesh, const ufbx_matrix& geometry, const ufbx_matrix& normalMatrix,
			std::uint32_t corner, const std::filesystem::path& path)
		{
			const auto position = ufbx_transform_position(&geometry, ufbx_get_vertex_vec3(&mesh.vertex_position, corner));
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

		// Appends a node's mesh, transformed by `geometry` (to the model or to the node's own space).
		void AppendMesh(const ufbx_node& node, const ufbx_matrix& geometry, std::vector<ColorVertex>& vertices,
			std::size_t& triangles, const std::filesystem::path& path)
		{
			const auto& mesh = *node.mesh;
			if (mesh.all_deformers.count != 0)
				FbxError(path, "expected a static mesh");
			triangles += mesh.num_triangles;
			if (!mesh.vertex_position.exists || mesh.num_triangles == 0 || triangles > MaxTriangles)
				FbxError(path, "mesh is empty or exceeds the triangle limit");
			const auto determinant = ufbx_matrix_determinant(&geometry);
			if (!std::isfinite(determinant) || determinant == 0)
				FbxError(path, "mesh transform is singular or non-finite");
			const auto normalMatrix = ufbx_matrix_for_normals(&geometry);
			vertices.reserve(vertices.size() + mesh.num_triangles * 3);
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
						vertices.push_back(ReadVertex(mesh, geometry, normalMatrix, faceIndices[index + corner], path));
				}
			}
		}
	}

	std::vector<std::byte> ReadFbxBytes(const std::filesystem::path& path)
	{
		return Files::ReadBinary(path, MaxFileBytes);
	}

	FbxMesh DecodeFbxMesh(std::span<const std::byte> bytes, const std::filesystem::path& path, std::string_view part)
	{
		const auto scene = LoadScene(bytes, path, !part.empty(), true);
		FbxMesh result;
		std::size_t triangles = 0;
		if (part.empty())
		{
			for (const auto* node : scene->nodes)
				if (node->mesh)
					AppendMesh(*node, node->geometry_to_world, result.Vertices, triangles, path);
			if (triangles == 0)
				FbxError(path, "expected a mesh object");
		}
		else
		{
			const auto nodes = NameNodes(*scene);
			const auto found = std::find_if(nodes.begin(), nodes.end(),
				[&](const NamedNode& node) { return node.Path == part && node.Node->mesh; });
			if (found == nodes.end())
				FbxError(path, "the file has no part \"" + std::string(part) + "\"; if it was renamed, repair its ID with "
					"RavenSceneTool rename-part");
			AppendMesh(*found->Node, found->Node->geometry_to_node, result.Vertices, triangles, path);
		}

		// Merge complete vertex tuples while preserving normal, UV, and color seams.
		ufbx_error error{};
		const ufbx_vertex_stream stream{result.Vertices.data(), result.Vertices.size(), sizeof(ColorVertex)};
		std::vector<std::uint32_t> indices(result.Vertices.size());
		const ufbx_allocator_opts allocator{ .memory_limit = MaxParserBytes };
		const auto unique = ufbx_generate_indices(&stream, 1, indices.data(), indices.size(), &allocator, &error);
		// 32-bit indices (R6): the triangle limit bounds the vertex count well below the index range.
		if (unique == 0)
			FbxError(path, "cannot index mesh");
		result.Vertices.resize(unique);
		result.Indices.assign(indices.begin(), indices.end());
		return result;
	}

	std::vector<FbxNode> ReadFbxNodes(std::span<const std::byte> bytes, const std::filesystem::path& path)
	{
		const auto scene = LoadScene(bytes, path, true, false);
		const auto named = NameNodes(*scene);
		// Keep the nodes on the way to a mesh: a parent is kept when any descendant is.
		std::vector<bool> keep(named.size(), false);
		// Children follow their parents in the list, so walking it backwards sees every child first.
		for (std::size_t index = named.size(); index-- > 0;)
		{
			if (named[index].Node->mesh)
				keep[index] = true;
			if (keep[index] && named[index].Parent >= 0)
				keep[static_cast<std::size_t>(named[index].Parent)] = true;
		}
		std::vector<std::int32_t> remap(named.size(), -1);
		std::vector<FbxNode> result;
		for (std::size_t index = 0; index < named.size(); ++index)
		{
			if (!keep[index])
				continue;
			const auto& node = *named[index].Node;
			const auto transform = ufbx_matrix_to_transform(&node.node_to_parent);
			const auto finite = [](double value) { return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(); };
			if (!finite(transform.translation.x) || !finite(transform.translation.y) || !finite(transform.translation.z) ||
				!finite(transform.scale.x) || !finite(transform.scale.y) || !finite(transform.scale.z) ||
				!finite(transform.rotation.x) || !finite(transform.rotation.y) || !finite(transform.rotation.z) ||
				!finite(transform.rotation.w))
				FbxError(path, "node " + named[index].Path + " has a non-finite transform");
			FbxNode entry;
			entry.Path = named[index].Path;
			entry.Name = std::string(node.name.data, node.name.length);
			entry.Parent = named[index].Parent >= 0 ? remap[static_cast<std::size_t>(named[index].Parent)] : -1;
			entry.Position = { static_cast<float>(transform.translation.x), static_cast<float>(transform.translation.y),
				static_cast<float>(transform.translation.z) };
			entry.Rotation = { static_cast<float>(transform.rotation.x), static_cast<float>(transform.rotation.y),
				static_cast<float>(transform.rotation.z), static_cast<float>(transform.rotation.w) };
			entry.Scale = { static_cast<float>(transform.scale.x), static_cast<float>(transform.scale.y),
				static_cast<float>(transform.scale.z) };
			entry.Mesh = node.mesh != nullptr;
			remap[index] = static_cast<std::int32_t>(result.size());
			result.push_back(std::move(entry));
		}
		return result;
	}

	std::vector<std::string> ListFbxParts(std::span<const std::byte> bytes, const std::filesystem::path& path)
	{
		const auto scene = LoadScene(bytes, path, true, false);
		std::vector<std::string> result;
		for (const auto& node : NameNodes(*scene))
			if (node.Node->mesh)
				result.push_back(node.Path);
		return result;
	}
}
