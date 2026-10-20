#include "Raven/Core/GameModule.hpp"
#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/Core/Log.hpp"
#include "Platform/GameModulePlatform.hpp"
#include "GameBuildConfig.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace Raven
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		std::atomic<std::uint64_t> NextCopy{ 0 };

		void HashFile(const std::filesystem::path& path, std::uint64_t& hash)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream) throw std::runtime_error("Cannot read watched game file");
			char buffer[4096];
			while (stream.read(buffer, sizeof(buffer)) || stream.gcount())
				for (std::streamsize i = 0; i < stream.gcount(); ++i)
					hash = (hash ^ static_cast<unsigned char>(buffer[i])) * 1099511628211ull;
			if (!stream.eof()) throw std::runtime_error("Cannot finish reading watched game file");
		}

		struct FileStamp
		{
			std::filesystem::file_time_type Time{};
			std::uintmax_t Size = 0;
			std::uint64_t Digest = 0;
			bool operator==(const FileStamp&) const = default;
		};

		FileStamp Stamp(const std::filesystem::path& path)
		{
			FileStamp stamp{ std::filesystem::last_write_time(path), std::filesystem::file_size(path), 14695981039346656037ull };
			HashFile(path, stamp.Digest);
			if (stamp.Time != std::filesystem::last_write_time(path) || stamp.Size != std::filesystem::file_size(path))
				throw std::runtime_error("Game DLL changed during observation; retry reload");
			return stamp;
		}

		// Content changes drive builds; timestamps alone are not a compatibility check.
		std::uint64_t SourceSignature()
		{
			const std::filesystem::path root(GameBuild::Root);
			std::vector<std::filesystem::path> files;
			for (const auto& directory : { root / "RavenEngine/Public", root / "RavenEngine/Private/Game", root / "cmake" })
			{
				for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
				{
					const auto extension = entry.path().extension();
					if (entry.is_regular_file() && (extension == ".hpp" || extension == ".cpp" ||
						extension == ".cmake" || extension == ".in" || entry.path().filename() == "CMakeLists.txt"))
						files.push_back(entry.path());
				}
			}
			for (const auto* name : { "CMakeLists.txt", "CMakePresets.json", "RavenEngine/CMakeLists.txt" })
				files.push_back(root / name);
			std::sort(files.begin(), files.end());
			std::uint64_t hash = 14695981039346656037ull;
			const auto append = [&hash](unsigned char byte) { hash = (hash ^ byte) * 1099511628211ull; };
			for (const auto& path : files)
			{
				for (unsigned char byte : path.generic_string()) append(byte);
				append(0);
				HashFile(path, hash);
				append(0);
			}
			return hash;
		}

		struct LoadedGame
		{
			void* Library = nullptr;
			std::filesystem::path Copy;
			GameUpdateFunction Update = nullptr;
			~LoadedGame()
			{
				Update = nullptr;
				Platform::UnloadGameLibrary(Library);
				std::error_code ignored;
				if (!Copy.empty())
				{
					std::filesystem::remove(Copy, ignored);
					std::filesystem::remove(Copy.parent_path(), ignored); // Only if empty.
				}
			}
		};
	}

	struct GameModule::Impl
	{
		std::filesystem::path Path;
		std::unique_ptr<LoadedGame> Active;
		std::unique_ptr<Platform::GameBuildProcess, decltype(&Platform::DestroyGameBuild)>
			Build{ nullptr, Platform::DestroyGameBuild };
		std::uint64_t Generation = 0;
		std::string Error;
		bool AutoBuild = false;
		bool BuildQueued = false;
		std::uint64_t ObservedSource = 0;
		std::uint64_t BuiltSource = 0;
		Clock::time_point SourceChanged{};
		Clock::time_point NextPoll{};
		FileStamp ObservedFile{};
		FileStamp AttemptedFile{};

		void Report(const std::string& error)
		{
			if (Error != error) Log(LogLevel::Warning, error);
			Error = error;
		}
	};

	GameModule::GameModule(const std::filesystem::path& path, bool autoBuild)
		: m_Impl(std::make_unique<Impl>())
	{
		if (!GameBuild::Enabled) throw std::runtime_error("Game modules are disabled or unsupported in this build");
		m_Impl->Path = std::filesystem::absolute(path);
		m_Impl->AutoBuild = autoBuild;
		if (autoBuild && !std::filesystem::equivalent(m_Impl->Path,
			std::filesystem::path(GameBuild::BuildDirectory).parent_path() / "RavenGame.dll"))
			throw std::invalid_argument("Automatic builds require this build's configured RavenGame.dll output");
		if (!Reload()) throw std::runtime_error(m_Impl->Error);
		if (autoBuild)
		{
			// Also build on startup: source may have changed while the host was stopped.
			m_Impl->ObservedSource = SourceSignature();
			m_Impl->BuildQueued = true;
			m_Impl->SourceChanged = Clock::now();
		}
	}

	GameModule::~GameModule() = default;

	bool GameModule::Reload()
	{
		auto& state = *m_Impl;
		try
		{
			const auto before = Stamp(state.Path);
			state.AttemptedFile = before;
			state.ObservedFile = before;
			auto candidate = std::make_unique<LoadedGame>();
			// The build output is never loaded, so the linker can replace it at any time.
			// create_directory reserves a unique directory across hosts and processes.
			std::filesystem::path directory;
			do
			{
				directory = state.Path.parent_path() / (".raven-game-" + std::to_string(++NextCopy));
			} while (!std::filesystem::create_directory(directory));
			candidate->Copy = directory / state.Path.filename();
			std::filesystem::copy_file(state.Path, candidate->Copy);
			if (!(Stamp(state.Path) == before)) throw std::runtime_error("Game DLL changed during copy; retry reload");
			candidate->Library = Platform::LoadGameLibrary(candidate->Copy);
			const auto contract = reinterpret_cast<GetGameContractFunction>(
				Platform::FindGameEntryPoint(candidate->Library, GameContractEntryPoint));
			const char* value = contract();
			if (!value || std::string_view(value) != GameBuild::Contract)
				throw std::runtime_error("Game contract changed: rebuild and restart the host; existing game remains active");
			const auto getUpdate = reinterpret_cast<GetGameUpdateFunction>(
				Platform::FindGameEntryPoint(candidate->Library, GameUpdateEntryPoint));
			candidate->Update = getUpdate();
			if (!candidate->Update) throw std::runtime_error("Game DLL returned a null update function");
			state.Active.swap(candidate);
			++state.Generation;
			state.Error.clear();
			Log(LogLevel::Info, "Game module activated, generation " + std::to_string(state.Generation));
			return true;
		}
		catch (const std::exception& error)
		{
			state.Report(error.what());
			return false;
		}
	}

	void GameModule::Poll()
	{
		auto& state = *m_Impl;
		try
		{
			bool buildCompleted = false;
			if (state.Build)
			{
				int exitCode = 0;
				if (Platform::PollGameBuild(*state.Build, exitCode))
				{
					state.Build.reset();
					buildCompleted = true;
					if (exitCode == 0)
					{
						if (SourceSignature() == state.BuiltSource) Reload();
						else
						{
							state.BuildQueued = true;
							state.SourceChanged = Clock::now();
						}
					}
					else state.Report("Game build failed; keeping active code. See " +
						(std::filesystem::path(GameBuild::BuildDirectory) / "AutoBuild.log").string());
				}
			}
			const auto now = Clock::now();
			if (now < state.NextPoll) return;
			state.NextPoll = now + std::chrono::milliseconds(500);
			if (state.AutoBuild)
			{
				const auto signature = SourceSignature();
				if (signature != state.ObservedSource)
				{
					state.ObservedSource = signature;
					state.SourceChanged = now;
					state.BuildQueued = true;
				}
				if (!state.Build && state.BuildQueued && now - state.SourceChanged >= std::chrono::milliseconds(350))
				{
					const std::filesystem::path buildDirectory(GameBuild::BuildDirectory);
					state.Build.reset(Platform::StartGameBuild(GameBuild::Cmake,
						{ L"-P", (buildDirectory / "BuildGame.cmake").wstring() }, buildDirectory / "AutoBuild.log"));
					state.BuiltSource = signature;
					state.BuildQueued = false;
					Log(LogLevel::Info, "Building game module in background");
				}
			}
			// Observe external/manual builds only after two stable samples, never mid-build.
			if (!state.Build && !buildCompleted)
			{
				const auto file = Stamp(state.Path);
				if (file == state.ObservedFile && !(file == state.AttemptedFile)) Reload();
				state.ObservedFile = file;
			}
		}
		catch (const std::exception& error) { state.Report(error.what()); }
	}

	void GameModule::Update(GameInstance& game, const InputState& input, const FrameTime& frame)
	{
		m_Impl->Active->Update(game, input, frame);
	}

	std::uint64_t GameModule::GetGeneration() const noexcept { return m_Impl->Generation; }
	bool GameModule::IsBuilding() const noexcept { return m_Impl->Build != nullptr; }
	std::string GameModule::GetLastError() const { return m_Impl->Error; }
}
