#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "Raven/Core/Api.hpp"

namespace Raven
{
	// One HLSL compile with the build's DXC flags and validator (docs/HotReloadDesign.md, H2), shared by
	// the engine's programs and material shaders. The include root is the engine shader folder, so
	// `#include "Raven/Material.hlsli"` works from any source. Development hosts only: it needs the
	// toolchain the build found. A failed compile returns its diagnostics in Error and the includes DXC
	// listed before failing.
	struct CompiledHlsl
	{
		std::vector<std::uint32_t> Words;
		std::vector<std::filesystem::path> Includes;
		std::string Error;
		double Milliseconds = 0.0;
	};

	// scratch is a folder the call may fill with its intermediate files.
	RAVEN_API CompiledHlsl CompileHlsl(const std::filesystem::path& source, const char* profile, const char* entry,
		const std::filesystem::path& scratch, std::stop_token stop = {});
	RAVEN_API std::filesystem::path GetEngineShaderDirectory();

	// Every engine shader program (docs/HotReloadDesign.md, H2). cmake/RavenContent.cmake
	// compiles the same sources with the same profiles and entry points.
	enum class ShaderProgram : std::uint8_t
	{
		LitVertex, LitFragment, UnlitFragment, NormalsFragment, WireframeFragment,
		TexturedVertex, TexturedFragment, Scatter, Cull, DebugLineVertex, DebugLineFragment, OverlayVertex,
		OverlayFragment, Count
	};

	// The SPIR-V of every program, loaded from the build and replaced by watched compiles.
	class ShaderLibrary
	{
	public:
		// Runs on a reload worker after SPIR-V validation; throws to reject the candidate.
		using Check = std::function<void(ShaderProgram, std::span<const std::uint32_t>)>;

		struct Candidate
		{
			ShaderProgram Program;
			std::vector<std::uint32_t> Words;
			double CompileMs = 0.0;
			// File names of the inputs whose change caused this compile, for the reload summary.
			std::vector<std::string> Trigger;
		};

		explicit ShaderLibrary(const std::filesystem::path& compiledDirectory);
		~ShaderLibrary();

		static const char* GetSourceName(ShaderProgram program) noexcept;
		std::span<const std::uint32_t> Get(ShaderProgram program) const;
		// Records words the renderer has published, and the time publishing took (H7).
		void Set(ShaderProgram program, std::vector<std::uint32_t> words,
			std::chrono::duration<double, std::milli> publish = {});

		// Compiles each program's source whenever it or one of its includes changes. Windows only.
		void Watch(Check check);
		bool IsWatching() const noexcept { return !m_Programs.empty(); }

		// Candidates that differ from the current words. Empty while any watched program is still
		// settling or compiling, so the dependents of one header edit are published together.
		std::vector<Candidate> TakeReady();

		ShaderLibrary(const ShaderLibrary&) = delete;
		ShaderLibrary& operator=(const ShaderLibrary&) = delete;

	private:
		class Program;
		friend class Program;

		std::array<std::vector<std::uint32_t>, static_cast<std::size_t>(ShaderProgram::Count)> m_Words;
		std::mutex m_ReadyMutex;
		std::vector<Candidate> m_Ready;
		Check m_Check;
		// Declared last: the reload subscriptions stop before the state they use is destroyed.
		std::vector<std::unique_ptr<Program>> m_Programs;
	};
}
