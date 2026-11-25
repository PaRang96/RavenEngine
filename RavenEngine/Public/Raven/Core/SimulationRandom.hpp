#pragma once

#include <cstdint>

namespace Raven
{
	// The simulation's random numbers (docs/GameServicesDesign.md, FD-48). The engine seeds it when a
	// scene starts, so the same scene, seed and input stream replay the same fixed steps. Fixed-step
	// code uses it instead of std::random_device, rand() or the wall clock. Plain state, so a
	// snapshot of the world can carry it.
	class SimulationRandom
	{
	public:
		constexpr explicit SimulationRandom(std::uint64_t seed = 0) noexcept : m_State(seed) {}

		// SplitMix64.
		constexpr std::uint64_t NextU64() noexcept
		{
			std::uint64_t value = (m_State += 0x9E3779B97F4A7C15ull);
			value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
			value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
			return value ^ (value >> 31);
		}

		// Uniform in [0, 1), from the top 24 bits, so every platform gets the same float.
		constexpr float NextFloat() noexcept
		{
			return static_cast<float>(NextU64() >> 40) * (1.0f / 16777216.0f);
		}

		// Uniform in [low, high).
		constexpr float Range(float low, float high) noexcept
		{
			return low + (high - low) * NextFloat();
		}

		// Uniform in [low, high]; `low` when high <= low.
		constexpr std::int32_t RangeInt(std::int32_t low, std::int32_t high) noexcept
		{
			if (high <= low)
				return low;
			const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(high) - low) + 1;
			return static_cast<std::int32_t>(low + static_cast<std::int64_t>(NextU64() % span));
		}

		constexpr std::uint64_t GetState() const noexcept { return m_State; }
		constexpr void SetState(std::uint64_t state) noexcept { m_State = state; }

	private:
		std::uint64_t m_State;
	};
}
