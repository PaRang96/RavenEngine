#include "Renderer/Shaders/ShaderLibrary.hpp"

#include "Core/ImporterVersions.hpp"
#include "Core/PathText.hpp"
#include "Core/ReloadService.hpp"
#include "Platform/PlatformCapabilities.hpp"
#include "Platform/PlatformProcess.hpp"
#include "Raven/Assets/AssetId.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Core/Log.hpp"
#include "Renderer/Vulkan/VulkanShaderModule.hpp"
#include "ShaderWatchConfig.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace Raven
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr std::size_t MaxSourceBytes = 1024 * 1024;
		constexpr std::size_t MaxOutputBytes = 4 * 1024 * 1024;

		struct ProgramDesc
		{
			const char* Source;
			const char* Profile;
			const char* Entry;
		};

		constexpr std::array<ProgramDesc, static_cast<std::size_t>(ShaderProgram::Count)> Programs{ {
			{ "Color.vert.hlsl", "vs_6_0", "VSMain" },
			{ "Color.frag.hlsl", "ps_6_0", "PSMain" },
			{ "Unlit.frag.hlsl", "ps_6_0", "PSMain" },
			{ "Normals.frag.hlsl", "ps_6_0", "PSMain" },
			{ "Wireframe.frag.hlsl", "ps_6_0", "PSMain" },
			{ "Sprite.vert.hlsl", "vs_6_0", "VSMain" },
			{ "Sprite.frag.hlsl", "ps_6_0", "PSMain" },
			{ "Scatter.comp.hlsl", "cs_6_0", "CSMain" },
			{ "Cull.comp.hlsl", "cs_6_0", "CSMain" },
			{ "DebugLine.vert.hlsl", "vs_6_0", "VSMain" },
			{ "DebugLine.frag.hlsl", "ps_6_0", "PSMain" },
			{ "Overlay.vert.hlsl", "vs_6_0", "VSMain" },
			{ "Overlay.frag.hlsl", "ps_6_0", "PSMain" },
		} };

		const ProgramDesc& Describe(ShaderProgram program)
		{
			return Programs[static_cast<std::size_t>(program)];
		}

		std::vector<std::byte> ReadSource(const std::filesystem::path& path)
		{
			return Files::ReadBinary(path, MaxSourceBytes);
		}

		std::string ReadDiagnostics(const std::filesystem::path& log)
		{
			std::ifstream file(log, std::ios::binary);
			std::string diagnostics(8192, '\0');
			file.read(diagnostics.data(), static_cast<std::streamsize>(diagnostics.size()));
			diagnostics.resize(static_cast<std::size_t>(file.gcount()));
			return diagnostics;
		}

		// DXC writes Make syntax: "target: first \<newline> second", with "\ " for spaces in paths.
		std::vector<std::filesystem::path> ParseDependencies(const std::filesystem::path& file)
		{
			std::ifstream stream(file, std::ios::binary);
			std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			const auto colon = text.find(": ");
			if (colon == std::string::npos)
				throw std::runtime_error("unreadable DXC dependency list");
			std::vector<std::filesystem::path> paths;
			std::string token;
			const auto flush = [&]
			{
				if (!token.empty())
					paths.push_back(std::filesystem::path(std::u8string(token.begin(), token.end())).lexically_normal());
				token.clear();
			};
			for (std::size_t index = colon + 2; index < text.size(); ++index)
			{
				const char value = text[index];
				if (value == '\\' && index + 1 < text.size() && text[index + 1] == ' ')
				{
					token += ' ';
					++index;
				}
				else if (value == '\\' && index + 1 < text.size() && (text[index + 1] == '\n' || text[index + 1] == '\r'))
					flush();
				else if (value == ' ' || value == '\t' || value == '\n' || value == '\r')
					flush();
				else
					token += value;
			}
			flush();
			return paths;
		}

		// Scratch folders of hosts that did not exit cleanly and have not compiled for an hour, once
		// per process (AA2-HR-05). A live host's folder that sat unused that long is recreated by its
		// next compile.
		void SweepStaleScratch(const std::filesystem::path& own)
		{
			static std::once_flag swept;
			std::call_once(swept, [&own]
			{
				try
				{
					const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
					std::error_code ignored;
					for (const auto& entry : std::filesystem::directory_iterator(own.parent_path(), ignored))
					{
						if (entry.path() == own || !entry.path().filename().string().starts_with("raven-shader-"))
							continue;
						const auto time = entry.last_write_time(ignored);
						if (!ignored && time < cutoff)
							std::filesystem::remove_all(entry.path(), ignored);
						ignored.clear();
					}
				}
				catch (const std::exception&)
				{
					// Leftover folders cost disk space only; the host starts either way.
				}
			});
		}

		std::vector<std::uint32_t> ToWords(const std::vector<std::byte>& bytes)
		{
			if (bytes.size() < 5 * sizeof(std::uint32_t) || bytes.size() % sizeof(std::uint32_t) != 0)
				throw std::runtime_error("invalid SPIR-V output size");
			std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
			std::memcpy(words.data(), bytes.data(), bytes.size());
			return words;
		}
	}

	// One watched program: a reload consumer that compiles with the build's DXC flags, checks
	// the result, and follows the includes DXC reports.
	class ShaderLibrary::Program final : private ReloadConsumer
	{
	public:
		Program(ShaderLibrary& library, ShaderProgram program, const std::filesystem::path& source)
			: m_Library(library), m_Program(program), m_Source(std::filesystem::absolute(source).lexically_normal()),
			  m_Scratch(std::filesystem::temp_directory_path() / ("raven-shader-" + AssetId::Generate().ToString()))
		{
			if (!std::filesystem::create_directory(m_Scratch))
				throw std::runtime_error("Cannot reserve shader scratch directory");
			SweepStaleScratch(m_Scratch);
			ReloadOptions options;
			options.Name = "shader " + PathToUtf8(m_Source);
			options.ImporterVersion = ImporterVersion::Hlsl;
			// Import calls SetInputs on this subscription, so it starts only once stored.
			options.StartPaused = true;
			// No baseline: the first compile reports the includes, which then become inputs.
			m_Subscription = ReloadService::Subscribe({ ReloadInput{ m_Source, ReadSource } }, *this, std::move(options));
			m_Subscription->SetPaused(false);
		}

		~Program()
		{
			// Destroying the subscription waits for an import in progress, and that import may still
			// call SetInputs through m_Subscription. unique_ptr::reset clears the pointer before it
			// deletes, so the import would read null (an access violation when the host quits during
			// the startup compiles). Delete in place, and release the pointer once the wait is over.
			delete m_Subscription.get();
			static_cast<void>(m_Subscription.release());
			std::error_code ignored;
			std::filesystem::remove_all(m_Scratch, ignored);
		}

		bool IsSettled() const { return m_Subscription->IsSettled(); }
		// The renderer logs the summary itself; this keeps the record's publish time.
		void Published(std::chrono::duration<double, std::milli> publish) { m_Subscription->Published(publish); }

	private:
		std::string Import(const ReloadSnapshot&, std::stop_token stop) override
		{
			const auto& desc = Describe(m_Program);
			// Another host's sweep may have removed a folder that sat unused for an hour.
			std::filesystem::create_directories(m_Scratch);
			auto compiled = CompileHlsl(m_Source, desc.Profile, desc.Entry, m_Scratch, stop);
			// Includes join the inputs, so editing one recompiles this program (H2). DXC lists them
			// before compiling, so a program whose compile fails still follows its includes.
			FollowIncludes(compiled.Includes);
			if (!compiled.Error.empty())
				throw std::runtime_error(compiled.Error);
			m_Library.m_Check(m_Program, compiled.Words);
			const auto description = std::to_string(compiled.Words.size()) + " SPIR-V words";
			m_Pending = Candidate{ m_Program, std::move(compiled.Words), compiled.Milliseconds };
			return description;
		}

		void FollowIncludes(const std::vector<std::filesystem::path>& includes)
		{
			std::vector<ReloadInput> inputs{ ReloadInput{ m_Source, ReadSource } };
			for (const auto& path : includes)
				if (std::filesystem::absolute(path).lexically_normal() != m_Source)
					inputs.push_back(ReloadInput{ std::filesystem::absolute(path).lexically_normal(), ReadSource });
			std::vector<std::filesystem::path> paths;
			for (const auto& input : inputs)
				paths.push_back(input.Path);
			if (paths != m_Inputs)
			{
				m_Inputs = std::move(paths);
				m_Subscription->SetInputs(std::move(inputs));
			}
		}

		void Accept(const ReloadRecord& record) override
		{
			for (const auto& path : record.Trigger)
				m_Pending->Trigger.push_back(path.filename().string());
			const std::scoped_lock lock(m_Library.m_ReadyMutex);
			std::erase_if(m_Library.m_Ready, [&](const Candidate& ready) { return ready.Program == m_Program; });
			m_Library.m_Ready.push_back(std::move(*m_Pending));
			m_Pending.reset();
		}

		void Discard() noexcept override
		{
			m_Pending.reset();
		}

		void Changed() noexcept override
		{
			const std::scoped_lock lock(m_Library.m_ReadyMutex);
			std::erase_if(m_Library.m_Ready, [&](const Candidate& ready) { return ready.Program == m_Program; });
		}

		ShaderLibrary& m_Library;
		const ShaderProgram m_Program;
		const std::filesystem::path m_Source;
		const std::filesystem::path m_Scratch;
		// Reload-worker state between Import and Accept or Discard.
		std::optional<Candidate> m_Pending;
		std::vector<std::filesystem::path> m_Inputs{ m_Source };
		std::unique_ptr<ReloadSubscription> m_Subscription;
	};

	CompiledHlsl CompileHlsl(const std::filesystem::path& source, const char* profile, const char* entry,
		const std::filesystem::path& scratch, std::stop_token stop)
	{
		if constexpr (!Platform::Capabilities.BuildProcesses)
			throw std::runtime_error(std::string("Shaders cannot be compiled on ") + Platform::Capabilities.Name + " yet");
		std::filesystem::create_directories(scratch);
		const auto output = scratch / "candidate.spv";
		const auto dependencies = scratch / "dependencies.d";
		const auto log = scratch / "compile.log";
		using namespace ShaderWatchConfig;
		const auto utf8 = [](const std::filesystem::path& path) { return PathToUtf8(path); };
		const std::vector<std::string> arguments{
			"-DDXC=" + utf8(Dxc), "-DVALIDATOR=" + utf8(Validator),
			std::string("-DOPTIMIZATION=") + Optimization, std::string("-DPROFILE=") + profile,
			std::string("-DENTRY=") + entry, "-DSOURCE=" + utf8(std::filesystem::absolute(source)),
			"-DOUTPUT=" + utf8(output), "-DINCLUDE=" + utf8(GetEngineShaderDirectory()),
			"-DDEPENDENCIES=" + utf8(dependencies), "-P", utf8(CompileScript)
		};
		std::error_code ignored;
		std::filesystem::remove(dependencies, ignored);
		std::filesystem::remove(output, ignored);
		const auto start = Clock::now();
		std::unique_ptr<Platform::BuildProcess, decltype(&Platform::DestroyBuildProcess)> process(
			Platform::StartBuildProcess(CMake, arguments, log), &Platform::DestroyBuildProcess);
		int exitCode = 0;
		while (!Platform::PollBuildProcess(*process, exitCode))
		{
			if (stop.stop_requested())
				throw std::runtime_error("shader compile cancelled");
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		CompiledHlsl result;
		if (std::filesystem::exists(dependencies))
			result.Includes = ParseDependencies(dependencies);
		if (exitCode != 0)
		{
			result.Error = "compiler or validator exited with " + std::to_string(exitCode) + "\n" + ReadDiagnostics(log);
			return result;
		}
		result.Milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		result.Words = ToWords(Files::ReadBinary(output, MaxOutputBytes));
		return result;
	}

	std::filesystem::path GetEngineShaderDirectory()
	{
		return std::filesystem::path(ShaderWatchConfig::SourceDirectory);
	}

	ShaderLibrary::ShaderLibrary(const std::filesystem::path& compiledDirectory)
	{
		for (std::size_t index = 0; index < m_Words.size(); ++index)
		{
			// "Color.frag.hlsl" builds to "Color.frag.spv".
			const auto name = std::filesystem::path(Programs[index].Source).stem().string() + ".spv";
			m_Words[index] = VulkanShaderModule::ReadWords(compiledDirectory / name);
		}
	}

	ShaderLibrary::~ShaderLibrary() = default;

	const char* ShaderLibrary::GetSourceName(ShaderProgram program) noexcept
	{
		return Describe(program).Source;
	}

	std::span<const std::uint32_t> ShaderLibrary::Get(ShaderProgram program) const
	{
		return m_Words[static_cast<std::size_t>(program)];
	}

	void ShaderLibrary::Set(ShaderProgram program, std::vector<std::uint32_t> words,
		std::chrono::duration<double, std::milli> publish)
	{
		m_Words[static_cast<std::size_t>(program)] = std::move(words);
		if (IsWatching())
			m_Programs[static_cast<std::size_t>(program)]->Published(publish);
	}

	void ShaderLibrary::Watch(Check check)
	{
		// Live compiles run DXC through a background build process.
		if constexpr (!Platform::Capabilities.BuildProcesses)
		{
			Log(LogLevel::Warning, std::string("Shaders are not watched: ") + Platform::Capabilities.Name +
				" cannot run background builds yet");
			return;
		}
		if (IsWatching())
			return;
		m_Check = std::move(check);
		const auto directory = GetEngineShaderDirectory();
		for (std::size_t index = 0; index < m_Words.size(); ++index)
			m_Programs.push_back(std::make_unique<Program>(*this, static_cast<ShaderProgram>(index),
				directory / Programs[index].Source));
		Log(LogLevel::Info, "Watching shaders in " + PathToUtf8(directory));
	}

	std::vector<ShaderLibrary::Candidate> ShaderLibrary::TakeReady()
	{
		for (const auto& program : m_Programs)
			if (!program->IsSettled())
				return {};
		std::vector<Candidate> ready;
		{
			const std::scoped_lock lock(m_ReadyMutex);
			ready.swap(m_Ready);
		}
		std::erase_if(ready, [&](const Candidate& candidate) { return std::ranges::equal(candidate.Words, Get(candidate.Program)); });
		return ready;
	}
}
