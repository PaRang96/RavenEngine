#include "Renderer/Vulkan/VulkanMesh.hpp"
#include "Core/DataError.hpp"

#include "Renderer/Vulkan/VulkanFrameTimeline.hpp"
#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{
	namespace
	{
		// Initial pool sizes cover the samples and the 4,000-mesh benchmark without growth.
		constexpr std::uint32_t InitialVertices = 1u << 16;
		constexpr std::uint32_t InitialIndices = 1u << 18;

		void CheckMeshResult(VkResult result, const char* operation)
		{
			CheckVulkanResult(result, operation);
		}

		std::span<const ColorVertex> ValidateMesh(std::span<const ColorVertex> vertices,
			std::span<const std::uint32_t> indices)
		{
			if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 ||
				indices.size() > std::numeric_limits<std::uint32_t>::max() ||
				vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
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

		std::vector<Vec3> PickPositions(std::span<const ColorVertex> vertices)
		{
			std::vector<Vec3> positions;
			positions.reserve(vertices.size());
			for (const auto& vertex : vertices)
				positions.push_back({ vertex.Position[0], vertex.Position[1], vertex.Position[2] });
			return positions;
		}

		std::shared_ptr<const VulkanBuffer> CreatePoolBuffer(const VulkanDevice& device,
			const VulkanPhysicalDevice& physicalDevice, const VulkanMeshPool::Pool& pool, std::uint32_t capacity)
		{
			return std::make_shared<const VulkanBuffer>(device, physicalDevice, pool.ElementSize * capacity,
				pool.Usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VulkanBuffer::Mapping::DeviceLocal);
		}

		VulkanMeshPool::Pool CreatePool(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			VkDeviceSize elementSize, VkBufferUsageFlags usage, std::uint32_t maxElements, std::uint32_t capacity)
		{
			VulkanMeshPool::Pool pool{ elementSize, usage, maxElements };
			pool.Buffer = CreatePoolBuffer(device, physicalDevice, pool, capacity);
			pool.Capacity = capacity;
			pool.Free.push_back({ 0, capacity });
			return pool;
		}

		std::optional<std::uint32_t> TakeFirstFit(VulkanMeshPool::Pool& pool, std::uint32_t count)
		{
			for (auto range = pool.Free.begin(); range != pool.Free.end(); ++range)
			{
				if (range->Count < count)
					continue;
				const auto first = range->First;
				range->First += count;
				range->Count -= count;
				if (range->Count == 0)
					pool.Free.erase(range);
				return first;
			}
			return std::nullopt;
		}

		void GiveBack(VulkanMeshPool::Pool& pool, VulkanMeshPool::Range freed)
		{
			auto next = std::lower_bound(pool.Free.begin(), pool.Free.end(), freed.First,
				[](const auto& range, std::uint32_t first) { return range.First < first; });
			if (next != pool.Free.begin())
			{
				auto previous = std::prev(next);
				if (previous->First + previous->Count == freed.First)
				{
					previous->Count += freed.Count;
					if (next != pool.Free.end() && previous->First + previous->Count == next->First)
					{
						previous->Count += next->Count;
						pool.Free.erase(next);
					}
					return;
				}
			}
			if (next != pool.Free.end() && freed.First + freed.Count == next->First)
			{
				next->First = freed.First;
				next->Count += freed.Count;
				return;
			}
			pool.Free.insert(next, freed);
		}

		// Allocates first fit, growing the pool when no range fits. A grown pool keeps the old buffer
		// in outgrown so the caller can copy its contents and retire it.
		std::uint32_t Allocate(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			VulkanMeshPool::Pool& pool, std::uint32_t count,
			std::shared_ptr<const VulkanBuffer>& outgrown, std::uint32_t& outgrownCapacity)
		{
			if (const auto first = TakeFirstFit(pool, count))
				return *first;
			if (count > pool.MaxElements - pool.Capacity)
				// Content can exceed it, so it is a data error: the mesh draws nothing (FD-06).
				throw DataError("Mesh pool cannot grow to fit the mesh");
			const auto capacity = static_cast<std::uint32_t>(std::min<std::uint64_t>(pool.MaxElements,
				std::max<std::uint64_t>(std::uint64_t{ pool.Capacity } * 2, std::uint64_t{ pool.Capacity } + count)));
			auto buffer = CreatePoolBuffer(device, physicalDevice, pool, capacity);
			outgrown = std::exchange(pool.Buffer, std::move(buffer));
			outgrownCapacity = pool.Capacity;
			GiveBack(pool, { pool.Capacity, capacity - pool.Capacity });
			pool.Capacity = capacity;
			const auto first = TakeFirstFit(pool, count);
			if (!first)
				throw std::logic_error("Grown mesh pool has no fitting range");
			return *first;
		}

		struct UploadCommands
		{
			UploadCommands(VkDevice device, VkCommandPool pool) : Device(device), Pool(pool)
			{
				VkCommandBufferAllocateInfo allocation{};
				allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				allocation.commandPool = pool;
				allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				allocation.commandBufferCount = 1;
				const VkResult result = vkAllocateCommandBuffers(device, &allocation, &Commands);
				if (result != VK_SUCCESS)
					Commands = VK_NULL_HANDLE;
				CheckMeshResult(result, "vkAllocateCommandBuffers (mesh upload)");
			}

			~UploadCommands()
			{
				if (Commands != VK_NULL_HANDLE)
					vkFreeCommandBuffers(Device, Pool, 1, &Commands);
			}

			UploadCommands(const UploadCommands&) = delete;
			UploadCommands& operator=(const UploadCommands&) = delete;

			VkDevice Device;
			VkCommandPool Pool;
			VkCommandBuffer Commands = VK_NULL_HANDLE;
		};

		void Barrier(const VulkanDeviceCommands& vk, VkCommandBuffer commands,
			VkPipelineStageFlags2KHR sourceStages, VkAccessFlags2KHR sourceAccess,
			VkPipelineStageFlags2KHR destinationStages, VkAccessFlags2KHR destinationAccess)
		{
			VkMemoryBarrier2KHR barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR;
			barrier.srcStageMask = sourceStages;
			barrier.srcAccessMask = sourceAccess;
			barrier.dstStageMask = destinationStages;
			barrier.dstAccessMask = destinationAccess;
			VkDependencyInfoKHR dependency{};
			dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
			dependency.memoryBarrierCount = 1;
			dependency.pMemoryBarriers = &barrier;
			vk.PipelineBarrier2(commands, &dependency);
		}
	}

	VulkanMeshPool::VulkanMeshPool(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice)
		: m_DeviceOwner(device), m_PhysicalDevice(physicalDevice)
	{
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		poolInfo.queueFamilyIndex = physicalDevice.QueueFamilies.Graphics.value();
		VkCommandPool commandPool = VK_NULL_HANDLE;
		CheckMeshResult(vkCreateCommandPool(device.Get(), &poolInfo, nullptr, &commandPool),
			"vkCreateCommandPool (mesh upload)");
		m_CommandPool = commandPool;
		try
		{
			// vkCmdDrawIndexed takes a signed vertex offset.
			m_Vertices = CreatePool(device, physicalDevice, sizeof(ColorVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()), InitialVertices);
			m_Indices = CreatePool(device, physicalDevice, sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				std::numeric_limits<std::uint32_t>::max(), InitialIndices);
		}
		catch (...)
		{
			vkDestroyCommandPool(device.Get(), m_CommandPool, nullptr);
			throw;
		}
	}

	VulkanMeshPool::~VulkanMeshPool()
	{
		vkDestroyCommandPool(m_DeviceOwner.Get(), m_CommandPool, nullptr);
	}

	VulkanMeshPool::Ranges VulkanMeshPool::Upload(VulkanFrameTimeline& timeline,
		std::span<const ColorVertex> vertices, std::span<const std::uint32_t> indices)
	{
		const auto device = m_DeviceOwner.Get();
		const auto vertexBytes = std::as_bytes(vertices);
		const auto indexBytes = std::as_bytes(indices);
		// Everything that can fail before submission runs before the pools change, or on copies.
		auto staging = std::make_shared<const VulkanBuffer>(m_DeviceOwner, m_PhysicalDevice,
			static_cast<VkDeviceSize>(vertexBytes.size() + indexBytes.size()), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VulkanBuffer::Mapping::Persistent);
		std::memcpy(staging->GetMapped(), vertexBytes.data(), vertexBytes.size());
		std::memcpy(staging->GetMapped() + vertexBytes.size(), indexBytes.data(), indexBytes.size());
		auto upload = std::make_shared<const UploadCommands>(device, m_CommandPool);
		const auto commands = upload->Commands;

		auto vertexPool = m_Vertices;
		auto indexPool = m_Indices;
		std::shared_ptr<const VulkanBuffer> outgrownVertices, outgrownIndices;
		std::uint32_t outgrownVertexCapacity = 0, outgrownIndexCapacity = 0;
		const auto vertexCount = static_cast<std::uint32_t>(vertices.size());
		const auto indexCount = static_cast<std::uint32_t>(indices.size());
		const auto firstVertex = Allocate(m_DeviceOwner, m_PhysicalDevice, vertexPool, vertexCount,
			outgrownVertices, outgrownVertexCapacity);
		const auto firstIndex = Allocate(m_DeviceOwner, m_PhysicalDevice, indexPool, indexCount,
			outgrownIndices, outgrownIndexCapacity);

		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CheckMeshResult(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer (mesh upload)");
		const auto& vk = m_DeviceOwner.GetCommands();
		constexpr VkPipelineStageFlags2KHR geometryInput =
			VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR;
		// Earlier uploads and frames may still write or read the pools, including freed ranges.
		Barrier(vk, commands, VK_PIPELINE_STAGE_2_COPY_BIT_KHR | geometryInput, VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COPY_BIT_KHR, VK_ACCESS_2_TRANSFER_READ_BIT_KHR | VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR);
		const auto copyOutgrown = [commands](const std::shared_ptr<const VulkanBuffer>& outgrown,
			const Pool& pool, std::uint32_t capacity)
		{
			if (!outgrown)
				return;
			const VkBufferCopy region{ 0, 0, pool.ElementSize * capacity };
			vkCmdCopyBuffer(commands, outgrown->Get(), pool.Buffer->Get(), 1, &region);
		};
		copyOutgrown(outgrownVertices, vertexPool, outgrownVertexCapacity);
		copyOutgrown(outgrownIndices, indexPool, outgrownIndexCapacity);
		// A grown pool's copied free space may overlap the new ranges.
		if (outgrownVertices || outgrownIndices)
			Barrier(vk, commands, VK_PIPELINE_STAGE_2_COPY_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
				VK_PIPELINE_STAGE_2_COPY_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR);
		const VkBufferCopy vertexCopy{ 0, vertexPool.ElementSize * firstVertex, vertexBytes.size() };
		vkCmdCopyBuffer(commands, staging->Get(), vertexPool.Buffer->Get(), 1, &vertexCopy);
		const VkBufferCopy indexCopy{ vertexBytes.size(), indexPool.ElementSize * firstIndex, indexBytes.size() };
		vkCmdCopyBuffer(commands, staging->Get(), indexPool.Buffer->Get(), 1, &indexCopy);
		Barrier(vk, commands, VK_PIPELINE_STAGE_2_COPY_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			geometryInput, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT_KHR | VK_ACCESS_2_INDEX_READ_BIT_KHR);
		CheckMeshResult(vkEndCommandBuffer(commands), "vkEndCommandBuffer (mesh upload)");

		VkSemaphoreSubmitInfoKHR signal{};
		signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
		signal.semaphore = timeline.Get();
		signal.value = timeline.GetNextValue();
		signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
		VkCommandBufferSubmitInfoKHR commandInfo{};
		commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
		commandInfo.commandBuffer = commands;
		VkSubmitInfo2KHR submission{};
		submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR;
		submission.commandBufferInfoCount = 1;
		submission.pCommandBufferInfos = &commandInfo;
		submission.signalSemaphoreInfoCount = 1;
		submission.pSignalSemaphoreInfos = &signal;
		CheckMeshResult(vk.QueueSubmit2(m_DeviceOwner.GetGraphicsQueue(), 1, &submission, VK_NULL_HANDLE),
			"vkQueueSubmit2KHR (mesh upload)");

		// Committed: nothing below throws.
		timeline.MarkSubmitted();
		m_Vertices = std::move(vertexPool);
		m_Indices = std::move(indexPool);
		timeline.Retire(std::move(staging));
		timeline.Retire(std::move(upload));
		if (outgrownVertices)
			timeline.Retire(std::move(outgrownVertices));
		if (outgrownIndices)
			timeline.Retire(std::move(outgrownIndices));
		return { static_cast<std::int32_t>(firstVertex), vertexCount, firstIndex, indexCount };
	}

	void VulkanMeshPool::Free(const Ranges& ranges) noexcept
	{
		try
		{
			GiveBack(m_Vertices, { static_cast<std::uint32_t>(ranges.VertexOffset), ranges.VertexCount });
			GiveBack(m_Indices, { ranges.FirstIndex, ranges.IndexCount });
		}
		catch (...)
		{
			// Only a failed free-list insertion gets here; the range stays unused until shutdown.
		}
	}

	VulkanMeshData::VulkanMeshData(VulkanMeshPool& pool, VulkanFrameTimeline& timeline,
		std::span<const ColorVertex> vertices, std::span<const std::uint32_t> indices)
		: m_Pool(pool),
		  PickVertices(PickPositions(ValidateMesh(vertices, indices))),
		  PickIndices(indices.begin(), indices.end()),
		  LocalBounds(CalculateMeshBounds(vertices, indices)),
		  Ranges(pool.Upload(timeline, vertices, indices))
	{
	}

	VulkanMeshData::~VulkanMeshData()
	{
		m_Pool.Free(Ranges);
	}

	VulkanMesh::VulkanMesh(VulkanMeshPool& pool, VulkanFrameTimeline& timeline,
		std::span<const ColorVertex> vertices, std::span<const std::uint32_t> indices)
		: m_Data(std::make_shared<const VulkanMeshData>(pool, timeline, vertices, indices))
	{
	}

	void VulkanMesh::Replace(VulkanMeshPool& pool, VulkanFrameTimeline& timeline,
		std::span<const ColorVertex> vertices, std::span<const std::uint32_t> indices)
	{
		m_Data = std::make_shared<const VulkanMeshData>(pool, timeline, vertices, indices);
	}
}
