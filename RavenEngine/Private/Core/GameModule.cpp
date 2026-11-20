#include "Raven/Core/GameModule.hpp"
#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/Core/Log.hpp"
#include "Platform/GameModulePlatform.hpp"
#include "Platform/BuildProcess.hpp"
#include "Platform/DirectoryWatch.hpp"
#include "GameBuildConfig.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace Raven
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		std::atomic<std::uint64_t> NextCopy{ 0 };
		constexpr std::uint64_t HashOffset = 14695981039346656037ull;
		constexpr std::uint64_t HashPrime = 1099511628211ull;

		void AppendHashByte(std::uint64_t& hash, unsigned char byte)
		{
			hash ^= byte;
			hash *= HashPrime;
		}

		void HashFile(const std::filesystem::path& path, std::uint64_t& hash)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
				throw std::runtime_error("Cannot read watched game file");
			char buffer[4096];
			while (stream.read(buffer, sizeof(buffer)) || stream.gcount() > 0)
			{
				const auto byteCount = stream.gcount();
				for (std::streamsize index = 0; index < byteCount; ++index)
					AppendHashByte(hash, static_cast<unsigned char>(buffer[index]));
			}
			if (!stream.eof())
				throw std::runtime_error("Cannot finish reading watched game file");
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
			FileStamp stamp;
			stamp.Time = std::filesystem::last_write_time(path);
			stamp.Size = std::filesystem::file_size(path);
			stamp.Digest = HashOffset;
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
			std::uint64_t hash = HashOffset;
			for (const auto& path : files)
			{
				for (unsigned char byte : path.generic_string())
					AppendHashByte(hash, byte);
				AppendHashByte(hash, 0);
				HashFile(path, hash);
				AppendHashByte(hash, 0);
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
		struct WatchEvent
		{
			std::optional<FileStamp> File;
			std::string Error;
			std::uint64_t Revision = 0;
		};

		std::filesystem::path Path;
		std::unique_ptr<LoadedGame> Active;
		std::uint64_t Generation = 0;
		std::string Error;
		bool AutoBuild = false;
		FileStamp AttemptedFile{};
		std::atomic<bool> Building{ false };
		std::mutex EventMutex;
		std::optional<WatchEvent> Pending;
		std::atomic<bool> HasEvent{ false };
		std::atomic<std::uint64_t> SourceRevision{ 0 };
		std::jthread Worker; // Joins before the state it uses is destroyed.

		void Report(const std::string& error)
		{
			if (Error != error)
				Log(LogLevel::Warning, error);
			Error = error;
		}

		void Publish(WatchEvent event)
		{
			const std::scoped_lock lock(EventMutex);
			Pending = std::move(event);
			HasEvent.store(true, std::memory_order_release);
		}

		void DiscardCandidate()
		{
			const std::scoped_lock lock(EventMutex);
			if (Pending && Pending->File)
			{
				Pending.reset();
				HasEvent.store(false, std::memory_order_release);
			}
		}

		std::optional<WatchEvent> TakeEvent()
		{
			if (!HasEvent.load(std::memory_order_acquire))
				return {};
			const std::scoped_lock lock(EventMutex);
			HasEvent.store(false, std::memory_order_release);
			return std::exchange(Pending, std::nullopt);
		}

		bool Activate(std::optional<FileStamp> expected = {}, std::uint64_t revision = 0);
		void Watch(std::stop_token stop, FileStamp initial);
	};

	bool GameModule::Impl::Activate(std::optional<FileStamp> expected, std::uint64_t revision)
	{
		if (expected && (*expected == AttemptedFile || SourceRevision.load() != revision))
			return false;
		try
		{
			const auto before = Stamp(Path);
			if (expected && before != *expected)
				return false;
			auto candidate = std::make_unique<LoadedGame>();
			// The build output is never loaded, so the linker can replace it at any time.
			// create_directory reserves a unique directory across hosts and processes.
			std::filesystem::path directory;
			do
			{
				directory = Path.parent_path() / (".raven-game-" + std::to_string(++NextCopy));
			} while (!std::filesystem::create_directory(directory));
			candidate->Copy = directory / Path.filename();
			std::filesystem::copy_file(Path, candidate->Copy);
			if (!(Stamp(Path) == before) || Stamp(candidate->Copy).Digest != before.Digest)
				throw std::runtime_error("Game DLL changed during copy; retry reload");
			candidate->Library = Platform::LoadGameLibrary(candidate->Copy);
			const auto contract = reinterpret_cast<GetGameContractFunction>(
				Platform::FindGameEntryPoint(candidate->Library, GameContractEntryPoint));
			const char* value = contract();
			if (!value || std::string_view(value) != GameBuild::Contract)
				throw std::runtime_error("Game contract changed: rebuild and restart the host; existing game remains active");
			const auto getUpdate = reinterpret_cast<GetGameUpdateFunction>(
				Platform::FindGameEntryPoint(candidate->Library, GameUpdateEntryPoint));
			candidate->Update = getUpdate();
			if (!candidate->Update)
				throw std::runtime_error("Game DLL returned a null update function");
			if (expected && SourceRevision.load() != revision)
				return false;
			Active.swap(candidate);
			AttemptedFile = before;
			++Generation;
			Error.clear();
			Log(LogLevel::Info, "Game module activated, generation " + std::to_string(Generation));
			return true;
		}
		catch (const std::exception& error)
		{
			Report(error.what());
			return false;
		}
	}

	void GameModule::Impl::Watch(std::stop_token stop, FileStamp initial)
	{
		using namespace std::chrono_literals;
		using BuildProcess = std::unique_ptr<Platform::BuildProcess, decltype(&Platform::DestroyBuildProcess)>;
		BuildProcess build{ nullptr, &Platform::DestroyBuildProcess };
		const auto directory = std::filesystem::path(GameBuild::BuildDirectory);
		std::vector<Platform::WatchDirectory> directories{ { Path.parent_path() } };
		if (AutoBuild)
		{
			const std::filesystem::path root(GameBuild::Root);
			directories.insert(directories.end(), {
				{ root }, { root / "RavenEngine" }, { root / "RavenEngine/Public", true },
				{ root / "RavenEngine/Private/Game", true }, { root / "cmake", true }
			});
		}
		Platform::DirectoryWatch watch(directories);
		auto observedFile = initial;
		auto publishedFile = initial;
		std::uint64_t publishedRevision = 0;
		std::optional<std::uint64_t> observedSource;
		std::uint64_t builtSource = 0;
		Clock::time_point nextPoll{};
		Clock::time_point sourceChanged{};
		bool buildQueued = AutoBuild;
		bool buildFailed = false;
		std::string lastError;
		const auto report = [&](const std::string& error)
		{
			if (lastError != error)
			{
				lastError = error;
				Publish({ {}, error });
			}
		};
		const auto observeSource = [&](std::uint64_t signature)
		{
			if (observedSource != signature)
			{
				observedSource = signature;
				++SourceRevision;
				sourceChanged = Clock::now();
				buildQueued = true;
				DiscardCandidate();
			}
		};
		const auto offer = [&](FileStamp file, bool successfulBuild = false)
		{
			const auto revision = SourceRevision.load();
			if (publishedFile != file || publishedRevision != revision)
			{
				publishedFile = file;
				publishedRevision = revision;
				lastError.clear();
				Publish({ file, {}, revision });
			}
			else if (successfulBuild && !lastError.empty())
			{
				lastError.clear();
				Publish({});
			}
		};

		while (!stop.stop_requested())
		{
			try
			{
				const auto now = Clock::now();
				int exitCode = 0;
				if (build && Platform::PollBuildProcess(*build, exitCode))
				{
					build.reset();
					Building.store(false);
					if (exitCode != 0)
					{
						buildFailed = true;
						report("Game build failed; keeping active code. See " + (directory / "AutoBuild.log").string());
					}
					// Keep external observation suspended until the build's inputs can be rechecked.
					const bool superseded = buildQueued;
					buildQueued = true;
					const auto signature = SourceSignature();
					observeSource(signature);
					if (!superseded && signature == builtSource)
					{
						if (exitCode == 0)
						{
							offer(Stamp(Path), true);
							buildFailed = false;
						}
						buildQueued = false;
					}
				}

				if (now >= nextPoll)
				{
					nextPoll = now + 500ms;
					if (AutoBuild)
						observeSource(SourceSignature());
					if (!build && buildQueued && now - sourceChanged >= 350ms)
					{
						build.reset(Platform::StartBuildProcess(GameBuild::Cmake,
							{ L"-P", (directory / "BuildGame.cmake").wstring() }, directory / "AutoBuild.log"));
						Building.store(true);
						builtSource = *observedSource;
						buildQueued = false;
						DiscardCandidate();
						Log(LogLevel::Info, "Building game module in background");
					}
					if (!build && !buildQueued && !buildFailed)
					{
						const auto file = Stamp(Path);
						if (file == observedFile)
							offer(file);
						observedFile = file;
					}
				}
			}
			catch (const std::exception& error)
			{
				report(error.what());
			}
			const bool settling = build || buildQueued || (!buildFailed && observedFile != publishedFile);
			if (watch.Wait(stop, settling || !watch.IsAvailable() ? 40ms : 2000ms))
				nextPoll = {};
		}
	}

	GameModule::GameModule(const std::filesystem::path& path, bool autoBuild)
		: m_Impl(std::make_unique<Impl>())
	{
		if (!GameBuild::Enabled)
			throw std::runtime_error("Game modules are disabled or unsupported in this build");
		m_Impl->Path = std::filesystem::absolute(path);
		m_Impl->AutoBuild = autoBuild;
		if (autoBuild && !std::filesystem::equivalent(m_Impl->Path,
			std::filesystem::path(GameBuild::BuildDirectory).parent_path() / GameBuild::ModuleFilename))
			throw std::invalid_argument("Automatic builds require this build's configured RavenGame.dll output");
		if (!Reload())
			throw std::runtime_error(m_Impl->Error);
	}

	GameModule::~GameModule() = default;

	bool GameModule::Reload()
	{
		return m_Impl->Activate();
	}

	void GameModule::Poll()
	{
		auto& state = *m_Impl;
		if (!state.Worker.joinable())
		{
			state.Worker = std::jthread([&state, initial = state.AttemptedFile](std::stop_token stop)
			{
				state.Watch(stop, initial);
			});
		}
		if (auto event = state.TakeEvent())
		{
			if (event->File)
			{
				if (*event->File == state.AttemptedFile && event->Revision == state.SourceRevision.load())
					state.Error.clear();
				else
					state.Activate(event->File, event->Revision);
			}
			else if (event->Error.empty())
				state.Error.clear();
			else
				state.Report(event->Error);
		}
	}

	GameUpdateResult GameModule::Update(GameInstance& game, const InputState& input, const FrameTime& frame,
		GameBindings bindings)
	{
		return m_Impl->Active->Update(game, input, frame, bindings);
	}

	std::uint64_t GameModule::GetGeneration() const noexcept { return m_Impl->Generation; }
	bool GameModule::IsBuilding() const noexcept { return m_Impl->Building.load(); }
	std::string GameModule::GetLastError() const { return m_Impl->Error; }
}
