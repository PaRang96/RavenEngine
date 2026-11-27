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
	class VulkanUploadBatch;

	// Device-local vertex and index pools shared by every lit mesh (docs/RendererDesign.md, R6).
	// Rendering thread only. Draws bind both pools once and select a mesh by its ranges.
	class VulkanMeshPool
	{
	public:
		struct Ranges
		{
			std::int32_t VertexOffset = 0;
			std::uint32_t VertexCount = 0;
			std::uint32_t FirstIndex = 0;
			std::uint32_t IndexCount = 0;
		};

		VulkanMeshPool(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice);

		// Copies through a staging buffer, recorded into the per-frame upload batch with no submission or
		// wait of its own (B0 contract 5; FD-22): a barrier orders the copy after earlier use of the pools,
		// the batch's closing barrier before later vertex input, and the batch keeps the staging buffer
		// and any outgrown pool buffer until the GPU has run it. A failure leaves the pools unchanged.
		Ranges Upload(VulkanUploadBatch& uploads, std::span<const ColorVertex> vertices,
			std::span<const std::uint32_t> indices);
		// Only once the GPU no longer reads the ranges, which retirement of the mesh data ensures (R3).
		void Free(const Ranges& ranges) noexcept;

		VkBuffer GetVertices() const noexcept { return m_Vertices.Buffer->Get(); }
		VkBuffer GetIndices() const noexcept { return m_Indices.Buffer->Get(); }

		VulkanMeshPool(const VulkanMeshPool&) = delete;
		VulkanMeshPool& operator=(const VulkanMeshPool&) = delete;

		// Elements of one pool, kept sorted by First with neighbors merged.
		struct Range
		{
			std::uint32_t First = 0;
			std::uint32_t Count = 0;
		};

		struct Pool
		{
			VkDeviceSize ElementSize = 0;
			VkBufferUsageFlags Usage = 0;
			std::uint32_t MaxElements = 0;
			std::shared_ptr<const VulkanBuffer> Buffer;
			std::uint32_t Capacity = 0;
			std::vector<Range> Free;
		};

	private:
		const VulkanDevice& m_DeviceOwner;
		const VulkanPhysicalDevice& m_PhysicalDevice;
		Pool m_Vertices;
		Pool m_Indices;
	};

	struct VulkanMeshData
	{
		VulkanMeshData(VulkanMeshPool& pool, VulkanUploadBatch& uploads,
			std::span<const ColorVertex> vertices, std::span<const std::uint32_t> indices);
		// Returns the ranges to the pool; the mesh data is retired until the GPU is done with it.
		~VulkanMeshData();

		VulkanMeshData(const VulkanMeshData&) = delete;
		VulkanMeshData& operator=(const VulkanMeshData&) = delete;

	private:
		VulkanMeshPool& m_Pool;

	public:
		// CPU triangle positions share the exact replacement/retirement lifetime of GPU data.
		std::vector<Vec3> PickVertices;
		std::vector<std::uint32_t> PickIndices;
		MeshBounds LocalBounds;
		VulkanMeshPool::Ranges Ranges;
	};

	class VulkanMesh
	{
	public:
		VulkanMesh(VulkanMeshPool& pool, VulkanUploadBatch& uploads,
			std::span<const ColorVertex> vertices, std::span<const std::uint32_t> indices);

		void SwapData(VulkanMesh& other) noexcept { m_Data.swap(other.m_Data); }

		const std::shared_ptr<const VulkanMeshData>& GetData() const noexcept { return m_Data; }

		VulkanMesh(const VulkanMesh&) = delete;
		VulkanMesh& operator=(const VulkanMesh&) = delete;

	private:
		std::shared_ptr<const VulkanMeshData> m_Data;
	};
}
