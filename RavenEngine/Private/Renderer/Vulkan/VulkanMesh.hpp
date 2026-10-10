#pragma once

#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Raven/Renderer/ColorVertex.hpp"

#include <cstdint>
#include <span>

namespace Raven
{
	class VulkanMesh
	{
	public:
		VulkanMesh(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			std::span<const ColorVertex> vertices, std::span<const std::uint16_t> indices);

		const VulkanBuffer& GetVertices() const { return m_Vertices; }
		const VulkanBuffer& GetIndices() const { return m_Indices; }
		std::uint32_t GetIndexCount() const { return m_IndexCount; }

		VulkanMesh(const VulkanMesh&) = delete;
		VulkanMesh& operator=(const VulkanMesh&) = delete;

	private:
		VulkanBuffer m_Vertices;
		VulkanBuffer m_Indices;
		std::uint32_t m_IndexCount;
	};
}
