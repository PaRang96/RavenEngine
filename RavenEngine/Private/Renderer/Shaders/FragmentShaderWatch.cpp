#include "Renderer/Shaders/FragmentShaderWatch.hpp"

#include "Raven/Core/Files.hpp"
#include "Core/PathText.hpp"
#include "Platform/BuildProcess.hpp"
#include "Platform/DirectoryWatch.hpp"
#include "Raven/Assets/AssetId.hpp"
#include "Raven/Core/Log.hpp"
#include "ShaderWatchConfig.hpp"

#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr auto PollInterval = std::chrono::milliseconds(40);
		constexpr auto SettleTime = std::chrono::milliseconds(100);
		constexpr auto AuditInterval = std::chrono::milliseconds(2000);
		constexpr std::size_t MaxSourceBytes = 1024 * 1024;
		constexpr std::size_t MaxOutputBytes = 4 * 1024 * 1024;

		struct Snapshot
		{
			std::vector<std::byte> Bytes;
			std::string Error;
			bool operator==(const Snapshot&) const = default;
		};

		Snapshot ReadSource(const std::filesystem::path& source)
		{
			try
			{
				return { Files::ReadBinary(source, MaxSourceBytes), {} };
			}
			catch (const std::exception& error)
			{
				return { {}, error.what() };
			}
		}

		class ShaderScratch
		{
		public:
			ShaderScratch()
				: m_Path(std::filesystem::temp_directory_path() /
					("raven-shader-" + AssetId::Generate().ToString()))
			{
				if (!std::filesystem::create_directory(m_Path))
					throw std::runtime_error("Cannot reserve shader scratch directory");
			}

			~ShaderScratch()
			{
				std::error_code ignored;
				for (const auto* name : { "input.frag.hlsl", "candidate.spv", "compile.log" })
					std::filesystem::remove(m_Path / name, ignored);
				std::filesystem::remove(m_Path, ignored);
			}

			const std::filesystem::path& GetPath() const { return m_Path; }

		private:
			std::filesystem::path m_Path;
		};

		void WriteSnapshot(const std::filesystem::path& input, const Snapshot& snapshot)
		{
			std::ofstream file(input, std::ios::binary | std::ios::trunc);
			if (!snapshot.Bytes.empty())
				file.write(reinterpret_cast<const char*>(snapshot.Bytes.data()),
					static_cast<std::streamsize>(snapshot.Bytes.size()));
			file.close();
			if (!file)
				throw std::runtime_error("Cannot write shader snapshot: " + PathToUtf8(input));
		}

		std::string ReadDiagnostics(const std::filesystem::path& log)
		{
			std::ifstream file(log, std::ios::binary);
			std::string diagnostics(8192, '\0');
			file.read(diagnostics.data(), static_cast<std::streamsize>(diagnostics.size()));
			diagnostics.resize(static_cast<std::size_t>(file.gcount()));
			return diagnostics;
		}

		std::optional<FragmentShaderWatch::Candidate> Compile(const ShaderScratch& scratch,
			const Snapshot& snapshot, FragmentShaderKind kind, std::stop_token stop)
		{
			if (!snapshot.Error.empty())
				throw std::runtime_error(snapshot.Error);
			const auto input = scratch.GetPath() / "input.frag.hlsl";
			const auto output = scratch.GetPath() / "candidate.spv";
			const auto log = scratch.GetPath() / "compile.log";
			WriteSnapshot(input, snapshot);
			const auto start = Clock::now();
			using namespace ShaderWatchConfig;
			const std::vector<std::wstring> arguments{
				std::wstring(L"-DDXC=") + Dxc, std::wstring(L"-DVALIDATOR=") + Validator,
				std::wstring(L"-DOPTIMIZATION=") + Optimization, std::wstring(L"-DPROFILE=") + Profile,
				std::wstring(L"-DENTRY=") + Entry, L"-DSOURCE=" + input.wstring(),
				L"-DOUTPUT=" + output.wstring(), L"-P", CompileScript
			};
			std::unique_ptr<Platform::BuildProcess, decltype(&Platform::DestroyBuildProcess)> process(
				Platform::StartBuildProcess(CMake, arguments, log), &Platform::DestroyBuildProcess);
			int exitCode = 0;
			while (!Platform::PollBuildProcess(*process, exitCode))
			{
				if (stop.stop_requested())
					return std::nullopt;
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			if (exitCode != 0)
				throw std::runtime_error("compiler or validator exited with " + std::to_string(exitCode) +
					"\n" + ReadDiagnostics(log));
			const auto compileTime = Clock::now() - start;
			const auto bytes = Files::ReadBinary(output, MaxOutputBytes);
			if (bytes.size() < 5 * sizeof(std::uint32_t) || bytes.size() % sizeof(std::uint32_t) != 0)
				throw std::runtime_error("invalid SPIR-V output size");
			std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
			std::memcpy(words.data(), bytes.data(), bytes.size());
			CheckFragmentShaderInterface(words, kind);
			return FragmentShaderWatch::Candidate{ std::move(words), snapshot.Bytes, compileTime };
		}
	}

	FragmentShaderWatch::FragmentShaderWatch(std::filesystem::path source, FragmentShaderKind kind)
		: m_Source(std::filesystem::absolute(source)),
		  m_Kind(kind),
		  m_Worker([this](std::stop_token stop) { Run(stop); })
	{
	}

	std::optional<FragmentShaderWatch::Candidate> FragmentShaderWatch::TakeCandidate()
	{
		const std::scoped_lock lock(m_Mutex);
		return std::exchange(m_Ready, std::nullopt);
	}

	bool FragmentShaderWatch::IsCurrent(const Candidate& candidate) const
	{
		const auto snapshot = ReadSource(m_Source);
		return snapshot.Error.empty() && snapshot.Bytes == candidate.Source;
	}

	void FragmentShaderWatch::Run(std::stop_token stop)
	{
		try
		{
			const ShaderScratch scratch;
			const Platform::WatchDirectory directory{ m_Source.parent_path() };
			Platform::DirectoryWatch watch(std::span(&directory, 1));
			std::optional<Snapshot> observed;
			auto stableSince = Clock::now();
			bool attempted = false;
			while (!stop.stop_requested())
			{
				auto current = ReadSource(m_Source);
				const auto now = Clock::now();
				if (!observed || current != *observed)
				{
					observed = std::move(current);
					stableSince = now;
					attempted = false;
					const std::scoped_lock lock(m_Mutex);
					m_Ready.reset();
				}
				else if (!attempted && now - stableSince >= SettleTime)
				{
					attempted = true;
					std::optional<Candidate> candidate;
					std::string error;
					try
					{
						candidate = Compile(scratch, *observed, m_Kind, stop);
					}
					catch (const std::exception& failure)
					{
						error = failure.what();
					}
					if (stop.stop_requested())
						return;
					if (ReadSource(m_Source) != *observed)
						observed.reset();
					else if (!error.empty())
						Log(LogLevel::Warning, "Cannot reload shader " + PathToUtf8(m_Source) + ": " + error);
					else
					{
						const std::scoped_lock lock(m_Mutex);
						m_Ready = std::move(candidate);
					}
				}
				watch.Wait(stop, observed && attempted && watch.IsAvailable() ? AuditInterval : PollInterval);
			}
		}
		catch (const std::exception& error)
		{
			Log(LogLevel::Warning, "Shader watcher stopped for " + PathToUtf8(m_Source) + ": " + error.what());
		}
	}
}
