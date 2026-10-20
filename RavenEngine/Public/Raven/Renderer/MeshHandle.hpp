#pragma once

#include <cstdint>

namespace Raven
{
	class VulkanContext;

	// A runtime color-mesh identity, scoped to one renderer. Slots are never reused.
	class MeshHandle
	{
	public:
		constexpr MeshHandle() noexcept = default;
		constexpr bool IsNull() const noexcept { return m_Context == 0; }
		constexpr bool operator==(const MeshHandle&) const noexcept = default;

	private:
		friend class VulkanContext;
		constexpr MeshHandle(std::uint64_t context, std::uint64_t index) noexcept
			: m_Context(context), m_Index(index) {}

		std::uint64_t m_Context = 0;
		std::uint64_t m_Index = 0;
	};
}
