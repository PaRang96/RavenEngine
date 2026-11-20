#pragma once

#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Raven/Renderer/ColorVertex.hpp"
#include "Renderer/MeshBounds.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Raven
{
	struct VulkanMeshData
	{
		VulkanMeshData(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			std::span<const ColorVertex> vertices, std::span<const std::uint16_t> indices);

		// CPU triangle positions share the exact replacement/retirement lifetime of GPU data.
		std::vector<Vec3> PickVertices;
		std::vector<std::uint16_t> PickIndices;
		MeshBounds LocalBounds;
		VulkanBuffer Vertices;
		VulkanBuffer Indices;
		std::uint32_t IndexCount;
	};

	class VulkanMesh
	{
	public:
		VulkanMesh(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			std::span<const ColorVertex> vertices, std::span<const std::uint16_t> indices);
		void Replace(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			std::span<const ColorVertex> vertices, std::span<const std::uint16_t> indices);

		void SwapData(VulkanMesh& other) noexcept { m_Data.swap(other.m_Data); }

		const std::shared_ptr<const VulkanMeshData>& GetData() const noexcept { return m_Data; }

		VulkanMesh(const VulkanMesh&) = delete;
		VulkanMesh& operator=(const VulkanMesh&) = delete;

	private:
		std::shared_ptr<const VulkanMeshData> m_Data;
	};
}
