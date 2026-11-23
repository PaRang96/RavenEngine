#include "Renderer/Vulkan/VulkanGpuScene.hpp"

#include "Renderer/Visibility.hpp"
#include "Renderer/Vulkan/VulkanColorPipeline.hpp"
#include "Renderer/Vulkan/VulkanFrameData.hpp"
#include "Renderer/Vulkan/VulkanFrameTimeline.hpp"
#include "Renderer/Vulkan/VulkanMesh.hpp"
#include "Renderer/Vulkan/VulkanShaderModule.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		// Bindings of the scene set; Shaders/GpuScene.hlsli names them.
		enum Binding : std::uint32_t
		{
			Objects = 0, Visible = 1, Materials = 2, Meshes = 3, Commands = 4, ObjectBytes = 5, Upload = 6, Count = 7
		};

		struct CullConstants
		{
			std::array<std::array<float, 4>, 6> Planes;
			std::uint32_t ObjectCount;
			std::uint32_t Padding[3];
		};

		struct ScatterConstants
		{
			std::uint32_t SlotOffset;
			std::uint32_t RecordOffset;
			std::uint32_t RecordCount;
			std::uint32_t Padding;
		};

		static_assert(sizeof(CullConstants) == 112);
		static_assert(sizeof(VkDrawIndexedIndirectCommand) == 20);

		constexpr VkPipelineStageFlags2KHR GeometryStages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR |
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR;

		void CheckSceneResult(VkResult result, const char* operation)
		{
			CheckVulkanResult(result, operation);
		}

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

		VkPipeline CreateCompute(VkDevice device, const VulkanDevice& owner, VkPipelineLayout layout,
			std::span<const std::uint32_t> words, const char* name)
		{
			const VulkanShaderModule module(owner, words);
			VkComputePipelineCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			info.stage.module = module.Get();
			info.stage.pName = "CSMain";
			info.layout = layout;
			VkPipeline pipeline = VK_NULL_HANDLE;
			const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
			if (result != VK_SUCCESS)
			{
				if (pipeline != VK_NULL_HANDLE)
					vkDestroyPipeline(device, pipeline, nullptr);
				CheckSceneResult(result, name);
			}
			return pipeline;
		}

		template<typename T>
		std::span<const std::byte> Bytes(const std::vector<T>& values, std::size_t first = 0)
		{
			return std::as_bytes(std::span(values).subspan(first));
		}
	}

	VulkanGpuScene::VulkanGpuScene(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
		VulkanFrameTimeline& timeline, std::span<const std::uint32_t> scatterWords,
		std::span<const std::uint32_t> cullWords)
		: m_DeviceOwner(device), m_PhysicalDevice(physicalDevice), m_Timeline(timeline), m_Device(device.Get())
	{
		try
		{
			std::array<VkDescriptorSetLayoutBinding, Count> bindings{};
			for (std::uint32_t binding = 0; binding < Count; ++binding)
			{
				bindings[binding].binding = binding;
				bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				bindings[binding].descriptorCount = 1;
				bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			}
			bindings[Objects].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
			bindings[Visible].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
			bindings[Materials].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = Count;
			layoutInfo.pBindings = bindings.data();
			CheckSceneResult(vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_Layout),
				"vkCreateDescriptorSetLayout (GPU scene)");

			const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Count * FrameSlots };
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.maxSets = FrameSlots;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &size;
			CheckSceneResult(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_Pool),
				"vkCreateDescriptorPool (GPU scene)");
			std::array<VkDescriptorSetLayout, FrameSlots> layouts{};
			layouts.fill(m_Layout);
			std::array<VkDescriptorSet, FrameSlots> sets{};
			VkDescriptorSetAllocateInfo allocation{};
			allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocation.descriptorPool = m_Pool;
			allocation.descriptorSetCount = FrameSlots;
			allocation.pSetLayouts = layouts.data();
			CheckSceneResult(vkAllocateDescriptorSets(m_Device, &allocation, sets.data()),
				"vkAllocateDescriptorSets (GPU scene)");
			for (std::size_t slot = 0; slot < FrameSlots; ++slot)
				m_Slots[slot].Set = sets[slot];

			const VkPushConstantRange constants{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullConstants) };
			VkPipelineLayoutCreateInfo pipelineLayout{};
			pipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayout.setLayoutCount = 1;
			pipelineLayout.pSetLayouts = &m_Layout;
			pipelineLayout.pushConstantRangeCount = 1;
			pipelineLayout.pPushConstantRanges = &constants;
			CheckSceneResult(vkCreatePipelineLayout(m_Device, &pipelineLayout, nullptr, &m_PipelineLayout),
				"vkCreatePipelineLayout (GPU scene)");
			m_Pipelines = CreatePipelines(scatterWords, cullWords);
		}
		catch (...)
		{
			Destroy();
			throw;
		}
	}

	VulkanGpuScene::~VulkanGpuScene()
	{
		Destroy();
	}

	struct VulkanGpuScene::ComputePipelines
	{
		VkDevice Device = VK_NULL_HANDLE;
		VkPipeline Scatter = VK_NULL_HANDLE;
		VkPipeline Cull = VK_NULL_HANDLE;

		~ComputePipelines()
		{
			if (Cull != VK_NULL_HANDLE)
				vkDestroyPipeline(Device, Cull, nullptr);
			if (Scatter != VK_NULL_HANDLE)
				vkDestroyPipeline(Device, Scatter, nullptr);
		}
	};

	std::shared_ptr<const VulkanGpuScene::ComputePipelines> VulkanGpuScene::CreatePipelines(
		std::span<const std::uint32_t> scatterWords, std::span<const std::uint32_t> cullWords) const
	{
		auto pipelines = std::make_shared<ComputePipelines>();
		pipelines->Device = m_Device;
		pipelines->Scatter = CreateCompute(m_Device, m_DeviceOwner, m_PipelineLayout, scatterWords,
			"vkCreateComputePipelines (scatter)");
		pipelines->Cull = CreateCompute(m_Device, m_DeviceOwner, m_PipelineLayout, cullWords,
			"vkCreateComputePipelines (cull)");
		return pipelines;
	}

	void VulkanGpuScene::ReplacePipelines(std::span<const std::uint32_t> scatterWords,
		std::span<const std::uint32_t> cullWords)
	{
		auto next = CreatePipelines(scatterWords, cullWords);
		m_Timeline.Retire(std::move(m_Pipelines));
		m_Pipelines = std::move(next);
	}

	std::vector<ShaderBufferLayout> VulkanGpuScene::GetBufferLayouts(std::uint32_t set)
	{
		using Command = VkDrawIndexedIndirectCommand;
		return {
			{ set, Objects, sizeof(GpuObjectData), { offsetof(GpuObjectData, WorldRows), offsetof(GpuObjectData, NormalToWorld),
				offsetof(GpuObjectData, Tint), offsetof(GpuObjectData, Group), offsetof(GpuObjectData, Material),
				offsetof(GpuObjectData, Flags), offsetof(GpuObjectData, Padding) } },
			{ set, Visible, sizeof(std::uint32_t), {} },
			{ set, Materials, sizeof(GpuMaterialData), { offsetof(GpuMaterialData, BaseColor), offsetof(GpuMaterialData, Texture),
				offsetof(GpuMaterialData, Lit), offsetof(GpuMaterialData, Padding) } },
			{ set, Meshes, sizeof(MeshEntry), { offsetof(MeshEntry, IndexCount), offsetof(MeshEntry, FirstIndex),
				offsetof(MeshEntry, VertexOffset), offsetof(MeshEntry, Padding), offsetof(MeshEntry, Center),
				offsetof(MeshEntry, HalfExtent) } },
			{ set, Commands, sizeof(Command), { offsetof(Command, indexCount), offsetof(Command, instanceCount),
				offsetof(Command, firstIndex), offsetof(Command, vertexOffset), offsetof(Command, firstInstance) } },
			// Byte-address buffers: runtime arrays of uint.
			{ set, ObjectBytes, sizeof(std::uint32_t), {} },
			{ set, Binding::Upload, sizeof(std::uint32_t), {} },
		};
	}

	ShaderInterface VulkanGpuScene::GetScatterInterface()
	{
		ShaderInterface result;
		result.Kind = ShaderInterface::Stage::Compute;
		result.Buffers = GetBufferLayouts(0);
		result.PushConstantOffsets = { offsetof(ScatterConstants, SlotOffset), offsetof(ScatterConstants, RecordOffset),
			offsetof(ScatterConstants, RecordCount), offsetof(ScatterConstants, Padding) };
		// RecordPrepare dispatches groups of 64 threads.
		result.LocalSize = { 64, 1, 1 };
		return result;
	}

	ShaderInterface VulkanGpuScene::GetCullInterface()
	{
		ShaderInterface result;
		result.Kind = ShaderInterface::Stage::Compute;
		result.Buffers = GetBufferLayouts(0);
		result.PushConstantOffsets = { offsetof(CullConstants, Planes), offsetof(CullConstants, ObjectCount),
			offsetof(CullConstants, Padding) };
		result.LocalSize = { 64, 1, 1 };
		return result;
	}

	void VulkanGpuScene::Destroy() noexcept
	{
		m_Pipelines.reset();
		if (m_PipelineLayout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
		if (m_Pool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
		if (m_Layout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
		m_PipelineLayout = VK_NULL_HANDLE;
		m_Pool = VK_NULL_HANDLE;
		m_Layout = VK_NULL_HANDLE;
	}

	bool VulkanGpuScene::Reserve(Storage& storage, VkDeviceSize bytes, VkBufferUsageFlags usage)
	{
		bytes = std::max<VkDeviceSize>(bytes, 256);
		if (storage.Buffer && storage.Capacity >= bytes)
			return false;
		const auto capacity = std::max(bytes, storage.Capacity * 2);
		auto replacement = std::make_shared<const VulkanBuffer>(m_DeviceOwner, m_PhysicalDevice, capacity,
			usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VulkanBuffer::Mapping::DeviceLocal);
		// Submitted frames may still read the previous buffer (R3).
		if (storage.Buffer)
			m_Timeline.Retire(std::move(storage.Buffer));
		storage.Buffer = std::move(replacement);
		storage.Capacity = capacity;
		return true;
	}

	VkDeviceSize VulkanGpuScene::Plan(RenderWorld::State* world, const MeshResolver& resolveMesh)
	{
		m_World = world;
		m_MeshTable.clear();
		m_Templates.clear();
		m_DirtySlots.clear();
		m_DirtyRecords.clear();
		m_ObjectCount = world ? static_cast<std::uint32_t>(world->Objects.size()) : 0;
		m_GroupCount = world ? static_cast<std::uint32_t>(world->Groups.size()) : 0;
		m_MaterialCount = world ? static_cast<std::uint32_t>(world->Materials.size()) : 0;
		m_PlannedBytes = 0;
		if (!world)
			return 0;
		m_Full = m_PendingFull || world != m_UploadedWorld || world->Generation != m_UploadedGeneration ||
			VkDeviceSize{ m_ObjectCount } * sizeof(GpuObjectData) > m_Objects.Capacity;
		const bool allMaterials = m_Full || VkDeviceSize{ m_MaterialCount } * sizeof(GpuMaterialData) > m_Materials.Capacity;
		m_FirstMaterial = allMaterials ? 0 : std::min(m_UploadedMaterials, m_MaterialCount);

		// Draw groups take consecutive instance regions sized by their object counts (G5).
		std::uint32_t first = 0;
		m_MeshTable.reserve(m_GroupCount);
		m_Templates.reserve(m_GroupCount);
		for (std::uint32_t group = 0; group < m_GroupCount; ++group)
		{
			// Released or replaced mesh data is retired, so these ranges outlive the frame (R3).
			const auto* mesh = resolveMesh ? resolveMesh(world->Groups[group]) : nullptr;
			MeshEntry entry;
			VkDrawIndexedIndirectCommand command{ 0, 0, 0, 0, first };
			if (mesh)
			{
				entry = { mesh->Ranges.IndexCount, mesh->Ranges.FirstIndex, mesh->Ranges.VertexOffset, 0,
					{ mesh->LocalBounds.Center.X, mesh->LocalBounds.Center.Y, mesh->LocalBounds.Center.Z, 0.0f },
					{ mesh->LocalBounds.HalfExtent.X, mesh->LocalBounds.HalfExtent.Y, mesh->LocalBounds.HalfExtent.Z, 0.0f } };
				command.indexCount = mesh->Ranges.IndexCount;
				command.firstIndex = mesh->Ranges.FirstIndex;
				command.vertexOffset = mesh->Ranges.VertexOffset;
			}
			m_MeshTable.push_back(entry);
			m_Templates.push_back(command);
			first += world->GroupCounts[group];
		}
		if (!m_Full)
		{
			m_DirtySlots.reserve(world->Dirty.size());
			m_DirtyRecords.reserve(world->Dirty.size());
			for (const auto slot : world->Dirty)
				if (slot < m_ObjectCount)
				{
					m_DirtySlots.push_back(slot);
					m_DirtyRecords.push_back(world->Objects[slot]);
				}
		}
		const auto align = VulkanUploadRing::Align;
		VkDeviceSize bytes = align(m_MeshTable.size() * sizeof(MeshEntry)) +
			align(m_Templates.size() * sizeof(VkDrawIndexedIndirectCommand)) +
			align(VkDeviceSize{ m_MaterialCount - m_FirstMaterial } * sizeof(GpuMaterialData));
		if (m_Full)
			bytes += align(VkDeviceSize{ m_ObjectCount } * sizeof(GpuObjectData));
		else
			bytes += align(m_DirtySlots.size() * sizeof(std::uint32_t)) + align(m_DirtyRecords.size() * sizeof(GpuObjectData));
		m_PlannedBytes = bytes;
		return bytes;
	}

	void VulkanGpuScene::Upload(std::size_t slotIndex, VulkanUploadRing& ring)
	{
		auto& slot = m_Slots.at(slotIndex);
		slot.ReadbackGroups = 0;
		if (!m_World)
			return;
		// Rebuilt every frame, so replacing them needs no copy.
		Reserve(m_Visible, VkDeviceSize{ m_ObjectCount } * sizeof(std::uint32_t), 0);
		Reserve(m_Meshes, VkDeviceSize{ m_GroupCount } * sizeof(MeshEntry), 0);
		Reserve(m_Commands, VkDeviceSize{ m_GroupCount } * sizeof(VkDrawIndexedIndirectCommand),
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		// Plan chose a full upload whenever these must grow.
		if (Reserve(m_Objects, VkDeviceSize{ m_ObjectCount } * sizeof(GpuObjectData), 0))
			m_PendingFull = true;
		if (Reserve(m_Materials, VkDeviceSize{ m_MaterialCount } * sizeof(GpuMaterialData), 0))
			m_UploadedMaterials = 0;
		const VkDeviceSize readbackBytes = std::max<VkDeviceSize>(256,
			VkDeviceSize{ m_GroupCount } * sizeof(VkDrawIndexedIndirectCommand));
		if (!slot.Readback || slot.Readback->GetSize() < readbackBytes)
		{
			// The slot's previous frame has completed, so its readback buffer is idle.
			slot.Readback = std::make_shared<const VulkanBuffer>(m_DeviceOwner, m_PhysicalDevice, readbackBytes * 2,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanBuffer::Mapping::Persistent);
		}

		slot.MeshOffset = ring.Push(Bytes(m_MeshTable));
		slot.TemplateOffset = ring.Push(Bytes(m_Templates));
		auto* world = m_World;
		const auto* worldBegin = reinterpret_cast<const std::byte*>(world->Materials.data());
		slot.MaterialOffset = ring.Push(std::span(worldBegin + m_FirstMaterial * sizeof(GpuMaterialData),
			(m_MaterialCount - m_FirstMaterial) * sizeof(GpuMaterialData)));
		if (m_Full)
		{
			slot.ObjectOffset = ring.Push(Bytes(world->Objects));
			slot.SlotOffset = 0;
		}
		else
		{
			slot.SlotOffset = ring.Push(Bytes(m_DirtySlots));
			slot.ObjectOffset = ring.Push(Bytes(m_DirtyRecords));
		}
		slot.Ring = ring.Get();

		// The slot's previous frame has completed, so its set is not in use.
		const std::array<VkDescriptorBufferInfo, Count> buffers
		{
			VkDescriptorBufferInfo{ m_Objects.Buffer->Get(), 0, VK_WHOLE_SIZE },
			VkDescriptorBufferInfo{ m_Visible.Buffer->Get(), 0, VK_WHOLE_SIZE },
			VkDescriptorBufferInfo{ m_Materials.Buffer->Get(), 0, VK_WHOLE_SIZE },
			VkDescriptorBufferInfo{ m_Meshes.Buffer->Get(), 0, VK_WHOLE_SIZE },
			VkDescriptorBufferInfo{ m_Commands.Buffer->Get(), 0, VK_WHOLE_SIZE },
			VkDescriptorBufferInfo{ m_Objects.Buffer->Get(), 0, VK_WHOLE_SIZE },
			VkDescriptorBufferInfo{ slot.Ring, 0, VK_WHOLE_SIZE }
		};
		std::array<VkWriteDescriptorSet, Count> writes{};
		for (std::uint32_t binding = 0; binding < Count; ++binding)
		{
			writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[binding].dstSet = slot.Set;
			writes[binding].dstBinding = binding;
			writes[binding].descriptorCount = 1;
			writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[binding].pBufferInfo = &buffers[binding];
		}
		vkUpdateDescriptorSets(m_Device, Count, writes.data(), 0, nullptr);
	}

	void VulkanGpuScene::RecordPrepare(VkCommandBuffer commands, std::size_t slotIndex, const Mat4& viewProjection) const
	{
		if (!m_World || m_GroupCount == 0)
			return;
		const auto& slot = m_Slots.at(slotIndex);
		const auto& vk = m_DeviceOwner.GetCommands();
		// Earlier frames on this queue may still read or write these buffers.
		Barrier(vk, commands, GeometryStages | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_COPY_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR | VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COPY_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR);
		const auto copy = [commands, &slot](VkDeviceSize offset, const Storage& storage, VkDeviceSize target, VkDeviceSize bytes)
		{
			if (bytes == 0)
				return;
			const VkBufferCopy region{ offset, target, bytes };
			vkCmdCopyBuffer(commands, slot.Ring, storage.Buffer->Get(), 1, &region);
		};
		copy(slot.MeshOffset, m_Meshes, 0, m_MeshTable.size() * sizeof(MeshEntry));
		copy(slot.TemplateOffset, m_Commands, 0, m_Templates.size() * sizeof(VkDrawIndexedIndirectCommand));
		copy(slot.MaterialOffset, m_Materials, VkDeviceSize{ m_FirstMaterial } * sizeof(GpuMaterialData),
			VkDeviceSize{ m_MaterialCount - m_FirstMaterial } * sizeof(GpuMaterialData));
		if (m_Full)
			copy(slot.ObjectOffset, m_Objects, 0, VkDeviceSize{ m_ObjectCount } * sizeof(GpuObjectData));

		vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, m_PipelineLayout, 0, 1, &slot.Set, 0, nullptr);
		if (!m_Full && !m_DirtySlots.empty())
		{
			const ScatterConstants scatter{ static_cast<std::uint32_t>(slot.SlotOffset),
				static_cast<std::uint32_t>(slot.ObjectOffset), static_cast<std::uint32_t>(m_DirtySlots.size()), 0 };
			vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipelines->Scatter);
			vkCmdPushConstants(commands, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(scatter), &scatter);
			// Eight threads per record, one per 16 bytes.
			vkCmdDispatch(commands, (static_cast<std::uint32_t>(m_DirtySlots.size()) * 8 + 63) / 64, 1, 1);
		}
		Barrier(vk, commands, VK_PIPELINE_STAGE_2_COPY_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR);

		if (m_ObjectCount != 0)
		{
			CullConstants cull{};
			const ViewFrustum frustum(viewProjection);
			const auto& planes = frustum.GetPlanes();
			for (std::size_t plane = 0; plane < planes.size(); ++plane)
				for (std::size_t component = 0; component < 4; ++component)
					cull.Planes[plane][component] = static_cast<float>(planes[plane][component]);
			cull.ObjectCount = m_ObjectCount;
			vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipelines->Cull);
			vkCmdPushConstants(commands, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(cull), &cull);
			vkCmdDispatch(commands, (m_ObjectCount + 63) / 64, 1, 1);
		}
		Barrier(vk, commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			GeometryStages | VK_PIPELINE_STAGE_2_COPY_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_TRANSFER_READ_BIT_KHR);
	}

	void VulkanGpuScene::RecordDraw(VkCommandBuffer commands, std::size_t slotIndex, const VulkanColorPipeline& pipeline,
		VkDescriptorSet textures, VkDescriptorSet frame, VkBuffer vertices, VkBuffer indices) const
	{
		if (!m_World || m_GroupCount == 0)
			return;
		pipeline.Bind(commands, textures);
		pipeline.BindFrame(commands, frame);
		pipeline.BindScene(commands, m_Slots.at(slotIndex).Set);
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(commands, 0, 1, &vertices, &offset);
		vkCmdBindIndexBuffer(commands, indices, 0, VK_INDEX_TYPE_UINT32);
		// One command per mesh; meshes with nothing visible have zero instances (Q4).
		vkCmdDrawIndexedIndirect(commands, m_Commands.Buffer->Get(), 0, m_GroupCount,
			sizeof(VkDrawIndexedIndirectCommand));
	}

	void VulkanGpuScene::RecordReadback(VkCommandBuffer commands, std::size_t slotIndex) const
	{
		if (!m_World || m_GroupCount == 0)
			return;
		const auto& slot = m_Slots.at(slotIndex);
		const VkBufferCopy region{ 0, 0, VkDeviceSize{ m_GroupCount } * sizeof(VkDrawIndexedIndirectCommand) };
		vkCmdCopyBuffer(commands, m_Commands.Buffer->Get(), slot.Readback->Get(), 1, &region);
		Barrier(m_DeviceOwner.GetCommands(), commands, VK_PIPELINE_STAGE_2_COPY_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_READ_BIT_KHR);
	}

	void VulkanGpuScene::Commit(std::size_t slotIndex) noexcept
	{
		if (!m_World)
			return;
		m_Slots.at(slotIndex).ReadbackGroups = m_GroupCount;
		m_UploadedWorld = m_World;
		m_UploadedGeneration = m_World->Generation;
		m_UploadedMaterials = m_MaterialCount;
		m_PendingFull = false;
		m_World->CommitChanges();
	}

	std::uint32_t VulkanGpuScene::ReadVisible(std::size_t slotIndex) const noexcept
	{
		const auto& slot = m_Slots.at(slotIndex);
		if (!slot.Readback || slot.ReadbackGroups == 0)
			return 0;
		std::uint32_t visible = 0;
		const auto* mapped = slot.Readback->GetMapped();
		for (std::uint32_t group = 0; group < slot.ReadbackGroups; ++group)
		{
			VkDrawIndexedIndirectCommand command;
			std::memcpy(&command, mapped + group * sizeof(command), sizeof(command));
			visible += command.instanceCount;
		}
		return visible;
	}
}
