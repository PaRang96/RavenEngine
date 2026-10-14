#include "Renderer/Vulkan/VulkanTexturedMesh.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace Raven
{
	namespace
	{
		std::span<const TexturedVertex> ValidateMesh(std::span<const TexturedVertex> vertices,
			std::span<const std::uint16_t> indices)
		{
			if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 ||
				indices.size() > std::numeric_limits<std::uint32_t>::max())
				throw std::invalid_argument("A textured mesh requires vertices and complete indexed triangles");
			for (const auto index : indices)
				if (index >= vertices.size())
					throw std::invalid_argument("Textured mesh index is outside the vertex array");
			for (const auto& vertex : vertices)
			{
				for (const float value : vertex.Position)
					if (!std::isfinite(value))
						throw std::invalid_argument("Textured mesh positions must be finite");
				for (const float value : vertex.UV)
					if (!std::isfinite(value))
						throw std::invalid_argument("Textured mesh UV coordinates must be finite");
			}
			return vertices;
		}
	}

	VulkanTexturedMesh::VulkanTexturedMesh(const VulkanDevice& device,
		const VulkanPhysicalDevice& physicalDevice, std::span<const TexturedVertex> vertices,
		std::span<const std::uint16_t> indices)
		: m_LocalBounds(CalculateMeshBounds(ValidateMesh(vertices, indices), indices)),
		  m_Device(device.Get()),
		  m_Vertices(device, physicalDevice, std::as_bytes(vertices),
			  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
		  m_Indices(device, physicalDevice, std::as_bytes(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
		  m_IndexCount(static_cast<std::uint32_t>(indices.size()))
	{
	}
}
