#include "Renderer/Vulkan/VulkanMesh.hpp"

#include <limits>
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
			for (const auto index : indices)
				if (index >= vertices.size())
					throw std::invalid_argument("Mesh index is outside the vertex array");
			return vertices;
		}
	}

	VulkanMesh::VulkanMesh(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
		std::span<const ColorVertex> vertices, std::span<const std::uint16_t> indices)
		: m_Vertices(device, physicalDevice, std::as_bytes(ValidateMesh(vertices, indices)),
			  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
		  m_Indices(device, physicalDevice, std::as_bytes(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
		  m_IndexCount(static_cast<std::uint32_t>(indices.size()))
	{
	}
}
