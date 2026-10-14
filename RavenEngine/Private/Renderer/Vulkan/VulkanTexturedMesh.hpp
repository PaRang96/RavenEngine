#pragma once

#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Raven/Renderer/TexturedVertex.hpp"
#include "Renderer/MeshBounds.hpp"

#include <cstdint>
#include <span>

namespace Raven
{
	class VulkanTexturedMesh
	{
	public:
		VulkanTexturedMesh(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			std::span<const TexturedVertex> vertices, std::span<const std::uint16_t> indices);

		VkDevice GetDevice() const { return m_Device; }
		const VulkanBuffer& GetVertices() const { return m_Vertices; }
		const VulkanBuffer& GetIndices() const { return m_Indices; }
		std::uint32_t GetIndexCount() const { return m_IndexCount; }
		const MeshBounds& GetLocalBounds() const { return m_LocalBounds; }

		VulkanTexturedMesh(const VulkanTexturedMesh&) = delete;
		VulkanTexturedMesh& operator=(const VulkanTexturedMesh&) = delete;

	private:
		MeshBounds m_LocalBounds;
		VkDevice m_Device = VK_NULL_HANDLE;
		VulkanBuffer m_Vertices;
		VulkanBuffer m_Indices;
		std::uint32_t m_IndexCount = 0;
	};
}
