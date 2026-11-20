#pragma once

#include <cstdint>

namespace Raven
{
	class VulkanContext;

	// Runtime identity scoped to one renderer; a reused slot has a new generation.
	class TextureHandle
	{
	public:
		constexpr TextureHandle() noexcept = default;
		constexpr bool IsNull() const noexcept { return m_Context == 0; }
		constexpr bool operator==(const TextureHandle&) const noexcept = default;

	private:
		friend class VulkanContext;
		constexpr TextureHandle(std::uint64_t context, std::uint64_t index, std::uint64_t generation) noexcept
			: m_Context(context), m_Index(index), m_Generation(generation) {}

		std::uint64_t m_Context = 0;
		std::uint64_t m_Index = 0;
		std::uint64_t m_Generation = 0;
	};
}
