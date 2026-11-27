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
			Objects = 0, Visible = 1, Materials = 2, Meshes = 3, Commands = 4, ObjectBytes = 5, Upload = 6,
			MaterialValues = 7, Count = 8
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
			// Material shaders read their records and values (B0 contract 1).
			bindings[Materials].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[MaterialValues].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
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
			{ set, Materials, sizeof(GpuMaterialData), { offsetof(GpuMaterialData, BaseColor), offsetof(GpuMaterialData, Textures),
				offsetof(GpuMaterialData, Samplers), offsetof(GpuMaterialData, Flags), offsetof(GpuMaterialData, ValueOffset),
				offsetof(GpuMaterialData, ValueCount), offsetof(GpuMaterialData, Padding) } },
			{ set, Meshes, sizeof(MeshEntry), { offsetof(MeshEntry, IndexCount), offsetof(MeshEntry, FirstIndex),
				offsetof(MeshEntry, VertexOffset), offsetof(MeshEntry, CommandIndex), offsetof(MeshEntry, Center),
				offsetof(MeshEntry, HalfExtent) } },
			{ set, Commands, sizeof(Command), { offsetof(Command, indexCount), offsetof(Command, instanceCount),
				offsetof(Command, firstIndex), offsetof(Command, vertexOffset), offsetof(Command, firstInstance) } },
			// Material values are float4 elements (B0 contract 1).
			{ set, MaterialValues, sizeof(Vec4), {}, true },
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

	VkDeviceSize VulkanGpuScene::Plan(RenderWorld::State* world, const MeshResolver& resolveMesh, std::uint64_t meshRevision)
	{
		m_World = world;
		m_DirtySlots.clear();
		m_DirtyRecords.clear();
		m_ObjectCount = world ? static_cast<std::uint32_t>(world->Objects.size()) : 0;
		m_GroupSlots = world ? static_cast<std::uint32_t>(world->Groups.size()) : 0;
		m_MaterialCount = world ? static_cast<std::uint32_t>(world->Materials.size()) : 0;
		m_MeshRevision = meshRevision;
		m_UploadMeshes = false;
		m_UploadTemplates = false;
		m_PlannedBytes = 0;
		if (!world)
		{
			m_CommandCount = 0;
			return 0;
		}
		m_Full = m_PendingFull || world != m_UploadedWorld || world->Generation != m_UploadedGeneration ||
			VkDeviceSize{ m_ObjectCount } * sizeof(GpuObjectData) > m_Objects.Capacity;
		// The material table uploads whole when the buffer is new or must grow, and otherwise only its
		// changed slots (B0 contract 6).
		m_AllMaterials = m_Full || m_PendingMaterials ||
			VkDeviceSize{ m_MaterialCount } * sizeof(GpuMaterialData) > m_Materials.Capacity;
		m_MaterialSlots.clear();
		m_MaterialRecords.clear();
		// The values are small, so any change uploads them whole.
		m_UploadValues = m_Full || world->ValuesRevision != m_UploadedValuesRevision ||
			VkDeviceSize{ world->Values.size() } * sizeof(Vec4) > m_Values.Capacity;
		if (!m_AllMaterials)
			for (const auto slot : world->DirtyMaterials)
				if (slot < m_MaterialCount)
				{
					m_MaterialSlots.push_back(slot);
					m_MaterialRecords.push_back(world->Materials[slot]);
				}

		// The mesh table and the command order follow the groups (B0 contract 3): live groups get
		// commands, contiguous per pipeline, and each table entry names its command.
		m_UploadMeshes = m_Full || world->LayoutRevision != m_UploadedLayout || meshRevision != m_UploadedMeshRevision ||
			VkDeviceSize{ m_GroupSlots } * sizeof(MeshEntry) > m_Meshes.Capacity;
		if (m_UploadMeshes)
		{
			m_CommandGroups.clear();
			for (std::uint32_t group = 0; group < m_GroupSlots; ++group)
				if (world->Groups[group].Live)
					m_CommandGroups.push_back(group);
			std::stable_sort(m_CommandGroups.begin(), m_CommandGroups.end(), [world](std::uint32_t left, std::uint32_t right)
			{
				return world->Groups[left].Key.Pipeline < world->Groups[right].Key.Pipeline;
			});
			m_Ranges.clear();
			m_MeshTable.assign(m_GroupSlots, MeshEntry{});
			for (std::uint32_t command = 0; command < m_CommandGroups.size(); ++command)
			{
				const auto& group = world->Groups[m_CommandGroups[command]];
				if (m_Ranges.empty() || !(m_Ranges.back().Pipeline == group.Key.Pipeline))
					m_Ranges.push_back({ group.Key.Pipeline, command, 0 });
				++m_Ranges.back().Count;
				// Released or replaced mesh data is retired, so these ranges outlive the frame (R3).
				auto& entry = m_MeshTable[m_CommandGroups[command]];
				entry.CommandIndex = command;
				if (const auto* mesh = resolveMesh ? resolveMesh(group.Key.Mesh) : nullptr)
				{
					entry.IndexCount = mesh->Ranges.IndexCount;
					entry.FirstIndex = mesh->Ranges.FirstIndex;
					entry.VertexOffset = mesh->Ranges.VertexOffset;
					entry.Center = { mesh->LocalBounds.Center.X, mesh->LocalBounds.Center.Y, mesh->LocalBounds.Center.Z, 0.0f };
					entry.HalfExtent = { mesh->LocalBounds.HalfExtent.X, mesh->LocalBounds.HalfExtent.Y,
						mesh->LocalBounds.HalfExtent.Z, 0.0f };
				}
			}
		}
		m_CommandCount = static_cast<std::uint32_t>(m_CommandGroups.size());
		// Commands take consecutive instance regions sized by their groups' counts (G5).
		m_UploadTemplates = m_UploadMeshes || world->CountRevision != m_UploadedCounts ||
			VkDeviceSize{ m_CommandCount } * sizeof(VkDrawIndexedIndirectCommand) > m_Templates.Capacity;
		if (m_UploadTemplates)
		{
			m_TemplateRecords.clear();
			std::uint32_t first = 0;
			for (const auto group : m_CommandGroups)
			{
				const auto& entry = m_MeshTable[group];
				m_TemplateRecords.push_back({ entry.IndexCount, 0, entry.FirstIndex, entry.VertexOffset, first });
				first += world->Groups[group].Count;
			}
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
		VkDeviceSize bytes = align((m_AllMaterials ? VkDeviceSize{ m_MaterialCount } : m_MaterialRecords.size()) *
			sizeof(GpuMaterialData));
		if (m_UploadValues)
			bytes += align(world->Values.size() * sizeof(Vec4));
		if (m_UploadMeshes)
			bytes += align(m_MeshTable.size() * sizeof(MeshEntry));
		if (m_UploadTemplates)
			bytes += align(m_TemplateRecords.size() * sizeof(VkDrawIndexedIndirectCommand));
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
		if (Reserve(m_Values, VkDeviceSize{ m_World->Values.size() } * sizeof(Vec4), 0))
			m_UploadedValuesRevision = ~0ull;
		Reserve(m_Commands, VkDeviceSize{ m_CommandCount } * sizeof(VkDrawIndexedIndirectCommand),
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		// Plan uploads these whenever they must grow; a replaced one is forgotten until committed.
		if (Reserve(m_Meshes, VkDeviceSize{ m_GroupSlots } * sizeof(MeshEntry), 0))
			m_UploadedLayout = ~0ull;
		if (Reserve(m_Templates, VkDeviceSize{ m_CommandCount } * sizeof(VkDrawIndexedIndirectCommand),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
			m_UploadedCounts = ~0ull;
		// Plan chose a full upload whenever these must grow.
		if (Reserve(m_Objects, VkDeviceSize{ m_ObjectCount } * sizeof(GpuObjectData), 0))
			m_PendingFull = true;
		if (Reserve(m_Materials, VkDeviceSize{ m_MaterialCount } * sizeof(GpuMaterialData), 0))
			m_PendingMaterials = true;
		const VkDeviceSize readbackBytes = std::max<VkDeviceSize>(256,
			VkDeviceSize{ m_CommandCount } * sizeof(VkDrawIndexedIndirectCommand));
		if (!slot.Readback || slot.Readback->GetSize() < readbackBytes)
		{
			// The slot's previous frame has completed, so its readback buffer is idle.
			slot.Readback = std::make_shared<const VulkanBuffer>(m_DeviceOwner, m_PhysicalDevice, readbackBytes * 2,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanBuffer::Mapping::Persistent);
		}

		slot.MeshOffset = m_UploadMeshes ? ring.Push(Bytes(m_MeshTable)) : 0;
		slot.TemplateOffset = m_UploadTemplates ? ring.Push(Bytes(m_TemplateRecords)) : 0;
		auto* world = m_World;
		slot.ValuesOffset = m_UploadValues ? ring.Push(std::span(reinterpret_cast<const std::byte*>(world->Values.data()),
			world->Values.size() * sizeof(Vec4))) : 0;
		slot.MaterialOffset = m_AllMaterials ? ring.Push(std::span(reinterpret_cast<const std::byte*>(world->Materials.data()),
			std::size_t{ m_MaterialCount } * sizeof(GpuMaterialData))) : ring.Push(Bytes(m_MaterialRecords));
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
			VkDescriptorBufferInfo{ slot.Ring, 0, VK_WHOLE_SIZE },
			VkDescriptorBufferInfo{ m_Values.Buffer->Get(), 0, VK_WHOLE_SIZE }
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
		if (!m_World || m_CommandCount == 0)
			return;
		const auto& slot = m_Slots.at(slotIndex);
		const auto& vk = m_DeviceOwner.GetCommands();
		// Earlier frames on this queue may still read or write these buffers.
		Barrier(vk, commands, GeometryStages | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_COPY_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR | VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COPY_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_TRANSFER_READ_BIT_KHR | VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR |
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR);
		const auto copy = [commands, &slot](VkDeviceSize offset, const Storage& storage, VkDeviceSize target, VkDeviceSize bytes)
		{
			if (bytes == 0)
				return;
			const VkBufferCopy region{ offset, target, bytes };
			vkCmdCopyBuffer(commands, slot.Ring, storage.Buffer->Get(), 1, &region);
		};
		const VkDeviceSize commandBytes = VkDeviceSize{ m_CommandCount } * sizeof(VkDrawIndexedIndirectCommand);
		if (m_UploadMeshes)
			copy(slot.MeshOffset, m_Meshes, 0, m_MeshTable.size() * sizeof(MeshEntry));
		if (m_UploadTemplates)
		{
			// New templates reset the commands straight from the ring and are kept for later frames.
			copy(slot.TemplateOffset, m_Templates, 0, commandBytes);
			copy(slot.TemplateOffset, m_Commands, 0, commandBytes);
		}
		else
		{
			// Cull accumulates instance counts, so the commands start from the templates every frame.
			const VkBufferCopy region{ 0, 0, commandBytes };
			vkCmdCopyBuffer(commands, m_Templates.Buffer->Get(), m_Commands.Buffer->Get(), 1, &region);
		}
		if (m_UploadValues)
			copy(slot.ValuesOffset, m_Values, 0, VkDeviceSize{ m_World->Values.size() } * sizeof(Vec4));
		if (m_AllMaterials)
			copy(slot.MaterialOffset, m_Materials, 0, VkDeviceSize{ m_MaterialCount } * sizeof(GpuMaterialData));
		else if (!m_MaterialSlots.empty())
		{
			// A changed material copies its one record (B0 contract 6).
			std::vector<VkBufferCopy> regions;
			regions.reserve(m_MaterialSlots.size());
			for (std::size_t index = 0; index < m_MaterialSlots.size(); ++index)
				regions.push_back({ slot.MaterialOffset + index * sizeof(GpuMaterialData),
					VkDeviceSize{ m_MaterialSlots[index] } * sizeof(GpuMaterialData), sizeof(GpuMaterialData) });
			vkCmdCopyBuffer(commands, slot.Ring, m_Materials.Buffer->Get(), static_cast<std::uint32_t>(regions.size()),
				regions.data());
		}
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

	void VulkanGpuScene::RecordDraw(VkCommandBuffer commands, std::size_t slotIndex, const PipelineFor& pipelineFor,
		VkDescriptorSet textures, VkDescriptorSet frame, VkBuffer vertices, VkBuffer indices) const
	{
		if (!m_World || m_CommandCount == 0)
			return;
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(commands, 0, 1, &vertices, &offset);
		vkCmdBindIndexBuffer(commands, indices, 0, VK_INDEX_TYPE_UINT32);
		// One command per group, one draw per pipeline range; groups with nothing visible have zero
		// instances (Q4). Every lit pipeline shares one layout, so the sets bind with each pipeline.
		const VulkanColorPipeline* bound = nullptr;
		for (const auto& range : m_Ranges)
		{
			const auto& pipeline = pipelineFor(range.Pipeline);
			if (&pipeline != bound)
			{
				pipeline.Bind(commands, textures);
				pipeline.BindFrame(commands, frame);
				pipeline.BindScene(commands, m_Slots.at(slotIndex).Set);
				bound = &pipeline;
			}
			vkCmdDrawIndexedIndirect(commands, m_Commands.Buffer->Get(),
				VkDeviceSize{ range.First } * sizeof(VkDrawIndexedIndirectCommand), range.Count,
				sizeof(VkDrawIndexedIndirectCommand));
		}
	}

	void VulkanGpuScene::RecordReadback(VkCommandBuffer commands, std::size_t slotIndex) const
	{
		if (!m_World || m_CommandCount == 0)
			return;
		const auto& slot = m_Slots.at(slotIndex);
		const VkBufferCopy region{ 0, 0, VkDeviceSize{ m_CommandCount } * sizeof(VkDrawIndexedIndirectCommand) };
		vkCmdCopyBuffer(commands, m_Commands.Buffer->Get(), slot.Readback->Get(), 1, &region);
		Barrier(m_DeviceOwner.GetCommands(), commands, VK_PIPELINE_STAGE_2_COPY_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_READ_BIT_KHR);
	}

	void VulkanGpuScene::Commit(std::size_t slotIndex) noexcept
	{
		if (!m_World)
			return;
		m_Slots.at(slotIndex).ReadbackGroups = m_CommandCount;
		m_UploadedWorld = m_World;
		m_UploadedGeneration = m_World->Generation;
		// Only a frame that recorded its copies holds them; one without commands copied nothing.
		if (m_CommandCount != 0)
		{
			if (m_UploadValues)
				m_UploadedValuesRevision = m_World->ValuesRevision;
			m_PendingMaterials = false;
			m_UploadedLayout = m_World->LayoutRevision;
			m_UploadedCounts = m_World->CountRevision;
			m_UploadedMeshRevision = m_MeshRevision;
		}
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
