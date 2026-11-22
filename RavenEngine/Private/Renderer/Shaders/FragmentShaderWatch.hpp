#pragma once

#include "Renderer/Vulkan/FragmentShaderInterface.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace Raven
{
	class FragmentShaderWatch
	{
	public:
		struct Candidate
		{
			std::vector<std::uint32_t> Words;
			std::vector<std::byte> Source;
			// Raven/Material.hlsli for lit shaders, which reload when it changes (G4).
			std::vector<std::byte> Header;
			std::chrono::duration<double, std::milli> CompileTime;
		};

		FragmentShaderWatch(std::filesystem::path source, FragmentShaderKind kind);
		~FragmentShaderWatch() = default;
		std::optional<Candidate> TakeCandidate();
		bool IsCurrent(const Candidate& candidate) const;
		const std::filesystem::path& GetSource() const { return m_Source; }

	private:
		void Run(std::stop_token stop);

		const std::filesystem::path m_Source;
		// Empty for shaders without an engine header dependency.
		const std::filesystem::path m_Header;
		const FragmentShaderKind m_Kind;
		std::mutex m_Mutex;
		std::optional<Candidate> m_Ready;
		std::jthread m_Worker;
	};
}
