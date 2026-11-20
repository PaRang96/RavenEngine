#include "Renderer/Vulkan/VulkanMesh.hpp"

#include <limits>
#include <cmath>
#include <stdexcept>

namespace Raven
{
	namespace
	{
		std::span<const ColorVertex> ValidateMesh(std::span<const ColorVertex> vertices,
			std::span<const std::uint16_t> indices)
		{
			if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 ||
				indices.size() > std::numeric_limits<std::uint32_t>::max())
				throw std::invalid_argument("A mesh requires vertices and complete indexed triangles");
			for (const auto& vertex : vertices)
			{
				const float normalLength = std::hypot(vertex.Normal[0], vertex.Normal[1], vertex.Normal[2]);
				if (!std::isfinite(normalLength) || normalLength == 0.0f ||
					!std::isfinite(vertex.UV[0]) || !std::isfinite(vertex.UV[1]))
					throw std::invalid_argument("Mesh vertices need finite UVs and nonzero finite normals");
			}
			for (const auto index : indices)
				if (index >= vertices.size())
					throw std::invalid_argument("Mesh index is outside the vertex array");
			return vertices;
		}
	}

	VulkanMeshData::VulkanMeshData(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
		std::span<const ColorVertex> vertices, std::span<const std::uint16_t> indices)
		: PickIndices(indices.begin(), indices.end()),
		  LocalBounds(CalculateMeshBounds(ValidateMesh(vertices, indices), indices)),
		  Vertices(device, physicalDevice, std::as_bytes(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
		  Indices(device, physicalDevice, std::as_bytes(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
		  IndexCount(static_cast<std::uint32_t>(indices.size()))
	{
		PickVertices.reserve(vertices.size());
		for (const auto& vertex : vertices)
			PickVertices.push_back({ vertex.Position[0], vertex.Position[1], vertex.Position[2] });
	}

	VulkanMesh::VulkanMesh(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
		std::span<const ColorVertex> vertices, std::span<const std::uint16_t> indices)
		: m_Data(std::make_shared<const VulkanMeshData>(device, physicalDevice, vertices, indices))
	{
	}

	void VulkanMesh::Replace(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
		std::span<const ColorVertex> vertices, std::span<const std::uint16_t> indices)
	{
		m_Data = std::make_shared<const VulkanMeshData>(device, physicalDevice, vertices, indices);
	}
}
