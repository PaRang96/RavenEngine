#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace Raven
{
	class VulkanContext;
	class MeshHandle;
}

template<>
struct std::hash<Raven::MeshHandle>
{
	std::size_t operator()(const Raven::MeshHandle& handle) const noexcept;
};

namespace Raven
{

	// Runtime identity scoped to one renderer; a reused slot has a new generation.
	class MeshHandle
	{
	public:
		constexpr MeshHandle() noexcept = default;
		constexpr bool IsNull() const noexcept { return m_Context == 0; }
		constexpr bool operator==(const MeshHandle&) const noexcept = default;

	private:
		friend class VulkanContext;
		friend struct std::hash<MeshHandle>;
		constexpr MeshHandle(std::uint64_t context, std::uint64_t index, std::uint64_t generation) noexcept
			: m_Context(context), m_Index(index), m_Generation(generation) {}

		std::uint64_t m_Context = 0;
		std::uint64_t m_Index = 0;
		std::uint64_t m_Generation = 0;
	};
}

inline std::size_t std::hash<Raven::MeshHandle>::operator()(const Raven::MeshHandle& handle) const noexcept
{
	// The index varies most; the generation and context separate reused slots and renderers.
	const auto mixed = handle.m_Index * 0x9e3779b97f4a7c15ull ^ handle.m_Generation * 0xc2b2ae3d27d4eb4full ^
		handle.m_Context;
	return static_cast<std::size_t>(mixed ^ (mixed >> 29));
}
