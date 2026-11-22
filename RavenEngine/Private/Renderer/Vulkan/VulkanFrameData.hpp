#pragma once

#include "Raven/Math/Transform.hpp"
#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Renderer/Vulkan/VulkanFrameTimeline.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>

namespace Raven
{
	struct VulkanPhysicalDevice;

	// Camera and lighting written once per frame and read by the lit vertex shader at set 1
	// (docs/RendererDesign.md, R4). Matrices are column-major, matching float4[4] columns in HLSL.
	struct FrameUniformData
	{
		Mat4 ViewProjection;
		Mat4 View;
		Vec4 CameraPosition;
		Vec4 Ambient;
		Vec4 LightDirection;
		Vec4 LightColor;
	};

	static_assert(std::is_standard_layout_v<FrameUniformData>);
	static_assert(std::is_trivially_copyable_v<FrameUniformData>);
	static_assert(sizeof(FrameUniformData) == 192);
	static_assert(offsetof(FrameUniformData, View) == 64);
	static_assert(offsetof(FrameUniformData, CameraPosition) == 128);
	static_assert(offsetof(FrameUniformData, LightColor) == 176);

	// Set layout for the frame uniform. It outlives passes because retained pipelines use it.
	class VulkanFrameLayout
	{
	public:
		explicit VulkanFrameLayout(const VulkanDevice& device);
		~VulkanFrameLayout();

		VkDescriptorSetLayout Get() const noexcept { return m_Layout; }

		VulkanFrameLayout(const VulkanFrameLayout&) = delete;
		VulkanFrameLayout& operator=(const VulkanFrameLayout&) = delete;

	private:
		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
	};

	// One persistently mapped upload buffer per frame slot. Each frame bump-allocates its
	// uniform and instance data from the start; the slot wait covers the previous contents.
	class VulkanUploadRing
	{
	public:
		static constexpr VkDeviceSize Alignment = 16;
		static constexpr VkBufferUsageFlags Usage =
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

		static constexpr VkDeviceSize Align(VkDeviceSize value) noexcept
		{
			return (value + Alignment - 1) & ~(Alignment - 1);
		}

		// Starts a frame needing up to `required` bytes. Returns true when the buffer was
		// replaced; the old one is retired, and descriptors that name it must be rewritten.
		bool Begin(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			VulkanFrameTimeline& timeline, VkDeviceSize required);
		// Copies bytes at the next aligned offset and returns that offset.
		VkDeviceSize Push(std::span<const std::byte> bytes);

		VkBuffer Get() const noexcept { return m_Buffer ? m_Buffer->Get() : VK_NULL_HANDLE; }
		VkDeviceSize GetCapacity() const noexcept { return m_Buffer ? m_Buffer->GetSize() : 0; }

	private:
		std::shared_ptr<VulkanBuffer> m_Buffer;
		VkDeviceSize m_Offset = 0;
	};
}
